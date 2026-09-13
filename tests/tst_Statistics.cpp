/// @file tst_Statistics.cpp
/// @brief Tests for stats/Statistics — session stats accumulation,
///        transfer counters, ratio calculation, history ring buffer.

#include "TestHelpers.h"
#include "client/ClientStateDefs.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"

#include <QCborMap>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;

class tst_Statistics : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void init_loadsFromPrefs();
    void sessionReceivedBytes_accumulates();
    void sessionSentBytes_accumulates();
    void sessionSentBytesToFriend_accumulates();
    void updateConnectionStats_tracksRates();
    void updateConnectionStats_updatesMaxRates();
    void updateConnectionStats_emitsSignal();
    void transferTime_tracking();
    void serverDuration_tracking();
    void add2TotalServerDuration_accumulates();
    void serverConnected_countsReconnectsNotConnections();
    void serverDisconnected_banksWithoutATickAndIsIdempotent();
    void overheadDown_accumulates();
    void overheadUp_accumulates();
    void overheadDown_packetCounting();
    void compDownDatarateOverhead_computes();
    void compUpDatarateOverhead_computes();
    void resetDownDatarateOverhead_clears();
    void resetUpDatarateOverhead_clears();
    void globalState_gettersSetters();
    void globalProgress_gettersSetters();
    void avgDownloadRate_session();
    void avgUploadRate_session();
    void recordRate_appendsHistory();
    void addTransferData_feedsSessionTotals();
    void addTransferData_stampsTransferStartOnce();
    void totalAverage_blendsWithTheRebaseSnapshotNotThePref();
    void combineCounters_sumsAndKeepsPeaks();
    void countersCbor_roundTripsAndMissingKeysReadZero();
    void cumulativeUsenet_isBasePlusSession();
    void overheadStatsUpdated_signal();
    void uptimeSecs_countsFromTheStartTick();
    void init_stampsStartTickOnceOnly();
    void init_stampsLastResetOnFreshInstallOnly();
    void init_raisesCumRunTimeToItsLowerBound();
};

void tst_Statistics::construct_default()
{
    Statistics stats;
    QCOMPARE(stats.rateDown(), 0.0f);
    QCOMPARE(stats.rateUp(), 0.0f);
    QCOMPARE(stats.maxDown(), 0.0f);
    QCOMPARE(stats.maxUp(), 0.0f);
    QCOMPARE(stats.maxCumDown(), 0.0f);
    QCOMPARE(stats.maxCumUp(), 0.0f);
    QCOMPARE(stats.sessionReceivedBytes(), uint64{0});
    QCOMPARE(stats.sessionSentBytes(), uint64{0});
    QCOMPARE(stats.sessionSentBytesToFriend(), uint64{0});
    QCOMPARE(stats.reconnects(), uint16{0});
    QCOMPARE(stats.filteredClients(), uint32{0});
    QCOMPARE(stats.startTick(), uint64{0});
    QCOMPARE(stats.uptimeSecs(), uint32{0});
    QCOMPARE(stats.transferStartTime(), uint32{0});
    QCOMPARE(stats.serverConnectTime(), uint32{0});
    QCOMPARE(stats.transferTime(), uint32{0});
    QCOMPARE(stats.uploadTime(), uint32{0});
    QCOMPARE(stats.downloadTime(), uint32{0});
    QCOMPARE(stats.serverDuration(), uint32{0});
    QCOMPARE(stats.globalDone(), 0.0f);
    QCOMPARE(stats.globalSize(), 0.0f);
    QCOMPARE(stats.overallStatus(), uint32{0});
}

void tst_Statistics::init_loadsFromPrefs()
{
    Preferences prefs;
    prefs.setConnMaxDownRate(100.0f);
    prefs.setConnAvgUpRate(50.0f);
    prefs.setConnMaxAvgDownRate(80.0f);
    prefs.setConnAvgDownRate(60.0f);
    prefs.setConnMaxAvgUpRate(40.0f);
    prefs.setConnMaxUpRate(90.0f);

    Statistics stats;
    stats.init(prefs);

    QCOMPARE(stats.maxCumDown(), 100.0f);
    QCOMPARE(stats.cumUpAvg(), 50.0f);
    QCOMPARE(stats.maxCumDownAvg(), 80.0f);
    QCOMPARE(stats.cumDownAvg(), 60.0f);
    QCOMPARE(stats.maxCumUpAvg(), 40.0f);
    QCOMPARE(stats.maxCumUp(), 90.0f);
}

void tst_Statistics::sessionReceivedBytes_accumulates()
{
    Statistics stats;
    stats.addSessionReceivedBytes(1000);
    stats.addSessionReceivedBytes(500);
    QCOMPARE(stats.sessionReceivedBytes(), uint64{1500});
}

void tst_Statistics::sessionSentBytes_accumulates()
{
    Statistics stats;
    stats.addSessionSentBytes(2000);
    stats.addSessionSentBytes(300);
    QCOMPARE(stats.sessionSentBytes(), uint64{2300});
}

void tst_Statistics::sessionSentBytesToFriend_accumulates()
{
    Statistics stats;
    stats.addSessionSentBytesToFriend(100);
    stats.addSessionSentBytesToFriend(200);
    QCOMPARE(stats.sessionSentBytesToFriend(), uint64{300});
}

void tst_Statistics::updateConnectionStats_tracksRates()
{
    Statistics stats;
    stats.updateConnectionStats(10.0f, 20.0f);
    QCOMPARE(stats.rateUp(), 10.0f);
    QCOMPARE(stats.rateDown(), 20.0f);
}

void tst_Statistics::updateConnectionStats_updatesMaxRates()
{
    Statistics stats;
    stats.updateConnectionStats(10.0f, 20.0f);
    QCOMPARE(stats.maxUp(), 10.0f);
    QCOMPARE(stats.maxDown(), 20.0f);

    stats.updateConnectionStats(5.0f, 15.0f);
    // Max should not decrease
    QCOMPARE(stats.maxUp(), 10.0f);
    QCOMPARE(stats.maxDown(), 20.0f);

    stats.updateConnectionStats(15.0f, 25.0f);
    QCOMPARE(stats.maxUp(), 15.0f);
    QCOMPARE(stats.maxDown(), 25.0f);
}

void tst_Statistics::updateConnectionStats_emitsSignal()
{
    Statistics stats;
    QSignalSpy spy(&stats, &Statistics::statsUpdated);
    stats.updateConnectionStats(10.0f, 20.0f);
    QCOMPARE(spy.count(), 1);
}

void tst_Statistics::transferTime_tracking()
{
    Statistics stats;
    // Initially no transfer time
    QCOMPARE(stats.transferTime(), uint32{0});
    QCOMPARE(stats.uploadTime(), uint32{0});
    QCOMPARE(stats.downloadTime(), uint32{0});
}

void tst_Statistics::serverDuration_tracking()
{
    Statistics stats;
    QCOMPARE(stats.serverDuration(), uint32{0});

    // Set server connect time to simulate connection
    stats.setServerConnectTime(static_cast<uint32>(getTickCount()));
    // After updateConnectionStats, serverDuration will be calculated
    stats.updateConnectionStats(0.0f, 0.0f);
    // Should be very close to 0 since we just set it
    QVERIFY(stats.serverDuration() < 2);
}

void tst_Statistics::add2TotalServerDuration_accumulates()
{
    Statistics stats;
    // Simulate a server connection that lasted some time
    // by setting internal state through the public API
    stats.setServerConnectTime(static_cast<uint32>(getTickCount()) - SEC2MS(10));
    stats.updateConnectionStats(0.0f, 0.0f);

    const uint32 dur1 = stats.serverDuration();
    QVERIFY(dur1 >= 9);  // ~10 seconds, allow for timing variance

    stats.add2TotalServerDuration();
    // After adding, current server duration resets but total remains
    stats.setServerConnectTime(0);
    stats.updateConnectionStats(0.0f, 0.0f);
    QCOMPARE(stats.serverDuration(), dur1);  // only accumulated part
}

// "Reconnects" answers how often the server connection dropped and came back, so
// the first login of the session is not one. MFC keeps the raw count and
// subtracts one wherever it shows or saves it (StatisticsDlg.cpp:1415,
// Preferences.cpp:857); counting the right thing here keeps the GUI, the web
// server and the cumulative total honest without each repeating the rule.
void tst_Statistics::serverConnected_countsReconnectsNotConnections()
{
    Statistics stats;
    QCOMPARE(stats.reconnects(), uint16{0});

    stats.serverConnected();
    QCOMPARE(stats.reconnects(), uint16{0});
    QVERIFY(stats.serverConnectTime() != 0);   // the clock is running

    stats.serverDisconnected();
    QCOMPARE(stats.serverConnectTime(), uint32{0});

    stats.serverConnected();
    QCOMPARE(stats.reconnects(), uint16{1});
}

// The bank happens from the connect stamp, not from whatever the 1 Hz update
// last computed, so a connection that ends between ticks keeps its seconds. And
// every teardown path may call it: several of them reach the same drop.
void tst_Statistics::serverDisconnected_banksWithoutATickAndIsIdempotent()
{
    Statistics stats;
    stats.serverConnected();
    stats.setServerConnectTime(stats.serverConnectTime() - SEC2MS(10));

    // No updateConnectionStats() in between — this is the point.
    stats.serverDisconnected();
    const uint32 banked = stats.serverDuration();
    QVERIFY2(banked >= 9, qPrintable(QString::number(banked)));
    QCOMPARE(stats.thisServerDuration(), uint32{0});

    stats.serverDisconnected();
    stats.serverDisconnected();
    QCOMPARE(stats.serverDuration(), banked);

    // A tick while disconnected does not revive the clock either.
    stats.updateConnectionStats(0.0f, 0.0f);
    QCOMPARE(stats.serverDuration(), banked);
}

void tst_Statistics::overheadDown_accumulates()
{
    Statistics stats;
    stats.addDownDataOverheadSourceExchange(100);
    stats.addDownDataOverheadFileRequest(200);
    stats.addDownDataOverheadServer(300);
    stats.addDownDataOverheadKad(400);
    stats.addDownDataOverheadOther(500);

    QCOMPARE(stats.downDataOverheadSourceExchange(), uint64{100});
    QCOMPARE(stats.downDataOverheadFileRequest(), uint64{200});
    QCOMPARE(stats.downDataOverheadServer(), uint64{300});
    QCOMPARE(stats.downDataOverheadKad(), uint64{400});
    QCOMPARE(stats.downDataOverheadOther(), uint64{500});
}

void tst_Statistics::overheadUp_accumulates()
{
    Statistics stats;
    stats.addUpDataOverheadSourceExchange(150);
    stats.addUpDataOverheadFileRequest(250);
    stats.addUpDataOverheadServer(350);
    stats.addUpDataOverheadKad(450);
    stats.addUpDataOverheadOther(550);

    QCOMPARE(stats.upDataOverheadSourceExchange(), uint64{150});
    QCOMPARE(stats.upDataOverheadFileRequest(), uint64{250});
    QCOMPARE(stats.upDataOverheadServer(), uint64{350});
    QCOMPARE(stats.upDataOverheadKad(), uint64{450});
    QCOMPARE(stats.upDataOverheadOther(), uint64{550});
}

void tst_Statistics::overheadDown_packetCounting()
{
    Statistics stats;
    stats.addDownDataOverheadSourceExchange(10);
    stats.addDownDataOverheadSourceExchange(20);
    stats.addDownDataOverheadFileRequest(30);

    QCOMPARE(stats.downDataOverheadSourceExchangePackets(), uint64{2});
    QCOMPARE(stats.downDataOverheadFileRequestPackets(), uint64{1});
    QCOMPARE(stats.downDataOverheadServerPackets(), uint64{0});
}

void tst_Statistics::compDownDatarateOverhead_computes()
{
    Statistics stats;
    // Add some overhead data
    stats.addDownDataOverheadServer(1000);
    // Compute overhead rate — first call just starts accumulating
    stats.compDownDatarateOverhead();
    // With only 1 entry, rate should be 0 (need >10 entries)
    QCOMPARE(stats.downDatarateOverhead(), uint64{0});
}

void tst_Statistics::compUpDatarateOverhead_computes()
{
    Statistics stats;
    stats.addUpDataOverheadServer(1000);
    stats.compUpDatarateOverhead();
    QCOMPARE(stats.upDatarateOverhead(), uint64{0});
}

void tst_Statistics::resetDownDatarateOverhead_clears()
{
    Statistics stats;
    stats.addDownDataOverheadServer(1000);
    stats.compDownDatarateOverhead();
    stats.resetDownDatarateOverhead();
    QCOMPARE(stats.downDatarateOverhead(), uint64{0});
}

void tst_Statistics::resetUpDatarateOverhead_clears()
{
    Statistics stats;
    stats.addUpDataOverheadServer(1000);
    stats.compUpDatarateOverhead();
    stats.resetUpDatarateOverhead();
    QCOMPARE(stats.upDatarateOverhead(), uint64{0});
}

void tst_Statistics::globalState_gettersSetters()
{
    Statistics stats;

    stats.setReconnects(5);
    QCOMPARE(stats.reconnects(), uint16{5});
    stats.addReconnect();
    QCOMPARE(stats.reconnects(), uint16{6});

    stats.setFilteredClients(10);
    QCOMPARE(stats.filteredClients(), uint32{10});
    stats.addFilteredClient();
    QCOMPARE(stats.filteredClients(), uint32{11});

    stats.setStartTick(12345);
    QCOMPARE(stats.startTick(), uint64{12345});

    stats.setTransferStartTime(67890);
    QCOMPARE(stats.transferStartTime(), uint32{67890});

    stats.setServerConnectTime(11111);
    QCOMPARE(stats.serverConnectTime(), uint32{11111});
}

void tst_Statistics::globalProgress_gettersSetters()
{
    Statistics stats;

    stats.setGlobalDone(50.5f);
    QCOMPARE(stats.globalDone(), 50.5f);

    stats.setGlobalSize(100.0f);
    QCOMPARE(stats.globalSize(), 100.0f);

    stats.setOverallStatus(0x01);
    QCOMPARE(stats.overallStatus(), uint32{0x01});
}

void tst_Statistics::avgDownloadRate_session()
{
    Statistics stats;
    // No transfer start time — should return 0
    QCOMPARE(stats.avgDownloadRate(AverageType::Session), 0.0f);

    // Set transfer start time far enough in the past (>5 seconds)
    stats.setTransferStartTime(static_cast<uint32>(getTickCount()) - SEC2MS(10));
    stats.addSessionReceivedBytes(10240);  // 10 KB

    // Session average: 10240 bytes / 1024 / 10s = 1.0 KB/s
    const float rate = stats.avgDownloadRate(AverageType::Session);
    QVERIFY(rate > 0.5f);
    QVERIFY(rate < 2.0f);
}

void tst_Statistics::avgUploadRate_session()
{
    Statistics stats;
    QCOMPARE(stats.avgUploadRate(AverageType::Session), 0.0f);

    stats.setTransferStartTime(static_cast<uint32>(getTickCount()) - SEC2MS(10));
    stats.addSessionSentBytes(10240);  // 10 KB

    const float rate = stats.avgUploadRate(AverageType::Session);
    QVERIFY(rate > 0.5f);
    QVERIFY(rate < 2.0f);
}

void tst_Statistics::recordRate_appendsHistory()
{
    Preferences prefs;
    prefs.setStatsAverageMinutes(5);

    Statistics stats;
    stats.init(prefs);

    // Without transfer start, recordRate should be a no-op
    stats.recordRate();
    QCOMPARE(stats.avgDownloadRate(AverageType::Time), 0.0f);

    // Set transfer start time and add some data
    stats.setTransferStartTime(static_cast<uint32>(getTickCount()) - SEC2MS(60));
    stats.addSessionReceivedBytes(1024 * 100);
    stats.recordRate();

    // With only one entry, time-based average needs at least 2 entries
    // to compute a rate. So it should still be 0 with 1 entry.
    QCOMPARE(stats.avgDownloadRate(AverageType::Time), 0.0f);
}

// MFC's Add2SessionTransferData ends in UpdateSentBytes/UpdateReceivedBytes: the
// session totals are the per-client breakdown summed. They used to be fed by
// nothing, so "Uploaded Data" read 0 however much was uploaded.
void tst_Statistics::addTransferData_feedsSessionTotals()
{
    Statistics stats;
    stats.addTransferData(ClientSoftware::eMule, 4662, false, true, 800);
    stats.addTransferData(ClientSoftware::aMule, 5000, true, true, 200);
    stats.addTransferData(ClientSoftware::eMule, 4662, false, false, 900);
    stats.addTransferData(ClientSoftware::URL, 0, false, false, 100);

    QCOMPARE(stats.sessionSentBytes(), uint64{1000});
    QCOMPARE(stats.sessionReceivedBytes(), uint64{1000});

    // The breakdown underneath still adds up to the same totals.
    QCOMPARE(stats.sesUpByClient(0) + stats.sesUpByClient(3), uint64{1000});
    QCOMPARE(stats.sesUpPort4662() + stats.sesUpPortOther(), uint64{1000});
    QCOMPARE(stats.sesDownByClient(0) + stats.sesDownByClient(7), uint64{1000});
    QCOMPARE(stats.sesDownPort4662(), uint64{900});
    QCOMPARE(stats.sesDownPortOther(), uint64{100});
}

// MFC's SetTimeOnTransfer: the first byte starts the clock the session averages
// run on, and later bytes leave it where it is.
void tst_Statistics::addTransferData_stampsTransferStartOnce()
{
    Statistics stats;
    stats.addTransferData(ClientSoftware::eMule, 4662, false, false, 0);
    QCOMPARE(stats.transferStartTime(), uint32{0});   // nothing moved, nothing stamped

    stats.addTransferData(ClientSoftware::eMule, 4662, false, false, 10);
    const uint32 first = stats.transferStartTime();
    QVERIFY(first != 0);

    stats.addTransferData(ClientSoftware::eMule, 4662, false, true, 10);
    stats.addSessionReceivedBytes(10);
    QCOMPARE(stats.transferStartTime(), first);
}

// The flush banks the Total average into connAvgDownRate, as MFC's SaveStats
// does. If the Total then read that pref back it would compound every interval;
// it blends with the value captured at the rebase instead.
void tst_Statistics::totalAverage_blendsWithTheRebaseSnapshotNotThePref()
{
    Preferences prefs;
    prefs.setConnAvgDownRate(100.0f);

    Statistics stats;
    stats.init(prefs);
    stats.setTransferStartTime(static_cast<uint32>(getTickCount()) - SEC2MS(10));
    stats.addSessionReceivedBytes(10 * 1024 * 10);   // 10 KB/s over 10 s

    const float total = stats.avgDownloadRate(AverageType::Total);
    QVERIFY(total > 54.0f && total < 56.0f);           // (10 + 100) / 2

    stats.flushCumulativeToPrefs(prefs, {});
    QVERIFY(qAbs(prefs.connAvgDownRate() - total) < 0.5f);

    // Flushing again, and asking again, changes nothing.
    stats.flushCumulativeToPrefs(prefs, {});
    QVERIFY(qAbs(stats.avgDownloadRate(AverageType::Total) - total) < 0.5f);
    QVERIFY(qAbs(prefs.connAvgDownRate() - total) < 0.5f);
}

void tst_Statistics::combineCounters_sumsAndKeepsPeaks()
{
    UsenetCounters base;
    base.wireBytes = 1000;
    base.maxDownRate = 500;
    base.peakConnections = 40;

    UsenetCounters session;
    session.wireBytes = 250;
    session.maxDownRate = 700;
    session.peakConnections = 10;

    const UsenetCounters total = combineCounters(base, session);
    QCOMPARE(total.wireBytes, uint64{1250});
    QCOMPARE(total.maxDownRate, uint64{700});    // a peak, not an amount
    QCOMPARE(total.peakConnections, uint64{40});
}

void tst_Statistics::countersCbor_roundTripsAndMissingKeysReadZero()
{
    IndexerCounters c;
    c.searches = 3;
    c.apiRequests = 12;
    c.feedMatches = 1;

    QCborMap map = countersToCbor(c);
    QCOMPARE(map.value(QStringLiteral("apiRequests")).toInteger(), 12);
    QCOMPARE(countersFromCbor<IndexerCounters>(map), c);

    // An older sender without a field: the field reads 0 rather than failing.
    map.remove(QStringLiteral("searches"));
    QCOMPARE(countersFromCbor<IndexerCounters>(map).searches, uint64{0});
    QCOMPARE(countersFromCbor<IndexerCounters>(map).apiRequests, uint64{12});
}

void tst_Statistics::cumulativeUsenet_isBasePlusSession()
{
    Preferences prefs;
    UsenetCounters banked;
    banked.decodedBytes = 5000;
    banked.itemsCompleted = 2;
    banked.maxDownRate = 900;
    prefs.setCumUsenet(banked);
    IndexerCounters bankedIdx;
    bankedIdx.searches = 7;
    prefs.setCumIndexer(bankedIdx);

    Statistics stats;
    stats.init(prefs);
    stats.usenetSession().decodedBytes += 1000;
    ++stats.usenetSession().itemsCompleted;
    raiseCounter(stats.usenetSession().maxDownRate, 400);
    ++stats.indexerSession().searches;

    const UsenetCounters total = stats.cumulativeUsenet();
    QCOMPARE(total.decodedBytes, uint64{6000});
    QCOMPARE(total.itemsCompleted, uint64{3});
    QCOMPARE(total.maxDownRate, uint64{900});
    QCOMPARE(stats.cumulativeIndexer().searches, uint64{8});

    // The flush writes the same totals back, absolutely.
    stats.flushCumulativeToPrefs(prefs, {});
    stats.flushCumulativeToPrefs(prefs, {});
    QCOMPARE(prefs.cumUsenet(), total);
    QCOMPARE(prefs.cumIndexer().searches, uint64{8});
}

void tst_Statistics::overheadStatsUpdated_signal()
{
    Statistics stats;
    QSignalSpy spy(&stats, &Statistics::overheadStatsUpdated);

    stats.addDownDataOverheadServer(100);
    stats.compDownDatarateOverhead();
    QCOMPARE(spy.count(), 1);

    stats.addUpDataOverheadServer(200);
    stats.compUpDatarateOverhead();
    QCOMPARE(spy.count(), 2);
}

// The uptime bug this replaces: setStartTime() had no caller, so the tick stayed
// 0 and the snapshot's `now - startTime()` reported seconds since 1970.
void tst_Statistics::uptimeSecs_countsFromTheStartTick()
{
    Statistics stats;

    // Unstarted means 0, not "since the epoch".
    QCOMPARE(stats.uptimeSecs(), uint32{0});

    stats.setStartTick(getTickCount() - SEC2MS(90));
    QCOMPARE(stats.uptimeSecs(), uint32{90});
}

void tst_Statistics::init_stampsStartTickOnceOnly()
{
    Preferences prefs;
    Statistics stats;

    stats.init(prefs);
    QVERIFY(stats.startTick() != 0);

    // Backdate the session by an hour — two init() calls in the same millisecond
    // would otherwise agree no matter what — then re-initialise. Reloading
    // preferences must not restart the session clock.
    const uint64 backdated = stats.startTick() - SEC2MS(3600);
    stats.setStartTick(backdated);

    stats.init(prefs);
    QCOMPARE(stats.startTick(), backdated);
    QVERIFY(stats.uptimeSecs() >= 3600);
}

void tst_Statistics::init_stampsLastResetOnFreshInstallOnly()
{
    // Nothing has ever been counted -> the statistics are "as of now".
    Preferences fresh;
    QCOMPARE(fresh.statsLastReset(), uint64{0});
    Statistics statsFresh;
    statsFresh.init(fresh);
    QVERIFY(fresh.statsLastReset() > 0);

    // An existing install with history keeps "never reset" until it is reset.
    Preferences used;
    used.setCumTotalDownloaded(12345);
    Statistics statsUsed;
    statsUsed.init(used);
    QCOMPARE(used.statsLastReset(), uint64{0});
}

// Installs that ran with the broken session clock have transfer time but no run
// time, which makes every cumulative percentage nonsense.
void tst_Statistics::init_raisesCumRunTimeToItsLowerBound()
{
    Preferences prefs;
    prefs.setCumRunTime(0);
    prefs.setCumTransferTime(18329);
    prefs.setCumUploadTime(4000);
    prefs.setCumDownloadTime(15000);
    prefs.setCumServerDuration(9000);

    Statistics stats;
    stats.init(prefs);
    QCOMPARE(prefs.cumRunTime(), uint64{18329});

    // A run time that already exceeds the bound is left alone.
    Preferences healthy;
    healthy.setCumRunTime(50000);
    healthy.setCumTransferTime(18329);
    Statistics stats2;
    stats2.init(healthy);
    QCOMPARE(healthy.cumRunTime(), uint64{50000});
}

QTEST_MAIN(tst_Statistics)
#include "tst_Statistics.moc"
