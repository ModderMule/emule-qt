/// @file tst_DefaultGateway.cpp
/// @brief Tests for net/DefaultGateway — /proc route parsing and next-hop selection.
///
/// Everything here is pure: the platform gatherers are excluded on purpose, so the
/// suite behaves identically on macOS, Windows and Linux. The selection rule is
/// shared by all three backends, so exercising it here covers the decision even on
/// the platforms whose extraction cannot be tested offline.

#include "TestHelpers.h"
#include "net/DefaultGateway.h"

#include <QTest>

using namespace eMule;

namespace {

RouteEntry makeRoute(const char* gateway,
                     uint8 prefixLength = 0,
                     const char* ifName = "en0",
                     uint32 metric = 0,
                     bool isUp = true,
                     bool isGateway = true)
{
    RouteEntry e;
    e.gateway = Address::fromString(QString::fromLatin1(gateway));
    e.prefixLength = prefixLength;
    e.interfaceName = QString::fromLatin1(ifName);
    e.metric = metric;
    e.isUp = isUp;
    e.isGateway = isGateway;
    return e;
}

// Real values from the dev network, so the fixtures mirror production.
constexpr const char* kFritzBoxV4 = "192.168.178.1";
constexpr const char* kFritzBoxV6 = "fe80::36e1:a9ff:fe2d:2db2";

/// A /proc/net/route fixture. Columns are tab-separated, values little-endian hex.
/// 0102A8C0 == 192.168.2.1, 0000FEA9 == 169.254.0.0, 00FFFFFF == 255.255.255.0.
constexpr const char* kProcNetRoute =
    "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n"
    "eth0\t00000000\t0102A8C0\t0003\t0\t0\t100\t00000000\t0\t0\t0\n"
    "eth0\t0002A8C0\t00000000\t0001\t0\t0\t100\t00FFFFFF\t0\t0\t0\n"
    "wlan0\t00000000\t0104A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0\n";

/// A /proc/net/ipv6_route fixture: a default route via a link-local next hop,
/// then an on-link /64, then a default route whose next hop is bare fe80::.
constexpr const char* kProcNetIpv6Route =
    "00000000000000000000000000000000 00 "
    "00000000000000000000000000000000 00 "
    "fe8000000000000036e1a9fffe2d2db2 00000400 00000000 00000000 00000003 eth0\n"
    "2a0d334415db32000000000000000000 40 "
    "00000000000000000000000000000000 00 "
    "00000000000000000000000000000000 00000100 00000000 00000000 00000001 eth0\n"
    "00000000000000000000000000000000 00 "
    "00000000000000000000000000000000 00 "
    "fe800000000000000000000000000000 00000800 00000000 00000000 00000003 utun0\n";

} // namespace

class tst_DefaultGateway : public QObject {
    Q_OBJECT

private slots:
    // -- GatewayCandidate --------------------------------------------------
    void candidate_reattachesScopeIdForLinkLocal();
    void candidate_leavesIPv4Alone();
    void candidate_nullFormatsEmpty();

    // -- /proc/net/route ---------------------------------------------------
    void procRoute_parsesDefaultRoute();
    void procRoute_decodesLittleEndianHex();
    void procRoute_skipsHeaderRow();
    void procRoute_readsMetricAndPrefixLength();
    void procRoute_ignoresMalformedLines();

    // -- /proc/net/ipv6_route ----------------------------------------------
    void procIpv6Route_parsesLinkLocalNextHop();
    void procIpv6Route_readsPrefixLengthAsHex();

    // -- selection ---------------------------------------------------------
    void select_keepsOnlyDefaultRoutes();
    void select_ordersByMetric();
    void select_rejectsDownAndNonGatewayRoutes();
    void select_rejectsWrongFamily();
    void select_rejectsUnspecifiedNextHop();
    void select_rejectsLinkLocalWithZeroInterfaceId();
    void select_rejectsLinkLocalWithoutInterfaceName();
    void select_keepsGlobalIPv6WithoutInterfaceName();
    void select_deduplicates();
    void select_emptyInputYieldsNoGuess();
    void select_endToEndFromProcFixtures();

    // -- platform backend --------------------------------------------------
    void platform_lookupIsSaneOrEmpty();
};

// ---------------------------------------------------------------------------
// GatewayCandidate
// ---------------------------------------------------------------------------

void tst_DefaultGateway::candidate_reattachesScopeIdForLinkLocal()
{
    GatewayCandidate c;
    c.address = Address::fromString(QString::fromLatin1(kFritzBoxV6));
    c.interfaceName = QStringLiteral("en0");

    // The probe against the real FRITZ!Box showed that dropping the zone index
    // makes a link-local next hop unreachable, so this round-trip is load-bearing.
    QCOMPARE(c.toQHostAddress().scopeId(), QStringLiteral("en0"));
    QCOMPARE(c.toString(), QStringLiteral("fe80::36e1:a9ff:fe2d:2db2%en0"));
}

void tst_DefaultGateway::candidate_leavesIPv4Alone()
{
    GatewayCandidate c;
    c.address = Address::fromString(QString::fromLatin1(kFritzBoxV4));
    c.interfaceName = QStringLiteral("en0");

    QVERIFY(c.toQHostAddress().scopeId().isEmpty());
    QCOMPARE(c.toString(), QString::fromLatin1(kFritzBoxV4));
}

void tst_DefaultGateway::candidate_nullFormatsEmpty()
{
    GatewayCandidate c;
    QVERIFY(c.isNull());
    QVERIFY(c.toString().isEmpty());
}

// ---------------------------------------------------------------------------
// /proc/net/route
// ---------------------------------------------------------------------------

void tst_DefaultGateway::procRoute_parsesDefaultRoute()
{
    const auto routes = parseProcNetRoute(QString::fromLatin1(kProcNetRoute));
    QCOMPARE(routes.size(), std::size_t(3));
    QCOMPARE(routes[0].interfaceName, QStringLiteral("eth0"));
    QVERIFY(routes[0].isUp);
    QVERIFY(routes[0].isGateway);
    QCOMPARE(routes[0].prefixLength, uint8(0));
}

void tst_DefaultGateway::procRoute_decodesLittleEndianHex()
{
    const auto routes = parseProcNetRoute(QString::fromLatin1(kProcNetRoute));
    QVERIFY(!routes.empty());
    // "0102A8C0" is 192.168.2.1, not 1.2.168.192 — the kernel prints a __be32
    // with %08X, so the digits are byte-reversed relative to dotted notation.
    QCOMPARE(routes[0].gateway.toString(), QStringLiteral("192.168.2.1"));
    QCOMPARE(routes[1].destination.toString(), QStringLiteral("192.168.2.0"));
}

void tst_DefaultGateway::procRoute_skipsHeaderRow()
{
    const auto routes = parseProcNetRoute(QString::fromLatin1(kProcNetRoute));
    for (const RouteEntry& r : routes)
        QVERIFY(r.interfaceName != QStringLiteral("Iface"));
}

void tst_DefaultGateway::procRoute_readsMetricAndPrefixLength()
{
    const auto routes = parseProcNetRoute(QString::fromLatin1(kProcNetRoute));
    QCOMPARE(routes[0].metric, uint32(100));
    QCOMPARE(routes[2].metric, uint32(600));
    QCOMPARE(routes[1].prefixLength, uint8(24));   // mask 255.255.255.0
}

void tst_DefaultGateway::procRoute_ignoresMalformedLines()
{
    QVERIFY(parseProcNetRoute(QStringLiteral("")).empty());
    QVERIFY(parseProcNetRoute(QStringLiteral("garbage\n")).empty());
    QVERIFY(parseProcNetRoute(QStringLiteral("eth0\tzzzz\t0102A8C0\t0003\t0\t0\t0\t0\n")).empty());
    // Too few columns.
    QVERIFY(parseProcNetRoute(QStringLiteral("eth0\t00000000\t0102A8C0\n")).empty());
}

// ---------------------------------------------------------------------------
// /proc/net/ipv6_route
// ---------------------------------------------------------------------------

void tst_DefaultGateway::procIpv6Route_parsesLinkLocalNextHop()
{
    const auto routes = parseProcNetIpv6Route(QString::fromLatin1(kProcNetIpv6Route));
    QCOMPARE(routes.size(), std::size_t(3));
    QCOMPARE(routes[0].gateway.toString(), QString::fromLatin1(kFritzBoxV6));
    QCOMPARE(routes[0].interfaceName, QStringLiteral("eth0"));
    QVERIFY(routes[0].isUp);
    QVERIFY(routes[0].isGateway);
}

void tst_DefaultGateway::procIpv6Route_readsPrefixLengthAsHex()
{
    const auto routes = parseProcNetIpv6Route(QString::fromLatin1(kProcNetIpv6Route));
    QCOMPARE(routes[0].prefixLength, uint8(0));
    QCOMPARE(routes[1].prefixLength, uint8(0x40));   // "40" is hex, so 64
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void tst_DefaultGateway::select_keepsOnlyDefaultRoutes()
{
    const std::vector<RouteEntry> routes = {
        makeRoute(kFritzBoxV4, /*prefixLength=*/24),
        makeRoute("192.168.178.254", /*prefixLength=*/0),
    };
    const auto picked = selectDefaultGateways(routes, Address::Family::IPv4);
    QCOMPARE(picked.size(), std::size_t(1));
    QCOMPARE(picked[0].address.toString(), QStringLiteral("192.168.178.254"));
}

void tst_DefaultGateway::select_ordersByMetric()
{
    const std::vector<RouteEntry> routes = {
        makeRoute("192.168.1.1", 0, "wlan0", /*metric=*/600),
        makeRoute(kFritzBoxV4, 0, "en0", /*metric=*/100),
        makeRoute("192.168.2.1", 0, "en1", /*metric=*/300),
    };
    const auto picked = selectDefaultGateways(routes, Address::Family::IPv4);
    QCOMPARE(picked.size(), std::size_t(3));
    QCOMPARE(picked[0].address.toString(), QString::fromLatin1(kFritzBoxV4));
    QCOMPARE(picked[1].address.toString(), QStringLiteral("192.168.2.1"));
    QCOMPARE(picked[2].address.toString(), QStringLiteral("192.168.1.1"));
}

void tst_DefaultGateway::select_rejectsDownAndNonGatewayRoutes()
{
    const std::vector<RouteEntry> down = {
        makeRoute(kFritzBoxV4, 0, "en0", 0, /*isUp=*/false, /*isGateway=*/true)};
    QVERIFY(selectDefaultGateways(down, Address::Family::IPv4).empty());

    const std::vector<RouteEntry> onLink = {
        makeRoute(kFritzBoxV4, 0, "en0", 0, /*isUp=*/true, /*isGateway=*/false)};
    QVERIFY(selectDefaultGateways(onLink, Address::Family::IPv4).empty());
}

void tst_DefaultGateway::select_rejectsWrongFamily()
{
    const std::vector<RouteEntry> routes = {makeRoute(kFritzBoxV4)};
    QVERIFY(selectDefaultGateways(routes, Address::Family::IPv6).empty());
    QCOMPARE(selectDefaultGateways(routes, Address::Family::IPv4).size(), std::size_t(1));
}

void tst_DefaultGateway::select_rejectsUnspecifiedNextHop()
{
    // Address::fromString("0.0.0.0") yields a null Address, and an all-zero IPv6
    // is a real value that must still be refused.
    const std::vector<RouteEntry> routes = {makeRoute("::", 0, "eth0")};
    QVERIFY(selectDefaultGateways(routes, Address::Family::IPv6).empty());
}

void tst_DefaultGateway::select_rejectsLinkLocalWithZeroInterfaceId()
{
    // `fe80::%utunN` names an interface rather than a router; probing it would
    // send our port layout to nobody useful.
    const std::vector<RouteEntry> routes = {makeRoute("fe80::", 0, "utun0")};
    QVERIFY(selectDefaultGateways(routes, Address::Family::IPv6).empty());
}

void tst_DefaultGateway::select_rejectsLinkLocalWithoutInterfaceName()
{
    // Unreachable without a zone index, so it is unusable rather than merely
    // lower quality.
    const std::vector<RouteEntry> routes = {makeRoute(kFritzBoxV6, 0, "")};
    QVERIFY(selectDefaultGateways(routes, Address::Family::IPv6).empty());
}

void tst_DefaultGateway::select_keepsGlobalIPv6WithoutInterfaceName()
{
    const std::vector<RouteEntry> routes = {makeRoute("2a0d:3344:15db:3200::1", 0, "")};
    QCOMPARE(selectDefaultGateways(routes, Address::Family::IPv6).size(), std::size_t(1));
}

void tst_DefaultGateway::select_deduplicates()
{
    const std::vector<RouteEntry> routes = {
        makeRoute(kFritzBoxV4, 0, "en0", 100),
        makeRoute(kFritzBoxV4, 0, "en0", 100),
    };
    QCOMPARE(selectDefaultGateways(routes, Address::Family::IPv4).size(), std::size_t(1));

    // Same address on a different interface is a genuinely different next hop.
    const std::vector<RouteEntry> twoLinks = {
        makeRoute(kFritzBoxV6, 0, "en0", 100),
        makeRoute(kFritzBoxV6, 0, "en1", 100),
    };
    QCOMPARE(selectDefaultGateways(twoLinks, Address::Family::IPv6).size(), std::size_t(2));
}

void tst_DefaultGateway::select_emptyInputYieldsNoGuess()
{
    // An empty result must mean "do not send" — never a synthesized .1-of-our-/24.
    QVERIFY(selectDefaultGateways({}, Address::Family::IPv4).empty());
    QVERIFY(selectDefaultGateways({}, Address::Family::IPv6).empty());
}

void tst_DefaultGateway::select_endToEndFromProcFixtures()
{
    const auto v4 = selectDefaultGateways(
        parseProcNetRoute(QString::fromLatin1(kProcNetRoute)), Address::Family::IPv4);
    QCOMPARE(v4.size(), std::size_t(2));
    QCOMPARE(v4[0].address.toString(), QStringLiteral("192.168.2.1"));   // metric 100
    QCOMPARE(v4[1].address.toString(), QStringLiteral("192.168.4.1"));   // metric 600

    // The utun0 row carries a bare fe80:: next hop and must be dropped, leaving
    // only the real router.
    const auto v6 = selectDefaultGateways(
        parseProcNetIpv6Route(QString::fromLatin1(kProcNetIpv6Route)), Address::Family::IPv6);
    QCOMPARE(v6.size(), std::size_t(1));
    QCOMPARE(v6[0].address.toString(), QString::fromLatin1(kFritzBoxV6));
    QCOMPARE(v6[0].interfaceName, QStringLiteral("eth0"));
}

// ---------------------------------------------------------------------------
// Platform backend
// ---------------------------------------------------------------------------

/// Whatever the real routing table says, every candidate must satisfy the
/// invariants the PCP/NAT-PMP backends rely on. Asserts nothing about the
/// specific addresses, so it stays green on a CI runner with no default route.
void tst_DefaultGateway::platform_lookupIsSaneOrEmpty()
{
    for (const Address::Family family : {Address::Family::IPv4, Address::Family::IPv6}) {
        for (const GatewayCandidate& c : defaultGateways(family)) {
            qInfo() << "gateway candidate:" << c.toString() << "metric" << c.metric;
            QVERIFY(!c.address.isNull());
            QCOMPARE(c.address.family(), family);

            if (!c.address.isIPv6())
                continue;

            const auto& bytes = c.address.ipv6Bytes();
            const bool linkLocal = bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0x80;
            if (!linkLocal)
                continue;

            // A link-local next hop is useless without its zone index...
            QVERIFY(!c.interfaceName.isEmpty());
            QVERIFY(!c.toQHostAddress().scopeId().isEmpty());
            // ...and the KAME embedded scope id must have been stripped out of
            // bytes 2-3, or we would be addressing something like fe80:8:: that
            // no host answers.
            QCOMPARE(bytes[2], uint8(0));
            QCOMPARE(bytes[3], uint8(0));
        }
    }
}

QTEST_MAIN(tst_DefaultGateway)
#include "tst_DefaultGateway.moc"
