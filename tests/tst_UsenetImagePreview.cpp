/// @file tst_UsenetImagePreview.cpp
/// @brief Which NZB files search Details › Images reads, offline. The fetch itself
///        runs against a provider in tst_UsenetLivePreviewImages.

#include "stream/UsenetImagePreview.h"

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

QTEST_GUILESS_MAIN(tst_UsenetImagePreview)
#include "tst_UsenetImagePreview.moc"
