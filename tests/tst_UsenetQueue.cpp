/// @file tst_UsenetQueue.cpp
/// @brief Phase 3's exit criterion: an NZB queued, downloaded and shared.
///
/// The joins this covers, all of which the layers below cannot:
///
///   - Segments are dispatched across worker threads and arrive out of order, so
///     the assembled file is only correct if every writer placed its part at the
///     absolute offset the article declared.
///   - The completed file lands in the *incoming* directory, not a subfolder:
///     the share scan does not recurse, so a subfolder would never be offered.
///   - Nothing named `.usenetpart` survives a completion.
///   - Resume state is per-segment, so a restart must not refetch what is done.

#include "FakeNntpServer.h"
#include "TestHelpers.h"

#include "decode/YencDecoder.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"
#include "queue/UsenetQueueStore.h"

#include "prefs/Preferences.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace eMule::usenet;
using eMule::testing::FakeNntpServer;

namespace {

constexpr int kPartSize = 2500;
constexpr int kParts = 6;

QByteArray payload(int size)
{
    QByteArray data;
    data.resize(size);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 17 + (i >> 6) * 5) & 0xFF);
    return data;
}

/// One yEnc article, exactly as a provider serves it — 1-based `begin` included.
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

QString messageIdFor(int part, const QString& prefix = QStringLiteral("q"))
{
    return QStringLiteral("%1%2@example.com").arg(prefix).arg(part);
}

/// @p date is the NZB's `date` attribute, i.e. when the release was posted.
/// Retention is decided from it, so a case about old articles has to set it.
QByteArray makeNzb(const QString& fileName, int parts, qint64 date = 1700000000,
                   const QString& idPrefix = QStringLiteral("q"))
{
    QByteArray xml;
    xml += R"(<?xml version="1.0" encoding="iso-8859-1" ?>)"
           "\n<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";
    xml += QStringLiteral(
               "  <file poster=\"tester\" date=\"%3\" "
               "subject=\"&quot;%1&quot; yEnc (1/%2)\">\n")
               .arg(fileName).arg(parts).arg(date).toUtf8();
    xml += "    <groups><group>alt.binaries.test</group></groups>\n";
    xml += "    <segments>\n";
    for (int p = 1; p <= parts; ++p) {
        xml += QStringLiteral("      <segment bytes=\"3500\" number=\"%1\">%2</segment>\n")
                   .arg(p).arg(messageIdFor(p, idPrefix)).toUtf8();
    }
    xml += "    </segments>\n  </file>\n</nzb>\n";
    return xml;
}

/// An NZB of @p fileCount files, @p partsPerFile articles each, with distinct
/// message-ids per file. makeNzb() only ever produces one file, and the sampling
/// rule — one article per file — cannot be seen at all with one file.
QByteArray makeMultiFileNzb(int fileCount, int partsPerFile, qint64 date = 1700000000)
{
    QByteArray xml;
    xml += R"(<?xml version="1.0" encoding="iso-8859-1" ?>)"
           "\n<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";
    for (int f = 0; f < fileCount; ++f) {
        xml += QStringLiteral(
                   "  <file poster=\"tester\" date=\"%3\" "
                   "subject=\"&quot;m.r%1&quot; yEnc (1/%2)\">\n")
                   .arg(f, 2, 10, QLatin1Char('0')).arg(partsPerFile).arg(date).toUtf8();
        xml += "    <groups><group>alt.binaries.test</group></groups>\n";
        xml += "    <segments>\n";
        for (int p = 1; p <= partsPerFile; ++p) {
            xml += QStringLiteral(
                       "      <segment bytes=\"3500\" number=\"%1\">f%2p%1@example.com"
                       "</segment>\n")
                       .arg(p).arg(f).toUtf8();
        }
        xml += "    </segments>\n  </file>\n";
    }
    xml += "</nzb>\n";
    return xml;
}

/// Index of the first command starting with @p verb, or -1.
int firstCommandIndex(const QStringList& commands, const QString& verb)
{
    for (int i = 0; i < commands.size(); ++i) {
        if (commands.at(i).startsWith(verb))
            return i;
    }
    return -1;
}

NewsServer serverConfig(quint16 port, int maxConnections)
{
    NewsServer s;
    s.name = QStringLiteral("fake");
    s.host = QStringLiteral("127.0.0.1");
    s.port = port;
    s.tlsMode = NntpTlsMode::None;
    s.user = QStringLiteral("testuser");
    s.pass = QStringLiteral("testpass");
    s.maxConnections = maxConnections;
    // Distinct and deterministic, so a case can plant a usage row for it. The
    // meter is keyed by this and not by key(), which moves when a port does.
    s.accountId = QStringLiteral("acct-%1").arg(port);
    return s;
}

/// Write a usage sidecar saying @p accountId has already spent @p bytes this
/// period. The only way to reach "over its allowance" without downloading the
/// bytes for real.
void plantUsage(const QString& accountId, qint64 bytes)
{
    QDir().mkpath(UsenetQueueStore::stateDir());
    QFile f(QDir(UsenetQueueStore::stateDir()).filePath(QStringLiteral("usage.yml")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QStringLiteral("version: 1\naccounts:\n  - id: %1\n    kind: 2\n"
                           "    periodBytes: %2\n    totalBytes: %2\n")
                .arg(accountId).arg(bytes).toUtf8());
    f.close();
}

} // namespace

class tst_UsenetQueue : public QObject {
    Q_OBJECT

private slots:
    void downloadsAnNzbByteIdentically();
    void leavesNoScratchBehind();
    void persistedStateResumesInsteadOfRefetching();
    void connectionBudgetIsDividedNotReplicated();
    void missingArticlesEscalateToTheNextLevel();
    void everyServerOnALevelIsAskedBeforeEscalating();
    void retentionSkipsAServerThatCannotHoldTheArticle();
    void aRetentionGuessNeverMakesAnArticleMissing();
    void aDeadOptionalServerCostsOneArticleNotTheDownload();
    void aRequiredServerStillFailsTheItem();
    void addNzbWithNoNameUsesTheNzbsOwn();
    void bytesAreAttributedToTheAccountThatServedThem();
    void aRefusedArticleStillCostsTheAccountThatRefusedIt();
    void anExhaustedAccountIsSkippedWhileAnotherCanServe();
    void aQuotaNeverMakesAnArticleMissing();
    void aDeadOptionalServerCannotStrandAQuotaBlockedArticle();
    void anExhaustedLevelParksUnlessAskedToFallThrough();
    void nearingTheAllowanceWarnsOnceBeforeItBites();
    void theSameNzbTwiceIsRefused();
    void addNzbReportsWhyItRefused();
    void anAutomaticAddIsPausedWhenTheUserAsksForIt();
    void aManualReAddOfACompletedReleaseIsAllowed();
    void anAutomaticReAddOfACompletedReleaseIsSkipped();
    void aRepostWithFreshMessageIdsIsNotAnExactDuplicate();
    void theDuplicateIndexSurvivesARestart();
    void aRefusalSentenceCarriesNoEmDash();
    void aProbeIssuesStatBeforeAnyBody();
    void sampleModeProbesOneArticlePerFile();
    void anArticleTheSecondServerHoldsIsNotCountedUnavailable();
    void aProbeNeverMakesAnArticleMissing();
    void aShortReleaseIsAddedPausedNotFailed();
    void aProbingItemDoesNotCountAsAnActiveDownload();
    void probingIsSkippedWhenNoAccountCanBeAsked();
    void theNewSidecarKeysAreOptional();
    void aRecheckOfAPausedItemLeavesItPaused();
    void changingTheServerListDoesNotStrandACheckingItem();
    void probeBytesAreBilledToTheAccountThatAnsweredThem();
};

void tst_UsenetQueue::downloadsAnNzbByteIdentically()
{
    const QByteArray whole = payload(kPartSize * kParts);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, kParts,
                                                       QStringLiteral("movie.mkv")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("movie.mkv"), kParts),
                                    QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(30000), "the download never reported a terminal outcome");
    QCOMPARE(finished.at(0).at(0).toString(), id);
    QVERIFY2(finished.at(0).at(1).toBool(), "completed with missing articles");

    // The whole point: the file must be in the incoming directory itself, because
    // the share scan does not recurse into subfolders.
    const QString out = QDir(thePrefs.incomingDir()).filePath(QStringLiteral("movie.mkv"));
    QVERIFY2(QFile::exists(out), qPrintable(QStringLiteral("missing: %1").arg(out)));

    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    // Byte-for-byte, not a hash of the parts: an off-by-one in the 1-based
    // `=ypart begin` still passes every pcrc32, because yEnc verifies the payload
    // and not where it lands.
    QCOMPARE(f.readAll(), whole);
    f.close();

    queue.stop();
}

void tst_UsenetQueue::leavesNoScratchBehind()
{
    const QByteArray whole = payload(kPartSize * 3);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 3, 1, 3);
    for (int p = 1; p <= 3; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 3,
                                                       QStringLiteral("small.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 2)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("small.bin"), 3),
                          QStringLiteral("small"), error).isEmpty());
    QVERIFY(finished.wait(30000));

    // A leftover .usenetpart is the one failure that is silent: the file is not
    // shared, so nothing breaks, it just accumulates forever.
    QStringList leftovers;
    QDirIterator it(tmp.path(), {QStringLiteral("*.usenetpart")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        leftovers << it.next();
    QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QStringLiteral(", "))));

    queue.stop();
}

void tst_UsenetQueue::persistedStateResumesInsteadOfRefetching()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueueItem item;
    item.id = QStringLiteral("11111111-2222-3333-4444-555555555555");
    item.name = QStringLiteral("resume me");
    item.priority = 2;
    item.status = UsenetItemStatus::Downloading;

    NzbFileInfo info;
    info.subject = QStringLiteral("\"resume.bin\" yEnc (1/4)");
    info.fileName = QStringLiteral("resume.bin");
    info.groups << QStringLiteral("alt.binaries.test");
    for (int p = 1; p <= 4; ++p)
        info.segments.append(NzbSegment{messageIdFor(p), 3500, p});
    item.nzb.files.append(info);
    item.initFileStates(thePrefs.usenetTempDir());

    // Two of four already fetched, and one of those two is a hole — a segment
    // missing on every server is also "resolved", and must not come back.
    item.files[0].done.setBit(0);
    item.files[0].done.setBit(2);
    item.files[0].missingSegments = 1;
    item.files[0].decodedBytes = 5000;

    QVERIFY(UsenetQueueStore::save(item));

    UsenetQueueItem loaded;
    QString error;
    QVERIFY2(UsenetQueueStore::load(UsenetQueueStore::statePath(item.id), loaded, error),
             qPrintable(error));

    QCOMPARE(loaded.id, item.id);
    QCOMPARE(loaded.name, item.name);
    QCOMPARE(loaded.priority, 2);
    // Downloading is not a state anything resumes *into* — nothing is in flight
    // after a restart, so it must come back as Queued.
    QCOMPARE(loaded.status, UsenetItemStatus::Queued);

    QCOMPARE(loaded.nzb.files.size(), 1);
    QCOMPARE(loaded.nzb.files.at(0).segments.size(), 4);
    QCOMPARE(loaded.nzb.files.at(0).segments.at(2).messageId, messageIdFor(3));

    QCOMPARE(loaded.files.size(), 1);
    QCOMPARE(loaded.files.at(0).done.size(), 4);
    QVERIFY(loaded.files.at(0).done.testBit(0));
    QVERIFY(!loaded.files.at(0).done.testBit(1));
    QVERIFY(loaded.files.at(0).done.testBit(2));
    QVERIFY(!loaded.files.at(0).done.testBit(3));
    QCOMPARE(loaded.files.at(0).missingSegments, 1);
    QCOMPARE(loaded.doneSegmentCount(), 2);
}

void tst_UsenetQueue::connectionBudgetIsDividedNotReplicated()
{
    // Exceeding a provider's connection limit gets an account throttled or
    // suspended, which is a worse failure than downloading slowly — so this is
    // asserted rather than left to review.
    const QByteArray whole = payload(kPartSize * 8);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 8, 1, 8);
    for (int p = 1; p <= 8; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 8,
                                                       QStringLiteral("budget.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    constexpr int kMaxConnections = 3;

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, kMaxConnections)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("budget.bin"), 8),
                          QStringLiteral("budget"), error).isEmpty());
    QVERIFY(finished.wait(30000));

    QVERIFY2(server.connectionCount() <= kMaxConnections,
             qPrintable(QStringLiteral("opened %1 connections, limit is %2")
                            .arg(server.connectionCount()).arg(kMaxConnections)));

    queue.stop();
}

void tst_UsenetQueue::missingArticlesEscalateToTheNextLevel()
{
    // The failover rule is the whole of Usenet fault tolerance, so it gets its
    // own test: a 430 must move *up* a level, and the level-0 account must not be
    // asked for the same article twice.
    //
    // Two servers on two ports, because NewsServer::key() is host:port/user — one
    // port cannot host two identities, and the pool would treat them as one.
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer level0;          // knows nothing; answers 430 to everything
    level0.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    const quint16 port0 = level0.start();
    QVERIFY(port0 != 0);

    FakeNntpServer level1;          // the fill server that actually has it
    level1.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        level1.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                       QStringLiteral("fill.bin")));
    const quint16 port1 = level1.start();
    QVERIFY(port1 != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer main = serverConfig(port0, 2);
    main.name = QStringLiteral("main");
    main.level = 0;

    NewsServer fill = serverConfig(port1, 2);
    fill.name = QStringLiteral("fill");
    fill.level = 1;

    UsenetQueue queue;
    queue.applyServers({main, fill}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("fill.bin"), 4),
                          QStringLiteral("failover"), error).isEmpty());
    QVERIFY2(finished.wait(30000), "the escalation never completed");
    QVERIFY2(finished.at(0).at(1).toBool(),
             "articles present on the level-1 server were reported missing");

    const QString out = QDir(thePrefs.incomingDir()).filePath(QStringLiteral("fill.bin"));
    QVERIFY(QFile::exists(out));
    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), whole);
    f.close();

    // Each article asked of the level-0 server exactly once. More would mean the
    // tried-server exclusion is not being carried across the escalation.
    int bodyRequests = 0;
    for (const QString& cmd : level0.receivedCommands()) {
        if (cmd.startsWith(QStringLiteral("BODY"), Qt::CaseInsensitive))
            ++bodyRequests;
    }
    QCOMPARE(bodyRequests, 4);

    queue.stop();
}

// ---------------------------------------------------------------------------
// Duplicate guard
//
// The identity is the set of message-ids, not the name: two indexers hand out
// the same release under different titles, and a repost carries a familiar title
// over entirely fresh articles. Only the ids say whether the bytes are the same
// bytes.
// ---------------------------------------------------------------------------

void tst_UsenetQueue::theSameNzbTwiceIsRefused()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    const QByteArray nzb = makeNzb(QStringLiteral("dup.bin"), 3);

    QString error;
    QVERIFY2(!queue.addNzb(nzb, QStringLiteral("first"), error).isEmpty(), qPrintable(error));

    // A different display name, the same articles. Nothing about the name may
    // decide this — it is the same request to the same servers either way.
    error.clear();
    QVERIFY(queue.addNzb(nzb, QStringLiteral("a different name"), error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("first")), qPrintable(error));
    QCOMPARE(queue.items().size(), 1);
}

void tst_UsenetQueue::addNzbReportsWhyItRefused()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    const QByteArray nzb = makeNzb(QStringLiteral("why.bin"), 2);
    QString error;
    UsenetAddOutcome outcome = UsenetAddOutcome::Failed;

    QVERIFY(!queue.addNzb(nzb, QStringLiteral("first"), error,
                          UsenetAddSource::Manual, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::Added);

    // The distinction the watch folder and the feed poller both live on: an
    // actor that cannot tell "we already have this" from "that did not work"
    // retries a duplicate forever.
    QVERIFY(queue.addNzb(nzb, QStringLiteral("again"), error,
                         UsenetAddSource::Manual, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::Duplicate);

    // And apart from both: never worth another attempt.
    QVERIFY(queue.addNzb(QByteArrayLiteral("not xml"), QStringLiteral("junk"), error,
                         UsenetAddSource::Manual, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::Invalid);

    QVERIFY(queue.addNzb(QByteArrayLiteral("<nzb></nzb>"), QStringLiteral("empty"), error,
                         UsenetAddSource::Manual, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::Invalid);
}

void tst_UsenetQueue::anAutomaticAddIsPausedWhenTheUserAsksForIt()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setUsenetAutoAddPaused(true);

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    QString error;
    // Enforced inside addNzb() rather than at each intake path, so every present
    // and future automatic caller inherits it by construction.
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("auto.bin"), 2, 1700000000,
                                  QStringLiteral("auto")),
                          QStringLiteral("from a feed"), error,
                          UsenetAddSource::Automatic).isEmpty());
    QCOMPARE(queue.items().size(), 1);
    QCOMPARE(queue.items().first()->status, UsenetItemStatus::Paused);

    // A person asking for it is not what the setting is about.
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("manual.bin"), 2, 1700000000,
                                  QStringLiteral("man")),
                          QStringLiteral("by hand"), error,
                          UsenetAddSource::Manual).isEmpty());
    QCOMPARE(queue.items().size(), 2);
    QCOMPARE(queue.items().at(1)->status, UsenetItemStatus::Queued);

    thePrefs.setUsenetAutoAddPaused(false);
}

void tst_UsenetQueue::aManualReAddOfACompletedReleaseIsAllowed()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    const QByteArray nzb = makeNzb(QStringLiteral("done.bin"), 3);
    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("done"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    // Finish it, the way the pipeline would.
    QVERIFY(queue.pauseItem(id));
    const_cast<UsenetQueueItem*>(queue.findItem(id))->status = UsenetItemStatus::Complete;

    // Deliberately fetching something again is a thing people do — the same call
    // Ed2kLinkImporter makes for a completed eD2K file, and the half of this
    // guard a naive "have I seen these ids" check gets wrong.
    error.clear();
    QVERIFY2(!queue.addNzb(nzb, QStringLiteral("again"), error,
                           UsenetAddSource::Manual).isEmpty(),
             qPrintable(error));
    QCOMPARE(queue.items().size(), 2);
}

void tst_UsenetQueue::anAutomaticReAddOfACompletedReleaseIsSkipped()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    const QByteArray nzb = makeNzb(QStringLiteral("done.bin"), 3);
    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("done"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    const_cast<UsenetQueueItem*>(queue.findItem(id))->status = UsenetItemStatus::Complete;

    // The other half: a feed re-offering last week's release must not download
    // it a second time. Nothing is automatic today, which is exactly why this is
    // pinned now rather than when the first feed arrives.
    error.clear();
    QVERIFY(queue.addNzb(nzb, QStringLiteral("again"), error,
                         UsenetAddSource::Automatic).isEmpty());
    QVERIFY(!error.isEmpty());
    QCOMPARE(queue.items().size(), 1);
}

void tst_UsenetQueue::aRepostWithFreshMessageIdsIsNotAnExactDuplicate()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    QString error;
    QVERIFY2(!queue.addNzb(makeNzb(QStringLiteral("r.bin"), 3),
                           QStringLiteral("same name"), error).isEmpty(),
             qPrintable(error));

    // Same name, same size, entirely different articles: a repost. The original
    // may well have expired, which is the whole reason people repost — refusing
    // this would refuse the only copy that still exists.
    error.clear();
    const QString id = queue.addNzb(
        makeNzb(QStringLiteral("r.bin"), 3, 1700000000, QStringLiteral("z")),
        QStringLiteral("same name"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QCOMPARE(queue.items().size(), 2);
}

void tst_UsenetQueue::theDuplicateIndexSurvivesARestart()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    const QByteArray nzb = makeNzb(QStringLiteral("restart.bin"), 3);

    {
        UsenetQueue queue;
        queue.applyServers({serverConfig(1119, 1)}, 0);
        QString error;
        QVERIFY2(!queue.addNzb(nzb, QStringLiteral("before"), error).isEmpty(),
                 qPrintable(error));
    }

    // The identity is re-derived from the sidecar's own message-ids rather than
    // stored. Without that step the guard is blind to everything restored from
    // disk — which, after one restart, is every item there is.
    UsenetQueue restarted;
    restarted.applyServers({serverConfig(1119, 1)}, 0);
    restarted.start();
    QCOMPARE(restarted.items().size(), 1);

    QString error;
    QVERIFY(restarted.addNzb(nzb, QStringLiteral("after"), error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("before")), qPrintable(error));
    QCOMPARE(restarted.items().size(), 1);
    restarted.stop();
}

void tst_UsenetQueue::aRefusalSentenceCarriesNoEmDash()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    const QByteArray nzb = makeNzb(QStringLiteral("dash.bin"), 2);
    QString error;
    QVERIFY2(!queue.addNzb(nzb, QStringLiteral("first"), error).isEmpty(), qPrintable(error));

    error.clear();
    QVERIFY(queue.addNzb(nzb, QStringLiteral("second"), error).isEmpty());
    QVERIFY(!error.isEmpty());

    // AddNzbUrlDialog reports a failed line as "<url> — <reason>" and recovers
    // the URL with section(" — ", 0, 0) to repopulate the box. A refusal
    // containing that separator silently truncates the retry list instead.
    QVERIFY2(!error.contains(QStringLiteral(" — ")), qPrintable(error));
}

// ---------------------------------------------------------------------------
// Availability probe
//
// "Does anybody still have this?", asked with STAT before an article is paid
// for. Everything here is advice: the load-bearing cases are the ones asserting
// that believing a wrong answer still costs the download nothing.
// ---------------------------------------------------------------------------

void tst_UsenetQueue::aProbeIssuesStatBeforeAnyBody()
{
    const QByteArray whole = payload(kPartSize * kParts);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, kParts,
                                                       QStringLiteral("probe.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
    thePrefs.setUsenetHealthCheck(1);

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 2)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("probe.bin"), kParts),
                                    QStringLiteral("probe"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY(finished.wait(30000));

    const QStringList commands = server.receivedCommands();
    const int firstStat = firstCommandIndex(commands, QStringLiteral("STAT "));
    const int firstBody = firstCommandIndex(commands, QStringLiteral("BODY "));

    // The whole point of the feature: the question is asked before the money is
    // spent, not after.
    QVERIFY2(firstStat >= 0, "no STAT was ever issued");
    QVERIFY2(firstBody >= 0, "nothing was downloaded");
    QVERIFY2(firstStat < firstBody, "the probe ran after the download had started");

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QCOMPARE(item->healthPercent, 100);
    QVERIFY(item->healthProbed);

    queue.stop();
}

void tst_UsenetQueue::sampleModeProbesOneArticlePerFile()
{
    constexpr int kFiles = 5;
    constexpr int kPartsEach = 4;

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 100, 1, 100);
    for (int f = 0; f < kFiles; ++f) {
        for (int p = 1; p <= kPartsEach; ++p) {
            server.addArticle(QStringLiteral("f%1p%2@example.com").arg(f).arg(p),
                              QByteArrayLiteral("x"));
        }
    }
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
    thePrefs.setUsenetHealthCheck(1);

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 2)}, 60);
    queue.start();

    QString error;
    const QString id = queue.addNzb(makeMultiFileNzb(kFiles, kPartsEach),
                                    QStringLiteral("sampled"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    // Wait for the verdict rather than for the download: the articles are not
    // real yEnc, so the fetch that follows will not complete.
    QTRY_VERIFY_WITH_TIMEOUT(queue.findItem(id) != nullptr
                                 && queue.findItem(id)->healthPercent >= 0,
                             30000);

    int stats = 0;
    for (const QString& cmd : server.receivedCommands()) {
        if (cmd.startsWith(QStringLiteral("STAT ")))
            ++stats;
    }

    // One article stands for its whole file. Probing every article would be
    // twenty round trips here and tens of thousands on a real release.
    QCOMPARE(stats, kFiles);

    // And it is the *first* article of each: the others may legitimately be
    // absent from a short NZB without the file being gone.
    for (int f = 0; f < kFiles; ++f) {
        QVERIFY2(server.receivedCommands().contains(
                     QStringLiteral("STAT <f%1p1@example.com>").arg(f)),
                 qPrintable(QStringLiteral("file %1 was not sampled").arg(f)));
    }

    queue.stop();
}

void tst_UsenetQueue::anArticleTheSecondServerHoldsIsNotCountedUnavailable()
{
    const QByteArray whole = payload(kPartSize * kParts);

    // Rung 0 has nothing at all; rung 1 has everything. Exactly the shape of a
    // cheap primary plus a block fill, and the shape that reads as "0%
    // available" to any probe that asks one account and believes the answer.
    FakeNntpServer empty;
    empty.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    const quint16 emptyPort = empty.start();
    QVERIFY(emptyPort != 0);

    FakeNntpServer full;
    full.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        full.addArticle(messageIdFor(p), makeArticle(whole, p, kParts,
                                                     QStringLiteral("two.bin")));
    const quint16 fullPort = full.start();
    QVERIFY(fullPort != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
    thePrefs.setUsenetHealthCheck(1);
    thePrefs.setUsenetHealthMinPercent(95);

    NewsServer first = serverConfig(emptyPort, 2);
    first.level = 0;
    NewsServer second = serverConfig(fullPort, 2);
    second.level = 1;

    UsenetQueue queue;
    queue.applyServers({first, second}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("two.bin"), kParts),
                                    QStringLiteral("two"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(30000), "never reached a terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(), "completed with missing articles");

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    // "Unavailable" may only ever mean *every rung refused it*. One account
    // saying no is what the failover ladder exists to survive.
    QCOMPARE(item->healthPercent, 100);
    QCOMPARE(item->healthMissingBytes, qint64(0));

    queue.stop();
}

void tst_UsenetQueue::aProbeNeverMakesAnArticleMissing()
{
    const QByteArray whole = payload(kPartSize * kParts);

    // Every article is there and every BODY will serve it — but STAT denies the
    // sampled one. NNTP has a single code for expired, taken down, never
    // propagated and "not on this server", so a wrong answer is not a contrived
    // case; it is the normal failure mode of asking at all.
    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, kParts,
                                                       QStringLiteral("liar.bin")));
    server.setStatRefusal(messageIdFor(1));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
    thePrefs.setUsenetHealthCheck(1);
    // The pause is the user's decision to make, and this case is about what
    // happens to the *articles*, so take the pause out of the way.
    thePrefs.setUsenetHealthMinPercent(0);

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 2)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("liar.bin"), kParts),
                                    QStringLiteral("liar"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(30000), "never reached a terminal outcome");

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);

    // The probe was believed — it says the release is short.
    QVERIFY2(item->healthPercent < 100, "the refusal was not even recorded");

    // And it changed nothing. No article was marked missing, no file was left
    // with a hole, and the payload is byte-identical. This is the fail-safe twin
    // of aRetentionGuessNeverMakesAnArticleMissing: a guess may reorder or warn,
    // never decide.
    QCOMPARE(item->files.at(0).missingSegments, 0);
    QVERIFY2(finished.at(0).at(1).toBool(), "a probe's guess failed the download");

    const QString out = QDir(thePrefs.incomingDir()).filePath(QStringLiteral("liar.bin"));
    QVERIFY2(QFile::exists(out), "the release was not published");
    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), whole);
    f.close();

    queue.stop();
}

void tst_UsenetQueue::aShortReleaseIsAddedPausedNotFailed()
{
    const QByteArray whole = payload(kPartSize * kParts);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, kParts,
                                                       QStringLiteral("short.bin")));
    server.setStatRefusal(messageIdFor(1));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
    thePrefs.setUsenetHealthCheck(1);
    thePrefs.setUsenetHealthMinPercent(95);

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 2)}, 60);
    queue.start();

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("short.bin"), kParts),
                                    QStringLiteral("short"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QTRY_VERIFY_WITH_TIMEOUT(queue.findItem(id) != nullptr
                                 && queue.findItem(id)->status != UsenetItemStatus::Checking,
                             30000);

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);

    // Paused, with a reason — never Failed, and never refused at the door. The
    // figure is a guess about articles nobody can ask a second question about,
    // so the user is the one who decides.
    QCOMPARE(item->status, UsenetItemStatus::Paused);
    QVERIFY(!item->stalledReason.isEmpty());
    QVERIFY(item->error.isEmpty());
    QCOMPARE(item->files.at(0).missingSegments, 0);

    // And resuming downloads it exactly as if the probe had never run.
    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QVERIFY(queue.resumeItem(id));
    QVERIFY2(finished.wait(30000), "resuming a paused-by-health item did nothing");
    QVERIFY(finished.at(0).at(1).toBool());

    queue.stop();
}

void tst_UsenetQueue::aProbingItemDoesNotCountAsAnActiveDownload()
{
    // Accepts the connection and then says nothing, so the probe never
    // completes and the item sits in Checking for as long as we look at it.
    FakeNntpServer server;
    server.setMute(true);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
    thePrefs.setUsenetHealthCheck(1);

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 2)}, 60);
    queue.start();

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("mute.bin"), kParts),
                                    QStringLiteral("mute"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QCOMPARE(item->status, UsenetItemStatus::Checking);

    // UsenetSession::updateBandwidthSplit() reserves Usenet's share of the line
    // while this is true. An engine issuing status lines is not downloading, and
    // saying otherwise withholds an eighth of the user's line from ED2K for as
    // long as the probe takes.
    QVERIFY2(!queue.hasActiveDownloads(),
             "a checking item was counted as a live download");

    queue.stop();
}

void tst_UsenetQueue::probingIsSkippedWhenNoAccountCanBeAsked()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setUsenetHealthCheck(1);

    // Never started: no workers, no ladder, nothing to ask. Every such case has
    // to produce *no verdict* and a normal queued item — a probe that cannot run
    // must not cost a download a single tick.
    UsenetQueue queue;

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("noserver.bin"), 2),
                                    QStringLiteral("noserver"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QCOMPARE(item->status, UsenetItemStatus::Queued);
    QCOMPARE(item->healthPercent, -1);     // not assessed, which is not 100
    QVERIFY(!item->healthProbed);
    QVERIFY(item->stalledReason.isEmpty());
}

void tst_UsenetQueue::theNewSidecarKeysAreOptional()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueueItem item;
    item.id = QStringLiteral("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
    item.name = QStringLiteral("health");
    item.healthPercent = 82;
    item.healthMissingBytes = 4096;
    item.healthRecoveryBytes = 1024;
    item.healthProbed = true;

    NzbFileInfo info;
    info.subject = QStringLiteral("\"h.bin\" yEnc (1/2)");
    info.partsTotal = 2;
    for (int p = 1; p <= 2; ++p)
        info.segments.append(NzbSegment{messageIdFor(p), 3500, p});
    item.nzb.files.append(info);
    item.initFileStates(thePrefs.usenetTempDir());

    QVERIFY(UsenetQueueStore::save(item));

    UsenetQueueItem loaded;
    QString error;
    QVERIFY2(UsenetQueueStore::load(UsenetQueueStore::statePath(item.id), loaded, error),
             qPrintable(error));
    QCOMPARE(loaded.healthPercent, 82);
    QCOMPARE(loaded.healthMissingBytes, qint64(4096));
    QCOMPARE(loaded.healthRecoveryBytes, qint64(1024));
    QVERIFY(loaded.healthProbed);

    // A sidecar written before this feature existed must still load, and must
    // come back saying "not assessed" rather than 0% — which would read as a
    // dead release. kStateVersion deliberately did not move for these keys:
    // load() refuses anything newer than it knows, so a bump would make an older
    // daemon drop the whole queue rather than lose one advisory figure.
    const QString path = UsenetQueueStore::statePath(item.id);
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray yaml = f.readAll();
    f.close();
    QVERIFY2(yaml.contains("version: 2"), "kStateVersion moved");

    QByteArray stripped;
    for (const QByteArray& line : yaml.split('\n')) {
        // On the key, not anywhere in the line: the item here is *called*
        // "health", and a looser filter deletes its name too.
        if (!line.trimmed().startsWith("health"))
            stripped += line + '\n';
    }
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(stripped);
    f.close();

    UsenetQueueItem old;
    QVERIFY2(UsenetQueueStore::load(path, old, error), qPrintable(error));
    QCOMPARE(old.healthPercent, -1);
    QVERIFY(!old.healthProbed);
    QCOMPARE(old.name, QStringLiteral("health"));
    QCOMPARE(old.nzb.files.size(), 1);
}

void tst_UsenetQueue::aRecheckOfAPausedItemLeavesItPaused()
{
    const QByteArray whole = payload(kPartSize * kParts);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, kParts,
                                                       QStringLiteral("held.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
    thePrefs.setUsenetHealthCheck(0);   // add it plain, then ask on purpose

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 2)}, 60);
    queue.start();

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("held.bin"), kParts),
                                    QStringLiteral("held"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY(queue.pauseItem(id));
    QCOMPARE(queue.findItem(id)->status, UsenetItemStatus::Paused);

    thePrefs.setUsenetHealthCheck(1);
    QVERIFY(queue.recheckItem(id));

    QTRY_VERIFY_WITH_TIMEOUT(queue.findItem(id)->status != UsenetItemStatus::Checking, 30000);

    // Asking a question about something the user paused is not a decision to
    // start it. The verdict is recorded; the pause is left exactly where it was.
    QCOMPARE(queue.findItem(id)->status, UsenetItemStatus::Paused);
    QCOMPARE(queue.findItem(id)->healthPercent, 100);

    queue.stop();
}

void tst_UsenetQueue::changingTheServerListDoesNotStrandACheckingItem()
{
    // Mute: the connection is accepted and then nothing is ever said, so the
    // probe stays in flight for as long as we leave it.
    FakeNntpServer server;
    server.setMute(true);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setUsenetHealthCheck(1);

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 2)}, 60);
    queue.start();

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("strand.bin"), kParts),
                                    QStringLiteral("strand"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QCOMPARE(queue.findItem(id)->status, UsenetItemStatus::Checking);

    // Saving the Options page tears every worker down and rebuilds it, so the
    // requests this probe is waiting on cease to exist. Nothing else can ever
    // finish it, and an item left in Checking is a download that never starts.
    NewsServer other = serverConfig(port, 3);
    queue.applyServers({other}, 60);

    QCOMPARE(queue.findItem(id)->status, UsenetItemStatus::Queued);
    QVERIFY(queue.findItem(id)->stalledReason.isEmpty());
    // No verdict, rather than a bad one: nothing was ever answered.
    QCOMPARE(queue.findItem(id)->healthPercent, -1);

    queue.stop();
}

void tst_UsenetQueue::probeBytesAreBilledToTheAccountThatAnsweredThem()
{
    const QByteArray whole = payload(kPartSize * kParts);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, kParts,
                                                       QStringLiteral("bill.bin")));
    // Refused, so the verdict pauses the item and no BODY ever follows — which
    // is what makes every byte booked here provably the probe's.
    server.setStatRefusal(messageIdFor(1));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
    thePrefs.setUsenetHealthCheck(1);
    thePrefs.setUsenetHealthMinPercent(95);

    const NewsServer config = serverConfig(port, 2);
    UsenetQueue queue;
    queue.applyServers({config}, 60);
    queue.start();

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("bill.bin"), kParts),
                                    QStringLiteral("bill"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QTRY_VERIFY_WITH_TIMEOUT(queue.findItem(id) != nullptr
                                 && queue.findItem(id)->status != UsenetItemStatus::Checking,
                             30000);
    QCOMPARE(queue.findItem(id)->status, UsenetItemStatus::Paused);

    bool sawBody = false;
    for (const QString& cmd : server.receivedCommands()) {
        if (cmd.startsWith(QStringLiteral("BODY ")))
            sawBody = true;
    }
    QVERIFY2(!sawBody, "a paused item downloaded anyway; the byte count is not the probe's");

    // The provider billed the greeting, the login and the status line whether or
    // not it had the article, so the meter has to as well. A probe that spent
    // nothing on the books would understate an allowance, and understating an
    // allowance is how it gets overspent.
    QVERIFY2(queue.usage().periodBytes(config.accountId) > 0,
             "the probe's wire cost was not booked against the account");

    queue.stop();
}

QTEST_MAIN(tst_UsenetQueue)
void tst_UsenetQueue::everyServerOnALevelIsAskedBeforeEscalating()
{
    // "A higher level is only reached when *every* server below reported the
    // article missing" -- NntpServerPool.h, docs/usenet-module.md. Two accounts
    // on level 0, one of which has the release: the level-1 fill server must
    // never be contacted at all. Escalating on the first 430 means paying a block
    // account for articles an idle sibling was holding.
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer empty;                 // level 0, answers 430 to everything
    empty.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    const quint16 emptyPort = empty.start();

    FakeNntpServer sibling;               // level 0, has the whole release
    sibling.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        sibling.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                        QStringLiteral("sib.bin")));
    const quint16 siblingPort = sibling.start();

    FakeNntpServer fill;                  // level 1, also has it -- must stay untouched
    fill.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        fill.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                     QStringLiteral("sib.bin")));
    const quint16 fillPort = fill.start();
    QVERIFY(emptyPort != 0 && siblingPort != 0 && fillPort != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer a = serverConfig(emptyPort, 1);
    a.name = QStringLiteral("empty");
    a.level = 0;
    NewsServer b = serverConfig(siblingPort, 1);
    b.name = QStringLiteral("sibling");
    b.level = 0;
    NewsServer c = serverConfig(fillPort, 1);
    c.name = QStringLiteral("fill");
    c.level = 1;

    UsenetQueue queue;
    queue.applyServers({a, b, c}, 0);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("sib.bin"), 4),
                          QStringLiteral("siblings"), error).isEmpty());
    QVERIFY2(finished.wait(30000), "the download never completed");
    QVERIFY2(finished.at(0).at(1).toBool(), "a release its own level held was failed");

    QCOMPARE(QFile::exists(QDir(thePrefs.incomingDir())
                               .filePath(QStringLiteral("sib.bin"))), true);
    QCOMPARE(fill.connectionCount(), 0);

    queue.stop();
}

void tst_UsenetQueue::retentionSkipsAServerThatCannotHoldTheArticle()
{
    // A release posted long before a provider's retention window cannot be there,
    // and asking costs a round trip per article to be told 430. With a sibling
    // that can hold it, the short-retention account is not even leased.
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer young;       // 30-day retention; holds everything, must not be asked
    young.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        young.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                      QStringLiteral("old.bin")));
    const quint16 youngPort = young.start();

    FakeNntpServer archive;     // unknown retention; the one that should serve it
    archive.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        archive.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                        QStringLiteral("old.bin")));
    const quint16 archivePort = archive.start();
    QVERIFY(youngPort != 0 && archivePort != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer fast = serverConfig(youngPort, 2);
    fast.name = QStringLiteral("young");
    fast.level = 0;
    fast.retention = 30;

    NewsServer deep = serverConfig(archivePort, 2);
    deep.name = QStringLiteral("archive");
    deep.level = 0;
    deep.retention = 0;         // unknown == no opinion

    UsenetQueue queue;
    queue.applyServers({fast, deep}, 0);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    // Posted in 1970: older than any retention anyone sells.
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("old.bin"), 4, /*date*/ 100),
                          QStringLiteral("ancient"), error).isEmpty());
    QVERIFY2(finished.wait(30000), "the download never completed");
    QVERIFY2(finished.at(0).at(1).toBool(), "the archive server's release was failed");

    QCOMPARE(young.connectionCount(), 0);
    QVERIFY(archive.connectionCount() > 0);

    queue.stop();
}

void tst_UsenetQueue::aRetentionGuessNeverMakesAnArticleMissing()
{
    // The fail-safe, and the reason retention is honoured at all. The figure is
    // typed by a user off a pricing page, not read off the wire -- so when
    // believing it would leave nothing to ask, it is ignored instead. Getting
    // this wrong turns a fetchable release into a hole PAR2 has to pay for.
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer only;
    only.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        only.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                     QStringLiteral("old.bin")));
    const quint16 port = only.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer s = serverConfig(port, 2);
    s.retention = 30;           // and the release is decades old

    UsenetQueue queue;
    queue.applyServers({s}, 0);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("old.bin"), 4, /*date*/ 100),
                                    QStringLiteral("ancient"), error);
    QVERIFY(!id.isEmpty());
    QVERIFY2(finished.wait(30000), "retention stranded the only server that had it");
    QVERIFY2(finished.at(0).at(1).toBool(), "the item failed on a retention guess");

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QCOMPARE(item->files.at(0).missingSegments, 0);

    QFile f(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("old.bin")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), whole);
    f.close();

    queue.stop();
}

void tst_UsenetQueue::aDeadOptionalServerCostsOneArticleNotTheDownload()
{
    // A block account is bought precisely because it is a fallback, so it being
    // down is an ordinary Tuesday. It costs the one article it was asked for --
    // not the whole download, which is what six connection refusals used to buy.
    //
    // The item still ends up Failed here, and that is a *different*, deliberate
    // rule: a release with holes and no PAR2 is never published, or the client
    // advertises corrupt data (docs/usenet-module.md, "What gets published"). So
    // the assertion is on which failure happened -- the release was assembled and
    // then rejected for its hole, rather than abandoned at article 2 while
    // articles 3 and 4 were never even asked for.
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer main;
    main.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p) {
        if (p == 2)
            continue;                    // one article the main provider lost
        main.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                     QStringLiteral("holed.bin")));
    }
    const quint16 mainPort = main.start();
    QVERIFY(mainPort != 0);

    // A port with nothing behind it: started to reserve it, then destroyed.
    quint16 deadPort = 0;
    {
        FakeNntpServer gone;
        deadPort = gone.start();
    }
    QVERIFY(deadPort != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer primary = serverConfig(mainPort, 2);
    primary.name = QStringLiteral("main");
    primary.level = 0;

    NewsServer block = serverConfig(deadPort, 2);
    block.name = QStringLiteral("block");
    block.level = 1;
    block.optional = true;

    UsenetQueue queue;
    queue.applyServers({primary, block}, 0);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("holed.bin"), 4),
                                    QStringLiteral("blocked"), error);
    QVERIFY(!id.isEmpty());
    QVERIFY2(finished.wait(60000), "the item never resolved");

    // Not the transport error. Without the optional exemption the item dies at
    // article 2 carrying "Connection refused", and this is the line that says so.
    const QString message = finished.at(0).at(2).toString();
    QVERIFY2(!message.contains(QStringLiteral("Connection"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("a dead optional account failed the download "
                                       "with a transport error: %1").arg(message)));

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);

    // Exactly the one article the dead account was asked for -- so the other
    // three were fetched from the main provider rather than abandoned.
    QCOMPARE(item->files.at(0).missingSegments, 1);
    QCOMPARE(item->files.at(0).done.count(true), 4);

    queue.stop();
}

void tst_UsenetQueue::aRequiredServerStillFailsTheItem()
{
    // The counterpart, so the exemption above cannot quietly become "never fail".
    // A main provider that is unreachable is a real failure and has to say so --
    // a silently stalled queue is the worse outcome for a user.
    FakeNntpServer main;
    main.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        main.addArticle(messageIdFor(p), makeArticle(payload(kPartSize * 4), p, 4,
                                                     QStringLiteral("dead.bin")));
    const quint16 mainPort = main.start();
    QVERIFY(mainPort != 0);

    quint16 deadPort = 0;
    {
        FakeNntpServer gone;
        deadPort = gone.start();
    }
    QVERIFY(deadPort != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer dead = serverConfig(deadPort, 2);
    dead.name = QStringLiteral("dead");
    dead.level = 0;
    dead.optional = false;

    UsenetQueue queue;
    queue.applyServers({dead}, 0);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("dead.bin"), 4),
                          QStringLiteral("required"), error).isEmpty());
    QVERIFY2(finished.wait(60000), "the item neither completed nor failed");
    QVERIFY2(!finished.at(0).at(1).toBool(),
             "an unreachable required provider was treated as blameless");

    queue.stop();
}

void tst_UsenetQueue::addNzbWithNoNameUsesTheNzbsOwn()
{
    // The daemon sends no name when a URL carried none, and this is what makes
    // that the right thing to do rather than a shrug: the NZB names itself, and
    // the item is not called "Usenet download".
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 1)}, 0);

    QByteArray nzb = makeNzb(QStringLiteral("a.bin"), 1);
    nzb.replace("<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n",
                "<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n"
                "  <head><meta type=\"name\">Named.By.The.Nzb</meta></head>\n");

    QString error;
    const QString id = queue.addNzb(nzb, /*name*/ QString(), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QCOMPARE(item->name, QStringLiteral("Named.By.The.Nzb"));
}

// ---------------------------------------------------------------------------
// Provider allowances
//
// The rule these pin, and it is the whole reason the feature is safe to have:
// an allowance may reorder and it may skip an account, but it may **never** be
// the reason an article is declared missing or an item is failed. Where
// retention falls back to asking anyway, an allowance falls back to waiting —
// believing a wrong retention figure costs a round trip, ignoring an allowance
// costs the user money.
// ---------------------------------------------------------------------------

void tst_UsenetQueue::bytesAreAttributedToTheAccountThatServedThem()
{
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer only;
    only.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        only.addArticle(messageIdFor(p), makeArticle(whole, p, 4, QStringLiteral("m.bin")));
    const quint16 port = only.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    const NewsServer s = serverConfig(port, 2);

    UsenetQueue queue;
    queue.applyServers({s}, 0);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("m.bin"), 4),
                          QStringLiteral("metered"), error).isEmpty());
    QVERIFY(finished.wait(30000));

    // Raw wire bytes, so strictly more than the payload: yEnc expansion, the
    // CRLFs, the status lines, the greeting and the AUTHINFO exchange are all
    // things a provider meters and a decoded-bytes count would miss.
    const qint64 spent = queue.usage().periodBytes(s.accountId);
    QVERIFY2(spent > whole.size(),
             qPrintable(QStringLiteral("only %1 bytes booked for %2 of payload")
                            .arg(spent).arg(whole.size())));

    queue.stop();
}

void tst_UsenetQueue::aRefusedArticleStillCostsTheAccountThatRefusedIt()
{
    // A 430 is a round trip the provider bills. Counting only successful
    // articles would make a failover ladder look free, which is exactly the
    // account whose spending a user most wants to see.
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer empty;                       // has the group, has no articles
    empty.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    const quint16 emptyPort = empty.start();
    QVERIFY(emptyPort != 0);

    FakeNntpServer full;
    full.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        full.addArticle(messageIdFor(p), makeArticle(whole, p, 4, QStringLiteral("r.bin")));
    const quint16 fullPort = full.start();
    QVERIFY(fullPort != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer a = serverConfig(emptyPort, 2);
    a.name = QStringLiteral("refuser");
    a.level = 0;
    NewsServer b = serverConfig(fullPort, 2);
    b.name = QStringLiteral("holder");
    b.level = 1;

    UsenetQueue queue;
    queue.applyServers({a, b}, 0);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("r.bin"), 4),
                          QStringLiteral("refused"), error).isEmpty());
    QVERIFY(finished.wait(30000));

    QVERIFY2(queue.usage().periodBytes(a.accountId) > 0,
             "the account that answered 430 was billed nothing");

    queue.stop();
}

void tst_UsenetQueue::anExhaustedAccountIsSkippedWhileAnotherCanServe()
{
    // Both on the same rung: one has spent its allowance, the sibling has not.
    // The article must go to the sibling, and the spent account must not be
    // asked at all — asking and discarding would spend the very bytes the limit
    // exists to stop.
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer spent;
    spent.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    FakeNntpServer fresh;
    fresh.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p) {
        const QByteArray art = makeArticle(whole, p, 4, QStringLiteral("s.bin"));
        spent.addArticle(messageIdFor(p), art);
        fresh.addArticle(messageIdFor(p), art);
    }
    const quint16 spentPort = spent.start();
    const quint16 freshPort = fresh.start();
    QVERIFY(spentPort != 0 && freshPort != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer over = serverConfig(spentPort, 2);
    over.name = QStringLiteral("over");
    over.quotaKind = NntpQuotaKind::Block;
    over.quotaBytes = 1000;
    NewsServer under = serverConfig(freshPort, 2);
    under.name = QStringLiteral("under");

    plantUsage(over.accountId, 5000);   // well past its 1000-byte allowance

    UsenetQueue queue;
    queue.applyServers({over, under}, 0);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("s.bin"), 4),
                          QStringLiteral("skipped"), error).isEmpty());
    QVERIFY2(finished.wait(30000), "the sibling never picked the release up");
    QVERIFY(finished.at(0).at(1).toBool());

    QVERIFY2(!spent.receivedCommands().join(QLatin1Char(' '))
                  .contains(QStringLiteral("BODY")),
             "an account over its allowance was asked for an article anyway");

    queue.stop();
}

void tst_UsenetQueue::aQuotaNeverMakesAnArticleMissing()
{
    // The fail-safe, and the twin of aRetentionGuessNeverMakesAnArticleMissing.
    // One account, allowance spent, and it holds every article. The download
    // must **wait** — not fail, and above all not record the articles as missing,
    // which would spend PAR2 blocks on bytes that were never gone.
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer only;
    only.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        only.addArticle(messageIdFor(p), makeArticle(whole, p, 4, QStringLiteral("w.bin")));
    const quint16 port = only.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer s = serverConfig(port, 2);
    s.quotaKind = NntpQuotaKind::Block;
    s.quotaBytes = 1000;
    plantUsage(s.accountId, 5000);

    UsenetQueue queue;
    queue.applyServers({s}, 0);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("w.bin"), 4),
                                    QStringLiteral("waiting"), error);
    QVERIFY(!id.isEmpty());

    // Long enough for the retry budget to have burned through several times over
    // if the allowance were being treated as a failure.
    QTest::qWait(4000);
    QVERIFY2(finished.isEmpty(), "a spent allowance resolved the item");

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QCOMPARE(item->files.at(0).missingSegments, 0);
    QVERIFY2(item->status != UsenetItemStatus::Failed, "a spent allowance failed the item");
    QVERIFY2(!item->stalledReason.isEmpty(), "the queue stopped without saying why");

    // And it is genuinely only waiting: raise the allowance and it finishes.
    s.quotaBytes = 1000000000;
    queue.applyServers({s}, 0);
    QVERIFY2(finished.wait(30000), "raising the allowance did not resume the download");
    QVERIFY(finished.at(0).at(1).toBool());

    QFile f(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("w.bin")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), whole);
    f.close();

    queue.stop();
}

void tst_UsenetQueue::aDeadOptionalServerCannotStrandAQuotaBlockedArticle()
{
    // The subtle one. handleSegmentFailure()'s `optional` exemption marks an
    // article *missing* once the transport budget runs out — and it never
    // consults the ladder. With the required account excluded for spending
    // rather than for saying no, a flaky block account would quietly declare
    // articles missing that the required account was holding all along.
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer main;
    main.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        main.addArticle(messageIdFor(p), makeArticle(whole, p, 4, QStringLiteral("o.bin")));
    const quint16 mainPort = main.start();
    QVERIFY(mainPort != 0);

    quint16 deadPort = 0;
    {
        FakeNntpServer gone;
        deadPort = gone.start();
    }
    QVERIFY(deadPort != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer required = serverConfig(mainPort, 2);
    required.name = QStringLiteral("main");
    required.level = 0;
    required.quotaKind = NntpQuotaKind::Block;
    required.quotaBytes = 1000;

    NewsServer block = serverConfig(deadPort, 2);
    block.name = QStringLiteral("block");
    block.level = 0;              // same rung, so it is the one actually asked
    block.optional = true;

    plantUsage(required.accountId, 5000);

    UsenetQueue queue;
    queue.applyServers({required, block}, 0);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("o.bin"), 4),
                                    QStringLiteral("stranded"), error);
    QVERIFY(!id.isEmpty());

    QTest::qWait(6000);           // several times kMaxTransportRetries' worth

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QVERIFY2(item->files.at(0).missingSegments == 0,
             "a dead optional account declared articles missing that the "
             "required account was holding, because the allowance hid it");
    QVERIFY2(item->status != UsenetItemStatus::Failed, "the item failed");

    // Pay for the required account again and the release lands intact.
    required.quotaBytes = 1000000000;
    queue.applyServers({required, block}, 0);
    QVERIFY2(finished.wait(60000), "the download never resumed");

    const auto* after = queue.findItem(id);
    QVERIFY(after != nullptr);
    QCOMPARE(after->files.at(0).missingSegments, 0);

    queue.stop();
}

void tst_UsenetQueue::anExhaustedLevelParksUnlessAskedToFallThrough()
{
    // Spending the *next* level's money to honour a limit on this one is a
    // decision only the user can make: block credit usually costs more per GB
    // than the plan it would be covering. So the default is to wait, and the
    // opt-in is per account.
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer low;
    low.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    FakeNntpServer high;
    high.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p) {
        const QByteArray art = makeArticle(whole, p, 4, QStringLiteral("f.bin"));
        low.addArticle(messageIdFor(p), art);
        high.addArticle(messageIdFor(p), art);
    }
    const quint16 lowPort = low.start();
    const quint16 highPort = high.start();
    QVERIFY(lowPort != 0 && highPort != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer main = serverConfig(lowPort, 2);
    main.name = QStringLiteral("main");
    main.level = 0;
    main.quotaKind = NntpQuotaKind::Block;
    main.quotaBytes = 1000;

    NewsServer fill = serverConfig(highPort, 2);
    fill.name = QStringLiteral("fill");
    fill.level = 1;

    plantUsage(main.accountId, 5000);

    UsenetQueue queue;
    queue.applyServers({main, fill}, 0);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("f.bin"), 4),
                          QStringLiteral("parked"), error).isEmpty());

    QTest::qWait(4000);
    QVERIFY2(finished.isEmpty(), "the download spent the fill account uninvited");
    QVERIFY2(!high.receivedCommands().join(QLatin1Char(' '))
                  .contains(QStringLiteral("BODY")),
             "the fill account was billed without the user asking for it");

    // Now ask for it.
    main.quotaFallThrough = true;
    queue.applyServers({main, fill}, 0);
    QVERIFY2(finished.wait(30000), "fall-through did not reach the next level");
    QVERIFY(finished.at(0).at(1).toBool());

    queue.stop();
}

void tst_UsenetQueue::nearingTheAllowanceWarnsOnceBeforeItBites()
{
    // A download that stops at 3 a.m. is a surprise; a line in the log the day
    // before is not. The warning has to fire once, not four times a second, and
    // it has to re-arm when the figure drops back under.
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer s = serverConfig(31337, 2);      // never connected to
    s.name = QStringLiteral("nearly");
    s.quotaKind = NntpQuotaKind::Block;
    s.quotaBytes = 1000;
    plantUsage(s.accountId, 950);               // 95%

    int warnings = 0;
    const auto counter = [&warnings](QtMsgType type, const QMessageLogContext&,
                                     const QString& text) {
        if (type == QtWarningMsg && text.contains(QStringLiteral("of its allowance")))
            ++warnings;
    };
    static std::function<void(QtMsgType, const QMessageLogContext&, const QString&)> sink;
    sink = counter;
    QtMessageHandler prev = qInstallMessageHandler(
        [](QtMsgType t, const QMessageLogContext& c, const QString& m) { sink(t, c, m); });

    UsenetQueue queue;
    queue.applyServers({s}, 0);
    queue.start();
    QTest::qWait(1500);          // several dispatch rounds and a tick or two
    queue.stop();

    qInstallMessageHandler(prev);

    QCOMPARE(warnings, 1);
}


#include "tst_UsenetQueue.moc"
