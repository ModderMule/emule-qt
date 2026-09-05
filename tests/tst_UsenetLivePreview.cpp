/// @file tst_UsenetLivePreview.cpp
/// @brief Streaming preview of a real release while it is still downloading.
///
/// The offline cases in tst_UsenetStream post RAR volumes this repository built
/// itself, so every offset they assert was computed from the format. That is the
/// right fixture for the parser and the wrong one for the question this test
/// asks: does the map survive an archive somebody else's packer produced, split
/// by somebody else's `-v` switch, indexed by somebody else's NZB?
///
/// Two answers are conformant, and the case asserts whichever it gets:
///
///   - a **stored** set streams — the listing reaches a terminal status, a read
///     at offset 0 returns bytes, and the HTTP route answers 206 with the same
///     bytes the queue handed back directly;
///   - a set with nothing streamable in it must say so — compressed, solid or
///     header-encrypted, or simply holding no media — with a
///     `notSeekableReason` and a 406 carrying that reason, instead of a player
///     left to time out. A Linux image in a `-m3` archive is the ordinary case:
///     the listing completes and names every inner file, and not one of them is
///     playable.
///
/// Asserting only the first would make the test a lottery on a stranger's `-m`
/// switch.
///
/// Post-processing is off throughout, for the reason tst_UsenetStream gives:
/// unpack and cleanup would delete the very volumes the case streams from.
///
/// Environment: the provider variables from UsenetLiveEnv.h, plus
///
///   EMULE_NZB_DIR         directory of .nzb files (required)
///   EMULE_NZB_PREVIEW_MIN per-release budget, default 10 (optional)
///
/// Cheap by design: it fetches the head of one file, not the release. Whatever
/// it does fetch is removed with the TempDir on the way out.
///
/// Labelled "live" and built only under EMULE_LIVE_TESTS.

#include "UsenetLiveEnv.h"
#include "UsenetPostingHarness.h"

#include "nzb/NzbFile.h"
#include "nzb/NzbInfo.h"
#include "queue/UsenetQueue.h"
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

/// A preview reads the head of one file. Taking the account's whole connection
/// budget for that is not just wasteful: consecutive rows re-open before the
/// provider has released the previous row's sockets, and the answer is
/// "502 Too many connections" followed by a 60 s backoff on every worker.
constexpr int kPreviewConnections = 2;

/// First file of the NZB that is not a recovery volume. PAR2 members are never
/// previewable and are usually first in the document.
int firstPayloadFileIndex(const NzbInfo& nzb)
{
    for (int i = 0; i < nzb.files.size(); ++i) {
        if (!nzb.files.at(i).isPar2())
            return i;
    }
    return -1;
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

    const int fileIndex = firstPayloadFileIndex(nzb);
    QVERIFY2(fileIndex >= 0, "the NZB is nothing but recovery volumes");

    qInfo().noquote() << QStringLiteral("previewing %1, file %2: %3")
                             .arg(nzb.name).arg(fileIndex)
                             .arg(nzb.files.at(fileIndex).fileName);

    TempDir tmp;
    LivePrefsGuard prefs(tmp);
    m_tmpPath = tmp.path();

    NewsServer provider = providerFromEnv();
    provider.maxConnections = qMin(provider.maxConnections, kPreviewConnections);

    UsenetQueue queue;
    queue.applyServers({provider}, 60);
    // Everything off: unpack and cleanup would delete the volumes this case
    // streams from, and a repair would rewrite them underneath the map.
    queue.setPostProcessingOptions({.par2 = false,
                                    .rename = false,
                                    .unpack = false,
                                    .cleanup = false,
                                    .directUnpack = false});
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

    // -- 3a. A set that cannot be streamed must say why ----------------------
    //
    // Two shapes reach here, and both are refusals: a set with nothing to list
    // (solid, header-encrypted), and a set listed in full whose every member is
    // compressed or is not media — a Linux image in a `-m3` archive is the
    // ordinary case of the second.

    int playable = 0;
    for (const auto& entry : listing.entries)
        playable += entry.playable ? 1 : 0;

    if (listing.status == UsenetQueue::ArchiveListing::Status::NotSeekable || playable == 0) {
        const auto info = queue.requestStream(id, fileIndex, 0, kProbeBytes);
        QVERIFY2(!info.notSeekableReason.isEmpty(),
                 "the listing refused the set but requestStream gave no reason");
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

    // -- 3b. A stored set streams from byte 0 --------------------------------

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

    const qint64 want = qMin<qint64>(kProbeBytes, info.availableEnd);
    const QByteArray direct = readThroughPieces(info.pieces, 0, want);
    QVERIFY2(!direct.isEmpty(), "the pieces resolved but read back nothing");

    qInfo().noquote() << QStringLiteral("  streaming %1: %2 of %3 bytes readable, %4 piece(s)")
                             .arg(info.fileName).arg(info.availableEnd)
                             .arg(info.totalSize).arg(info.pieces.size());

    // -- 4. The same bytes, over the wire the GUI uses -----------------------

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
