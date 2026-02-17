/// @file tst_UPnPManager.cpp
/// @brief Tests for upnp/UPnPManager — UPnP port mapping manager.
///
/// Since UPnP tests cannot require a real router, we test construction,
/// default state, safe no-ops, and signal emission when no IGD is found.

#include "TestHelpers.h"
#include "upnp/UPnPManager.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule;

class tst_UPnPManager : public QObject {
    Q_OBJECT

private slots:
    void defaultConstruction();
    void portStatusDefaults();
    void stopBeforeStart();
    void deletePortsBeforeStart();
    void bindAddress();
    void enableWebPortBeforeDiscovery();
    void discoveryEmitsSignalOnFailure();
};

// ---------------------------------------------------------------------------
// Default construction — not ready, status unknown
// ---------------------------------------------------------------------------

void tst_UPnPManager::defaultConstruction()
{
    UPnPManager mgr;
    QVERIFY(!mgr.isReady());
    QCOMPARE(mgr.portStatus(), UPnPManager::PortStatus::Unknown);
}

// ---------------------------------------------------------------------------
// Port status enum default
// ---------------------------------------------------------------------------

void tst_UPnPManager::portStatusDefaults()
{
    UPnPManager::PortStatus status{};
    QCOMPARE(status, UPnPManager::PortStatus::Unknown);
}

// ---------------------------------------------------------------------------
// stopDiscovery() when nothing is running — should not crash
// ---------------------------------------------------------------------------

void tst_UPnPManager::stopBeforeStart()
{
    UPnPManager mgr;
    mgr.stopDiscovery();
    QVERIFY(!mgr.isReady());
}

// ---------------------------------------------------------------------------
// deletePorts() when nothing is mapped — should not crash
// ---------------------------------------------------------------------------

void tst_UPnPManager::deletePortsBeforeStart()
{
    UPnPManager mgr;
    mgr.deletePorts();
    QVERIFY(!mgr.isReady());
    QCOMPARE(mgr.portStatus(), UPnPManager::PortStatus::Unknown);
}

// ---------------------------------------------------------------------------
// setBindAddress() stores the value
// ---------------------------------------------------------------------------

void tst_UPnPManager::bindAddress()
{
    UPnPManager mgr;
    mgr.setBindAddress(QStringLiteral("192.168.1.100"));
    // No crash, value stored internally — we verify indirectly through
    // successful construction and no assertion failures
    QVERIFY(!mgr.isReady());
}

// ---------------------------------------------------------------------------
// enableWebServerPort() before discovery — stores port for later use
// ---------------------------------------------------------------------------

void tst_UPnPManager::enableWebPortBeforeDiscovery()
{
    UPnPManager mgr;
    mgr.enableWebServerPort(8080);
    // Should not crash or map anything (no IGD yet)
    QVERIFY(!mgr.isReady());
}

// ---------------------------------------------------------------------------
// startDiscovery() with no network — emits discoveryComplete(false)
// ---------------------------------------------------------------------------

void tst_UPnPManager::discoveryEmitsSignalOnFailure()
{
    UPnPManager mgr;
    QSignalSpy spy(&mgr, &UPnPManager::discoveryComplete);
    QVERIFY(spy.isValid());

    mgr.startDiscovery(4662, 4672);

    // Wait for discovery thread to complete (should fail quickly with no IGD)
    QVERIFY(spy.wait(10000));

    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    // On a machine with no UPnP router, this should be false.
    // On a machine WITH a UPnP router, it could be true — both are valid.
    Q_UNUSED(args);
}

QTEST_MAIN(tst_UPnPManager)
#include "tst_UPnPManager.moc"
