/// @file tst_UsenetStream.cpp
/// @brief Phase 6a: what makes a half-downloaded release playable.
///
/// Three things have to hold, and none of them is visible from the layers
/// below:
///
///   - **Articles are fetched in part order.** Out of order the file still
///     assembles correctly — yEnc parts are self-locating — but the readable
///     prefix grows in holes, and a player can only read forward from byte 0.
///   - **A retry goes back to the front of the queue, not the end.** One early
///     article refetched last pins the prefix at that byte for the whole
///     download, which is the difference between preview working and preview
///     never starting.
///   - **`written` records where bytes actually landed.** The `done` bitmap
///     cannot answer it: its bit means *resolved*, and an article missing on
///     every server sets it having written nothing.

#include "FakeNntpServer.h"
#include "UsenetPostingHarness.h"
#include "RarFixtures.h"
#include "TestHelpers.h"

#include "decode/YencDecoder.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"
#include "queue/UsenetQueueStore.h"

#include "prefs/Preferences.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace eMule::usenet;
using namespace eMule::testing::usenet;
using eMule::testing::FakeNntpServer;

namespace {

constexpr int kPartSize = 2000;

QByteArray payload(int size)
{
    QByteArray data;
    data.resize(size);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 31 + (i >> 5) * 7) & 0xFF);
    return data;
}

/// One yEnc article as a provider serves it, 1-based `begin` included.
QByteArray makeArticle(const QByteArray& whole, int part, int total, const QString& name)
{
    const int offset = (part - 1) * kPartSize;
    const QByteArray chunk = whole.mid(offset, kPartSize);

    QByteArray out;
    out += QStringLiteral("=ybegin part=%1 total=%2 line=128 size=%3 name=%4")
               .arg(part).arg(total).arg(whole.size()).arg(name).toLatin1();
    out += '\n';
    out += QStringLiteral("=ypart begin=%1 end=%2")
               .arg(offset + 1).arg(offset + chunk.size()).toLatin1();
    out += '\n';

    QByteArray line;
    for (const char raw : chunk) {
        const auto enc = quint8(quint8(raw) + 42);
        if (enc == 0x00 || enc == 0x0A || enc == 0x0D || enc == '=') {
            line.append('=');
            line.append(char(quint8(enc + 64)));
        } else {
            line.append(char(enc));
        }
        if (line.size() >= 128) {
            out += line;
            out += '\n';
            line.clear();
        }
    }
    if (!line.isEmpty()) {
        out += line;
        out += '\n';
    }

    out += QStringLiteral("=yend size=%1 part=%2 pcrc32=%3")
               .arg(chunk.size()).arg(part)
               .arg(yencCrc32(0, chunk), 8, 16, QLatin1Char('0')).toLatin1();
    return out;
}

QString messageIdFor(int part) { return QStringLiteral("s%1@example.com").arg(part); }

/// An NZB whose `<segment>` elements are emitted in @p documentOrder, each
/// carrying its true `number=`. Real NZBs are not reliably sorted and the
/// parser does not sort them, so this is what the scheduler has to cope with.
QByteArray makeNzb(const QString& fileName, const QList<int>& documentOrder)
{
    QByteArray xml;
    xml += R"(<?xml version="1.0" encoding="iso-8859-1" ?>)"
           "\n<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";
    xml += QStringLiteral(
               "  <file poster=\"tester\" date=\"1700000000\" "
               "subject=\"&quot;%1&quot; yEnc (1/%2)\">\n")
               .arg(fileName).arg(documentOrder.size()).toUtf8();
    xml += "    <groups><group>alt.binaries.test</group></groups>\n";
    xml += "    <segments>\n";
    for (const int p : documentOrder) {
        xml += QStringLiteral("      <segment bytes=\"2800\" number=\"%1\">%2</segment>\n")
                   .arg(p).arg(messageIdFor(p)).toUtf8();
    }
    xml += "    </segments>\n  </file>\n</nzb>\n";
    return xml;
}

} // namespace

class tst_UsenetStream : public QObject {
    Q_OBJECT

private slots:
    void writtenRangesMergeAndStopAtTheFirstHole();
    void writtenRangesSurviveARestart();
    void previewableRejectsPar2AndNonMedia();
    void articlesAreFetchedInPartOrder();
    void aFailedArticleIsRetriedBeforeLaterOnes();
    void streamingResolvesToAFileAndItsAvailablePrefix();

    // Phase 6b
    void aMultiFileSetOffersEveryFileInsideIt();
    void withNoEntryNamedTheFirstPlayableFileIsStreamed();
    void eachFileOfAMultiFileSetMapsToItsOwnBytes();
    void listingAPausedItemNeitherFetchesNorGuesses();
    void aStoredRarSetResolvesToTheFileInsideIt();
    void aSeekIntoAStoredRarSetSkipsTheVolumesBetween();
    void aCompressedRarSetSaysWhyItCannotBeStreamed();
    void aNumberedSplitSetResolvesToOneLogicalFile();
};

// ---------------------------------------------------------------------------
// The availability model
// ---------------------------------------------------------------------------

void tst_UsenetStream::writtenRangesMergeAndStopAtTheFirstHole()
{
    UsenetFileState st;

    // Out of order, adjacent, and overlapping — all three happen. Two workers
    // finishing near-simultaneously produce the first, consecutive parts the
    // second, and a re-fetched article the third.
    st.addWritten(1000, 1000);
    st.addWritten(0, 1000);
    QCOMPARE(st.written.size(), 1);
    QCOMPARE(st.written.first().first, 0);
    QCOMPARE(st.written.first().second, 2000);
    QCOMPARE(st.availableEnd(), 2000);

    // A gap: bytes exist, but not reachable by reading forward from 0.
    st.addWritten(3000, 1000);
    QCOMPARE(st.written.size(), 2);
    QCOMPARE(st.availableEnd(), 2000);

    // Filling the gap coalesces all three into one run.
    st.addWritten(2000, 1000);
    QCOMPARE(st.written.size(), 1);
    QCOMPARE(st.availableEnd(), 4000);

    // Re-adding a range already covered changes nothing.
    st.addWritten(500, 500);
    QCOMPARE(st.written.size(), 1);
    QCOMPARE(st.availableEnd(), 4000);

    // Nothing at byte 0 means nothing streamable, however much has arrived.
    UsenetFileState later;
    later.addWritten(4096, 8192);
    QCOMPARE(later.availableEnd(), 0);

    // Degenerate inputs must not create a phantom range.
    UsenetFileState empty;
    empty.addWritten(0, 0);
    empty.addWritten(-5, 100);
    QVERIFY(empty.written.isEmpty());
    QCOMPARE(empty.availableEnd(), 0);
}

void tst_UsenetStream::writtenRangesSurviveARestart()
{
    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    UsenetQueueItem item;
    item.id = QStringLiteral("stream-resume");
    item.name = QStringLiteral("release");

    NzbFileInfo info;
    info.fileName = QStringLiteral("movie.mkv");
    for (int p = 1; p <= 4; ++p)
        info.segments.append(NzbSegment{messageIdFor(p), 2800, p});
    item.nzb.files.append(info);
    item.initFileStates(thePrefs.tempDirs().first());

    UsenetFileState& st = item.files[0];
    st.declaredSize = kPartSize * 4;
    st.done.setBit(0);
    st.done.setBit(1);
    st.addWritten(0, kPartSize);
    st.addWritten(kPartSize, kPartSize);
    // A third part that landed out of order, leaving a hole behind it.
    st.addWritten(3 * kPartSize, kPartSize);

    QVERIFY(UsenetQueueStore::save(item));

    UsenetQueueItem loaded;
    QString error;
    QVERIFY2(UsenetQueueStore::load(UsenetQueueStore::statePath(item.id), loaded, error),
             qPrintable(error));

    QCOMPARE(loaded.files.size(), 1);
    const UsenetFileState& back = loaded.files.at(0);

    // Two runs, not three: the first two coalesced on the way in and stayed
    // coalesced on the way back.
    QCOMPARE(back.written.size(), 2);
    QCOMPARE(back.written.at(0), qMakePair(qint64(0), qint64(2 * kPartSize)));
    QCOMPARE(back.written.at(1),
             qMakePair(qint64(3 * kPartSize), qint64(4 * kPartSize)));
    QCOMPARE(back.availableEnd(), qint64(2 * kPartSize));

    // Without persistence a restart would report nothing readable while the
    // done bits still stopped those articles being refetched — preview dead for
    // the rest of the download, and silently.
    QVERIFY(back.availableEnd() > 0);
}

void tst_UsenetStream::previewableRejectsPar2AndNonMedia()
{
    UsenetQueueItem item;
    item.id = QStringLiteral("preview-gate");

    const QStringList names{
        QStringLiteral("movie.mkv"),          // yes
        QStringLiteral("release.par2"),       // no — recovery set
        QStringLiteral("release.part01.rar"), // no — an archive is not playable
        QStringLiteral("readme.nfo"),         // no
        QStringLiteral("soundtrack.flac"),    // yes
    };
    for (const QString& n : names) {
        NzbFileInfo info;
        info.fileName = n;
        info.segments.append(NzbSegment{QStringLiteral("x@e"), 100, 1});
        item.nzb.files.append(info);
    }
    item.files.resize(item.nzb.files.size());

    QVERIFY(item.isFilePreviewable(0));
    QVERIFY(!item.isFilePreviewable(1));
    QVERIFY(!item.isFilePreviewable(2));
    QVERIFY(!item.isFilePreviewable(3));
    QVERIFY(item.isFilePreviewable(4));

    // Out of range is a question, not a crash.
    QVERIFY(!item.isFilePreviewable(-1));
    QVERIFY(!item.isFilePreviewable(99));

    // An obfuscated post has no usable subject name, so the answer is no until
    // the first article's =ybegin supplies the real one.
    UsenetQueueItem obfuscated;
    NzbFileInfo blank;
    blank.segments.append(NzbSegment{QStringLiteral("y@e"), 100, 1});
    obfuscated.nzb.files.append(blank);
    obfuscated.files.resize(1);
    QVERIFY(!obfuscated.isFilePreviewable(0));

    obfuscated.files[0].articleFileName = QStringLiteral("feature.mp4");
    QVERIFY(obfuscated.isFilePreviewable(0));
}

// ---------------------------------------------------------------------------
// The scheduler
// ---------------------------------------------------------------------------

void tst_UsenetStream::articlesAreFetchedInPartOrder()
{
    constexpr int kParts = 6;
    const QByteArray whole = payload(kPartSize * kParts);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        server.addArticle(messageIdFor(p),
                          makeArticle(whole, p, kParts, QStringLiteral("movie.mkv")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    UsenetQueue queue;
    // One connection, so the request stream is a single ordered sequence and the
    // assertion is about the schedule rather than about which worker won a race.
    queue.applyServers({serverConfig(port, 1)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);

    // Deliberately shuffled in the document. An NZB may list its segments in any
    // order and `number=` is the only authority; a scheduler that walked
    // document order would fetch 4,1,6,2,5,3 and the readable prefix would grow
    // in fragments.
    QString error;
    const QString id = queue.addNzb(
        makeNzb(QStringLiteral("movie.mkv"), {4, 1, 6, 2, 5, 3}),
        QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(30000), "the download never reported a terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(), "completed with missing articles");

    QStringList expected;
    for (int p = 1; p <= kParts; ++p)
        expected << QStringLiteral("<%1>").arg(messageIdFor(p));

    QCOMPARE(bodyOrder(server), expected);

    queue.stop();
}

void tst_UsenetStream::aFailedArticleIsRetriedBeforeLaterOnes()
{
    constexpr int kParts = 6;
    const QByteArray whole = payload(kPartSize * kParts);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        server.addArticle(messageIdFor(p),
                          makeArticle(whole, p, kParts, QStringLiteral("movie.mkv")));

    // Article 2 dies once at the transport level — the commonest Usenet failure
    // there is, and the branch that retries the *same* level rather than
    // climbing the ladder.
    server.setDropOnArticle(messageIdFor(2), 1);

    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 1)}, 1);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("movie.mkv"), {1, 2, 3, 4, 5, 6}),
                                    QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(60000), "the download never reported a terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(), "completed with missing articles");

    const QStringList order = bodyOrder(server);
    const QString second = QStringLiteral("<%1>").arg(messageIdFor(2));
    const QString third = QStringLiteral("<%1>").arg(messageIdFor(3));

    // Asked for twice: once for the drop, once for the retry.
    QCOMPARE(order.count(second), 2);

    // And the retry came before article 3 was ever asked for. Appending it to
    // the tail instead — which is what the code did before phase 6a — puts the
    // retry after every other article, so the readable prefix stays stuck at
    // 2000 bytes until the download is otherwise finished.
    QVERIFY2(order.lastIndexOf(second) < order.indexOf(third),
             qPrintable(order.join(QStringLiteral(" "))));

    // The file still assembles byte for byte. Reordering the schedule must not
    // disturb where parts land — they place themselves.
    QFile f(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("movie.mkv")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), whole);

    queue.stop();
}

void tst_UsenetStream::streamingResolvesToAFileAndItsAvailablePrefix()
{
    constexpr int kParts = 4;
    const QByteArray whole = payload(kPartSize * kParts);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        server.addArticle(messageIdFor(p),
                          makeArticle(whole, p, kParts, QStringLiteral("movie.mkv")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 1)}, 60);
    queue.start();

    // Nothing queued yet: a stale preview URL must answer "no", not crash.
    QVERIFY(!queue.requestStream(QStringLiteral("no-such-item"), 0).found);

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("movie.mkv"), {1, 2, 3, 4}),
                                    QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    // A file index the NZB does not have.
    QVERIFY(!queue.requestStream(id, 7).found);

    QVERIFY2(finished.wait(30000), "the download never reported a terminal outcome");

    const auto info = queue.requestStream(id, 0);
    QVERIFY(info.found);
    QVERIFY(info.complete);
    QCOMPARE(info.totalSize, qint64(kPartSize * kParts));
    // Sealed, so the whole file is readable and the interval list is one entry.
    QCOMPARE(info.availableEnd, qint64(kPartSize * kParts));
    QCOMPARE(info.fileName, QStringLiteral("movie.mkv"));

    // A raw-posted file is one piece covering the whole thing — the degenerate
    // case of the extent map a RAR set needs.
    QCOMPARE(info.pieces.size(), 1);
    QCOMPARE(info.pieces.first().virtualOffset, qint64(0));
    QCOMPARE(info.pieces.first().fileOffset, qint64(0));
    QCOMPARE(info.pieces.first().length, qint64(kPartSize * kParts));
    QVERIFY2(QFile::exists(info.pieces.first().path), qPrintable(info.pieces.first().path));

    queue.stop();
}


// ---------------------------------------------------------------------------
// Phase 6b — stored RAR sets
//
// The payload of a stored (`-m0`) volume is a byte-identical slice of the
// original file, so playing a release is reading the right volume at the right
// offset. These cases post real volumes as yEnc articles and stream them back.
// ---------------------------------------------------------------------------

namespace {

constexpr int kRarPartSize = 1600;
constexpr qint64 kVolumePayload = 3000;
constexpr int kVolumeCount = 7;

/// Post a stored RAR set and return the NZB. @p out receives the inner payload.
QByteArray postStoredRarSet(FakeNntpServer& server, QByteArray& innerPayload,
                            QList<QByteArray>& volumes, bool compressed = false)
{
    innerPayload = payload(int(kVolumePayload * kVolumeCount));
    volumes = compressed
                  ? eMule::testing::rar::makeCompressedRarSet("Some.Release.mkv", innerPayload,
                                                              kVolumePayload)
                  : eMule::testing::rar::makeStoredRarSet("Some.Release.mkv", innerPayload,
                                                          kVolumePayload);

    QList<PostedFile> files;
    for (int i = 0; i < volumes.size(); ++i) {
        files.append({QStringLiteral("Some.Release.part%1.rar")
                          .arg(i + 1, 2, 10, QLatin1Char('0')),
                      volumes.at(i)});
    }
    return postFiles(server, files, kRarPartSize);
}

constexpr qint64 kNfoSize = 500;
constexpr qint64 kFeatureSize = 20000;

/// A set holding a small `.nfo` ahead of the feature — the layout that used to
/// be mapped as a 500-byte "movie". The `.nfo` does not end on a volume
/// boundary, so the feature's own header sits mid-volume.
QByteArray postMultiFileRarSet(FakeNntpServer& server, QByteArray& nfo, QByteArray& feature,
                               QList<QByteArray>& volumes)
{
    nfo = QByteArray(int(kNfoSize), 'N');
    feature = payload(int(kFeatureSize));
    volumes = eMule::testing::rar::makeStoredRarSet(
        {{"intro.nfo", nfo}, {"Some.Release.mkv", feature}}, kVolumePayload);

    QList<PostedFile> files;
    for (int i = 0; i < volumes.size(); ++i) {
        files.append({QStringLiteral("Some.Release.part%1.rar")
                          .arg(i + 1, 2, 10, QLatin1Char('0')),
                      volumes.at(i)});
    }
    return postFiles(server, files, kRarPartSize);
}

} // namespace

void tst_UsenetStream::aMultiFileSetOffersEveryFileInsideIt()
{
    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    FakeNntpServer server;
    QVERIFY(server.listen());
    server.addGroup(QStringLiteral("alt.binaries.test"), 1, 1, 1);

    QByteArray nfo, feature;
    QList<QByteArray> volumes;
    const QByteArray nzb = postMultiFileRarSet(server, nfo, feature, volumes);
    QCOMPARE(volumes.size(), 7);   // pins the fixture the offsets below assume

    UsenetQueue queue;
    queue.applyServers({serverConfig(server.serverPort(), 2)}, 60);
    queue.setPostProcessingOptions({.par2 = false, .rename = false, .unpack = false,
                                   .cleanup = false, .directUnpack = false});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(60000), "the download never reported a terminal outcome");

    const auto listing = queue.listArchiveEntries(id, 0);
    QCOMPARE(listing.status, UsenetQueue::ArchiveListing::Status::Complete);
    QCOMPARE(listing.entries.size(), 2);

    QCOMPARE(listing.entries.at(0).entry, 0);
    QCOMPARE(listing.entries.at(0).name, QStringLiteral("intro.nfo"));
    QCOMPARE(listing.entries.at(0).size, kNfoSize);
    QVERIFY(!listing.entries.at(0).playable);
    QVERIFY(!listing.entries.at(0).note.isEmpty());   // a greyed row must say why

    QCOMPARE(listing.entries.at(1).entry, 1);
    QCOMPARE(listing.entries.at(1).name, QStringLiteral("Some.Release.mkv"));
    QCOMPARE(listing.entries.at(1).size, kFeatureSize);
    QVERIFY(listing.entries.at(1).playable);

    // Asking through any volume describes the same set, because the ordinal is
    // a property of the set rather than of the volume clicked.
    const auto viaLast = queue.listArchiveEntries(id, int(volumes.size()) - 1);
    QCOMPARE(viaLast.entries.size(), 2);
    QCOMPARE(viaLast.entries.at(1).name, QStringLiteral("Some.Release.mkv"));

    queue.stop();
}

void tst_UsenetStream::withNoEntryNamedTheFirstPlayableFileIsStreamed()
{
    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    FakeNntpServer server;
    QVERIFY(server.listen());
    server.addGroup(QStringLiteral("alt.binaries.test"), 1, 1, 1);

    QByteArray nfo, feature;
    QList<QByteArray> volumes;
    const QByteArray nzb = postMultiFileRarSet(server, nfo, feature, volumes);

    UsenetQueue queue;
    queue.applyServers({serverConfig(server.serverPort(), 2)}, 60);
    queue.setPostProcessingOptions({.par2 = false, .rename = false, .unpack = false,
                                   .cleanup = false, .directUnpack = false});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(60000), "the download never reported a terminal outcome");

    // The headline fix, and it needs no client change: a URL with no `entry=`
    // used to serve 500 bytes of .nfo as the movie.
    const auto info = queue.requestStream(id, 0, 0, 4096);
    QVERIFY(info.found);
    QVERIFY2(info.notSeekableReason.isEmpty(), qPrintable(info.notSeekableReason));
    QCOMPARE(info.fileName, QStringLiteral("Some.Release.mkv"));
    QCOMPARE(info.totalSize, kFeatureSize);
    QCOMPARE(readThroughPieces(info.pieces, 0, int(kFeatureSize)), feature);

    // And the daemon no longer claims the archive holds nothing playable.
    const auto preview = queue.previewability(id, 0);
    QVERIFY2(preview.previewable, qPrintable(preview.note));
    QVERIFY(preview.note.isEmpty());

    queue.stop();
}

void tst_UsenetStream::eachFileOfAMultiFileSetMapsToItsOwnBytes()
{
    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    FakeNntpServer server;
    QVERIFY(server.listen());
    server.addGroup(QStringLiteral("alt.binaries.test"), 1, 1, 1);

    QByteArray nfo, feature;
    QList<QByteArray> volumes;
    const QByteArray nzb = postMultiFileRarSet(server, nfo, feature, volumes);

    UsenetQueue queue;
    queue.applyServers({serverConfig(server.serverPort(), 2)}, 60);
    queue.setPostProcessingOptions({.par2 = false, .rename = false, .unpack = false,
                                   .cleanup = false, .directUnpack = false});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(60000), "the download never reported a terminal outcome");

    // Entry 0 is the .nfo, wholly inside volume 1.
    const auto first = queue.requestStream(id, 0, 0, 4096, /*entry*/ 0);
    QCOMPARE(first.fileName, QStringLiteral("intro.nfo"));
    QCOMPARE(first.totalSize, kNfoSize);
    QCOMPARE(first.pieces.size(), 1);
    QCOMPARE(readThroughPieces(first.pieces, 0, int(kNfoSize)), nfo);

    // Entry 1 is the feature, starting mid-volume and running to the last one.
    const auto second = queue.requestStream(id, 0, 0, 4096, /*entry*/ 1);
    QCOMPARE(second.fileName, QStringLiteral("Some.Release.mkv"));
    QCOMPARE(second.totalSize, kFeatureSize);
    QCOMPARE(second.pieces.size(), volumes.size());
    QCOMPARE(readThroughPieces(second.pieces, 0, int(kFeatureSize)), feature);

    // Its first volume carries only what was left after the .nfo, so the very
    // first extent is short — the case a set-wide uniformity model gets wrong.
    QCOMPARE(second.pieces.at(0).length, kVolumePayload - kNfoSize);

    // A window straddling that first boundary is where an off-by-one hides.
    const qint64 boundary = kVolumePayload - kNfoSize - 50;
    QCOMPARE(readThroughPieces(second.pieces, boundary, 100),
             feature.mid(int(boundary), 100));

    // An ordinal past the end is refused rather than silently clamped.
    const auto missing = queue.requestStream(id, 0, 0, 4096, /*entry*/ 9);
    QVERIFY(missing.found);
    QVERIFY(missing.pieces.isEmpty());

    queue.stop();
}

void tst_UsenetStream::listingAPausedItemNeitherFetchesNorGuesses()
{
    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    FakeNntpServer server;
    QVERIFY(server.listen());
    server.addGroup(QStringLiteral("alt.binaries.test"), 1, 1, 1);

    QByteArray nfo, feature;
    QList<QByteArray> volumes;
    const QByteArray nzb = postMultiFileRarSet(server, nfo, feature, volumes);

    UsenetQueue queue;
    queue.applyServers({serverConfig(server.serverPort(), 2)}, 60);
    queue.setPostProcessingOptions({.par2 = false, .rename = false, .unpack = false,
                                   .cleanup = false, .directUnpack = false});

    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY(queue.pauseItem(id));

    // Nothing on disk and nothing being fetched: the listing says so rather
    // than inventing rows or blocking. This is what separates it from
    // previewability(), which never promotes at all.
    const auto paused = queue.listArchiveEntries(id, 0);
    QCOMPARE(paused.status, UsenetQueue::ArchiveListing::Status::Unknown);
    QVERIFY(paused.entries.isEmpty());

    queue.start();
    QVERIFY(queue.resumeItem(id));
    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QVERIFY2(finished.wait(60000), "the download never reported a terminal outcome");

    const auto resumed = queue.listArchiveEntries(id, 0);
    QCOMPARE(resumed.status, UsenetQueue::ArchiveListing::Status::Complete);
    QCOMPARE(resumed.entries.size(), 2);

    queue.stop();
}

void tst_UsenetStream::aStoredRarSetResolvesToTheFileInsideIt()
{
    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    FakeNntpServer server;
    QVERIFY(server.listen());
    server.addGroup(QStringLiteral("alt.binaries.test"), 1, 1, 1);

    QByteArray inner;
    QList<QByteArray> volumes;
    const QByteArray nzb = postStoredRarSet(server, inner, volumes);

    UsenetQueue queue;
    queue.applyServers({serverConfig(server.serverPort(), 2)}, 60);
    // No post-processing: unpack and cleanup would delete the very volumes
    // this case streams from.
    queue.setPostProcessingOptions({.par2 = false, .rename = false, .unpack = false,
                                   .cleanup = false, .directUnpack = false});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(60000), "the download never reported a terminal outcome");

    // Any volume resolves to the same inner file — which is what lets the GUI
    // put Preview on every `.rar` row without inventing a new kind of row.
    for (const int volume : {0, 3, kVolumeCount - 1}) {
        const auto info = queue.requestStream(id, volume, 0, 4096);
        QVERIFY2(info.found, qPrintable(QStringLiteral("volume %1").arg(volume)));
        QVERIFY2(info.notSeekableReason.isEmpty(), qPrintable(info.notSeekableReason));
        QCOMPARE(info.fileName, QStringLiteral("Some.Release.mkv"));
        QCOMPARE(info.totalSize, qint64(inner.size()));
        QCOMPARE(info.pieces.size(), kVolumeCount);
    }

    const auto info = queue.requestStream(id, 0, 0, 4096);

    // Every volume placed exactly: the payload starts after the archive header,
    // and the header is the same length in each because the inner name is.
    const qint64 headerLength = volumes.first().size() - kVolumePayload;
    for (int k = 0; k < kVolumeCount; ++k) {
        QCOMPARE(info.pieces.at(k).virtualOffset, qint64(k) * kVolumePayload);
        QCOMPARE(info.pieces.at(k).fileOffset, headerLength);
        QCOMPARE(info.pieces.at(k).length, kVolumePayload);
    }

    // The map is only right if reading through it reproduces the original file
    // byte for byte — including a window that straddles a volume boundary,
    // which is the case an off-by-one in the extent walk survives everywhere else.
    QCOMPARE(readThroughPieces(info.pieces, 0, inner.size()), inner);

    const qint64 boundary = kVolumePayload - 50;
    QCOMPARE(readThroughPieces(info.pieces, boundary, 100), inner.mid(int(boundary), 100));

    QCOMPARE(info.availableEnd, qint64(inner.size()));
    queue.stop();
}

void tst_UsenetStream::aSeekIntoAStoredRarSetSkipsTheVolumesBetween()
{
    // The exit criterion for phase 6b, asserted directly: a seek to 80% must
    // complete *without fetching the articles it skipped over*. Anything less is
    // Tier A with a container parser bolted on.
    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    FakeNntpServer server;
    QVERIFY(server.listen());
    server.addGroup(QStringLiteral("alt.binaries.test"), 1, 1, 1);

    QByteArray inner;
    QList<QByteArray> volumes;
    const QByteArray nzb = postStoredRarSet(server, inner, volumes);

    UsenetQueue queue;
    // One connection, so the sequential download cannot race ahead and fetch the
    // volumes this case is about while the seek is being served.
    queue.applyServers({serverConfig(server.serverPort(), 1)}, 60);
    queue.setPostProcessingOptions({.par2 = false, .rename = false, .unpack = false,
                                   .cleanup = false, .directUnpack = false});
    queue.start();

    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    // 80% of the inner file. Volume 6 of 7 holds it (0-based index 5), so the
    // index needs volumes 1, 2 and 7 for its uniformity check plus volume 6
    // itself — and volumes 3, 4 and 5 for nothing at all.
    const qint64 seekTo = qint64(inner.size()) * 4 / 5;
    QCOMPARE(int(seekTo / kVolumePayload), 5);

    bool served = false;
    for (int tick = 0; tick < 200 && !served; ++tick) {
        const auto info = queue.requestStream(id, 5, seekTo, 4096);
        QVERIFY2(info.notSeekableReason.isEmpty(), qPrintable(info.notSeekableReason));
        if (info.availableEnd > seekTo) {
            // Served from the right place, not merely "some bytes arrived".
            QCOMPARE(readThroughPieces(info.pieces, seekTo, 64), inner.mid(int(seekTo), 64));
            served = true;
            break;
        }
        QTest::qWait(50);
    }
    QVERIFY2(served, "the seek never became readable");

    const QStringList asked = bodyOrder(server);
    for (const int skipped : {2, 3, 4}) {
        const int parts = int((volumes.at(skipped).size() + kRarPartSize - 1) / kRarPartSize);
        for (int p = 1; p <= parts; ++p) {
            QVERIFY2(!asked.contains(multiId(skipped, p)),
                     qPrintable(QStringLiteral("volume %1 part %2 was fetched, but the seek "
                                               "was past it — %3 BODYs so far")
                                    .arg(skipped + 1).arg(p).arg(asked.size())));
        }
    }

    queue.stop();
}

void tst_UsenetStream::aCompressedRarSetSaysWhyItCannotBeStreamed()
{
    // §7.2: a compressed archive is seekable only at compression-block
    // boundaries, which is not usable. The requirement is that this is *said*,
    // before a player opens, rather than discovered at play time.
    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    FakeNntpServer server;
    QVERIFY(server.listen());
    server.addGroup(QStringLiteral("alt.binaries.test"), 1, 1, 1);

    QByteArray inner;
    QList<QByteArray> volumes;
    const QByteArray nzb = postStoredRarSet(server, inner, volumes, /*compressed*/ true);

    UsenetQueue queue;
    queue.applyServers({serverConfig(server.serverPort(), 2)}, 60);
    // No post-processing: unpack and cleanup would delete the very volumes
    // this case streams from.
    queue.setPostProcessingOptions({.par2 = false, .rename = false, .unpack = false,
                                   .cleanup = false, .directUnpack = false});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(60000), "the download never reported a terminal outcome");

    const auto info = queue.requestStream(id, 0, 0, 4096);
    QVERIFY(info.found);
    QVERIFY2(!info.notSeekableReason.isEmpty(), "a compressed set must say why, not just refuse");
    QVERIFY(info.pieces.isEmpty());

    // And the same answer reaches the GUI through the previewability seam, which
    // is what turns it into a tooltip on a disabled menu entry.
    const auto preview = queue.previewability(id, 0);
    QVERIFY(!preview.previewable);
    QCOMPARE(preview.note, info.notSeekableReason);

    queue.stop();
}

void tst_UsenetStream::aNumberedSplitSetResolvesToOneLogicalFile()
{
    // `Movie.mkv.001` — no container at all, so the map is concatenation. It
    // falls out of the same extent abstraction, which is the point of having one.
    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    FakeNntpServer server;
    QVERIFY(server.listen());
    server.addGroup(QStringLiteral("alt.binaries.test"), 1, 1, 1);

    const QByteArray whole = payload(9000);
    QList<PostedFile> files;
    for (int i = 0; i < 3; ++i) {
        files.append({QStringLiteral("Movie.mkv.%1").arg(i + 1, 3, 10, QLatin1Char('0')),
                      whole.mid(i * 3000, 3000)});
    }
    const QByteArray nzb = postFiles(server, files, 1600);

    UsenetQueue queue;
    queue.applyServers({serverConfig(server.serverPort(), 2)}, 60);
    // No post-processing: unpack and cleanup would delete the very volumes
    // this case streams from.
    queue.setPostProcessingOptions({.par2 = false, .rename = false, .unpack = false,
                                   .cleanup = false, .directUnpack = false});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(60000), "the download never reported a terminal outcome");

    const auto info = queue.requestStream(id, 1, 0, 4096);
    QVERIFY(info.found);
    QCOMPARE(info.fileName, QStringLiteral("Movie.mkv"));
    QCOMPARE(info.totalSize, qint64(whole.size()));
    QCOMPARE(info.pieces.size(), 3);
    QCOMPARE(readThroughPieces(info.pieces, 0, whole.size()), whole);

    queue.stop();
}

QTEST_MAIN(tst_UsenetStream)
#include "tst_UsenetStream.moc"
