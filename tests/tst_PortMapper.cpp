/// @file tst_PortMapper.cpp
/// @brief Tests for the port-mapping facade: backend race, tie-breaking,
///        desired-state diffing, renewal and release.
///
/// Runs entirely against a fake backend — no sockets, no router. That is the
/// point of PortMapper never touching ListenSocket/ClientUDPSocket directly:
/// the whole state machine is reachable from a unit test.

#include "TestHelpers.h"
#include "portmap/PortMapBackend.h"
#include "portmap/PortMapper.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using namespace eMule;

namespace {

/// A scriptable PortMapBackend. Replies asynchronously (via a zero-timer) so the
/// real queued-signal ordering is exercised rather than a synchronous shortcut.
class FakeBackend : public PortMapBackend {
    Q_OBJECT

public:
    explicit FakeBackend(PortMapMethod method, bool available = true,
                         QObject* parent = nullptr)
        : PortMapBackend(parent), m_method(method), m_available(available)
    {
    }

    [[nodiscard]] PortMapMethod method() const override { return m_method; }
    [[nodiscard]] bool supports(PortMapFamily family) const override
    {
        return family == PortMapFamily::IPv4 || m_supportsIPv6;
    }

    void probe(int) override
    {
        QTimer::singleShot(0, this, [this] { emit probeFinished(m_available, QString()); });
    }

    void requestMapping(const PortMapRequest& request, uint32 lifetimeSecs) override
    {
        ++mapCalls;
        QTimer::singleShot(0, this, [this, request, lifetimeSecs] {
            PortMapping mapping;
            mapping.request = request;
            mapping.method = m_method;
            mapping.lifetimeSecs = m_grantedLifetime > 0 ? m_grantedLifetime : lifetimeSecs;
            mapping.externalAddress = m_externalAddress;
            mapping.externalPort = m_portOffset == 0
                                       ? request.internalPort
                                       : uint16(request.internalPort + m_portOffset);
            if (m_failMappings) {
                emit mappingResult(mapping, false, QStringLiteral("scripted failure"));
                return;
            }
            if (!m_externalAddress.isNull())
                emit externalAddressLearned(m_externalAddress);
            emit mappingResult(mapping, true, QString());
        });
    }

    void releaseMapping(const PortMapping& mapping) override
    {
        released.push_back(mapping.request.purpose);
    }

    void shutdown() override { ++shutdownCalls; }

    // Scripting knobs
    void setAvailable(bool value) { m_available = value; }
    void setPortOffset(int offset) { m_portOffset = offset; }
    void setFailMappings(bool value) { m_failMappings = value; }
    void setExternalAddress(const Address& address) { m_externalAddress = address; }
    void setGrantedLifetime(uint32 secs) { m_grantedLifetime = secs; }
    void setSupportsIPv6(bool value) { m_supportsIPv6 = value; }

    int mapCalls = 0;
    int shutdownCalls = 0;
    std::vector<PortMapPurpose> released;

private:
    PortMapMethod m_method;
    bool    m_available = true;
    bool    m_failMappings = false;
    bool    m_supportsIPv6 = false;
    int     m_portOffset = 0;
    uint32  m_grantedLifetime = 0;
    Address m_externalAddress;
};

PortMapRequest makeRequest(PortMapPurpose purpose, uint16 port,
                           PortMapProtocol protocol = PortMapProtocol::Tcp,
                           PortMapFamily family = PortMapFamily::IPv4)
{
    PortMapRequest r;
    r.purpose = purpose;
    r.protocol = protocol;
    r.family = family;
    r.internalPort = port;
    return r;
}

std::vector<PortMapRequest> defaultDesired()
{
    return {makeRequest(PortMapPurpose::Ed2kTcp, 4662, PortMapProtocol::Tcp),
            makeRequest(PortMapPurpose::Ed2kClientUdp, 4672, PortMapProtocol::Udp)};
}

constexpr const char* kPublicIp = "93.184.216.34";
constexpr const char* kCgnatIp = "100.83.250.167";

} // namespace

class tst_PortMapper : public QObject {
    Q_OBJECT

private slots:
    void singleBackend_mapsAndReportsMapped();
    void race_prefersPcpWhenBothSucceed();
    void race_exactPortMatchBeatsProtocolPreference();
    void race_fallsBackWhenPreferredIsUnavailable();
    void race_preferredMethodIsTriedFirst();
    void race_learnedMethodIsEmittedForPersistence();
    void race_noBackendAvailableReportsNotMapped();
    void race_allMappingsFailReportsFailed();
    void race_losingBackendMappingsAreReleased();

    void status_portMismatchIsDegradedNotMapped();
    void status_cgnatExternalAddressIsDegraded();

    void desired_diffReleasesRemovedAndAddsNew();
    void desired_unchangedEntriesAreNotRemapped();
    void desired_ipv6RequestSkippedByIPv4OnlyBackend();

    void renewal_firesAndRefreshesTheMapping();
    void stop_releasesEachMappingExactlyOnce();
    void stop_withoutReleaseLeavesMappingsAlone();
    void disabled_maskReportsDisabled();

private:
    /// Drive the event loop until the mapper leaves Probing/Trialling.
    static void settle(PortMapper& mapper)
    {
        QTRY_VERIFY_WITH_TIMEOUT(mapper.status() != PortMapStatus::Probing
                                     && mapper.status() != PortMapStatus::Unknown,
                                 2000);
    }
};

// ---------------------------------------------------------------------------

void tst_PortMapper::singleBackend_mapsAndReportsMapped()
{
    PortMapper mapper;
    auto backend = std::make_unique<FakeBackend>(PortMapMethod::UPnP);
    backend->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(backend));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    QCOMPARE(mapper.status(), PortMapStatus::Mapped);
    QCOMPARE(mapper.activeMethod(), PortMapMethod::UPnP);
    QCOMPARE(mapper.mappings().size(), std::size_t(2));
    QCOMPARE(mapper.externalPort(PortMapPurpose::Ed2kTcp, PortMapProtocol::Tcp), uint16(4662));
    QCOMPARE(mapper.externalPort(PortMapPurpose::Ed2kClientUdp, PortMapProtocol::Udp),
             uint16(4672));
    QCOMPARE(mapper.externalAddress().toString(), QString::fromLatin1(kPublicIp));
}

void tst_PortMapper::race_prefersPcpWhenBothSucceed()
{
    PortMapper mapper;
    auto upnp = std::make_unique<FakeBackend>(PortMapMethod::UPnP);
    auto pcp = std::make_unique<FakeBackend>(PortMapMethod::Pcp);
    upnp->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    pcp->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    // Deliberately add UPnP first: preference must come from the ordinal, not
    // from registration order.
    mapper.addBackendForTest(std::move(upnp));
    mapper.addBackendForTest(std::move(pcp));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    QCOMPARE(mapper.activeMethod(), PortMapMethod::Pcp);
    QCOMPARE(mapper.status(), PortMapStatus::Mapped);
}

void tst_PortMapper::race_exactPortMatchBeatsProtocolPreference()
{
    PortMapper mapper;
    auto pcp = std::make_unique<FakeBackend>(PortMapMethod::Pcp);
    auto upnp = std::make_unique<FakeBackend>(PortMapMethod::UPnP);
    // PCP is preferred by ordinal, but grants a different external port — which
    // eD2K cannot advertise, so it would be a silent LowID. UPnP must win.
    pcp->setPortOffset(1000);
    pcp->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    upnp->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(pcp));
    mapper.addBackendForTest(std::move(upnp));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    QCOMPARE(mapper.activeMethod(), PortMapMethod::UPnP);
    QCOMPARE(mapper.externalPort(PortMapPurpose::Ed2kTcp, PortMapProtocol::Tcp), uint16(4662));
    QCOMPARE(mapper.status(), PortMapStatus::Mapped);
}

void tst_PortMapper::race_fallsBackWhenPreferredIsUnavailable()
{
    PortMapper mapper;
    auto pcp = std::make_unique<FakeBackend>(PortMapMethod::Pcp, /*available=*/false);
    auto upnp = std::make_unique<FakeBackend>(PortMapMethod::UPnP);
    upnp->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(pcp));
    mapper.addBackendForTest(std::move(upnp));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    QCOMPARE(mapper.activeMethod(), PortMapMethod::UPnP);
}

void tst_PortMapper::race_preferredMethodIsTriedFirst()
{
    PortMapper mapper;
    auto pcp = std::make_unique<FakeBackend>(PortMapMethod::Pcp);
    auto upnp = std::make_unique<FakeBackend>(PortMapMethod::UPnP);
    auto* pcpRaw = pcp.get();
    auto* upnpRaw = upnp.get();
    pcp->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    upnp->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(pcp));
    mapper.addBackendForTest(std::move(upnp));
    // Last run learned UPnP, so it is tried before PCP and — being exact — wins
    // without PCP ever being asked to map.
    mapper.setPreferredMethod(PortMapMethod::UPnP);
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    QCOMPARE(mapper.activeMethod(), PortMapMethod::UPnP);
    QCOMPARE(upnpRaw->mapCalls, 2);
    QCOMPARE(pcpRaw->mapCalls, 0);
}

void tst_PortMapper::race_learnedMethodIsEmittedForPersistence()
{
    PortMapper mapper;
    QSignalSpy spy(&mapper, &PortMapper::preferredMethodLearned);
    auto pcp = std::make_unique<FakeBackend>(PortMapMethod::Pcp);
    pcp->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(pcp));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<PortMapMethod>(), PortMapMethod::Pcp);
}

void tst_PortMapper::race_noBackendAvailableReportsNotMapped()
{
    PortMapper mapper;
    mapper.addBackendForTest(
        std::make_unique<FakeBackend>(PortMapMethod::Pcp, /*available=*/false));
    mapper.addBackendForTest(
        std::make_unique<FakeBackend>(PortMapMethod::UPnP, /*available=*/false));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    QCOMPARE(mapper.status(), PortMapStatus::NotMapped);
    QCOMPARE(mapper.activeMethod(), PortMapMethod::None);
    QVERIFY(mapper.mappings().empty());
}

void tst_PortMapper::race_allMappingsFailReportsFailed()
{
    PortMapper mapper;
    auto backend = std::make_unique<FakeBackend>(PortMapMethod::UPnP);
    backend->setFailMappings(true);
    mapper.addBackendForTest(std::move(backend));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    QCOMPARE(mapper.status(), PortMapStatus::Failed);
    QVERIFY(mapper.mappings().empty());
}

void tst_PortMapper::race_losingBackendMappingsAreReleased()
{
    PortMapper mapper;
    auto pcp = std::make_unique<FakeBackend>(PortMapMethod::Pcp);
    auto upnp = std::make_unique<FakeBackend>(PortMapMethod::UPnP);
    auto* pcpRaw = pcp.get();
    pcp->setPortOffset(1000);   // will lose to UPnP's exact match
    pcp->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    upnp->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(pcp));
    mapper.addBackendForTest(std::move(upnp));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    QCOMPARE(mapper.activeMethod(), PortMapMethod::UPnP);
    // The loser's mappings must not be left behind on the router.
    QCOMPARE(pcpRaw->released.size(), std::size_t(2));
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

void tst_PortMapper::status_portMismatchIsDegradedNotMapped()
{
    PortMapper mapper;
    auto backend = std::make_unique<FakeBackend>(PortMapMethod::Pcp);
    backend->setPortOffset(1000);
    backend->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(backend));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    // Granted, but eD2K can only advertise the internal port, so this is a
    // LowID in practice and must not be reported as success.
    QCOMPARE(mapper.status(), PortMapStatus::Degraded);
    QCOMPARE(mapper.externalPort(PortMapPurpose::Ed2kTcp, PortMapProtocol::Tcp), uint16(5662));
}

void tst_PortMapper::status_cgnatExternalAddressIsDegraded()
{
    PortMapper mapper;
    auto backend = std::make_unique<FakeBackend>(PortMapMethod::Pcp);
    // Exactly what the dev FRITZ!Box reports: the mapping succeeds while the
    // port stays unreachable from the Internet.
    backend->setExternalAddress(Address::fromString(QString::fromLatin1(kCgnatIp)));
    mapper.addBackendForTest(std::move(backend));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    QCOMPARE(mapper.status(), PortMapStatus::Degraded);
}

// ---------------------------------------------------------------------------
// Desired-state diffing
// ---------------------------------------------------------------------------

void tst_PortMapper::desired_diffReleasesRemovedAndAddsNew()
{
    PortMapper mapper;
    auto backend = std::make_unique<FakeBackend>(PortMapMethod::UPnP);
    auto* raw = backend.get();
    backend->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(backend));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);
    QCOMPARE(mapper.mappings().size(), std::size_t(2));

    // Drop the UDP mapping, add the web-server one.
    mapper.setDesiredMappings({makeRequest(PortMapPurpose::Ed2kTcp, 4662),
                               makeRequest(PortMapPurpose::WebServer, 4711)});
    QTRY_COMPARE(mapper.mappings().size(), std::size_t(2));

    QCOMPARE(raw->released.size(), std::size_t(1));
    QCOMPARE(raw->released.front(), PortMapPurpose::Ed2kClientUdp);
    QCOMPARE(mapper.externalPort(PortMapPurpose::WebServer, PortMapProtocol::Tcp), uint16(4711));
    QCOMPARE(mapper.externalPort(PortMapPurpose::Ed2kClientUdp, PortMapProtocol::Udp), uint16(0));
}

void tst_PortMapper::desired_unchangedEntriesAreNotRemapped()
{
    PortMapper mapper;
    auto backend = std::make_unique<FakeBackend>(PortMapMethod::UPnP);
    auto* raw = backend.get();
    backend->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(backend));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);
    const int callsAfterStart = raw->mapCalls;

    // Re-declaring the same desired state must be a no-op on the wire.
    mapper.setDesiredMappings(defaultDesired());
    QTest::qWait(30);
    QCOMPARE(raw->mapCalls, callsAfterStart);
    QVERIFY(raw->released.empty());
}

void tst_PortMapper::desired_ipv6RequestSkippedByIPv4OnlyBackend()
{
    PortMapper mapper;
    auto backend = std::make_unique<FakeBackend>(PortMapMethod::NatPmp);
    backend->setSupportsIPv6(false);   // NAT-PMP is IPv4-only by specification
    backend->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(backend));
    mapper.setDesiredMappings(
        {makeRequest(PortMapPurpose::Ed2kTcp, 4662, PortMapProtocol::Tcp),
         makeRequest(PortMapPurpose::Ed2kTcp, 4662, PortMapProtocol::Tcp,
                     PortMapFamily::IPv6)});
    mapper.start();
    settle(mapper);

    QCOMPARE(mapper.mappings().size(), std::size_t(1));
    QCOMPARE(mapper.externalPort(PortMapPurpose::Ed2kTcp, PortMapProtocol::Tcp,
                                 PortMapFamily::IPv6),
             uint16(0));
}

// ---------------------------------------------------------------------------
// Renewal and teardown
// ---------------------------------------------------------------------------

void tst_PortMapper::renewal_firesAndRefreshesTheMapping()
{
    PortMapper mapper;
    auto backend = std::make_unique<FakeBackend>(PortMapMethod::Pcp);
    auto* raw = backend.get();
    backend->setGrantedLifetime(7200);
    backend->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(backend));
    mapper.setRenewalRandomForTest(0.0);
    mapper.setRenewalOverrideMsForTest(20);
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    const int callsAfterStart = raw->mapCalls;
    QTRY_VERIFY_WITH_TIMEOUT(raw->mapCalls > callsAfterStart, 2000);
    // Still healthy after renewing — the mapping is refreshed, not duplicated.
    QCOMPARE(mapper.mappings().size(), std::size_t(2));
    QCOMPARE(mapper.status(), PortMapStatus::Mapped);
}

void tst_PortMapper::stop_releasesEachMappingExactlyOnce()
{
    PortMapper mapper;
    auto backend = std::make_unique<FakeBackend>(PortMapMethod::UPnP);
    auto* raw = backend.get();
    backend->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(backend));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    mapper.stop(/*releaseMappings=*/true);
    QCOMPARE(raw->released.size(), std::size_t(2));
    QCOMPARE(raw->shutdownCalls, 1);
    QVERIFY(mapper.mappings().empty());
}

void tst_PortMapper::stop_withoutReleaseLeavesMappingsAlone()
{
    PortMapper mapper;
    auto backend = std::make_unique<FakeBackend>(PortMapMethod::UPnP);
    auto* raw = backend.get();
    backend->setExternalAddress(Address::fromString(QString::fromLatin1(kPublicIp)));
    mapper.addBackendForTest(std::move(backend));
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();
    settle(mapper);

    // closeUPnPOnExit=false: leave the mappings in place so a restart reclaims
    // the same ports instead of racing another host for them.
    mapper.stop(/*releaseMappings=*/false);
    QVERIFY(raw->released.empty());
    QVERIFY(mapper.mappings().empty());
}

void tst_PortMapper::disabled_maskReportsDisabled()
{
    PortMapper mapper;
    mapper.setEnabledMethods(0);
    mapper.setDesiredMappings(defaultDesired());
    mapper.start();

    QCOMPARE(mapper.status(), PortMapStatus::Disabled);
    QVERIFY(mapper.mappings().empty());
}

QTEST_MAIN(tst_PortMapper)
#include "tst_PortMapper.moc"
