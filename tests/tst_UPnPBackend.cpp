/// @file tst_UPnPBackend.cpp
/// @brief Tests for portmap/UPnPBackend — the UPnP IGD port-mapping backend.
///
/// Successor to tst_UPnPManager. Every case is offline-safe: the probe case
/// accepts both outcomes, since a developer machine has a router and a CI
/// container does not.

#include "TestHelpers.h"
#include "portmap/UPnPBackend.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule;

class tst_UPnPBackend : public QObject {
    Q_OBJECT

private slots:
    void defaultConstruction();
    void ipv6UnsupportedUntilPinholesConfirmed();
    void shutdownBeforeProbeIsSafe();
    void releaseBeforeProbeIsSafe();
    void requestBeforeProbeFailsCleanly();
    void bindAddressIsAccepted();
    void repeatedShutdownIsSafe();
    void probeEmitsExactlyOneResult();
};

void tst_UPnPBackend::defaultConstruction()
{
    UPnPBackend backend;
    QCOMPARE(backend.method(), PortMapMethod::UPnP);
    QCOMPARE(backend.name(), QStringLiteral("UPnP"));
    QVERIFY(backend.supports(PortMapFamily::IPv4));
}

void tst_UPnPBackend::ipv6UnsupportedUntilPinholesConfirmed()
{
    // Advertising WANIPv6FirewallControl is not enough — GetFirewallStatus has
    // to report both FirewallEnabled and InboundPinholeAllowed. Before any
    // probe we know neither, so IPv6 must read as unsupported.
    UPnPBackend backend;
    QVERIFY(!backend.supports(PortMapFamily::IPv6));
}

void tst_UPnPBackend::shutdownBeforeProbeIsSafe()
{
    UPnPBackend backend;
    backend.shutdown();   // no worker thread was ever started
    QVERIFY(true);
}

void tst_UPnPBackend::releaseBeforeProbeIsSafe()
{
    UPnPBackend backend;
    QSignalSpy spy(&backend, &PortMapBackend::mappingResult);
    PortMapping mapping;
    mapping.request.internalPort = 4662;
    backend.releaseMapping(mapping);
    QCOMPARE(spy.count(), 0);
}

void tst_UPnPBackend::requestBeforeProbeFailsCleanly()
{
    // The interface contract is that every requestMapping() produces exactly one
    // terminal signal; silence would deadlock PortMapper's trial state machine.
    UPnPBackend backend;
    QSignalSpy spy(&backend, &PortMapBackend::mappingResult);

    PortMapRequest request;
    request.purpose = PortMapPurpose::Ed2kTcp;
    request.protocol = PortMapProtocol::Tcp;
    request.internalPort = 4662;
    backend.requestMapping(request, 3600);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toBool(), false);
}

void tst_UPnPBackend::bindAddressIsAccepted()
{
    UPnPBackend backend;
    backend.setBindAddress(QStringLiteral("192.168.178.36"));
    backend.setBindAddress(QString());
    QVERIFY(true);
}

void tst_UPnPBackend::repeatedShutdownIsSafe()
{
    UPnPBackend backend;
    backend.shutdown();
    backend.shutdown();
    QVERIFY(true);
}

void tst_UPnPBackend::probeEmitsExactlyOneResult()
{
    UPnPBackend backend;
    QSignalSpy spy(&backend, &PortMapBackend::probeFinished);
    backend.probe(3000);

    // Either outcome is fine — the point is that the probe always terminates,
    // and terminates once.
    QVERIFY(spy.wait(15000) || spy.count() > 0);
    QCOMPARE(spy.count(), 1);

    backend.shutdown();
}

QTEST_MAIN(tst_UPnPBackend)
#include "tst_UPnPBackend.moc"
