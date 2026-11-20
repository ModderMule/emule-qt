/// @file tst_UsenetImagePreview.cpp
/// @brief Which NZB files search Details › Images and › NFO read, and how an
///        NFO is decoded, offline. The fetch itself runs against a provider in
///        tst_UsenetLivePreviewImages.

#include "nzb/NzbFile.h"
#include "stream/Mp4Index.h"
#include "stream/UsenetImagePreview.h"
#include "stream/UsenetNfoPreview.h"

#include "TestHelpers.h"

#include <QFile>

#include <QSignalSpy>
#include <QTest>
#include <QtEndian>

using namespace eMule::usenet;

namespace {

// ISO BMFF boxes for the mp4 index tests
QByteArray be32(quint32 v)
{
    QByteArray b(4, 0);
    qToBigEndian(v, b.data());
    return b;
}

QByteArray box(const char* type, const QByteArray& payload)
{
    return be32(quint32(8 + payload.size())) + QByteArray(type, 4) + payload;
}

QByteArray fullBox(const char* type, const QByteArray& payload)
{
    return box(type, be32(0) + payload);
}

/// One video track: 6 samples of 100 bytes in 2 chunks (3 each) at 1000 and 5000,
/// 1 s per sample at timescale 1000, keyframes 1 and 4. Plus a one-sample cover track.
QByteArray testMoov()
{
    auto trak = [](const char* handler, const QByteArray& stbl) {
        const QByteArray mdhd = fullBox("mdhd", be32(0) + be32(0) + be32(1000) + be32(6000) + be32(0));
        const QByteArray hdlr = fullBox("hdlr", be32(0) + QByteArray(handler, 4) + QByteArray(12, 0));
        return box("trak", box("mdia", mdhd + hdlr + box("minf", box("stbl", stbl))));
    };
    QByteArray video = fullBox("stts", be32(1) + be32(6) + be32(1000))
        + fullBox("stss", be32(2) + be32(1) + be32(4))
        + fullBox("stsc", be32(1) + be32(1) + be32(3) + be32(1))
        + fullBox("stsz", be32(100) + be32(6))
        + fullBox("stco", be32(2) + be32(1000) + be32(5000));
    QByteArray cover = fullBox("stts", be32(1) + be32(1) + be32(1000))
        + fullBox("stsc", be32(1) + be32(1) + be32(1) + be32(1))
        + fullBox("stsz", be32(50) + be32(1))
        + fullBox("stco", be32(1) + be32(9000));
    return box("moov", fullBox("mvhd", QByteArray(96, 0)) + trak("vide", cover) + trak("vide", video));
}


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
    void mp4MoovIsFoundBehindMdat();
    void mp4KeyframesHaveTimeAndOffset();
    void mp4KeyframesSpreadOverTheRuntime();
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

void tst_UsenetImagePreview::mp4MoovIsFoundBehindMdat()
{
    const QByteArray ftyp = box("ftyp", QByteArray("isom") + be32(512));
    // mdat header only; its body is "not received"
    const qint64 mdatSize = 1'000'000;
    const QByteArray moov = testMoov();
    const qint64 moovAt = ftyp.size() + mdatSize;
    const QByteArray head = ftyp + be32(quint32(mdatSize)) + QByteArray("mdat");
    auto read = [&](qint64 offset, qint64 length) -> QByteArray {
        if (offset < head.size())
            return head.mid(offset, length);
        if (offset >= moovAt)
            return moov.mid(offset - moovAt, length);
        return {};
    };
    const Mp4MoovSpot spot = findMp4Moov(read, moovAt + moov.size());
    QCOMPARE(spot.state, Mp4MoovSpot::State::Found);
    QCOMPARE(spot.offset, moovAt);
    QCOMPARE(spot.size, qint64(moov.size()));

    // the header behind mdat not there yet: ask for it
    auto partial = [&](qint64 offset, qint64 length) { return offset < head.size() ? head.mid(offset, length) : QByteArray(); };
    const Mp4MoovSpot need = findMp4Moov(partial, moovAt + moov.size());
    QCOMPARE(need.state, Mp4MoovSpot::State::NeedBytes);
    QCOMPARE(need.offset, moovAt);

    // fragmented and non-mp4 are refused
    QCOMPARE(findMp4Moov([](qint64, qint64) { return QByteArray("RIFFxxxxAVI LIST"); }, 100).state,
             Mp4MoovSpot::State::Unusable);
    const QByteArray frag = ftyp + box("moof", QByteArray(8, 0));
    QCOMPARE(findMp4Moov([&](qint64 o, qint64 l) { return frag.mid(o, l); }, frag.size()).state,
             Mp4MoovSpot::State::Unusable);
}

void tst_UsenetImagePreview::mp4KeyframesHaveTimeAndOffset()
{
    // the 6-sample track wins over the cover
    const QList<Mp4Keyframe> keys = mp4VideoKeyframes(testMoov());
    QCOMPARE(keys.size(), 2);
    QCOMPARE(keys.at(0).timeMs, 0);
    QCOMPARE(keys.at(0).offset, 1000);
    QCOMPARE(keys.at(0).size, 100);
    QCOMPARE(keys.at(1).timeMs, 3000);
    QCOMPARE(keys.at(1).offset, 5000);   // first sample of chunk 2
    QVERIFY(mp4VideoKeyframes(QByteArray("garbage")).isEmpty());
}

void tst_UsenetImagePreview::mp4KeyframesSpreadOverTheRuntime()
{
    QList<Mp4Keyframe> all;
    for (int i = 0; i < 600; ++i)
        all.append({qint64(i) * 2000, qint64(i) * 1'000'000, 50'000});   // 20 min, key every 2 s
    const auto spread = spreadKeyframes(all, 8);
    QCOMPARE(spread.size(), 8);
    QVERIFY(spread.front().timeMs > 60'000);                 // clear of the intro
    QVERIFY(spread.back().timeMs < all.back().timeMs - 60'000);
    for (qsizetype i = 1; i < spread.size(); ++i)
        QVERIFY(spread.at(i).timeMs > spread.at(i - 1).timeMs);
    // fewer keyframes than asked: each once, the one at 0 s left out
    const auto few = spreadKeyframes(all.mid(0, 3), 8);
    QCOMPARE(few.size(), 2);
    QCOMPARE(few.at(0).timeMs, 2000);
    QCOMPARE(few.at(1).timeMs, 4000);
}

QTEST_GUILESS_MAIN(tst_UsenetImagePreview)
#include "tst_UsenetImagePreview.moc"
