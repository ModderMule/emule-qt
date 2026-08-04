/// @file tst_PortMapLive.cpp
/// @brief Live end-to-end port mapping against the real router on this network.
///
/// Live-labelled: needs an actual gateway, so it is excluded from `ctest -LE live`.
/// Skips cleanly (rather than failing) when there is no default route, so a CI
/// container stays green.
///
/// Every mapping it creates uses an ephemeral high port and is released again.

#include "TestHelpers.h"
#include "net/DefaultGateway.h"
#include "portmap/PortMapper.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule;

namespace {

/// Ports unlikely to collide with anything real on this LAN. The IPv6 case gets
/// its own: a PCP mapping is owned by its nonce, so if some other client (or an
/// earlier experiment) holds the port, the router silently refuses ours rather
/// than reporting NOT_AUTHORIZED — which would look like "IPv6 unsupported".
constexpr uint16 kProbePortTcp = 51662;
constexpr uint16 kProbePortUdp = 51672;
constexpr uint16 kProbePortV6 = 51682;

PortMapRequest makeRequest(PortMapPurpose purpose, PortMapProtocol protocol, uint16 port,
                           PortMapFamily family = PortMapFamily::IPv4)
{
    PortMapRequest r;
    r.purpose = purpose;
    r.protocol = protocol;
    r.family = family;
    r.internalPort = port;
    r.description = QStringLiteral("eMuleQt live test");
    return r;
}

} // namespace

class tst_PortMapLive : public QObject {
    Q_OBJECT

private slots:
    void gatewayIsDiscoverable();
    void mapsAndReleasesIPv4();
    void mapsAndReleasesIPv6();
};

void tst_PortMapLive::gatewayIsDiscoverable()
{
    const auto v4 = defaultGateways(Address::Family::IPv4);
    const auto v6 = defaultGateways(Address::Family::IPv6);
    if (v4.empty() && v6.empty())
        QSKIP("no default route on this host");

    for (const GatewayCandidate& gateway : v4)
        qInfo() << "IPv4 gateway:" << gateway.toString();
    for (const GatewayCandidate& gateway : v6)
        qInfo() << "IPv6 gateway:" << gateway.toString();
    QVERIFY(!v4.empty() || !v6.empty());
}

void tst_PortMapLive::mapsAndReleasesIPv4()
{
    if (defaultGateways(Address::Family::IPv4).empty())
        QSKIP("no IPv4 default route");

    PortMapper mapper;
    mapper.setLeaseSeconds(120);
    mapper.setDesiredMappings(
        {makeRequest(PortMapPurpose::Ed2kTcp, PortMapProtocol::Tcp, kProbePortTcp),
         makeRequest(PortMapPurpose::Ed2kClientUdp, PortMapProtocol::Udp, kProbePortUdp)});
    mapper.start();

    QTRY_VERIFY_WITH_TIMEOUT(mapper.status() != PortMapStatus::Unknown
                                 && mapper.status() != PortMapStatus::Probing,
                             20000);

    qInfo() << "method:" << portMapMethodName(mapper.activeMethod())
            << "status:" << portMapStatusName(mapper.status())
            << "external:" << mapper.externalAddress().toString();

    if (mapper.status() == PortMapStatus::NotMapped)
        QSKIP("no port-mapping protocol available on this network");

    QVERIFY(mapper.activeMethod() != PortMapMethod::None);
    const uint16 external = mapper.externalPort(PortMapPurpose::Ed2kTcp, PortMapProtocol::Tcp);
    qInfo() << "TCP" << kProbePortTcp << "->" << external;
    QVERIFY(external != 0);

    // A CGNAT line maps successfully and is still unreachable, so Degraded is a
    // legitimate outcome here — but it must never be reported as Mapped.
    if (!mapper.externalAddress().isNull() && !mapper.externalAddress().isPublicIP()) {
        qInfo() << "external address is not publicly routable (CGNAT?) — expecting Degraded";
        QCOMPARE(mapper.status(), PortMapStatus::Degraded);
    }

    mapper.stop(/*releaseMappings=*/true);
    QVERIFY(mapper.mappings().empty());
}

void tst_PortMapLive::mapsAndReleasesIPv6()
{
    if (defaultGateways(Address::Family::IPv6).empty())
        QSKIP("no IPv6 default route");

    PortMapper mapper;
    mapper.setLeaseSeconds(120);
    mapper.setDesiredMappings({makeRequest(PortMapPurpose::Ed2kTcp, PortMapProtocol::Tcp,
                                           kProbePortV6, PortMapFamily::IPv6)});
    mapper.start();

    QTRY_VERIFY_WITH_TIMEOUT(mapper.status() != PortMapStatus::Unknown
                                 && mapper.status() != PortMapStatus::Probing,
                             20000);

    qInfo() << "IPv6 method:" << portMapMethodName(mapper.activeMethod())
            << "status:" << portMapStatusName(mapper.status());

    if (mapper.status() == PortMapStatus::NotMapped || mapper.mappings().empty())
        QSKIP("no IPv6 port-mapping protocol available on this network");

    // IPv6 has no translation: a mapping is a firewall pinhole, so the external
    // port must equal the internal one and the address must be our own GUA.
    const PortMapping mapping = mapper.mappings().front();
    qInfo() << "IPv6 pinhole on port" << mapping.externalPort
            << "for" << mapping.externalAddress.toString();
    QCOMPARE(mapping.externalPort, kProbePortV6);
    QVERIFY(mapping.externalAddress.isIPv6());

    mapper.stop(/*releaseMappings=*/true);
}

QTEST_MAIN(tst_PortMapLive)
#include "tst_PortMapLive.moc"
