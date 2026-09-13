/// @file tst_StatisticsReset.cpp
/// @brief Tests for stats/Statistics — session reset vs. cumulative stats.

#include "TestHelpers.h"
#include "client/ClientStateDefs.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace eMule::testing;

class tst_StatisticsReset : public QObject {
    Q_OBJECT

private slots:
    void resetDownOverhead_preservesCumulativeCounters();
    void resetUpOverhead_preservesCumulativeCounters();
    void sessionCounters_independentOfOverhead();
    void cumulativeRates_survivesMultipleUpdates();
    void overheadReset_doesNotAffectRates();
    void multipleInits_reloadsFromPrefs();
    void statsAverageMinutes_defaultValue();
    void statsAverageMinutes_roundTrip();
    void connRates_roundTrip();
    void flush_isIdempotent();
    void flush_addsToWhatWasAlreadyInPrefs();
    void flush_connPeakIsAHighWaterMark();
    void resetCumulativeStats_zeroesAndStamps();
    void resetThenFlush_doesNotResurrectOldTotals();
    void backupThenRestore_returnsThePreResetTotals();
    void restore_swapsSoASecondOneUndoesIt();
    void restore_keepsARecordRaisedAfterTheReset();
    void restore_withoutABackupFails();
    void statisticsYaml_everyCumulativeStatRoundTrips();
    void statisticsYaml_fileWithoutBlocksLoadsZeros();
};

namespace {

/// A Preferences bound to a file inside @p dir, which is what gives it somewhere
/// to put the statistics backup (the path is derived from the preferences file).
void bindToDir(Preferences& prefs, const QTemporaryDir& dir)
{
    const QString filePath = dir.path() + QStringLiteral("/prefs.yaml");
    prefs.saveTo(filePath);
    prefs.load(filePath);
}

} // namespace

void tst_StatisticsReset::resetDownOverhead_preservesCumulativeCounters()
{
    Statistics stats;

    // Accumulate some overhead
    stats.addDownDataOverheadSourceExchange(100);
    stats.addDownDataOverheadFileRequest(200);
    stats.addDownDataOverheadServer(300);
    stats.addDownDataOverheadKad(400);
    stats.addDownDataOverheadOther(500);

    // Reset only clears the averaging lists and rate, not cumulative counters
    stats.resetDownDatarateOverhead();

    // Rate should be cleared
    QCOMPARE(stats.downDatarateOverhead(), uint64{0});

    // Cumulative byte/packet counters should be preserved
    QCOMPARE(stats.downDataOverheadSourceExchange(), uint64{100});
    QCOMPARE(stats.downDataOverheadFileRequest(), uint64{200});
    QCOMPARE(stats.downDataOverheadServer(), uint64{300});
    QCOMPARE(stats.downDataOverheadKad(), uint64{400});
    QCOMPARE(stats.downDataOverheadOther(), uint64{500});
}

void tst_StatisticsReset::resetUpOverhead_preservesCumulativeCounters()
{
    Statistics stats;

    stats.addUpDataOverheadSourceExchange(150);
    stats.addUpDataOverheadFileRequest(250);
    stats.addUpDataOverheadServer(350);
    stats.addUpDataOverheadKad(450);
    stats.addUpDataOverheadOther(550);

    stats.resetUpDatarateOverhead();

    QCOMPARE(stats.upDatarateOverhead(), uint64{0});

    // Cumulative counters preserved
    QCOMPARE(stats.upDataOverheadSourceExchange(), uint64{150});
    QCOMPARE(stats.upDataOverheadFileRequest(), uint64{250});
    QCOMPARE(stats.upDataOverheadServer(), uint64{350});
    QCOMPARE(stats.upDataOverheadKad(), uint64{450});
    QCOMPARE(stats.upDataOverheadOther(), uint64{550});
}

void tst_StatisticsReset::sessionCounters_independentOfOverhead()
{
    Statistics stats;

    stats.addSessionReceivedBytes(5000);
    stats.addSessionSentBytes(3000);
    stats.addDownDataOverheadServer(100);

    stats.resetDownDatarateOverhead();
    stats.resetUpDatarateOverhead();

    // Session counters unaffected by overhead reset
    QCOMPARE(stats.sessionReceivedBytes(), uint64{5000});
    QCOMPARE(stats.sessionSentBytes(), uint64{3000});
}

void tst_StatisticsReset::cumulativeRates_survivesMultipleUpdates()
{
    Preferences prefs;
    Statistics stats;
    stats.init(prefs);

    // First update sets max rates
    stats.updateConnectionStats(10.0f, 20.0f);
    QCOMPARE(stats.maxUp(), 10.0f);
    QCOMPARE(stats.maxDown(), 20.0f);

    // Lower rates don't change max
    stats.updateConnectionStats(5.0f, 15.0f);
    QCOMPARE(stats.maxUp(), 10.0f);
    QCOMPARE(stats.maxDown(), 20.0f);

    // Higher rates update max and cumulative max
    stats.updateConnectionStats(30.0f, 40.0f);
    QCOMPARE(stats.maxUp(), 30.0f);
    QCOMPARE(stats.maxDown(), 40.0f);
    QCOMPARE(stats.maxCumUp(), 30.0f);
    QCOMPARE(stats.maxCumDown(), 40.0f);

    // Verify preferences were updated
    QCOMPARE(prefs.connMaxUpRate(), 30.0f);
    QCOMPARE(prefs.connMaxDownRate(), 40.0f);
}

void tst_StatisticsReset::overheadReset_doesNotAffectRates()
{
    Statistics stats;

    stats.updateConnectionStats(10.0f, 20.0f);
    stats.addDownDataOverheadServer(1000);

    stats.resetDownDatarateOverhead();
    stats.resetUpDatarateOverhead();

    // Connection rates unaffected
    QCOMPARE(stats.rateUp(), 10.0f);
    QCOMPARE(stats.rateDown(), 20.0f);
    QCOMPARE(stats.maxUp(), 10.0f);
    QCOMPARE(stats.maxDown(), 20.0f);
}

void tst_StatisticsReset::multipleInits_reloadsFromPrefs()
{
    Preferences prefs;
    prefs.setConnMaxDownRate(50.0f);
    prefs.setConnMaxUpRate(30.0f);

    Statistics stats;
    stats.init(prefs);
    QCOMPARE(stats.maxCumDown(), 50.0f);
    QCOMPARE(stats.maxCumUp(), 30.0f);

    // Change prefs and re-init
    prefs.setConnMaxDownRate(100.0f);
    prefs.setConnMaxUpRate(80.0f);
    stats.init(prefs);
    QCOMPARE(stats.maxCumDown(), 100.0f);
    QCOMPARE(stats.maxCumUp(), 80.0f);
}

void tst_StatisticsReset::statsAverageMinutes_defaultValue()
{
    Preferences prefs;
    QCOMPARE(prefs.statsAverageMinutes(), uint32{5});
}

void tst_StatisticsReset::statsAverageMinutes_roundTrip()
{
    TempDir tmp;
    const QString filePath = tmp.path() + QStringLiteral("/prefs.yaml");

    {
        Preferences prefs;
        prefs.setStatsAverageMinutes(15);
        prefs.saveTo(filePath);
    }
    {
        Preferences prefs;
        prefs.load(filePath);
        QCOMPARE(prefs.statsAverageMinutes(), uint32{15});
    }
}

void tst_StatisticsReset::connRates_roundTrip()
{
    TempDir tmp;
    const QString filePath = tmp.path() + QStringLiteral("/prefs.yaml");

    {
        Preferences prefs;
        prefs.setConnMaxDownRate(100.5f);
        prefs.setConnAvgDownRate(50.25f);
        prefs.setConnMaxAvgDownRate(80.75f);
        prefs.setConnAvgUpRate(40.125f);
        prefs.setConnMaxAvgUpRate(60.5f);
        prefs.setConnMaxUpRate(90.0f);
        prefs.saveTo(filePath);
    }
    {
        Preferences prefs;
        prefs.load(filePath);
        QVERIFY(qFuzzyCompare(prefs.connMaxDownRate(), 100.5f));
        QVERIFY(qFuzzyCompare(prefs.connAvgDownRate(), 50.25f));
        QVERIFY(qFuzzyCompare(prefs.connMaxAvgDownRate(), 80.75f));
        QVERIFY(qFuzzyCompare(prefs.connAvgUpRate(), 40.125f));
        QVERIFY(qFuzzyCompare(prefs.connMaxAvgUpRate(), 60.5f));
        QVERIFY(qFuzzyCompare(prefs.connMaxUpRate(), 90.0f));
    }
}

namespace {

/// A session with one of everything, so a flush has something to bank. The
/// session byte totals come from the transfer breakdown alone, as in the client
/// code: 800 up, 900 down.
void makeSomeSessionActivity(Statistics& stats)
{
    stats.addSessionSentBytesToFriend(300);
    stats.addReconnect();
    stats.addUpDataOverheadServer(64);
    stats.addDownDataOverheadKad(32);
    stats.addCompressionGain(500);
    stats.addCorruptionLoss(700);
    stats.addIchPartSaved();
    stats.addTransferData(ClientSoftware::eMule, 4662, false, true, 800);
    stats.addTransferData(ClientSoftware::aMule, 5000, true, false, 900);

    UsenetCounters& usenet = stats.usenetSession();
    usenet.decodedBytes += 4000;
    ++usenet.itemsCompleted;
    raiseCounter(usenet.maxDownRate, 300);
    ++stats.indexerSession().searches;
}

/// The leaves of a statistics backup as "key" or "block.key" -> value text.
/// Plain line parsing: the file is flat block-style YAML one level deep.
QHash<QString, QString> readStatLeaves(const QString& path)
{
    QHash<QString, QString> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;

    static const QRegularExpression kLine(QStringLiteral("^(\\s*)(\\w+):\\s*(\\S*)$"));
    QString block;
    for (const QByteArray& raw : file.readAll().split('\n')) {
        const auto m = kLine.match(QString::fromUtf8(raw));
        if (!m.hasMatch())
            continue;
        if (m.captured(3).isEmpty()) {
            block = m.captured(2);        // a nested block starts
            continue;
        }
        if (m.captured(1).isEmpty())
            block.clear();
        out.insert(block.isEmpty() ? m.captured(2) : block + u'.' + m.captured(2), m.captured(3));
    }
    return out;
}

/// Rewrite every leaf of @p path with a distinct value; returns what was planted.
QHash<QString, QString> plantDistinctStatValues(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QList<QByteArray> lines = file.readAll().split('\n');
    file.close();

    static const QRegularExpression kLeaf(QStringLiteral("^(\\s*\\w+:\\s*)(\\S+)$"));
    int next = 1;
    QByteArray rewritten;
    for (const QByteArray& raw : lines) {
        const QString line = QString::fromUtf8(raw);
        const auto m = kLeaf.match(line);
        rewritten += (m.hasMatch() ? m.captured(1) + QString::number(next++) : line).toUtf8();
        rewritten += '\n';
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return {};
    file.write(rewritten);
    file.close();
    return readStatLeaves(path);
}

Statistics::ExternalSessionCounters someExternalCounters()
{
    Statistics::ExternalSessionCounters ext;
    ext.upSuccessfulSessions = 4;
    ext.upFailedSessions = 1;
    ext.downSuccessfulSessions = 3;
    ext.downFailedSessions = 2;
    ext.downCompletedFiles = 3;
    ext.connPeak = 42;
    ext.connMaxLimitReached = 5;
    // HttpCacheManager keeps its own session half; the flush banks it like the
    // Usenet and indexer blocks.
    ext.httpCache.bytesPublished = 9000;
    ext.httpCache.chunksPublished = 2;
    ext.httpCache.bytesSaved = 18000;
    ext.httpCache.bytesFetched = 4500;
    ext.httpCache.chunksFetched = 1;
    ext.httpCache.offersReceived = 3;
    ext.httpCache.offersDeclined = 2;
    return ext;
}

} // namespace

// The property that lets the flush run on a timer: it writes absolute totals, so
// repeating it is a no-op. The old additive version doubled every counter.
void tst_StatisticsReset::flush_isIdempotent()
{
    Preferences prefs;
    Statistics stats;
    stats.init(prefs);
    makeSomeSessionActivity(stats);

    const auto ext = someExternalCounters();
    stats.flushCumulativeToPrefs(prefs, ext);

    const uint64 up = prefs.cumTotalUploaded();
    const uint64 down = prefs.cumTotalDownloaded();
    const uint64 friendUp = prefs.cumTotalUploadedToFriend();
    const uint64 compression = prefs.cumCompressionGain();
    const uint64 corruption = prefs.cumCorruptionLoss();
    const uint32 ich = prefs.cumIchPartsSaved();
    const uint64 upEmule = prefs.cumUpEmule();
    const uint64 upServerOh = prefs.cumUpOverheadServer();
    const uint32 reconnects = prefs.cumConnReconnects();
    const uint32 upSessions = prefs.cumUpSuccessfulSessions();
    const uint32 downSessions = prefs.cumDownSuccessfulSessions();
    const uint32 limitReached = prefs.cumConnMaxLimitReached();

    QCOMPARE(up, uint64{800});
    QCOMPARE(down, uint64{900});
    QCOMPARE(ich, uint32{1});
    const UsenetCounters usenet = prefs.cumUsenet();
    QCOMPARE(usenet.decodedBytes, uint64{4000});
    QCOMPARE(prefs.cumIndexer().searches, uint64{1});
    const HttpCacheCounters httpCache = prefs.cumHttpCache();
    QCOMPARE(httpCache.bytesSaved, uint64{18000});

    stats.flushCumulativeToPrefs(prefs, ext);
    stats.flushCumulativeToPrefs(prefs, ext);

    QCOMPARE(prefs.cumUsenet(), usenet);
    QCOMPARE(prefs.cumIndexer().searches, uint64{1});
    QCOMPARE(prefs.cumHttpCache(), httpCache);
    QCOMPARE(prefs.cumTotalUploaded(), up);
    QCOMPARE(prefs.cumTotalDownloaded(), down);
    QCOMPARE(prefs.cumTotalUploadedToFriend(), friendUp);
    QCOMPARE(prefs.cumCompressionGain(), compression);
    QCOMPARE(prefs.cumCorruptionLoss(), corruption);
    QCOMPARE(prefs.cumIchPartsSaved(), ich);
    QCOMPARE(prefs.cumUpEmule(), upEmule);
    QCOMPARE(prefs.cumUpOverheadServer(), upServerOh);
    QCOMPARE(prefs.cumConnReconnects(), reconnects);
    QCOMPARE(prefs.cumUpSuccessfulSessions(), upSessions);
    QCOMPARE(prefs.cumDownSuccessfulSessions(), downSessions);
    QCOMPARE(prefs.cumConnMaxLimitReached(), limitReached);
}

void tst_StatisticsReset::flush_addsToWhatWasAlreadyInPrefs()
{
    Preferences prefs;
    prefs.setCumTotalUploaded(5000);
    prefs.setCumTotalDownloaded(7000);
    prefs.setCumIchPartsSaved(9);
    prefs.setCumUpSuccessfulSessions(11);
    UsenetCounters banked;
    banked.decodedBytes = 6000;
    banked.itemsCompleted = 4;
    banked.maxDownRate = 1000;   // a peak the session does not beat
    prefs.setCumUsenet(banked);

    Statistics stats;
    stats.init(prefs);           // captures the baseline
    makeSomeSessionActivity(stats);
    stats.flushCumulativeToPrefs(prefs, someExternalCounters());

    QCOMPARE(prefs.cumTotalUploaded(), uint64{5800});
    QCOMPARE(prefs.cumTotalDownloaded(), uint64{7900});
    QCOMPARE(prefs.cumIchPartsSaved(), uint32{10});
    QCOMPARE(prefs.cumUpSuccessfulSessions(), uint32{15});
    QCOMPARE(prefs.cumUsenet().decodedBytes, uint64{10000});
    QCOMPARE(prefs.cumUsenet().itemsCompleted, uint64{5});
    QCOMPARE(prefs.cumUsenet().maxDownRate, uint64{1000});
}

void tst_StatisticsReset::flush_connPeakIsAHighWaterMark()
{
    Preferences prefs;
    prefs.setCumConnPeak(100);

    Statistics stats;
    stats.init(prefs);

    auto ext = someExternalCounters();
    ext.connPeak = 40;                      // this session peaked lower
    stats.flushCumulativeToPrefs(prefs, ext);
    QCOMPARE(prefs.cumConnPeak(), uint32{100});

    ext.connPeak = 160;                     // ...and then beat the record
    stats.flushCumulativeToPrefs(prefs, ext);
    QCOMPARE(prefs.cumConnPeak(), uint32{160});
}

void tst_StatisticsReset::resetCumulativeStats_zeroesAndStamps()
{
    Preferences prefs;
    prefs.setCumTotalUploaded(5000);
    prefs.setCumTotalDownloaded(7000);
    prefs.setCumConnPeak(80);
    prefs.setCumRunTime(3600);
    prefs.setCumIchPartsSaved(4);
    prefs.setConnMaxDownRate(123.0f);
    prefs.setRecMaxUsersOnline(999);
    UsenetCounters usenet;
    usenet.wireBytes = 1234;
    usenet.peakConnections = 40;
    prefs.setCumUsenet(usenet);
    IndexerCounters indexer;
    indexer.apiRequests = 55;
    prefs.setCumIndexer(indexer);
    HttpCacheCounters httpCache;
    httpCache.chunksFetched = 7;
    prefs.setCumHttpCache(httpCache);

    prefs.resetCumulativeStats(1700000000);

    QCOMPARE(prefs.cumUsenet(), UsenetCounters{});
    QCOMPARE(prefs.cumIndexer(), IndexerCounters{});
    QCOMPARE(prefs.cumHttpCache(), HttpCacheCounters{});

    QCOMPARE(prefs.cumTotalUploaded(), uint64{0});
    QCOMPARE(prefs.cumTotalDownloaded(), uint64{0});
    QCOMPARE(prefs.cumConnPeak(), uint32{0});
    QCOMPARE(prefs.cumRunTime(), uint64{0});
    QCOMPARE(prefs.cumIchPartsSaved(), uint32{0});
    QCOMPARE(prefs.connMaxDownRate(), 0.0f);
    QCOMPARE(prefs.statsLastReset(), uint64{1700000000});

    // Records are not cumulative counters; MFC leaves them alone.
    QCOMPARE(prefs.recMaxUsersOnline(), uint32{999});
}

void tst_StatisticsReset::resetThenFlush_doesNotResurrectOldTotals()
{
    Preferences prefs;
    prefs.setCumTotalUploaded(5000);

    Statistics stats;
    stats.init(prefs);
    makeSomeSessionActivity(stats);

    prefs.resetCumulativeStats(1700000000);
    stats.rebaseCumulative(prefs);   // what handleResetStats does via init()

    stats.flushCumulativeToPrefs(prefs, {});
    // Only what the session has counted since — the pre-reset 5000 is gone for good.
    QCOMPARE(prefs.cumTotalUploaded(), uint64{800});
    QCOMPARE(prefs.cumUsenet().decodedBytes, uint64{4000});
}

void tst_StatisticsReset::backupThenRestore_returnsThePreResetTotals()
{
    QTemporaryDir tmp;
    Preferences prefs;
    bindToDir(prefs, tmp);

    prefs.setCumTotalUploaded(5000);
    prefs.setCumConnPeak(80);
    prefs.setConnMaxDownRate(123.0f);
    prefs.setStatsLastReset(1000);
    UsenetCounters usenet;
    usenet.decodedBytes = 777;
    usenet.maxDownRate = 4096;
    prefs.setCumUsenet(usenet);
    HttpCacheCounters httpCache;
    httpCache.bytesFetched = 4242;
    httpCache.partsCorrupt = 1;
    prefs.setCumHttpCache(httpCache);
    IndexerCounters indexer;
    indexer.feedPolls = 12;
    prefs.setCumIndexer(indexer);

    QVERIFY(!prefs.hasCumulativeStatsBackup());
    QVERIFY(prefs.backupCumulativeStats());
    QVERIFY(prefs.hasCumulativeStatsBackup());

    prefs.resetCumulativeStats(2000);
    QCOMPARE(prefs.cumTotalUploaded(), uint64{0});

    QVERIFY(prefs.restoreCumulativeStats());
    QCOMPARE(prefs.cumTotalUploaded(), uint64{5000});
    QCOMPARE(prefs.cumConnPeak(), uint32{80});
    QCOMPARE(prefs.connMaxDownRate(), 123.0f);   // the rates travel with the block
    QCOMPARE(prefs.statsLastReset(), uint64{1000});
    QCOMPARE(prefs.cumUsenet(), usenet);            // nested blocks travel too
    QCOMPARE(prefs.cumIndexer(), indexer);
    QCOMPARE(prefs.cumHttpCache(), httpCache);
}

void tst_StatisticsReset::restore_swapsSoASecondOneUndoesIt()
{
    QTemporaryDir tmp;
    Preferences prefs;
    bindToDir(prefs, tmp);

    prefs.setCumTotalUploaded(5000);
    prefs.setStatsLastReset(1000);
    QVERIFY(prefs.backupCumulativeStats());
    prefs.resetCumulativeStats(2000);

    QVERIFY(prefs.restoreCumulativeStats());
    QCOMPARE(prefs.cumTotalUploaded(), uint64{5000});

    // Restoring made the post-reset values the new backup, so doing it again
    // walks back — MFC's statbkuptmp.ini rename, and what its dialog promises.
    QVERIFY(prefs.restoreCumulativeStats());
    QCOMPARE(prefs.cumTotalUploaded(), uint64{0});
    QCOMPARE(prefs.statsLastReset(), uint64{2000});

    QVERIFY(prefs.restoreCumulativeStats());
    QCOMPARE(prefs.cumTotalUploaded(), uint64{5000});
}

void tst_StatisticsReset::restore_keepsARecordRaisedAfterTheReset()
{
    QTemporaryDir tmp;
    Preferences prefs;
    bindToDir(prefs, tmp);

    prefs.setRecMaxUsersOnline(100);
    prefs.setRecMaxFilesAvail(900);
    QVERIFY(prefs.backupCumulativeStats());
    prefs.resetCumulativeStats(2000);

    // A record set while the statistics were zeroed is still a record.
    prefs.setRecMaxUsersOnline(500);
    prefs.setRecMaxFilesAvail(300);

    QVERIFY(prefs.restoreCumulativeStats());
    QCOMPARE(prefs.recMaxUsersOnline(), uint32{500});   // kept, not overwritten by 100
    QCOMPARE(prefs.recMaxFilesAvail(), uint32{900});    // raised back from the backup
}

void tst_StatisticsReset::restore_withoutABackupFails()
{
    QTemporaryDir tmp;
    Preferences prefs;
    bindToDir(prefs, tmp);

    prefs.setCumTotalUploaded(5000);

    QVERIFY(!prefs.hasCumulativeStatsBackup());
    QVERIFY(!prefs.restoreCumulativeStats());
    QCOMPARE(prefs.cumTotalUploaded(), uint64{5000});
    // A failed restore must not leave a backup behind, or the menu item would
    // come alive with nothing behind it.
    QVERIFY(!prefs.hasCumulativeStatsBackup());
}

// Reset, backup and restore share one field walk, but preferences.yml is loaded
// and saved by hand-written lines — so a field can be in the walk and missing
// from the file. The five HTTP Cache totals were, and went back to 0 on every
// restart. Plant a distinct value in every leaf through the walk (restore),
// round-trip the preferences file, and read them back through the walk (backup).
void tst_StatisticsReset::statisticsYaml_everyCumulativeStatRoundTrips()
{
    QTemporaryDir tmp;
    const QString prefsPath = tmp.path() + QStringLiteral("/prefs.yaml");
    Preferences prefs;
    bindToDir(prefs, tmp);

    QVERIFY(prefs.backupCumulativeStats());
    const QHash<QString, QString> planted =
        plantDistinctStatValues(prefs.cumulativeStatsBackupPath());
    QVERIFY(planted.contains(QStringLiteral("cumHttpCache.bytesSaved")));
    QVERIFY(planted.contains(QStringLiteral("cumUsenet.wireBytes")));
    QVERIFY(planted.contains(QStringLiteral("cumIndexer.feedMatches")));

    QVERIFY(prefs.restoreCumulativeStats());
    QVERIFY(prefs.saveTo(prefsPath));

    Preferences reloaded;
    QVERIFY(reloaded.load(prefsPath));
    QVERIFY(reloaded.backupCumulativeStats());
    const QHash<QString, QString> after = readStatLeaves(reloaded.cumulativeStatsBackupPath());

    QCOMPARE(after.size(), planted.size());
    for (auto it = planted.cbegin(); it != planted.cend(); ++it) {
        QVERIFY2(after.value(it.key()).toDouble() == it.value().toDouble(),
                 qPrintable(QStringLiteral("%1: planted %2, read back %3")
                                .arg(it.key(), it.value(), after.value(it.key()))));
    }
}

// A preferences.yml from before the blocks existed, or with a block missing
// fields, loads what it has and zeroes the rest.
void tst_StatisticsReset::statisticsYaml_fileWithoutBlocksLoadsZeros()
{
    QTemporaryDir tmp;
    const QString path = tmp.path() + QStringLiteral("/prefs.yaml");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("statistics:\n"
               "  cumTotalUploaded: 5\n"
               "  cumUsenet:\n"
               "    wireBytes: 77\n");
    file.close();

    Preferences prefs;
    QVERIFY(prefs.load(path));
    QCOMPARE(prefs.cumTotalUploaded(), uint64{5});
    QCOMPARE(prefs.cumUsenet().wireBytes, uint64{77});
    QCOMPARE(prefs.cumUsenet().decodedBytes, uint64{0});
    QCOMPARE(prefs.cumIndexer(), IndexerCounters{});
}

QTEST_MAIN(tst_StatisticsReset)
#include "tst_StatisticsReset.moc"
