/// @file tst_Statistics.cpp
/// @brief Tests for stats/Statistics — session stats accumulation,
///        transfer counters, ratio calculation, history ring buffer.

#include "TestHelpers.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"

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
    void sessionBytesChanged_signal();
    void overheadStatsUpdated_signal();
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
    QCOMPARE(stats.startTime(), uint32{0});
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

    stats.setStartTime(12345);
    QCOMPARE(stats.startTime(), uint32{12345});

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

void tst_Statistics::sessionBytesChanged_signal()
{
    Statistics stats;
    QSignalSpy spy(&stats, &Statistics::sessionBytesChanged);

    stats.addSessionReceivedBytes(100);
    QCOMPARE(spy.count(), 1);

    stats.addSessionSentBytes(200);
    QCOMPARE(spy.count(), 2);

    stats.addSessionSentBytesToFriend(300);
    QCOMPARE(spy.count(), 3);
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

QTEST_MAIN(tst_Statistics)
#include "tst_Statistics.moc"
