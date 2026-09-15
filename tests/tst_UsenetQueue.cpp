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
#include "TestFixtures.h"
#include "TestHelpers.h"

#include "decode/YencDecoder.h"
#include "queue/UsenetHealth.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"
#include "queue/UsenetQueueStore.h"

#include "prefs/Preferences.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>

using namespace eMule;
using namespace eMule::usenet;
using eMule::testing::FakeNntpServer;
using eMule::testing::ScopedStatistics;

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

/// An NZB whose files are named, in the order given. Distinct from
/// makeMultiFileNzb() because the point here is that NZB order and *name* order
/// disagree — the payload list is sorted by name, so a positional match between
/// the two is wrong exactly when the two orders differ.
QByteArray makeNamedFilesNzb(const QStringList& names, int partsPerFile)
{
    QByteArray xml;
    xml += R"(<?xml version="1.0" encoding="iso-8859-1" ?>)"
           "\n<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";
    for (int f = 0; f < names.size(); ++f) {
        xml += QStringLiteral(
                   "  <file poster=\"tester\" date=\"1700000000\" "
                   "subject=\"&quot;%1&quot; yEnc (1/%2)\">\n")
                   .arg(names.at(f)).arg(partsPerFile).toUtf8();
        xml += "    <groups><group>alt.binaries.test</group></groups>\n";
        xml += "    <segments>\n";
        for (int p = 1; p <= partsPerFile; ++p) {
            xml += QStringLiteral(
                       "      <segment bytes=\"3500\" number=\"%1\">n%2p%1@example.com"
                       "</segment>\n")
                       .arg(p).arg(f).toUtf8();
        }
        xml += "    </segments>\n  </file>\n";
    }
    xml += "</nzb>\n";
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
    void aCompletedReleaseLandsInItsCategoryFolder();
    void aCategoryWhoseFolderIsGoneDoesNotStrandTheRelease();
    void anOutOfRangeCategoryResolvesToAll();
    void theCategorySurvivesARestart();
    void aPar2NameSurvivesARestart();
    void aSidecarWithNoPar2NameKeyReadsAsUnknown();
    void anOldSidecarWithNoCategoryKeyLoadsAsAll();
    void deletingACategoryRemapsTheLiveQueue();
    void aQueueThatIsNotRunningIsStillRemappedOnDisk();
    void anAutomaticAddStillGetsAutoCategorised();
    void leavesNoScratchBehind();
    void persistedStateResumesInsteadOfRefetching();
    void connectionBudgetIsDividedNotReplicated();
    void missingArticlesEscalateToTheNextLevel();
    void everyServerOnALevelIsAskedBeforeEscalating();
    void retentionSkipsAServerThatCannotHoldTheArticle();
    void aRetentionGuessNeverMakesAnArticleMissing();
    void aDeadOptionalServerCostsOneArticleNotTheDownload();
    void aRequiredServerStillFailsTheItem();
    void aDamagedArticleIsFetchedFromAnotherServer();
    void anArticleDamagedEverywhereIsMissingNotFatal();
    void aSettingsSaveDoesNotSpendRetries();
    void aPausedItemIsNotPostProcessedByItsLastArticle();
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
    void aManualReAddOfACompletedReleaseAsksFirst();
    void anAutomaticReAddOfACompletedReleaseIsSkipped();
    void aRepostWithFreshMessageIdsIsNotAnExactDuplicate();
    void theDuplicateIndexSurvivesARestart();
    void aRefusalSentenceCarriesNoEmDash();
    void aReleaseRemovedFromTheQueueIsStillRecognisedWhenReAdded();
    void forcingAnAddOverridesTheHistoryButNeverTheLiveQueue();
    void aForcedReAddLeavesTheFinishedRowAndStillRefusesAThird();
    void anAutomaticAddOfSomethingInTheHistoryIsTerminalNotRetryable();
    void aReleaseNameIsFoldedTheSameWayFromEveryIntake();
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
    void aPublishedFileIsAttributedToTheNzbFileItCameFrom();
    void aRetryAsksOnlyForTheArticlesThatWereMissing();
    void aRetryKeepsTheHoleCountUntilTheBytesActuallyArrive();
    void aRetryThatFindsNothingFailsAgainAndSaysWhatItLearned();
    void resumingAFailedItemWithTheSameAccountsAsksForNothing();
    void resumingAFailedItemAfterAnAccountIsAddedReAsksTheDeadArticles();
    void theMissingArticleMapSurvivesARestartAndMatchesTheCount();
    void aSidecarWithNoMissingMapRefusesTheRetryRatherThanGuessing();
    void belowTheFloorTheQueueWaitsAndSaysWhy();
    void spaceReturningResumesWithoutTheUser();
    void theDiskFloorOffMeansTodaysBehaviourExactly();
    void aWriteFailureFailsLocallyAndNeverBlamesTheServer();
    void aVeryHighItemIsFetchedBeforeAHighOne();
    void aPriorityOutsideTheFiveLevelsIsClamped();
    void aDeadProxyNeverMakesAnArticleMissingOrBlamesAServer();
};

void tst_UsenetQueue::downloadsAnNzbByteIdentically()
{
    ScopedStatistics stats;
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

    // What the Statistics window is told: every article and its payload, one
    // completed release, and per account the same wire bytes the billing meter took.
    const UsenetCounters& c = stats->usenetSession();
    QCOMPARE(c.articlesDownloaded, uint64(kParts));
    QCOMPARE(c.decodedBytes, uint64(whole.size()));
    QCOMPARE(c.itemsCompleted, uint64(1));
    QCOMPARE(c.completedBytes, uint64(whole.size()));
    QCOMPARE(c.itemsFailed, uint64(0));
    QVERIFY(c.wireBytes > c.decodedBytes);   // yEnc and NNTP overhead
    const QString account = serverConfig(port, 4).accountId;
    QCOMPARE(qint64(queue.stats().servers().value(account).wireBytes),
             queue.usage().totalBytes(account));
    // Usenet is no longer booked as eD2K's "Downloaded Data".
    QCOMPARE(stats->sessionReceivedBytes(), uint64(0));

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
    ScopedStatistics stats;
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

    // Four 430s answered by a fill server: failovers, not losses.
    QCOMPARE(stats->usenetSession().articlesNotFound, uint64(4));
    QCOMPARE(stats->usenetSession().articlesMissing, uint64(0));
    QCOMPARE(queue.stats().servers().value(main.accountId).notFound, uint64(4));
    QCOMPARE(queue.stats().servers().value(fill.accountId).articles, uint64(4));

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

    QVERIFY(!queue.addNzb(nzb, QStringLiteral("first"), error, {}, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::Added);

    // The distinction the watch folder and the feed poller both live on: an
    // actor that cannot tell "we already have this" from "that did not work"
    // retries a duplicate forever.
    QVERIFY(queue.addNzb(nzb, QStringLiteral("again"), error, {}, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::Duplicate);

    // And apart from both: never worth another attempt.
    QVERIFY(queue.addNzb(QByteArrayLiteral("not xml"), QStringLiteral("junk"), error, {}, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::Invalid);

    QVERIFY(queue.addNzb(QByteArrayLiteral("<nzb></nzb>"), QStringLiteral("empty"), error, {}, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::Invalid);

    // And apart from Duplicate: nothing overrides a release still arriving, but a
    // finished one is a question the user may answer with force.
    const QString id = queue.items().front()->id;
    const_cast<UsenetQueueItem*>(queue.findItem(id))->status = UsenetItemStatus::Complete;
    QVERIFY(queue.addNzb(nzb, QStringLiteral("again"), error, {}, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::AlreadyDownloaded);
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
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("auto.bin"), 2, 1700000000, QStringLiteral("auto")), QStringLiteral("from a feed"), error, {.source = UsenetAddSource::Automatic}).isEmpty());
    QCOMPARE(queue.items().size(), 1);
    QCOMPARE(queue.items().first()->status, UsenetItemStatus::Paused);

    // A person asking for it is not what the setting is about.
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("manual.bin"), 2, 1700000000, QStringLiteral("man")), QStringLiteral("by hand"), error, {.source = UsenetAddSource::Manual}).isEmpty());
    QCOMPARE(queue.items().size(), 2);
    QCOMPARE(queue.items().at(1)->status, UsenetItemStatus::Queued);

    thePrefs.setUsenetAutoAddPaused(false);
}

void tst_UsenetQueue::aManualReAddOfACompletedReleaseAsksFirst()
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
    // Ed2kLinkImporter makes for a completed eD2K file. It is a *question*, not a
    // refusal, so nothing is queued until the answer comes back as force.
    error.clear();
    UsenetAddOutcome outcome = UsenetAddOutcome::Added;
    QVERIFY(queue.addNzb(nzb, QStringLiteral("again"), error, {}, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::AlreadyDownloaded);
    QVERIFY(!error.isEmpty());
    QCOMPARE(queue.items().size(), 1);

    // Said yes.
    error.clear();
    QVERIFY2(!queue.addNzb(nzb, QStringLiteral("again"), error, {.force = true}, &outcome).isEmpty(),
             qPrintable(error));
    QCOMPARE(outcome, UsenetAddOutcome::Added);
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
    UsenetAddOutcome outcome = UsenetAddOutcome::Added;
    QVERIFY(queue.addNzb(nzb, QStringLiteral("again"), error, {.source = UsenetAddSource::Automatic}, &outcome).isEmpty());
    // Terminal, and told apart from "already queued" so the feed does not retry.
    QCOMPARE(outcome, UsenetAddOutcome::AlreadyDownloaded);
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

    // The same rule for the three sentences the history produces, which the same
    // dialog formats the same way.
    const QString id = queue.items().front()->id;
    const_cast<UsenetQueueItem*>(queue.findItem(id))->status = UsenetItemStatus::Complete;
    error.clear();
    QVERIFY(queue.addNzb(nzb, QStringLiteral("third"), error).isEmpty());
    QVERIFY2(!error.contains(QStringLiteral(" — ")), qPrintable(error));

    QVERIFY(queue.removeItem(id, true));
    error.clear();
    QVERIFY(queue.addNzb(nzb, QStringLiteral("fourth"), error).isEmpty());
    QVERIFY2(!error.isEmpty() && !error.contains(QStringLiteral(" — ")), qPrintable(error));
}

// ---------------------------------------------------------------------------
// History — what survives leaving the queue
//
// The gap these close: findDuplicate() used to scan only m_items, so clearing a
// row erased every trace of a release and re-adding its NZB downloaded it again
// without a word.
// ---------------------------------------------------------------------------

void tst_UsenetQueue::aReleaseRemovedFromTheQueueIsStillRecognisedWhenReAdded()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setRememberDownloadedFiles(true);
    thePrefs.setRememberCancelledFiles(true);

    const QByteArray nzb = makeNzb(QStringLiteral("gone.bin"), 3);

    {
        UsenetQueue queue;
        queue.applyServers({serverConfig(1119, 1)}, 0);

        QString error;
        const QString id = queue.addNzb(nzb, QStringLiteral("gone"), error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        const_cast<UsenetQueueItem*>(queue.findItem(id))->status = UsenetItemStatus::Complete;
        QVERIFY(queue.removeItem(id, true));
        QVERIFY(queue.items().empty());
    }

    // A second queue, reading the file from disk: this is the case that matters,
    // because after one restart everything is restored rather than remembered.
    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    QString error;
    UsenetAddOutcome outcome = UsenetAddOutcome::Added;
    QVERIFY(queue.addNzb(nzb, QStringLiteral("gone"), error, {}, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::AlreadyDownloaded);
    QVERIFY(queue.items().empty());
}

void tst_UsenetQueue::forcingAnAddOverridesTheHistoryButNeverTheLiveQueue()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setRememberDownloadedFiles(true);

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    const QByteArray nzb = makeNzb(QStringLiteral("force.bin"), 3);
    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("force"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    // Still arriving. No answer the user could give makes a second copy useful,
    // so force must not reach this check at all.
    UsenetAddOutcome outcome = UsenetAddOutcome::Added;
    QVERIFY(queue.addNzb(nzb, QStringLiteral("force"), error, {.force = true}, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::Duplicate);
    QCOMPARE(queue.items().size(), 1);

    // Finished and cleared: now force is the whole point.
    const_cast<UsenetQueueItem*>(queue.findItem(id))->status = UsenetItemStatus::Complete;
    QVERIFY(queue.removeItem(id, true));

    error.clear();
    QVERIFY2(!queue.addNzb(nzb, QStringLiteral("force"), error, {.force = true}, &outcome).isEmpty(),
             qPrintable(error));
    QCOMPARE(outcome, UsenetAddOutcome::Added);
    QCOMPARE(queue.items().size(), 1);
}

void tst_UsenetQueue::aForcedReAddLeavesTheFinishedRowAndStillRefusesAThird()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setRememberDownloadedFiles(true);

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    const QByteArray nzb = makeNzb(QStringLiteral("twice.bin"), 3);
    QString error;
    const QString first = queue.addNzb(nzb, QStringLiteral("twice"), error);
    QVERIFY2(!first.isEmpty(), qPrintable(error));
    const_cast<UsenetQueueItem*>(queue.findItem(first))->status = UsenetItemStatus::Complete;

    // Said yes, without clearing the finished row. Two items now carry the same
    // digest — one Complete, one arriving.
    UsenetAddOutcome outcome = UsenetAddOutcome::Added;
    QVERIFY2(!queue.addNzb(nzb, QStringLiteral("twice"), error, {.force = true}, &outcome).isEmpty(),
             qPrintable(error));
    QCOMPARE(queue.items().size(), 2);

    // A third add must see the one still arriving, not the finished one that
    // happens to be listed first. Otherwise it reads as a question the user can
    // answer with force, and a release in flight gets started a third time.
    error.clear();
    QVERIFY(queue.addNzb(nzb, QStringLiteral("twice"), error, {.force = true}, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::Duplicate);
    QCOMPARE(queue.items().size(), 2);
}

void tst_UsenetQueue::anAutomaticAddOfSomethingInTheHistoryIsTerminalNotRetryable()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setRememberCancelledFiles(true);

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    const QByteArray nzb = makeNzb(QStringLiteral("feed.bin"), 3);
    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("feed"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    // Never finished, then thrown away: cancelled.
    QVERIFY(queue.removeItem(id, true));

    // AlreadyDownloaded rather than Failed, so DaemonApp's sink maps it to
    // AlreadyHave. Reading it as retryable would spend an API hit and an .nzb
    // fetch on this release on every single poll.
    UsenetAddOutcome outcome = UsenetAddOutcome::Added;
    QVERIFY(queue.addNzb(nzb, QStringLiteral("feed"), error, {.source = UsenetAddSource::Automatic}, &outcome).isEmpty());
    QCOMPARE(outcome, UsenetAddOutcome::AlreadyDownloaded);
}

void tst_UsenetQueue::aReleaseNameIsFoldedTheSameWayFromEveryIntake()
{
    // An indexer's title, an .nzb filename and the name inside the NZB are three
    // spellings of one release. knownTypeForTitle() joins on the folded form, so
    // the folding is what decides whether a search row is marked at all.
    const QString canonical =
        usenetFoldedReleaseName(QStringLiteral("Some.Release.Name-GROUP"));
    QCOMPARE(usenetFoldedReleaseName(QStringLiteral("Some_Release_Name-GROUP.nzb")), canonical);
    QCOMPARE(usenetFoldedReleaseName(QStringLiteral("  some release name GROUP  ")), canonical);
    QCOMPARE(usenetFoldedReleaseName(QStringLiteral("SOME--RELEASE..NAME - GROUP")), canonical);

    // And not so eager that two different releases collapse into one.
    QVERIFY(usenetFoldedReleaseName(QStringLiteral("Release.S01E01"))
            != usenetFoldedReleaseName(QStringLiteral("Release.S01E02")));
    QVERIFY(usenetFoldedReleaseName(QString()).isEmpty());
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
    ScopedStatistics stats;
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

    QCOMPARE(stats->usenetSession().healthChecks, uint64(1));
    QCOMPARE(stats->usenetSession().healthPaused, uint64(1));
    QCOMPARE(stats->usenetSession().healthPassed, uint64(0));
    QVERIFY(stats->usenetSession().statProbes >= 1);

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
    item.publishedPaths = {QStringLiteral("/incoming/h.bin")};

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
    // publishedPaths is optional on exactly the same terms, and round-trips.
    QCOMPARE(loaded.publishedPaths, item.publishedPaths);

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
    ScopedStatistics stats;
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

    // The probes we cut short are nobody's fault and were never answered.
    QTest::qWait(100);   // the aborted results arrive queued
    QCOMPARE(stats->usenetSession().connectionErrors, uint64(0));
    QCOMPARE(stats->usenetSession().statProbes, uint64(0));

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


// ---------------------------------------------------------------------------
// Download categories
//
// A category is an *index* into a list the user can edit, and the folder behind
// it is asked for at completion rather than cached when the release is queued.
// Everything here guards one half of that: that the index reaches the right
// folder, and that editing the list cannot leave an index pointing at a
// stranger's.
// ---------------------------------------------------------------------------

namespace {

/// A prefs setup with `n` real category folders under the temp root.
QStringList setUpCategories(eMule::testing::TempDir& tmp, const QStringList& titles)
{
    QStringList paths;
    QList<DownloadCategory> cats;
    cats.append(DownloadCategory{.title = QStringLiteral("All")});

    for (const QString& title : titles) {
        const QString dir = tmp.filePath(title);
        QDir().mkpath(dir);
        paths << QDir::cleanPath(dir);
        cats.append(DownloadCategory{.title = title, .incomingPath = dir});
    }

    thePrefs.setCategories(cats);
    return paths;
}

} // namespace

void tst_UsenetQueue::aCompletedReleaseLandsInItsCategoryFolder()
{
    const QByteArray whole = payload(kPartSize * 2);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 2, 1, 2);
    for (int p = 1; p <= 2; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 2,
                                                       QStringLiteral("show.mkv")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
    const QStringList catDirs = setUpCategories(tmp, {QStringLiteral("TV")});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("show.mkv"), 2), QStringLiteral("a show"), error, {.category = 1});
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QCOMPARE(queue.findItem(id)->category, 1);

    QVERIFY2(finished.wait(30000), "the download never reported a terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(), "completed with missing articles");

    // In the category's own folder — and, just as importantly, *not* in the
    // global incoming dir, which is where every release went before.
    QVERIFY2(QFile::exists(QDir(catDirs.at(0)).filePath(QStringLiteral("show.mkv"))),
             "the release did not land in its category folder");
    QVERIFY2(!QFile::exists(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("show.mkv"))),
             "the release also landed in the global incoming dir");

    queue.stop();
    thePrefs.setCategories({});
}

void tst_UsenetQueue::aCategoryWhoseFolderIsGoneDoesNotStrandTheRelease()
{
    const QByteArray whole = payload(kPartSize * 2);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 2, 1, 2);
    for (int p = 1; p <= 2; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 2,
                                                       QStringLiteral("gone.mkv")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
    const QStringList catDirs = setUpCategories(tmp, {QStringLiteral("Unmounted")});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("gone.mkv"), 2), QStringLiteral("a show"), error, {.category = 1});
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    // The volume goes away *after* the release was queued. This is why the path
    // is resolved at completion and never cached at add time -- and why
    // incomingDirForCategory() re-tests the directory on every call.
    QVERIFY(QDir(catDirs.at(0)).removeRecursively());

    QVERIFY2(finished.wait(30000), "the download never reported a terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(),
             "a missing category folder was allowed to fail the download");

    // It lands, in the global incoming dir. Losing the folder costs the label,
    // never the bytes.
    QVERIFY2(QFile::exists(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("gone.mkv"))),
             "the release was stranded when its category folder vanished");
    // And the category itself is kept -- the volume may come back.
    QCOMPARE(queue.findItem(id)->category, 1);

    queue.stop();
    thePrefs.setCategories({});
}

void tst_UsenetQueue::anOutOfRangeCategoryResolvesToAll()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    setUpCategories(tmp, {QStringLiteral("TV")});

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("stale.bin"), 2), QStringLiteral("stale"), error, {.category = 1});
    QVERIFY(!id.isEmpty());

    // Another GUI trims the list while this item is queued -- or a hand-edited
    // preferences.yml does it with the daemon down. The index now names nothing.
    thePrefs.setCategories({DownloadCategory{.title = QStringLiteral("All")}});

    // Resolution must not reach past the end of the list. It falls back to the
    // global incoming dir, which is what index 0 means.
    QCOMPARE(thePrefs.incomingDirForCategory(queue.findItem(id)->category),
             thePrefs.incomingDir());

    thePrefs.setCategories({});
}

void tst_UsenetQueue::theCategorySurvivesARestart()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    setUpCategories(tmp, {QStringLiteral("TV"), QStringLiteral("Movies")});

    QString id;
    {
        UsenetQueue queue;
        queue.applyServers({serverConfig(1119, 1)}, 0);
        QString error;
        id = queue.addNzb(makeNzb(QStringLiteral("restart.bin"), 2), QStringLiteral("restart"), error, {.category = 2});
        QVERIFY(!id.isEmpty());
        QCOMPARE(queue.findItem(id)->category, 2);
    }

    // Restored from the .nzbstate sidecar, not re-derived: the category is a
    // user decision and nothing else on disk records it.
    UsenetQueue restored;
    restored.applyServers({serverConfig(1119, 1)}, 0);
    restored.start();
    QCOMPARE(restored.items().size(), 1);
    QCOMPARE(restored.findItem(id)->category, 2);
    restored.stop();

    thePrefs.setCategories({});
}

void tst_UsenetQueue::anOldSidecarWithNoCategoryKeyLoadsAsAll()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    QString id;
    {
        UsenetQueue queue;
        queue.applyServers({serverConfig(1119, 1)}, 0);
        QString error;
        id = queue.addNzb(makeNzb(QStringLiteral("old.bin"), 2),
                          QStringLiteral("old"), error);
        QVERIFY(!id.isEmpty());
    }

    // Strip the key, as every sidecar written before categories existed. This is
    // why kStateVersion did not move for it: an absent key has to read as 0, or
    // an older daemon's queue would be refused wholesale.
    const QString path = UsenetQueueStore::statePath(id);
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray text = f.readAll();
    f.close();
    QVERIFY2(text.contains("category:"), "the sidecar never carried a category");
    QList<QByteArray> kept;
    for (const QByteArray& line : text.split('\n')) {
        if (!line.trimmed().startsWith("category:"))
            kept.append(line);
    }
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(kept.join('\n'));
    f.close();

    UsenetQueue restored;
    restored.applyServers({serverConfig(1119, 1)}, 0);
    restored.start();
    QCOMPARE(restored.items().size(), 1);
    QCOMPARE(restored.findItem(id)->category, 0);
    restored.stop();
}

void tst_UsenetQueue::deletingACategoryRemapsTheLiveQueue()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    setUpCategories(tmp, {QStringLiteral("TV"), QStringLiteral("Movies")});

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    QString error;
    const QString inTv = queue.addNzb(makeNzb(QStringLiteral("tv.bin"), 2, 1700000000, QStringLiteral("tv")), QStringLiteral("tv"), error, {.category = 1});
    const QString inMovies = queue.addNzb(makeNzb(QStringLiteral("mv.bin"), 2, 1700000000, QStringLiteral("mv")), QStringLiteral("movies"), error, {.category = 2});
    QVERIFY(!inTv.isEmpty() && !inMovies.isEmpty());

    // The user deletes "TV". Movies shifts from 2 to 1, and what was in TV has
    // no category any more -- the same answer DownloadQueue::remapCategories()
    // gives, and the same one MFC's ResetCatParts does.
    queue.remapCategories({{2u, 1u}});

    QCOMPARE(queue.findItem(inTv)->category, 0);
    QCOMPARE(queue.findItem(inMovies)->category, 1);

    thePrefs.setCategories({});
}

void tst_UsenetQueue::aQueueThatIsNotRunningIsStillRemappedOnDisk()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    setUpCategories(tmp, {QStringLiteral("TV"), QStringLiteral("Movies")});

    QString inTv;
    QString inMovies;
    {
        UsenetQueue queue;
        queue.applyServers({serverConfig(1119, 1)}, 0);
        QString error;
        inTv = queue.addNzb(makeNzb(QStringLiteral("tv.bin"), 2, 1700000000, QStringLiteral("tv")), QStringLiteral("tv"), error, {.category = 1});
        inMovies = queue.addNzb(makeNzb(QStringLiteral("mv.bin"), 2, 1700000000, QStringLiteral("mv")), QStringLiteral("movies"), error, {.category = 2});
        QVERIFY(!inTv.isEmpty() && !inMovies.isEmpty());
    }

    // With Usenet switched off the queue holds nothing and these items exist
    // only as sidecars. ED2K has no equivalent exposure -- theApp.downloadQueue
    // is live for as long as the daemon is -- so this path is the whole reason
    // UsenetQueueStore::remapCategories() exists.
    QCOMPARE(UsenetQueueStore::remapCategories({{2u, 1u}}), 2);

    UsenetQueue restored;
    restored.applyServers({serverConfig(1119, 1)}, 0);
    restored.start();
    QCOMPARE(restored.findItem(inTv)->category, 0);
    QCOMPARE(restored.findItem(inMovies)->category, 1);
    restored.stop();

    thePrefs.setCategories({});
}

void tst_UsenetQueue::anAutomaticAddStillGetsAutoCategorised()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    const QString dir = tmp.filePath(QStringLiteral("TV"));
    QDir().mkpath(dir);
    DownloadCategory tv{.title = QStringLiteral("TV"), .incomingPath = dir};
    tv.autocat = QStringLiteral("S0*E0*");
    thePrefs.setCategories({DownloadCategory{.title = QStringLiteral("All")}, tv});

    UsenetQueue queue;
    queue.applyServers({serverConfig(1119, 1)}, 0);

    QString error;
    // The watch folder, a feed, the command line: none of them names a category,
    // and all of them get one here because addNzb() does it rather than each
    // call site -- the same argument that put the duplicate guard inside it.
    const QString matched = queue.addNzb(makeNzb(QStringLiteral("ep.bin"), 2, 1700000000, QStringLiteral("ep")), QStringLiteral("Some.Show.S01E02.1080p"), error, {.source = UsenetAddSource::Automatic});
    QVERIFY(!matched.isEmpty());
    QCOMPARE(queue.findItem(matched)->category, 1);

    const QString unmatched = queue.addNzb(makeNzb(QStringLiteral("other.bin"), 2, 1700000000, QStringLiteral("ot")), QStringLiteral("Some.Film.2160p"), error, {.source = UsenetAddSource::Automatic});
    QVERIFY(!unmatched.isEmpty());
    QCOMPARE(queue.findItem(unmatched)->category, 0);

    // A category the caller *did* choose is never second-guessed, even when a
    // pattern would have picked a different one.
    const QString explicitCat = queue.addNzb(makeNzb(QStringLiteral("pick.bin"), 2, 1700000000, QStringLiteral("pk")), QStringLiteral("Some.Show.S03E04.1080p"), error, {.source = UsenetAddSource::Automatic});
    QVERIFY(!explicitCat.isEmpty());
    QCOMPARE(queue.findItem(explicitCat)->category, 1);

    thePrefs.setCategories({});
}

// ---------------------------------------------------------------------------
// The name PAR2 gave a file
// ---------------------------------------------------------------------------

namespace {

/// A one-file item with @p par2Name already resolved, saved to its sidecar.
UsenetQueueItem savedItemWithPar2Name(const QString& par2Name)
{
    UsenetQueueItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.name = QStringLiteral("Rel");

    NzbFileInfo info;
    info.subject = QStringLiteral("\"abcdef0123456789.bin\" yEnc (1/2)");
    info.fileName = QStringLiteral("abcdef0123456789.bin");
    info.groups << QStringLiteral("alt.binaries.test");
    for (int p = 1; p <= 2; ++p)
        info.segments.append(NzbSegment{messageIdFor(p), 3500, p});
    item.nzb.files.append(info);
    item.initFileStates(thePrefs.usenetTempDir());

    item.files[0].articleFileName = QStringLiteral("abcdef0123456789.bin");
    item.files[0].par2FileName = par2Name;
    return item;
}

} // namespace

void tst_UsenetQueue::aPar2NameSurvivesARestart()
{
    const UsenetQueueItem item = savedItemWithPar2Name(QStringLiteral("Real.Name.mkv"));
    QVERIFY(UsenetQueueStore::save(item));

    UsenetQueueItem loaded;
    QString error;
    QVERIFY2(UsenetQueueStore::load(UsenetQueueStore::statePath(item.id), loaded, error),
             qPrintable(error));

    QCOMPARE(loaded.files.at(0).par2FileName, QStringLiteral("Real.Name.mkv"));

    // And it still outranks the article's own name, which is what a restart has
    // to preserve: re-deriving it would mean re-reading the .par2 and re-hashing
    // 16 KiB of every file before anything could be decided again.
    QCOMPARE(loaded.bestFileName(0), QStringLiteral("Real.Name.mkv"));
}

void tst_UsenetQueue::aSidecarWithNoPar2NameKeyReadsAsUnknown()
{
    // kStateVersion does not move for this key, so every sidecar written before
    // it existed has to keep meaning what it meant: nothing is known, fall back
    // to the article's name.
    const UsenetQueueItem item = savedItemWithPar2Name({});
    QVERIFY(UsenetQueueStore::save(item));

    const QString path = UsenetQueueStore::statePath(item.id);
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray yaml = f.readAll();
    f.close();
    QVERIFY2(!yaml.contains("par2FileName"),
             "an empty name should not be written at all");

    UsenetQueueItem loaded;
    QString error;
    QVERIFY2(UsenetQueueStore::load(path, loaded, error), qPrintable(error));
    QVERIFY(loaded.files.at(0).par2FileName.isEmpty());
    QCOMPARE(loaded.bestFileName(0), QStringLiteral("abcdef0123456789.bin"));
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
    ScopedStatistics stats;
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

    QCOMPARE(stats->usenetSession().articlesMissing, uint64(1));
    QCOMPARE(stats->usenetSession().articlesDownloaded, uint64(3));
    QCOMPARE(stats->usenetSession().itemsFailed, uint64(1));   // a hole and no PAR2
    QVERIFY(stats->usenetSession().connectionErrors >= 1);      // the dead block account

    queue.stop();
}

void tst_UsenetQueue::aRequiredServerStillFailsTheItem()
{
    // The counterpart, so the exemption above cannot quietly become "never fail".
    // A main provider that is unreachable is a real failure and has to say so --
    // a silently stalled queue is the worse outcome for a user.
    ScopedStatistics stats;
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

    // The inline required-failure path counts like every other terminal one —
    // and announces itself once, however many of the item's other articles fail
    // behind it.
    QTest::qWait(500);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(stats->usenetSession().itemsFailed, uint64(1));
    QCOMPARE(stats->usenetSession().itemsCompleted, uint64(0));
    QVERIFY(stats->usenetSession().connectionErrors >= 1);

    queue.stop();
}

namespace {

/// @p article with its yEnc trailer CRC overwritten: decodes, then fails its
/// own checksum — what a provider with a damaged copy in its spool serves.
QByteArray damage(QByteArray article)
{
    const qsizetype crcAt = article.lastIndexOf("pcrc32=") + 7;
    article.replace(crcAt, 8, "deadbeef");
    return article;
}

} // namespace

// A damaged copy is this *server's* problem, so the article goes to the next
// one. It used to be a connection fault: the socket was dropped, the account
// backed off for the retry interval — 60 s here, which is why a stall would
// time this test out — and after six of those the whole download failed.
void tst_UsenetQueue::aDamagedArticleIsFetchedFromAnotherServer()
{
    ScopedStatistics stats;
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer main;
    FakeNntpServer fill;
    main.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    fill.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p) {
        const QByteArray good = makeArticle(whole, p, 4, QStringLiteral("damaged.bin"));
        main.addArticle(messageIdFor(p), p == 2 ? damage(good) : good);
        fill.addArticle(messageIdFor(p), good);
    }
    const quint16 mainPort = main.start();
    const quint16 fillPort = fill.start();
    QVERIFY(mainPort != 0 && fillPort != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    NewsServer primary = serverConfig(mainPort, 2);
    primary.name = QStringLiteral("main");
    primary.level = 0;

    NewsServer block = serverConfig(fillPort, 2);
    block.name = QStringLiteral("block");
    block.level = 1;

    UsenetQueue queue;
    queue.applyServers({primary, block}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("damaged.bin"), 4),
                          QStringLiteral("damaged"), error).isEmpty());
    QVERIFY2(finished.wait(30000), "the damaged article stalled the download");
    QVERIFY2(finished.at(0).at(1).toBool(), qPrintable(finished.at(0).at(2).toString()));

    QFile f(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("damaged.bin")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), whole);   // the fill server's copy, in the right place
    f.close();

    const UsenetCounters& c = stats->usenetSession();
    QCOMPARE(c.articlesCorrupt, uint64(1));
    QCOMPARE(c.articlesMissing, uint64(0));
    QCOMPARE(c.connectionErrors, uint64(0));   // nobody's connection misbehaved
    QCOMPARE(c.articlesDownloaded, uint64(4));

    queue.stop();
}

// Nowhere to go: the one server that has the article has a bad copy of it. That
// is a hole for PAR2, exactly like an article missing everywhere — not a failed
// download, and not seven attempts at the same damaged bytes either.
void tst_UsenetQueue::anArticleDamagedEverywhereIsMissingNotFatal()
{
    ScopedStatistics stats;
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p) {
        const QByteArray good = makeArticle(whole, p, 4, QStringLiteral("holed.bin"));
        server.addArticle(messageIdFor(p), p == 2 ? damage(good) : good);
    }
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 1)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("holed.bin"), 4),
                                    QStringLiteral("holed"), error);
    QVERIFY(!id.isEmpty());
    QVERIFY2(finished.wait(30000), "one damaged article held the whole download");

    // The release is still rejected — a hole with no PAR2 is never published —
    // but for the hole, not for a decode error six retries deep.
    const QString message = finished.at(0).at(2).toString();
    QVERIFY2(!message.contains(QStringLiteral("CRC"), Qt::CaseInsensitive),
             qPrintable(message));

    const auto* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QCOMPARE(item->files.at(0).missingSegments, 1);

    const UsenetCounters& c = stats->usenetSession();
    QCOMPARE(c.articlesCorrupt, uint64(1));
    QCOMPARE(c.articlesMissing, uint64(1));
    QCOMPARE(c.connectionErrors, uint64(0));
    // One connection, reused throughout: the damaged article cost neither the
    // socket nor a reconnect.
    QCOMPARE(server.connectionCount(), 1);

    queue.stop();
}

// Every settings save stops the workers, and whatever was mid-article comes back
// as "Shutting down". That is our doing, so it must cost nothing: seven saves
// used to spend an article's whole retry budget and fail the download.
void tst_UsenetQueue::aSettingsSaveDoesNotSpendRetries()
{
    ScopedStatistics stats;
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                       QStringLiteral("saved.bin")));
    server.setHoldArticle(messageIdFor(2));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    const NewsServer config = serverConfig(port, 2);

    UsenetQueue queue;
    queue.applyServers({config}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("saved.bin"), 4),
                          QStringLiteral("saved"), error).isEmpty());

    // One more save than kMaxTransportRetries allows, each one catching the held
    // article in flight.
    for (int i = 0; i < 8; ++i) {
        QTRY_VERIFY_WITH_TIMEOUT(queue.activeFetches() > 0, 10000);
        queue.applyServers({config}, 60);
        QTest::qWait(50);   // the aborted results arrive queued
    }

    QTRY_VERIFY_WITH_TIMEOUT(queue.activeFetches() > 0, 10000);
    server.releaseHeld();

    QVERIFY2(finished.wait(30000), "the download never resolved");
    QVERIFY2(finished.at(0).at(1).toBool(), qPrintable(finished.at(0).at(2).toString()));

    const UsenetCounters& c = stats->usenetSession();
    QCOMPARE(c.itemsFailed, uint64(0));
    QCOMPARE(c.itemsCompleted, uint64(1));
    QCOMPARE(c.connectionErrors, uint64(0));   // we stopped them, the server did not

    queue.stop();
}

// Pausing does not recall the articles already in flight, and the last of them
// landing used to carry the item off into post-processing — undoing the pause
// the user just asked for.
void tst_UsenetQueue::aPausedItemIsNotPostProcessedByItsLastArticle()
{
    ScopedStatistics stats;
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                       QStringLiteral("paused.bin")));
    server.setHoldArticle(messageIdFor(4));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 1)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("paused.bin"), 4),
                                    QStringLiteral("paused"), error);
    QVERIFY(!id.isEmpty());

    // Everything but the held article is in; pause with that one still out.
    QTRY_VERIFY_WITH_TIMEOUT(queue.findItem(id)->files.at(0).done.count(true) == 3, 20000);
    QVERIFY(queue.pauseItem(id));
    server.releaseHeld();
    QTest::qWait(300);

    QCOMPARE(queue.findItem(id)->status, UsenetItemStatus::Paused);
    QCOMPARE(finished.count(), 0);
    // The bytes were kept, though — a resume must not refetch them.
    QCOMPARE(queue.findItem(id)->files.at(0).done.count(true), 4);

    QVERIFY(queue.resumeItem(id));
    QVERIFY2(finished.wait(30000), "the resumed item never finished");
    QVERIFY2(finished.at(0).at(1).toBool(), qPrintable(finished.at(0).at(2).toString()));

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


/// The regression for the attribution bug: publishStaged() used to zip the
/// payload list into item.files by *position*.
///
/// The payload list is sorted by name and drops every .par2, so it agrees with
/// the NZB's file list only by luck. Here the two orders are deliberately
/// reversed — the NZB lists zzz.bin then aaa.bin, the payload comes back
/// aaa.bin then zzz.bin — so the old code attributed *both* files to the wrong
/// one, and "Open File" on a finished release opened its neighbour.
void tst_UsenetQueue::aPublishedFileIsAttributedToTheNzbFileItCameFrom()
{
    constexpr int kFileParts = 2;
    const QByteArray whole = payload(kPartSize * kFileParts);

    // NZB order: zzz.bin first. Name order, which is what the payload list uses:
    // aaa.bin first. That inversion is the whole test.
    const QStringList names{QStringLiteral("zzz.bin"), QStringLiteral("aaa.bin")};

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 100, 1, 100);
    for (int f = 0; f < names.size(); ++f) {
        for (int p = 1; p <= kFileParts; ++p) {
            server.addArticle(QStringLiteral("n%1p%2@example.com").arg(f).arg(p),
                              makeArticle(whole, p, kFileParts, names.at(f)));
        }
    }
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
    const QString id = queue.addNzb(makeNamedFilesNzb(names, kFileParts),
                                    QStringLiteral("attribution"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(30000), "the download never reported a terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(), qPrintable(finished.at(0).at(2).toString()));

    const UsenetQueueItem* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QCOMPARE(item->files.size(), names.size());

    // Each NZB file names *itself*, not its neighbour.
    for (int f = 0; f < names.size(); ++f) {
        QVERIFY2(!item->files.at(f).finalPath.isEmpty(),
                 qPrintable(QStringLiteral("file %1 published nothing").arg(f)));
        QCOMPARE(QFileInfo(item->files.at(f).finalPath).fileName(), names.at(f));
    }

    // And the release records what it published, which `files` cannot answer for
    // an unpacked release at all.
    QStringList publishedNames;
    for (const QString& path : item->publishedPaths)
        publishedNames << QFileInfo(path).fileName();
    publishedNames.sort();
    QCOMPARE(publishedNames, QStringList({QStringLiteral("aaa.bin"), QStringLiteral("zzz.bin")}));

    queue.stop();
}

namespace {

/// A release whose second article no server has: the shape every retry case
/// starts from. Leaves the queue running with the item Failed.
struct HoledRelease {
    QByteArray whole;
    QString    id;
};

HoledRelease postHoledRelease(FakeNntpServer& server, UsenetQueue& queue,
                              const QString& fileName)
{
    HoledRelease out;
    out.whole = payload(kPartSize * 4);
    server.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p) {
        if (p == 2)
            continue;                    // nobody has this one, yet
        server.addArticle(messageIdFor(p), makeArticle(out.whole, p, 4, fileName));
    }
    return out;
}

/// BODY requests for @p messageId, counting only those issued after @p from —
/// the whole point of a retry case is what happened *after* it started, and the
/// first attempt asked for the same article once already.
int bodyCountFor(const FakeNntpServer& server, const QString& messageId, int from = 0)
{
    const QStringList& cmds = server.receivedCommands();
    int n = 0;
    for (int i = qMax(0, from); i < cmds.size(); ++i) {
        if (cmds.at(i).startsWith(QStringLiteral("BODY"), Qt::CaseInsensitive)
            && cmds.at(i).contains(messageId))
            ++n;
    }
    return n;
}

} // namespace

// The whole feature in one case: an article nobody had is asked for again, and
// only it. Without the missing map the done bit is indistinguishable from an
// arrival, rebuildPlan() skips it, and the retry fetches nothing at all.
void tst_UsenetQueue::aRetryAsksOnlyForTheArticlesThatWereMissing()
{
    ScopedStatistics stats;

    FakeNntpServer server;
    UsenetQueue queue;
    const HoledRelease rel = postHoledRelease(server, queue, QStringLiteral("holed.bin"));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("holed.bin"), 4),
                                    QStringLiteral("holed"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(30000), "the download never resolved");
    QVERIFY2(!finished.at(0).at(1).toBool(), "a hole with no PAR2 should have failed");

    const UsenetQueueItem* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QCOMPARE(item->files.at(0).missingSegments, 1);
    QCOMPARE(int(item->files.at(0).missing.count(true)), 1);
    QVERIFY2(item->files.at(0).missing.testBit(1), "the wrong article was booked missing");
    QVERIFY2(item->files.at(0).finalized, "a resolved file should have sealed");

    // What the release is called on disk now. A retry must not rename it: a
    // second seal through uniqueDestination() would collide the file with itself
    // and mint "holed (1).bin".
    const QString sealedPath = item->files.at(0).tempPath;
    QCOMPARE(QFileInfo(sealedPath).fileName(), QStringLiteral("holed.bin"));

    // The provider finally has it.
    server.addArticle(messageIdFor(2), makeArticle(rel.whole, 2, 4,
                                                   QStringLiteral("holed.bin")));
    const int mark = server.receivedCommands().size();

    QVERIFY2(queue.retryMissingArticles(id), "the retry refused an item it should take");
    QVERIFY2(finished.wait(30000), "the retry never resolved");
    QCOMPARE(finished.count(), 2);
    QVERIFY2(finished.at(1).at(1).toBool(), qPrintable(finished.at(1).at(2).toString()));

    // One article asked for, and it is the one that was missing. The other three
    // are on disk and must not be fetched again — clearing every done bit rather
    // than only the missing ones would re-download the whole release.
    QCOMPARE(bodyCountFor(server, messageIdFor(2), mark), 1);
    for (int p : {1, 3, 4})
        QCOMPARE(bodyCountFor(server, messageIdFor(p), mark), 0);

    // The bytes landed in the hole of the file that was already sealed.
    const QString out = QDir(thePrefs.incomingDir()).filePath(QStringLiteral("holed.bin"));
    QFile f(out);
    QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(out));
    QCOMPARE(f.readAll(), rel.whole);
    f.close();

    // And nothing was renamed on the way.
    QVERIFY2(!QFile::exists(QDir(thePrefs.incomingDir())
                                .filePath(QStringLiteral("holed (1).bin"))),
             "the retry re-sealed the file instead of writing into it");

    queue.stop();
}

// missingSegments is not a damage estimate: direct unpack, the sealed-volume
// replay, the encrypted preview and post-processing all read it as "this file
// has zeros in it right now". Decrementing it when the retry *arms* a segment
// would hand each of them a zero-padded file to work on.
void tst_UsenetQueue::aRetryKeepsTheHoleCountUntilTheBytesActuallyArrive()
{
    ScopedStatistics stats;

    FakeNntpServer server;
    UsenetQueue queue;
    const HoledRelease rel = postHoledRelease(server, queue, QStringLiteral("held.bin"));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("held.bin"), 4),
                                    QStringLiteral("held"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(30000), "the download never resolved");

    // Serve it, but hold it in flight.
    server.addArticle(messageIdFor(2), makeArticle(rel.whole, 2, 4,
                                                   QStringLiteral("held.bin")));
    server.setHoldArticle(messageIdFor(2));

    QVERIFY(queue.retryMissingArticles(id));
    QTRY_VERIFY_WITH_TIMEOUT(queue.activeFetches() > 0, 10000);

    const UsenetQueueItem* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QCOMPARE(item->files.at(0).missingSegments, 1);   // still a hole: no bytes yet
    QVERIFY2(!item->files.at(0).done.testBit(1), "the re-armed segment was not re-armed");

    server.releaseHeld();
    QVERIFY2(finished.wait(30000), "the retry never resolved");
    QVERIFY2(finished.at(1).at(1).toBool(), qPrintable(finished.at(1).at(2).toString()));
    QCOMPARE(queue.findItem(id)->files.at(0).missingSegments, 0);

    queue.stop();
}

// A retry that comes back empty-handed must fail differently from the first
// attempt, or the button reads as broken — and it must not book the same hole
// twice while it is at it.
void tst_UsenetQueue::aRetryThatFindsNothingFailsAgainAndSaysWhatItLearned()
{
    ScopedStatistics stats;

    FakeNntpServer server;
    UsenetQueue queue;
    postHoledRelease(server, queue, QStringLiteral("gone.bin"));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("gone.bin"), 4),
                                    QStringLiteral("gone"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(30000), "the download never resolved");
    const QString firstError = queue.findItem(id)->error;
    QVERIFY(!firstError.isEmpty());

    QVERIFY(queue.retryMissingArticles(id));
    QVERIFY2(finished.wait(30000), "the retry never resolved");
    QVERIFY(!finished.at(1).at(1).toBool());

    const UsenetQueueItem* item = queue.findItem(id);
    QVERIFY2(item->error != firstError, "the second failure repeated the first verbatim");
    QVERIFY2(item->error.contains(QStringLiteral("came back")),
             qPrintable(QStringLiteral("unhelpful second failure: %1").arg(item->error)));

    // One hole, counted once, however many times it is asked for.
    QCOMPARE(item->files.at(0).missingSegments, 1);
    QCOMPARE(int(item->files.at(0).missing.count(true)), 1);
    QCOMPARE(stats->usenetSession().articlesMissing, uint64(1));

    queue.stop();
}

// Resume has three callers — this one, the category-wide resume where "All" is
// every item in the queue, and setItemPassword(). Re-asking thousands of dead
// articles on each of them is not what any of the three asked for, so the
// re-ask is gated on the account list having actually changed.
void tst_UsenetQueue::resumingAFailedItemWithTheSameAccountsAsksForNothing()
{
    ScopedStatistics stats;

    FakeNntpServer server;
    UsenetQueue queue;
    const HoledRelease rel = postHoledRelease(server, queue, QStringLiteral("same.bin"));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("same.bin"), 4),
                                    QStringLiteral("same"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(30000), "the download never resolved");

    // The article is available now, but nothing about the accounts changed, so
    // Resume is the user pressing the same button twice.
    server.addArticle(messageIdFor(2), makeArticle(rel.whole, 2, 4,
                                                   QStringLiteral("same.bin")));

    const int mark = server.receivedCommands().size();
    QVERIFY(queue.resumeItem(id));
    QTest::qWait(1000);
    QCOMPARE(bodyCountFor(server, messageIdFor(2), mark), 0);
    QCOMPARE(queue.findItem(id)->files.at(0).missingSegments, 1);

    queue.stop();
}

void tst_UsenetQueue::resumingAFailedItemAfterAnAccountIsAddedReAsksTheDeadArticles()
{
    ScopedStatistics stats;

    FakeNntpServer first;
    UsenetQueue queue;
    const HoledRelease rel = postHoledRelease(first, queue, QStringLiteral("fill.bin"));
    const quint16 firstPort = first.start();
    QVERIFY(firstPort != 0);

    // The fill account: it has the one article nobody else did.
    FakeNntpServer fill;
    fill.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        fill.addArticle(messageIdFor(p), makeArticle(rel.whole, p, 4,
                                                     QStringLiteral("fill.bin")));
    const quint16 fillPort = fill.start();
    QVERIFY(fillPort != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    queue.applyServers({serverConfig(firstPort, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("fill.bin"), 4),
                                    QStringLiteral("fill"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(30000), "the download never resolved");
    QVERIFY(!finished.at(0).at(1).toBool());

    // Exactly the user's story: buy a block account, add it, press Resume.
    NewsServer block = serverConfig(fillPort, 4);
    block.name = QStringLiteral("block");
    block.level = 1;
    queue.applyServers({serverConfig(firstPort, 4), block}, 60);

    QVERIFY(queue.resumeItem(id));
    QVERIFY2(finished.wait(30000), "the resumed item never resolved");
    QVERIFY2(finished.at(1).at(1).toBool(), qPrintable(finished.at(1).at(2).toString()));
    QCOMPARE(queue.findItem(id)->files.at(0).missingSegments, 0);

    const QString out = QDir(thePrefs.incomingDir()).filePath(QStringLiteral("fill.bin"));
    QFile f(out);
    QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(out));
    QCOMPARE(f.readAll(), rel.whole);

    queue.stop();
}

void tst_UsenetQueue::theMissingArticleMapSurvivesARestartAndMatchesTheCount()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});

    UsenetQueueItem item;
    item.id = QStringLiteral("retry-round-trip");
    item.name = QStringLiteral("holed");
    NzbFileInfo info;
    info.subject = QStringLiteral("\"holed.bin\" yEnc (1/4)");
    info.fileName = QStringLiteral("holed.bin");
    info.groups << QStringLiteral("alt.binaries.test");
    for (int p = 1; p <= 4; ++p)
        info.segments.append(NzbSegment{messageIdFor(p), 3500, p});
    item.nzb.files.append(info);
    item.initFileStates(thePrefs.usenetTempDir());

    UsenetFileState& st = item.files[0];
    st.done.fill(true);
    st.missing.resize(st.done.size());
    st.missing.setBit(1);
    st.missingSegments = 1;
    st.finalized = true;

    QVERIFY(UsenetQueueStore::save(item));

    UsenetQueueItem loaded;
    QString loadError;
    QVERIFY2(UsenetQueueStore::load(UsenetQueueStore::statePath(item.id), loaded, loadError),
             qPrintable(loadError));
    QCOMPARE(loaded.files.at(0).missingSegments, 1);
    QCOMPARE(int(loaded.files.at(0).missing.count(true)), 1);
    QVERIFY2(loaded.files.at(0).missing.testBit(1), "the map came back pointing elsewhere");

    // The invariant a retry checks before it is willing to act.
    QCOMPARE(int(loaded.files.at(0).missing.count(true)), loaded.files.at(0).missingSegments);

    // And kStateVersion did not move for it.
    QFile f(UsenetQueueStore::statePath(item.id));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY2(QString::fromUtf8(f.readAll()).contains(QStringLiteral("version: 2")),
             "kStateVersion moved");
}

// A sidecar written before the map existed carries a count and no map. Guessing
// which articles the count stood for would re-ask a whole file; refusing says so
// and costs nothing.
void tst_UsenetQueue::aSidecarWithNoMissingMapRefusesTheRetryRatherThanGuessing()
{
    ScopedStatistics stats;

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    // An item exactly as an older version would have left it: resolved, short,
    // and with no record of which article the hole was.
    UsenetQueueItem stored;
    stored.id = QStringLiteral("old-sidecar");
    stored.name = QStringLiteral("old");
    stored.status = UsenetItemStatus::Failed;
    stored.error = QStringLiteral("Articles are missing");
    NzbFileInfo info;
    info.subject = QStringLiteral("\"old.bin\" yEnc (1/4)");
    info.fileName = QStringLiteral("old.bin");
    info.groups << QStringLiteral("alt.binaries.test");
    for (int p = 1; p <= 4; ++p)
        info.segments.append(NzbSegment{messageIdFor(p), 3500, p});
    stored.nzb.files.append(info);
    stored.initFileStates(thePrefs.usenetTempDir());
    stored.files[0].done.fill(true);
    stored.files[0].missingSegments = 1;
    stored.files[0].finalized = true;
    QVERIFY(UsenetQueueStore::save(stored));

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QVERIFY2(!queue.retryMissingArticles(stored.id),
             "a retry guessed which articles a bare count stood for");
    QCOMPARE(queue.findItem(stored.id)->status, UsenetItemStatus::Failed);
    QVERIFY2(server.receivedCommands().isEmpty(), "the refused retry still asked for articles");

    queue.stop();
}

namespace {

/// A floor no volume can be over, which is how a full disk is expressed without
/// filling one. Restores what it found, like every other guard in this suite.
class DiskFloorGuard {
public:
    explicit DiskFloorGuard(quint64 floorBytes, bool enabled = true)
        : m_check(thePrefs.checkDiskspace()), m_floor(thePrefs.minFreeDiskSpace())
    {
        thePrefs.setCheckDiskspace(enabled);
        thePrefs.setMinFreeDiskSpace(floorBytes);
    }
    ~DiskFloorGuard()
    {
        thePrefs.setCheckDiskspace(m_check);
        thePrefs.setMinFreeDiskSpace(m_floor);
    }
    static void lift() { thePrefs.setMinFreeDiskSpace(0); }

private:
    bool m_check;
    quint64 m_floor;
};

constexpr quint64 kImpossibleFloor = 1024ULL * 1024 * 1024 * 1024 * 64;   // 64 TB

} // namespace

// The floor is a filter, and the module's rule for filters is that they wait
// rather than conclude anything. Before this, a disk that could not take the
// bytes surfaced as a write error, which read as a *protocol* error: the account
// was backed off for a minute and the article could be booked missing.
void tst_UsenetQueue::belowTheFloorTheQueueWaitsAndSaysWhy()
{
    ScopedStatistics stats;
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                       QStringLiteral("wait.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    DiskFloorGuard floor(kImpossibleFloor);

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("wait.bin"), 4),
                                    QStringLiteral("wait"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QTest::qWait(1500);

    const UsenetQueueItem* item = queue.findItem(id);
    QVERIFY(item != nullptr);
    QVERIFY2(item->status != UsenetItemStatus::Failed, "a floor became a verdict");
    QVERIFY2(!item->stalledReason.isEmpty(), "the queue stopped without saying why");
    QCOMPARE(item->files.at(0).missingSegments, 0);
    QCOMPARE(stats->usenetSession().articlesMissing, uint64(0));
    QCOMPARE(stats->usenetSession().connectionErrors, uint64(0));
    // A probe may still have run: STAT writes nothing, so the floor has no
    // opinion about it. What must not happen is a BODY, which arrives as bytes
    // needing somewhere to go.
    QCOMPARE(bodyCountFor(server, QStringLiteral("@example.com")), 0);

    queue.stop();
}

void tst_UsenetQueue::spaceReturningResumesWithoutTheUser()
{
    ScopedStatistics stats;
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                       QStringLiteral("back.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    DiskFloorGuard floor(kImpossibleFloor);

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("back.bin"), 4),
                                    QStringLiteral("back"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QTest::qWait(1000);
    QVERIFY(queue.findItem(id)->status != UsenetItemStatus::Downloading);

    // Somebody emptied the trash. Nobody pressed anything.
    DiskFloorGuard::lift();

    QVERIFY2(finished.wait(30000), "the queue never came back after space returned");
    QVERIFY2(finished.at(0).at(1).toBool(), qPrintable(finished.at(0).at(2).toString()));
    QVERIFY2(queue.findItem(id)->stalledReason.isEmpty(), "the reason outlived the stall");

    queue.stop();
}

void tst_UsenetQueue::theDiskFloorOffMeansTodaysBehaviourExactly()
{
    ScopedStatistics stats;
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                       QStringLiteral("off.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    // A floor nothing could satisfy — and the switch off, which is what every
    // user who never opened the Extended page has.
    DiskFloorGuard floor(kImpossibleFloor, /*enabled*/ false);

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY2(!queue.addNzb(makeNzb(QStringLiteral("off.bin"), 4),
                           QStringLiteral("off"), error).isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(30000), "the guard ran with the preference off");
    QVERIFY2(finished.at(0).at(1).toBool(), qPrintable(finished.at(0).at(2).toString()));

    queue.stop();
}

// The fault the floor cannot fix: a folder that cannot be written at all. It has
// to fail *locally* — naming the folder, leaving the account alone and the
// article un-missing — rather than through the ladder, which is what a
// ProtocolError used to do with it.
void tst_UsenetQueue::aWriteFailureFailsLocallyAndNeverBlamesTheServer()
{
    ScopedStatistics stats;
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                       QStringLiteral("ro.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    // A scratch root nothing can create a file in. Free space is fine, so the
    // floor has no opinion and the write path is on its own.
    const QString locked = tmp.filePath(QStringLiteral("locked"));
    QVERIFY(QDir().mkpath(locked));
    QVERIFY(QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    thePrefs.setTempDirs({locked});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("ro.bin"), 4),
                                    QStringLiteral("ro"), error);

    // The add itself may already refuse — it preallocates. Either way the item
    // must not end up blaming the provider.
    if (!id.isEmpty()) {
        QVERIFY2(finished.wait(30000), "an unwritable folder never resolved");
        QVERIFY(!finished.at(0).at(1).toBool());
        const UsenetQueueItem* item = queue.findItem(id);
        QVERIFY(item != nullptr);
        QVERIFY2(item->error.contains(QStringLiteral("write"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("a local fault reported as: %1").arg(item->error)));
        QCOMPARE(item->files.at(0).missingSegments, 0);
    }
    QCOMPARE(stats->usenetSession().articlesMissing, uint64(0));

    QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner);
    queue.stop();
}

// Five levels are only worth having if the queue acts on all five. The
// comparator sorts on the raw int, so the case that would have been silently
// wrong is the new pair — two items that used to both be "High".
void tst_UsenetQueue::aVeryHighItemIsFetchedBeforeAHighOne()
{
    ScopedStatistics stats;
    const QByteArray whole = payload(kPartSize * 2);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 2, 1, 2);
    for (int p = 1; p <= 2; ++p) {
        server.addArticle(messageIdFor(p, QStringLiteral("hi")),
                          makeArticle(whole, p, 2, QStringLiteral("high.bin")));
        server.addArticle(messageIdFor(p, QStringLiteral("vh")),
                          makeArticle(whole, p, 2, QStringLiteral("veryhigh.bin")));
    }
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    // One connection, so the order the queue *wants* is the order it gets.
    queue.applyServers({serverConfig(port, 1)}, 60);
    queue.start();

    QString error;
    const QString high = queue.addNzb(
        makeNzb(QStringLiteral("high.bin"), 2, 1700000000, QStringLiteral("hi")),
        QStringLiteral("high"), error, {.priority = 1});
    QVERIFY2(!high.isEmpty(), qPrintable(error));

    // Added second, and it must still go first.
    const QString veryHigh = queue.addNzb(
        makeNzb(QStringLiteral("veryhigh.bin"), 2, 1700000000, QStringLiteral("vh")),
        QStringLiteral("very high"), error, {.priority = 2});
    QVERIFY2(!veryHigh.isEmpty(), qPrintable(error));

    QCOMPARE(queue.findItem(high)->priority, 1);
    QCOMPARE(queue.findItem(veryHigh)->priority, 2);

    // Wait for a BODY, not for any command: the handshake and GROUP come first
    // and say nothing about which item won.
    const auto firstBodyOf = [&server] {
        for (const QString& cmd : server.receivedCommands()) {
            if (cmd.startsWith(QStringLiteral("BODY"), Qt::CaseInsensitive))
                return cmd;
        }
        return QString();
    };
    QTRY_VERIFY_WITH_TIMEOUT(!firstBodyOf().isEmpty(), 10000);
    const QString firstBody = firstBodyOf();
    QVERIFY2(firstBody.contains(QStringLiteral("vh1@example.com")),
             qPrintable(QStringLiteral("the queue started with: %1").arg(firstBody)));

    queue.stop();
}

// A sixth bucket would be one nothing can name and no menu entry could ever
// select again, so the value is bounded where it enters rather than obeyed.
void tst_UsenetQueue::aPriorityOutsideTheFiveLevelsIsClamped()
{
    ScopedStatistics stats;

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.start();

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("clamp.bin"), 2),
                                    QStringLiteral("clamp"), error, {.priority = 7});
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QCOMPARE(queue.findItem(id)->priority, 2);

    QVERIFY(queue.setItemPriority(id, -9));
    QCOMPARE(queue.findItem(id)->priority, -2);

    QVERIFY(queue.setItemPriority(id, 0));
    QCOMPARE(queue.findItem(id)->priority, 0);

    queue.stop();
}

// Every account sits behind the one proxy, so a dead proxy fails all of them at
// once. As a ConnectFailed it backed each provider off, counted connection errors
// against them and spent every article's retries. It waits instead — and the user
// fixing the proxy lets the queue straight through.
void tst_UsenetQueue::aDeadProxyNeverMakesAnArticleMissingOrBlamesAServer()
{
    ScopedStatistics stats;
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                       QStringLiteral("proxy.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    quint16 deadPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        deadPort = probe.serverPort();
    }

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), deadPort));
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("proxy.bin"), 4),
                                    QStringLiteral("proxy"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QTRY_VERIFY2_WITH_TIMEOUT(queue.findItem(id)->stalledReason.contains(QStringLiteral("proxy")),
                              "the queue stopped without saying the proxy was why", 10000);
    QTest::qWait(1000);

    const UsenetQueueItem* item = queue.findItem(id);
    QVERIFY2(item->status != UsenetItemStatus::Failed, "a dead proxy became a verdict");
    QCOMPARE(item->files.at(0).missingSegments, 0);
    QCOMPARE(stats->usenetSession().articlesMissing, uint64(0));
    QCOMPARE(stats->usenetSession().connectionErrors, uint64(0));
    QVERIFY(server.receivedCommands().isEmpty());

    // The user fixes it: a settings save that turns the proxy off.
    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    queue.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    queue.applyServers({serverConfig(port, 4)}, 60);

    QVERIFY2(finished.wait(30000), "the queue never came back once the proxy was fixed");
    QVERIFY2(finished.at(0).at(1).toBool(), qPrintable(finished.at(0).at(2).toString()));
    QVERIFY2(queue.findItem(id)->stalledReason.isEmpty(), "the reason outlived the stall");

    queue.stop();
}

#include "tst_UsenetQueue.moc"
