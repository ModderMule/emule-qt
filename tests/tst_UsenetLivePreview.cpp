/// @file tst_UsenetLivePreview.cpp
/// @brief Streaming preview of a real release while it is still downloading.
///
/// The offline cases in tst_UsenetStream post RAR volumes this repository built
/// itself, so every offset they assert was computed from the format. That is the
/// right fixture for the parser and the wrong one for the question this test
/// asks: does the map survive an archive somebody else's packer produced, split
/// by somebody else's `-v` switch, indexed by somebody else's NZB?
///
/// Three answers are conformant, and the case asserts whichever it gets:
///
///   - a **stored** set streams straight out of its volumes — the listing
///     reaches a terminal status, a read at offset 0 returns bytes, and the HTTP
///     route answers 206 with the same bytes the queue handed back directly;
///   - a **compressed or solid** set cannot be mapped and never will be, but is
///     being extracted as it downloads, so it streams out of *that* instead —
///     206 again, from `_unpacked/`, with the archive's own declared size as the
///     total;
///   - a set with nothing playable in it at all must say so — with a
///     `notSeekableReason` and a 406 carrying it, instead of a player left to
///     time out. `Ubuntu.nzb` is that case twice over: one 999 MB volume, so
///     nothing is extracted until the whole thing is down, and a `.vdi` inside,
///     which is not something a player opens however it is unpacked.
///
/// Asserting only the first would make the test a lottery on a stranger's `-m`
/// switch.
///
/// PAR2, rename and cleanup are off — a repair would rewrite the volumes under
/// the map, and cleanup would delete them. Unpack and direct unpack are **on**,
/// and both are needed: pumpDirectUnpack() checks the pair, so `directUnpack`
/// alone extracts nothing and the second outcome above would look like a bug.
///
/// Environment: the provider variables from UsenetLiveEnv.h, plus
///
///   EMULE_NZB_DIR         directory of .nzb files (required)
///   EMULE_NZB_PREVIEW_MIN per-release budget, default 10 (optional)
///
/// Cheap by design: it fetches one volume at most, not the release. Whatever it
/// does fetch is removed with the TempDir on the way out.
///
/// Labelled "live" and built only under EMULE_LIVE_TESTS.

#include "UsenetLiveEnv.h"
#include "UsenetPostingHarness.h"

#include "nzb/NzbFile.h"
#include "nzb/NzbInfo.h"
#include "queue/UsenetQueue.h"
#include "post/UsenetUnpacker.h"
#include "queue/UsenetQueueItem.h"

#include "prefs/Preferences.h"
#include "webserver/WebServer.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <iterator>
#include <limits>

using namespace eMule;
using namespace eMule::usenet;
using eMule::testing::TempDir;
using eMule::testing::loadProjectEnv;
using namespace eMule::testing::usenet;

namespace {

constexpr int kDefaultBudgetMinutes = 10;

/// The window a player asks for first. Small on purpose: this test is about the
/// map, not about throughput.
constexpr qint64 kProbeBytes = 64 * 1024;

constexpr int kPollMs = 250;

/// Enough to pull one volume promptly, far short of the account's budget.
/// Consecutive rows re-open before the provider has released the previous row's
/// sockets, and taking the whole allowance is answered with "502 Too many
/// connections" and a 60 s backoff on every worker. A set that has to be
/// extracted before it can be played needs its first volume whole, so this is
/// not as small as it was when only a header had to arrive.
constexpr int kPreviewConnections = 8;

/// What to preview, and how many volumes stand behind it.
struct PreviewTarget {
    int fileIndex = -1;
    int volumeCount = 0;   ///< 0 when the target is not part of an archive set
    QString setName;
};

/// Volume one of the largest archive set in the NZB, or failing that the largest
/// file that is not a recovery volume.
///
/// "First file that is not PAR2" is not good enough. A release that posts its own
/// `.nzb` alongside the volumes puts that first, and previewing a 500 KB text
/// file proves nothing about the release — nor about the archive enumerator,
/// which is the whole point of the case.
PreviewTarget previewTarget(const NzbInfo& nzb)
{
    QHash<QString, qint64> setBytes;
    QHash<QString, int> setVolumes;

    for (int i = 0; i < nzb.files.size(); ++i) {
        const NzbFileInfo& file = nzb.files.at(i);
        if (file.isPar2())
            continue;
        const auto pos = UsenetUnpacker::volumePositionOf(file.fileName);
        if (pos.index < 0)
            continue;
        setBytes[pos.baseName] += file.encodedBytes();
        setVolumes[pos.baseName] += 1;
    }

    PreviewTarget out;
    qint64 best = 0;
    for (auto it = setBytes.cbegin(); it != setBytes.cend(); ++it) {
        if (it.value() <= best)
            continue;
        best = it.value();
        out.setName = it.key();
    }

    if (!out.setName.isEmpty()) {
        out.volumeCount = setVolumes.value(out.setName);
        int lowest = std::numeric_limits<int>::max();
        for (int i = 0; i < nzb.files.size(); ++i) {
            const auto pos = UsenetUnpacker::volumePositionOf(nzb.files.at(i).fileName);
            if (pos.index < 0 || pos.baseName != out.setName)
                continue;
            if (pos.index < lowest) {
                lowest = pos.index;
                out.fileIndex = i;
            }
        }
        return out;
    }

    // No archive at all: the biggest raw payload, which streams directly.
    qint64 biggest = 0;
    for (int i = 0; i < nzb.files.size(); ++i) {
        const NzbFileInfo& file = nzb.files.at(i);
        if (file.isPar2())
            continue;
        if (file.encodedBytes() > biggest) {
            biggest = file.encodedBytes();
            out.fileIndex = i;
        }
    }
    return out;
}

int missingSegmentsIn(const UsenetQueueItem* item)
{
    if (!item)
        return 0;

    int missing = 0;
    for (const UsenetFileState& file : item->files)
        missing += file.missingSegments;

    return missing;
}

QString describeStatus(UsenetQueue::ArchiveListing::Status s)
{
    switch (s) {
    case UsenetQueue::ArchiveListing::Status::Unknown:      return QStringLiteral("Unknown");
    case UsenetQueue::ArchiveListing::Status::Scanning:     return QStringLiteral("Scanning");
    case UsenetQueue::ArchiveListing::Status::Complete:     return QStringLiteral("Complete");
    case UsenetQueue::ArchiveListing::Status::NotSeekable:  return QStringLiteral("NotSeekable");
    case UsenetQueue::ArchiveListing::Status::NotAnArchive: return QStringLiteral("NotAnArchive");
    }
    return QStringLiteral("?");
}

struct HttpResponse {
    int statusCode = 0;
    QByteArray body;
    QHash<QString, QString> headers;
};

/// One Range GET, driven by a local event loop exactly as tst_WebServer does —
/// which is also what keeps the queue's own 250 ms tick running underneath.
HttpResponse rangedGet(QNetworkAccessManager& nam, const QString& url, const QByteArray& range)
{
    QNetworkRequest req{QUrl(url)};
    if (!range.isEmpty())
        req.setRawHeader(QByteArrayLiteral("Range"), range);

    QNetworkReply* reply = nam.get(req);
    if (!reply->isFinished()) {
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(60000, &loop, &QEventLoop::quit);
        loop.exec();
    }

    HttpResponse out;
    out.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    out.body = reply->readAll();
    // Lower-cased: header names are case-insensitive and Qt does not promise
    // the casing it hands back.
    for (const QByteArray& name : reply->rawHeaderList()) {
        out.headers[QString::fromUtf8(name).toLower()] =
            QString::fromUtf8(reply->rawHeader(name));
    }

    reply->deleteLater();
    return out;
}

} // namespace

class tst_UsenetLivePreview : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void previewsAReleaseWhileItIsStillDownloading_data();
    void previewsAReleaseWhileItIsStillDownloading();

private:
    QString m_tmpPath;
};

void tst_UsenetLivePreview::initTestCase()
{
    loadProjectEnv();

    // QtTest reads QTEST_FUNCTION_TIMEOUT from the environment when it builds its
    // watchdog, so ctest supplies it (tests/CMakeLists.txt). Run the binary by
    // hand without it and a long release dies at Qt's 300 s default with the
    // unhelpful "Received a fatal error" — say so rather than let it look like a
    // download failure.
    if (qEnvironmentVariableIsEmpty("QTEST_FUNCTION_TIMEOUT")) {
        qWarning("QTEST_FUNCTION_TIMEOUT is unset: this run will be killed after "
                 "300 s. Run through ctest, or export it.");
    }

    if (!haveLiveProvider())
        QSKIP("Set EMULE_NNTP_HOST to run the live preview test");
    if (nzbDir().isEmpty())
        QSKIP("Set EMULE_NZB_DIR to a directory of .nzb files");

    QVERIFY2(QDir(nzbDir()).exists(),
             qPrintable(QStringLiteral("EMULE_NZB_DIR does not exist: %1").arg(nzbDir())));
}

void tst_UsenetLivePreview::cleanup()
{
    if (m_tmpPath.isEmpty())
        return;

    const QString path = m_tmpPath;
    m_tmpPath.clear();

    // The provider frees a connection slot some time after we close it. Rows run
    // back to back, so without this the next one races the last one's teardown.
    QTest::qWait(3000);

    if (!QDir(path).exists())
        return;

    QDir(path).removeRecursively();
    QFAIL(qPrintable(QStringLiteral("the run left its working tree behind: %1").arg(path)));
}

void tst_UsenetLivePreview::previewsAReleaseWhileItIsStillDownloading_data()
{
    QTest::addColumn<QString>("nzbPath");

    if (addNzbRows() == 0)
        QSKIP("No .nzb files in EMULE_NZB_DIR");
}

void tst_UsenetLivePreview::previewsAReleaseWhileItIsStillDownloading()
{
    QFETCH(QString, nzbPath);

    NzbInfo nzb;
    QString error;
    QVERIFY2(NzbFile::parseFile(nzbPath, nzb, error), qPrintable(error));

    const PreviewTarget target = previewTarget(nzb);
    const int fileIndex = target.fileIndex;
    QVERIFY2(fileIndex >= 0, "the NZB is nothing but recovery volumes");

    qInfo().noquote() << QStringLiteral("previewing %1, file %2: %3 (%4 volume(s))")
                             .arg(nzb.name).arg(fileIndex)
                             .arg(nzb.files.at(fileIndex).fileName)
                             .arg(target.volumeCount);

    TempDir tmp;
    LivePrefsGuard prefs(tmp);
    m_tmpPath = tmp.path();

    NewsServer provider = providerFromEnv();
    provider.maxConnections = qMin(provider.maxConnections, kPreviewConnections);

    UsenetQueue queue;
    queue.applyServers({provider}, 60);
    // Cleanup and repair off — they would delete or rewrite the volumes this
    // case streams from. Unpack and direct unpack on, as a pair: that is the
    // second byte source, and the only one a compressed release has.
    queue.setPostProcessingOptions({.par2 = false,
                                    .rename = false,
                                    .unpack = true,
                                    .cleanup = false,
                                    .directUnpack = true});
    queue.start();

    QFile nzbFile(nzbPath);
    QVERIFY2(nzbFile.open(QIODevice::ReadOnly), qPrintable(nzbFile.errorString()));

    const QString id = queue.addNzb(nzbFile.readAll(), QFileInfo(nzbPath).completeBaseName(), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    const int budgetMin = liveEnv("EMULE_NZB_PREVIEW_MIN").toInt() > 0
                              ? liveEnv("EMULE_NZB_PREVIEW_MIN").toInt()
                              : kDefaultBudgetMinutes;

    // -- 1. Enumerate what is inside the set --------------------------------
    //
    // Each call parses one more step and asks for the bytes it is missing; the
    // queue promotes them and the next call gets further. Nothing here fetches
    // directly.

    UsenetQueue::ArchiveListing listing;
    QDeadlineTimer deadline(qint64(budgetMin) * 60 * 1000);
    while (!deadline.hasExpired()) {
        listing = queue.listArchiveEntries(id, fileIndex);
        if (listing.status != UsenetQueue::ArchiveListing::Status::Scanning
            && listing.status != UsenetQueue::ArchiveListing::Status::Unknown) {
            break;
        }
        QTest::qWait(500);
    }

    qInfo().noquote() << QStringLiteral("  listing: %1 (%2 entries) %3")
                             .arg(describeStatus(listing.status))
                             .arg(listing.entries.size())
                             .arg(listing.note);
    for (const auto& entry : listing.entries) {
        qInfo().noquote() << QStringLiteral("    [%1] %2  %3 bytes  %4%5")
                                 .arg(entry.entry).arg(entry.name).arg(entry.size)
                                 .arg(entry.playable ? QStringLiteral("playable")
                                                     : QStringLiteral("not playable"))
                                 .arg(entry.note.isEmpty() ? QString()
                                                           : QStringLiteral(" — ") + entry.note);
    }

    if (listing.status == UsenetQueue::ArchiveListing::Status::Scanning
        || listing.status == UsenetQueue::ArchiveListing::Status::Unknown) {
        // Nothing arrived. If the articles are simply gone, that is a fact about
        // the post; anything else is ours and fails.
        const int missing = missingSegmentsIn(queue.findItem(id));
        if (missing > 0)
            QSKIP(qPrintable(QStringLiteral("%1 segments have aged out").arg(missing)));

        QFAIL(qPrintable(QStringLiteral("the scan never left %1 within %2 min")
                             .arg(describeStatus(listing.status)).arg(budgetMin)));
    }

    // -- 2. The web server, wired exactly as the daemon wires it -------------

    WebServer web;
    web.setPreferences(&thePrefs);
    web.setUsenetStreamResolver([&queue](const UsenetStreamRequest& ask) -> UsenetStreamSource {
        UsenetStreamSource out;
        const auto info = queue.requestStream(ask.itemId, ask.fileIndex, ask.wantOffset,
                                              ask.wantLength, ask.entryOrdinal);
        out.found             = info.found;
        out.fileName          = info.fileName;
        out.totalSize         = info.totalSize;
        out.availableEnd      = info.availableEnd;
        out.complete          = info.complete;
        out.notSeekableReason = info.notSeekableReason;
        for (const auto& piece : info.pieces)
            out.pieces.append({piece.path, piece.virtualOffset, piece.fileOffset, piece.length});
        return out;
    });

    WebServerConfig config;
    config.enabled = true;
    config.port = 0;
    QVERIFY(web.start(config));
    QVERIFY(web.port() > 0);

    // The URL PreviewLauncher::daemonUsenetStreamUrl() builds, with no `entry=`
    // — the default, "whichever the daemon prefers".
    const QString url = QStringLiteral("http://127.0.0.1:%1/api/v1/usenet/%2/%3/preview?token=%4")
                            .arg(web.port()).arg(id).arg(fileIndex).arg(web.streamToken());

    QNetworkAccessManager nam;

    // -- 3. Which of the three outcomes is this release? ---------------------
    //
    // A member the map can place streams out of the volumes. One it cannot may
    // still be extracted as the volumes land, and then it streams out of that.
    // Only a set with nothing playable in it at all is a refusal.

    int mappable = 0;
    for (const auto& entry : listing.entries)
        mappable += entry.playable ? 1 : 0;

    // A single-volume set cannot be extracted until the whole volume is down,
    // which for a 1 GB release is not what this test is for. Nothing is waited
    // for in that case.
    const bool extractionPossible = target.volumeCount >= 2;

    if (mappable == 0 && !extractionPossible) {
        const auto info = queue.requestStream(id, fileIndex, 0, kProbeBytes);
        QVERIFY2(!info.notSeekableReason.isEmpty(),
                 "nothing playable and nothing extracting, yet no reason was given");
        QVERIFY(info.pieces.isEmpty());

        const HttpResponse resp = rangedGet(nam, url, QByteArrayLiteral("bytes=0-"));
        QCOMPARE(resp.statusCode, 406);
        QVERIFY2(QString::fromUtf8(resp.body).contains(info.notSeekableReason),
                 qPrintable(QStringLiteral("406 body did not carry the reason: %1")
                                .arg(QString::fromUtf8(resp.body.left(200)))));

        qInfo().noquote() << QStringLiteral("  not streamable, and it says so: %1")
                                 .arg(info.notSeekableReason);
        web.stop();
        queue.removeItem(id, /*deleteFiles*/ true);
        queue.stop();
        return;
    }

    // -- 4. Bytes, from whichever source has them ----------------------------
    //
    // The same poll serves both: the map answers within a couple of ticks, the
    // extraction once its first volume has landed and libarchive has written
    // past its own read-ahead. Neither may ever answer with a refusal — the
    // route treats a reason as final, and a player that gets one does not come
    // back.

    UsenetQueue::StreamInfo info;
    QDeadlineTimer readDeadline(qint64(budgetMin) * 60 * 1000);
    while (!readDeadline.hasExpired()) {
        info = queue.requestStream(id, fileIndex, 0, kProbeBytes);
        QVERIFY2(info.notSeekableReason.isEmpty(), qPrintable(info.notSeekableReason));
        if (info.found && info.availableEnd > 0)
            break;
        QTest::qWait(kPollMs);
    }

    QVERIFY2(info.found, "the item vanished mid-preview");
    QVERIFY2(info.availableEnd > 0,
             qPrintable(QStringLiteral("nothing readable from byte 0 within %1 min")
                            .arg(budgetMin)));

    // Whichever source answered, the total is the finished file's size and not
    // the growing one's — the number a player takes its seek bar from.
    QVERIFY2(info.totalSize > 0, "served bytes without a declared total");
    QVERIFY(info.availableEnd <= info.totalSize);

    qInfo().noquote() << QStringLiteral("  source: %1")
                             .arg(mappable > 0 ? QStringLiteral("the volume map")
                                               : QStringLiteral("the extraction"));

    const qint64 want = qMin<qint64>(kProbeBytes, info.availableEnd);
    const QByteArray direct = readThroughPieces(info.pieces, 0, want);
    QVERIFY2(!direct.isEmpty(), "the pieces resolved but read back nothing");

    qInfo().noquote() << QStringLiteral("  streaming %1: %2 of %3 bytes readable, %4 piece(s)")
                             .arg(info.fileName).arg(info.availableEnd)
                             .arg(info.totalSize).arg(info.pieces.size());

    // -- 5. The same bytes, over the wire the GUI uses -----------------------

    const HttpResponse resp =
        rangedGet(nam, url, QByteArrayLiteral("bytes=0-") + QByteArray::number(want - 1));

    QCOMPARE(resp.statusCode, 206);
    QVERIFY(resp.headers.value(QStringLiteral("accept-ranges")) == QLatin1String("bytes"));
    QVERIFY2(resp.headers.value(QStringLiteral("content-range")).startsWith(QLatin1String("bytes 0-")),
             qPrintable(resp.headers.value(QStringLiteral("content-range"))));

    // A prefix, not an equality: more articles may have landed between the two
    // reads, and the route serves everything readable up to the window. What
    // must never change is the bytes already written at offset 0.
    QVERIFY2(resp.body.startsWith(direct),
             qPrintable(QStringLiteral("HTTP body diverges from the direct read at offset %1")
                            .arg(std::distance(direct.cbegin(),
                                               std::mismatch(direct.cbegin(), direct.cend(),
                                                             resp.body.cbegin(),
                                                             resp.body.cend()).first))));

    web.stop();
    QVERIFY(queue.removeItem(id, /*deleteFiles*/ true));
    queue.stop();
}

QTEST_MAIN(tst_UsenetLivePreview)
#include "tst_UsenetLivePreview.moc"
