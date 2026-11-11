/// @file tst_UsenetImagePreview.cpp
/// @brief Which NZB files search Details › Images and › NFO read, and how an
///        NFO is decoded, offline. The fetch itself runs against a provider in
///        tst_UsenetLivePreviewImages.

#include "nzb/NzbFile.h"
#include "stream/UsenetImagePreview.h"
#include "stream/UsenetNfoPreview.h"

#include "TestHelpers.h"

#include <QFile>

#include <QSignalSpy>
#include <QTest>

using namespace eMule::usenet;

namespace {

NzbFileInfo file(const QString& name, qint64 bytes, int segments = 1)
{
    NzbFileInfo f;
    f.subject = QStringLiteral("\"%1\" yEnc (1/%2)").arg(name).arg(segments);
    f.fileName = name;
    for (int i = 0; i < segments; ++i) {
        NzbSegment s;
        s.messageId = QStringLiteral("%1.%2@test").arg(name).arg(i);
        s.bytes = bytes / segments;
        s.number = i + 1;
        f.segments.append(s);
    }
    return f;
}

} // namespace

class tst_UsenetImagePreview : public QObject {
    Q_OBJECT

private slots:
    void aPostedImageAndThePlainVideoArePicked();
    void theOpeningVolumeOfTheBiggestSetIsTheVideo();
    void oversizedImagesAndPar2AreLeftOut();
    void noServerEndsWithANote();
    void serversOverTheirAllowanceAreNotAsked();
    void nfoFilesArePicked();
    void harryListsItsNfo();
    void nfoDecodesUtf8OrCp437();
};

void tst_UsenetImagePreview::aPostedImageAndThePlainVideoArePicked()
{
    NzbInfo nzb;
    nzb.files = {file(QStringLiteral("a.mp4"), 3'000'000'000LL, 10),
                 file(QStringLiteral("a.par2"), 40'000),
                 file(QStringLiteral("a.vol000+001.par2"), 1'000'000),
                 file(QStringLiteral("preview.jpg"), 300'000)};
    const auto plan = UsenetImagePreview::plan(nzb);
    QCOMPARE(plan.images, QList<int>{3});
    QCOMPARE(plan.video, 0);
    QVERIFY(!plan.videoInArchive);
}

void tst_UsenetImagePreview::theOpeningVolumeOfTheBiggestSetIsTheVideo()
{
    NzbInfo nzb;
    nzb.files = {file(QStringLiteral("x.nzb"), 1'500'000),
                 file(QStringLiteral("x.part002.rar"), 26'000'000, 3),
                 file(QStringLiteral("x.part001.rar"), 26'000'000, 3),
                 file(QStringLiteral("x.part003.rar"), 20'000'000, 3),
                 file(QStringLiteral("sample.mkv"), 5'000'000)};
    const auto plan = UsenetImagePreview::plan(nzb);
    QVERIFY(plan.images.isEmpty());
    QCOMPARE(plan.video, 2);
    QVERIFY(plan.videoInArchive);
}

void tst_UsenetImagePreview::oversizedImagesAndPar2AreLeftOut()
{
    NzbInfo nzb;
    nzb.files = {file(QStringLiteral("huge.png"), UsenetImagePreview::kMaxImageBytes + 1),
                 file(QStringLiteral("cover.PNG"), 50'000)};
    for (int i = 0; i < UsenetImagePreview::kMaxImages + 3; ++i)
        nzb.files.append(file(QStringLiteral("shot%1.jpg").arg(i), 10'000));
    const auto plan = UsenetImagePreview::plan(nzb);
    QVERIFY(!plan.images.contains(0));
    QCOMPARE(plan.images.first(), 1);
    QCOMPARE(plan.images.size(), UsenetImagePreview::kMaxImages);
    QCOMPARE(plan.video, -1);
}

void tst_UsenetImagePreview::noServerEndsWithANote()
{
    NzbInfo nzb;
    nzb.files = {file(QStringLiteral("preview.jpg"), 300'000)};
    UsenetImagePreview job({}, QNetworkProxy(QNetworkProxy::NoProxy));
    QSignalSpy done(&job, &UsenetImagePreview::finished);
    job.start(nzb);
    QCOMPARE(done.size(), 1);
    QVERIFY(done.first().at(0).value<QList<UsenetPreviewImage>>().isEmpty());
    QVERIFY(!done.first().at(1).toString().isEmpty());
}

void tst_UsenetImagePreview::serversOverTheirAllowanceAreNotAsked()
{
    // the daemon switches an over-quota account off before the grab sees it
    eMule::NewsServer spent;
    spent.host = QStringLiteral("news.example.invalid");
    spent.port = 563;
    spent.enabled = false;
    NzbInfo nzb;
    nzb.files = {file(QStringLiteral("r.nfo"), 20'000)};
    UsenetNfoPreview job({spent}, QNetworkProxy(QNetworkProxy::NoProxy));
    QSignalSpy done(&job, &UsenetNfoPreview::finished);
    QSignalSpy used(&job, &UsenetArticleGrab::bytesUsed);
    job.start(nzb);
    QCOMPARE(done.size(), 1);
    QVERIFY(done.first().at(0).value<QList<UsenetNfoText>>().isEmpty());
    QVERIFY(!done.first().at(1).toString().isEmpty());
    QCOMPARE(used.size(), 0);
}

void tst_UsenetImagePreview::nfoFilesArePicked()
{
    NzbInfo nzb;
    nzb.files = {file(QStringLiteral("a.mkv"), 3'000'000'000LL, 10),
                 file(QStringLiteral("a.mkv-[N-Z-B]_mediainfo.nfo"), 25'000),
                 file(QStringLiteral("a.par2"), 40'000),
                 file(QStringLiteral("huge.nfo"), UsenetNfoPreview::kMaxNfoBytes + 1),
                 file(QStringLiteral("B.NFO"), 4'000)};
    QCOMPARE(UsenetNfoPreview::plan(nzb), (QList<int>{1, 4}));

    NzbInfo none;
    none.files = {file(QStringLiteral("a.mkv"), 3'000'000'000LL, 10), file(QStringLiteral("a.nfo.par2"), 4'000)};
    QVERIFY(UsenetNfoPreview::plan(none).isEmpty());
}

void tst_UsenetImagePreview::harryListsItsNfo()
{
    const QString path = QStringLiteral(EMULE_STRINGIFY(EMULE_PROJECT_DATA_DIR) "/nzb/harry.nzb");
    if (!QFile::exists(path))
        QSKIP("data/nzb/harry.nzb is not here (not in git)");
    NzbInfo nzb;
    QString error;
    QVERIFY2(NzbFile::parseFile(path, nzb, error), qPrintable(error));
    const QList<int> nfos = UsenetNfoPreview::plan(nzb);
    QCOMPARE(nfos.size(), 1);
    QVERIFY(nzb.files.at(nfos.first()).fileName.endsWith(QStringLiteral("_mediainfo.nfo")));
    // the .mkv gives the Images tab its frames
    QVERIFY(UsenetImagePreview::plan(nzb).video >= 0);
}

void tst_UsenetImagePreview::nfoDecodesUtf8OrCp437()
{
    // UTF-8 stays UTF-8; CRLF and the DOS end mark go
    QCOMPARE(UsenetNfoPreview::decode(QByteArrayLiteral("Format : Matroska\r\nTitle : Bäcker\r\n\r\n\x1A")),
             QStringLiteral("Format : Matroska\nTitle : Bäcker"));
    // not UTF-8: the DOS code page, block art included
    QCOMPARE(UsenetNfoPreview::decode(QByteArrayLiteral("\xDC\xDC\xDB\xDF \x84 \xB0\xB1\xB2")),
             QStringLiteral("▄▄█▀ ä ░▒▓"));
    // a hole from a missing article ends the text
    QCOMPARE(UsenetNfoPreview::decode(QByteArray("head\0\0\0tail", 11)), QStringLiteral("head"));
}

QTEST_GUILESS_MAIN(tst_UsenetImagePreview)
#include "tst_UsenetImagePreview.moc"
