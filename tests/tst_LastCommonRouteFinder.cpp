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

QTEST_MAIN(tst_LastCommonRouteFinder)
#include "tst_LastCommonRouteFinder.moc"
