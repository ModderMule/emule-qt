/// @file tst_LastCommonRouteFinder.cpp
/// @brief Tests for LastCommonRouteFinder — USS adaptive upload control.

#include "TestHelpers.h"
#include "net/LastCommonRouteFinder.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule;

class tst_LastCommonRouteFinder : public QObject {
    Q_OBJECT

private slots:
    void constructionAndDefaults();
    void setPrefsAccepted();
    void addHostsSignal();
    void uploadLimitDefault();
    void acceptNewClientDefault();
    void endThreadBeforeStart();
    void statusWhenDisabled();
    void setPrefs_limitCountsAtOnceWhenDisabled();
    void adjustUpload_floorReopensTheQueue();
    void adjustUpload_data();
    void adjustUpload();
    void rampDividers_stages();
    void pingInterval_followsTheUpload();
};

// ---------------------------------------------------------------------------
// Test: construction defaults
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::constructionAndDefaults()
{
    LastCommonRouteFinder finder;
    QCOMPARE(finder.getUpload(), 0u);
    QVERIFY(finder.acceptNewClient());
    QVERIFY(!finder.isRunning());
}

// ---------------------------------------------------------------------------
// Test: setPrefs returns true
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::setPrefsAccepted()
{
    LastCommonRouteFinder finder;

    USSParams params;
    params.enabled = true;
    params.curUpload = 50000;
    params.minUpload = 10;
    params.maxUpload = 100;
    params.pingTolerance = 1.5;
    params.numberOfPingsForAverage = 5;

    QVERIFY(finder.setPrefs(params));
}

// ---------------------------------------------------------------------------
// Test: addHosts with no collection active
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::addHostsSignal()
{
    LastCommonRouteFinder finder;

    // Without the thread running and requesting hosts, addHostsToCheck should return false
    std::vector<uint32> ips = {0x7F000001, 0x08080808};
    QVERIFY(!finder.addHostsToCheck(ips));
}

// ---------------------------------------------------------------------------
// Test: upload limit starts at 0
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::uploadLimitDefault()
{
    LastCommonRouteFinder finder;
    QCOMPARE(finder.getUpload(), 0u);
}

// ---------------------------------------------------------------------------
// Test: acceptNewClient defaults to true
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::acceptNewClientDefault()
{
    LastCommonRouteFinder finder;
    QVERIFY(finder.acceptNewClient());
}

// ---------------------------------------------------------------------------
// Test: endThread before start is safe
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::endThreadBeforeStart()
{
    LastCommonRouteFinder finder;
    finder.endThread(); // Should not crash or hang
    QVERIFY(!finder.isRunning());
}

// ---------------------------------------------------------------------------
// Test: status when disabled
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::statusWhenDisabled()
{
    LastCommonRouteFinder finder;
    USSStatus status = finder.currentStatus();
    QCOMPARE(status.latency, 0u);
    QCOMPARE(status.lowest, 0u);
    QCOMPARE(status.currentLimit, 0u);
}

// The throttler reads getUpload(); a limit that waits for the thread's next wake-up
// (up to 3 minutes) leaves "unlimited -> 50 KB/s" uncapped that long.
void tst_LastCommonRouteFinder::setPrefs_limitCountsAtOnceWhenDisabled()
{
    LastCommonRouteFinder finder;         // thread not started: setPrefs alone must do it
    USSParams params;
    params.enabled = false;
    params.maxUpload = UINT32_MAX;
    finder.setPrefs(params);
    QCOMPARE(finder.getUpload(), UINT32_MAX);

    params.maxUpload = 50;
    finder.setPrefs(params);
    QCOMPARE(finder.getUpload(), 50u * 1024);

    params.maxUpload = 80;
    finder.setPrefs(params);
    QCOMPARE(finder.getUpload(), 80u * 1024);

    // Enabled: the controller owns the value, but never above the maximum.
    params.enabled = true;
    params.maxUpload = 20;
    finder.setPrefs(params);
    QCOMPARE(finder.getUpload(), 20u * 1024);
    params.maxUpload = 90;
    finder.setPrefs(params);
    QCOMPARE(finder.getUpload(), 20u * 1024);
}

void tst_LastCommonRouteFinder::adjustUpload_floorReopensTheQueue()
{
    // A step far below the minimum lands on it.
    bool accept = false;
    QCOMPARE(LastCommonRouteFinder::adjustUpload(500, 0, 1000, 10, 20, 1000, 1000,
                                                 1024, 50 * 1024, accept), 1024u);
    QVERIFY(accept);                      // at the floor new clients are welcome again
}

void tst_LastCommonRouteFinder::adjustUpload_data()
{
    QTest::addColumn<uint32>("upload");
    QTest::addColumn<uint32>("curUpload");
    QTest::addColumn<int>("normalizedPing");
    QTest::addColumn<uint32>("expected");
    QTest::addColumn<int>("accept");      // -1 untouched, 0 false, 1 true

    // target 80 ms over a lowest ping of 20 ms, dividers 1000, limits 10..500 KB/s.
    // step = headroom * 10240 / 1000 / 20
    const uint32 kb = 1024;
    QTest::newRow("on target: no change") << 100 * kb << 100 * kb << 80 << 100 * kb << -1;
    QTest::newRow("headroom 80: +40") << 100 * kb << 100 * kb << 0 << 100 * kb + 40u << 1;
    QTest::newRow("headroom 40: +20") << 100 * kb << 100 * kb << 40 << 100 * kb + 20u << 1;
    QTest::newRow("faster than the baseline: bigger step")
        << 100 * kb << 100 * kb << -20 << 100 * kb + 51u << 1;
    QTest::newRow("excess 100: -51") << 100 * kb << 100 * kb << 180 << 100 * kb - 51u << 0;
    QTest::newRow("excess 20: -10") << 100 * kb << 100 * kb << 100 << 100 * kb - 10u << 0;
    QTest::newRow("not using the limit: no rise") << 100 * kb << 60 * kb << 0 << 100 * kb << 1;
    QTest::newRow("within 30 KB/s of it: rises") << 100 * kb << 71 * kb << 0 << 100 * kb + 40u << 1;
    QTest::newRow("capped at the maximum") << 500 * kb - 10u << 500 * kb << 0 << 500 * kb << 1;
    QTest::newRow("floored at the minimum") << 10 * kb + 5u << 10 * kb << 180 << 10 * kb << 1;
}

void tst_LastCommonRouteFinder::adjustUpload()
{
    QFETCH(uint32, upload);
    QFETCH(uint32, curUpload);
    QFETCH(int, normalizedPing);
    QFETCH(uint32, expected);
    QFETCH(int, accept);

    for (const bool before : {false, true}) {
        bool acceptNewClient = before;
        QCOMPARE(LastCommonRouteFinder::adjustUpload(upload, curUpload, normalizedPing, 80, 20,
                                                     1000, 1000, 10 * 1024, 500 * 1024,
                                                     acceptNewClient),
                 expected);
        QCOMPARE(acceptNewClient, accept < 0 ? before : accept == 1);
    }
}

void tst_LastCommonRouteFinder::rampDividers_stages()
{
    const auto at = [](qint64 ms) {
        uint32 up = 1000, down = 2000;
        LastCommonRouteFinder::rampDividers(ms, up, down);
        return std::pair{up, down};
    };
    QCOMPARE(at(0), (std::pair{1000u, 2000u}));
    QCOMPARE(at(25'000), (std::pair{250u, 500u}));
    QCOMPARE(at(35'000), (std::pair{500u, 1000u}));
    QCOMPARE(at(50'000), (std::pair{750u, 1500u}));
    QCOMPARE(at(60'000), (std::pair{1000u, 2000u}));

    uint32 up = 2, down = 0;              // never a zero divider
    LastCommonRouteFinder::rampDividers(25'000, up, down);
    QCOMPARE(up, 1u);
    QCOMPARE(down, 1u);
}

void tst_LastCommonRouteFinder::pingInterval_followsTheUpload()
{
    QCOMPARE(LastCommonRouteFinder::pingIntervalMs(0), 1000u);
    QCOMPARE(LastCommonRouteFinder::pingIntervalMs(5 * 1024), 1000u);
    QCOMPARE(LastCommonRouteFinder::pingIntervalMs(16 * 1024), 390u);
    QCOMPARE(LastCommonRouteFinder::pingIntervalMs(200 * 1024), 125u);
}

QTEST_MAIN(tst_LastCommonRouteFinder)
#include "tst_LastCommonRouteFinder.moc"
