/// @file tst_Address.cpp
/// @brief Tests for net/Address — construction, comparison, hashing, validation, round-trips.

#include "TestHelpers.h"
#include "net/Address.h"

#include <QTest>

#include <unordered_set>

using namespace eMule;

class tst_Address : public QObject {
    Q_OBJECT

private slots:
    // Address construction
    void defaultAddress_isNull();
    void fromHostOrder_basic();
    void fromHostOrder_zero_isNull();
    void fromNetworkOrder_basic();
    void fromNetworkOrder_zero_isNull();
    void fromQHostAddress_ipv4();
    void fromQHostAddress_ipv6();
    void fromQHostAddress_mappedV4();
    void fromQHostAddress_null();
    void fromString_ipv4();
    void fromString_ipv6();
    void fromString_invalid();
    void fromIPv6Bytes();

    // Address round-trips
    void roundTrip_hostOrder();
    void roundTrip_networkOrder();
    void roundTrip_qHostAddress_ipv4();
    void roundTrip_qHostAddress_ipv6();
    void roundTrip_string_ipv4();
    void roundTrip_string_ipv6();

    // Address extraction
    void toUint32_ipv6_returnsZero();
    void toNetworkUint32_ipv6_returnsZero();
    void toString_null_returnsEmpty();

    // Address comparison
    void equality_same();
    void equality_different();
    void equality_differentFamily();
    void ordering_ipv4();
    void ordering_familyPrecedence();

    // Address hashing
    void hash_consistency();
    void hash_differentValues();
    void hash_unorderedSet();

    // Address validation
    void isRoutable_null();
    void isRoutable_lan();
    void isRoutable_lan_forced();
    void isRoutable_zeroFirst();
    void isRoutable_public();
    void isLan_ipv4_private();
    void isLan_ipv4_public();
    void isLan_ipv6_loopback();
    void isLan_ipv6_linkLocal();
    void isLan_ipv6_uniqueLocal();
    void isLan_ipv6_global();

    // isPublicIP — comprehensive reserved range checks
    void isPublicIP_ipv4_multicast();
    void isPublicIP_ipv4_reserved240();
    void isPublicIP_ipv4_cgnat();
    void isPublicIP_ipv4_testNets();
    void isPublicIP_ipv4_benchmarking();
    void isPublicIP_ipv6_teredo();
    void isPublicIP_ipv6_documentation();
    void isPublicIP_ipv6_6to4();
    void isPublicIP_ipv6_multicast();
    void isPublicIP_ipv6_srv6();
    void isPublicIP_ipv6_global_ok();

    // IPv4 ↔ IPv6 mapped conversion
    void toIPv6Mapped_basic();
    void toIPv6Mapped_null();
    void toIPv4_fromMapped();
    void toIPv4_fromNonMapped();
    void toIPv4_alreadyV4();
    void conversion_roundTrip();

    // Endpoint construction
    void endpoint_default_isNull();
    void endpoint_fromHostOrder();
    void endpoint_fromNetworkOrder();
    void endpoint_toString_ipv4();
    void endpoint_toString_ipv6();
    void endpoint_comparison();
    void endpoint_hash();

    // ipstr overloads
    void ipstr_address();
    void ipstr_endpoint();
};

// ===========================================================================
// Address construction
// ===========================================================================

void tst_Address::defaultAddress_isNull()
{
    Address a;
    QVERIFY(a.isNull());
    QVERIFY(!a.isIPv4());
    QVERIFY(!a.isIPv6());
    QCOMPARE(a.family(), Address::Family::None);
}

void tst_Address::fromHostOrder_basic()
{
    // 192.168.1.1 in HBO = (192 << 24) | (168 << 16) | (1 << 8) | 1
    constexpr uint32 ip = 0xC0A80101u;
    auto a = Address::fromHostOrder(ip);
    QVERIFY(a.isIPv4());
    QVERIFY(!a.isNull());
    QCOMPARE(a.toUint32(), ip);
}

void tst_Address::fromHostOrder_zero_isNull()
{
    auto a = Address::fromHostOrder(0);
    QVERIFY(a.isNull());
}

void tst_Address::fromNetworkOrder_basic()
{
    // 10.0.0.1 in NBO (little-endian x86): bytes 10, 0, 0, 1 → 0x0100000A on LE
    uint32 nbo = htonl(0x0A000001u); // 10.0.0.1
    auto a = Address::fromNetworkOrder(nbo);
    QVERIFY(a.isIPv4());
    QCOMPARE(a.toUint32(), uint32(0x0A000001u)); // HBO
    QCOMPARE(a.toNetworkUint32(), nbo);
}

void tst_Address::fromNetworkOrder_zero_isNull()
{
    auto a = Address::fromNetworkOrder(0);
    QVERIFY(a.isNull());
}

void tst_Address::fromQHostAddress_ipv4()
{
    QHostAddress ha(QStringLiteral("172.16.0.1"));
    auto a = Address::fromQHostAddress(ha);
    QVERIFY(a.isIPv4());
    QCOMPARE(a.toUint32(), uint32(0xAC100001u));
}

void tst_Address::fromQHostAddress_ipv6()
{
    QHostAddress ha(QStringLiteral("2001:db8::1"));
    auto a = Address::fromQHostAddress(ha);
    QVERIFY(a.isIPv6());
    QVERIFY(!a.isIPv4());
}

void tst_Address::fromQHostAddress_mappedV4()
{
    // ::ffff:192.168.1.1 should normalize to plain IPv4
    QHostAddress ha(QStringLiteral("::ffff:192.168.1.1"));
    auto a = Address::fromQHostAddress(ha);
    QVERIFY(a.isIPv4());
    QCOMPARE(a.toUint32(), uint32(0xC0A80101u));
}

void tst_Address::fromQHostAddress_null()
{
    auto a = Address::fromQHostAddress(QHostAddress());
    QVERIFY(a.isNull());
}

void tst_Address::fromString_ipv4()
{
    auto a = Address::fromString(QStringLiteral("8.8.8.8"));
    QVERIFY(a.isIPv4());
    QCOMPARE(a.toUint32(), uint32(0x08080808u));
}

void tst_Address::fromString_ipv6()
{
    auto a = Address::fromString(QStringLiteral("::1"));
    QVERIFY(a.isIPv6());
}

void tst_Address::fromString_invalid()
{
    auto a = Address::fromString(QStringLiteral("not-an-ip"));
    QVERIFY(a.isNull());
}

void tst_Address::fromIPv6Bytes()
{
    // ::1
    uint8 bytes[16] = {};
    bytes[15] = 1;
    auto a = Address::fromIPv6Bytes(bytes);
    QVERIFY(a.isIPv6());
    QCOMPARE(a.ipv6Bytes()[15], uint8(1));
}

// ===========================================================================
// Round-trips
// ===========================================================================

void tst_Address::roundTrip_hostOrder()
{
    constexpr uint32 ip = 0xDEADBEEFu;
    QCOMPARE(Address::fromHostOrder(ip).toUint32(), ip);
}

void tst_Address::roundTrip_networkOrder()
{
    const uint32 nbo = htonl(0x01020304u);
    QCOMPARE(Address::fromNetworkOrder(nbo).toNetworkUint32(), nbo);
}

void tst_Address::roundTrip_qHostAddress_ipv4()
{
    QHostAddress orig(QStringLiteral("1.2.3.4"));
    auto a = Address::fromQHostAddress(orig);
    QCOMPARE(a.toQHostAddress(), orig);
}

void tst_Address::roundTrip_qHostAddress_ipv6()
{
    QHostAddress orig(QStringLiteral("2001:db8::1"));
    auto a = Address::fromQHostAddress(orig);
    QCOMPARE(a.toQHostAddress(), orig);
}

void tst_Address::roundTrip_string_ipv4()
{
    const QString s = QStringLiteral("1.2.3.4");
    QCOMPARE(Address::fromString(s).toString(), s);
}

void tst_Address::roundTrip_string_ipv6()
{
    const QString s = QStringLiteral("2001:db8::1");
    auto a = Address::fromString(s);
    // Qt may normalize, so compare via QHostAddress
    QCOMPARE(QHostAddress(a.toString()), QHostAddress(s));
}

// ===========================================================================
// Extraction edge cases
// ===========================================================================

void tst_Address::toUint32_ipv6_returnsZero()
{
    auto a = Address::fromString(QStringLiteral("::1"));
    QCOMPARE(a.toUint32(), uint32(0));
}

void tst_Address::toNetworkUint32_ipv6_returnsZero()
{
    auto a = Address::fromString(QStringLiteral("::1"));
    QCOMPARE(a.toNetworkUint32(), uint32(0));
}

void tst_Address::toString_null_returnsEmpty()
{
    QVERIFY(Address().toString().isEmpty());
}

// ===========================================================================
// Comparison
// ===========================================================================

void tst_Address::equality_same()
{
    auto a = Address::fromHostOrder(0x01020304u);
    auto b = Address::fromHostOrder(0x01020304u);
    QCOMPARE(a, b);
}

void tst_Address::equality_different()
{
    auto a = Address::fromHostOrder(0x01020304u);
    auto b = Address::fromHostOrder(0x04030201u);
    QVERIFY(a != b);
}

void tst_Address::equality_differentFamily()
{
    // IPv4 127.0.0.1 != IPv6 ::1
    auto v4 = Address::fromString(QStringLiteral("127.0.0.1"));
    auto v6 = Address::fromString(QStringLiteral("::1"));
    QVERIFY(v4 != v6);
}

void tst_Address::ordering_ipv4()
{
    auto a = Address::fromHostOrder(1);
    auto b = Address::fromHostOrder(2);
    QVERIFY(a < b);
    QVERIFY(!(b < a));
}

void tst_Address::ordering_familyPrecedence()
{
    // None < IPv4 < IPv6
    Address none;
    auto v4 = Address::fromHostOrder(1);
    auto v6 = Address::fromString(QStringLiteral("::1"));
    QVERIFY(none < v4);
    QVERIFY(v4 < v6);
}

// ===========================================================================
// Hashing
// ===========================================================================

void tst_Address::hash_consistency()
{
    auto a = Address::fromHostOrder(0xAABBCCDDu);
    QCOMPARE(a.hash(), a.hash());
}

void tst_Address::hash_differentValues()
{
    auto a = Address::fromHostOrder(1);
    auto b = Address::fromHostOrder(2);
    // Not guaranteed, but highly likely for distinct small values
    QVERIFY(a.hash() != b.hash());
}

void tst_Address::hash_unorderedSet()
{
    std::unordered_set<Address> s;
    s.insert(Address::fromHostOrder(1));
    s.insert(Address::fromHostOrder(2));
    s.insert(Address::fromHostOrder(1)); // duplicate
    QCOMPARE(s.size(), std::size_t(2));
}

// ===========================================================================
// Validation
// ===========================================================================

void tst_Address::isRoutable_null()
{
    QVERIFY(!Address().isRoutable());
}

void tst_Address::isRoutable_lan()
{
    QVERIFY(!Address::fromHostOrder(0x0A000001u).isRoutable()); // 10.0.0.1
    QVERIFY(!Address::fromHostOrder(0xC0A80101u).isRoutable()); // 192.168.1.1
    QVERIFY(!Address::fromHostOrder(0x7F000001u).isRoutable()); // 127.0.0.1
}

void tst_Address::isRoutable_lan_forced()
{
    QVERIFY(Address::fromHostOrder(0x0A000001u).isRoutable(true)); // 10.0.0.1 with allowLan
}

void tst_Address::isRoutable_zeroFirst()
{
    // 0.1.2.3 — first octet zero
    QVERIFY(!Address::fromHostOrder(0x00010203u).isRoutable(true));
}

void tst_Address::isRoutable_public()
{
    QVERIFY(Address::fromHostOrder(0x08080808u).isRoutable()); // 8.8.8.8
}

void tst_Address::isLan_ipv4_private()
{
    QVERIFY(Address::fromHostOrder(0x0A000001u).isLan());  // 10.0.0.1
    QVERIFY(Address::fromHostOrder(0xAC100001u).isLan());  // 172.16.0.1
    QVERIFY(Address::fromHostOrder(0xC0A80001u).isLan());  // 192.168.0.1
    QVERIFY(Address::fromHostOrder(0x7F000001u).isLan());  // 127.0.0.1
    QVERIFY(Address::fromHostOrder(0xA9FE0001u).isLan());  // 169.254.0.1
}

void tst_Address::isLan_ipv4_public()
{
    QVERIFY(!Address::fromHostOrder(0x08080808u).isLan()); // 8.8.8.8
}

void tst_Address::isLan_ipv6_loopback()
{
    QVERIFY(Address::fromString(QStringLiteral("::1")).isLan());
}

void tst_Address::isLan_ipv6_linkLocal()
{
    QVERIFY(Address::fromString(QStringLiteral("fe80::1")).isLan());
}

void tst_Address::isLan_ipv6_uniqueLocal()
{
    QVERIFY(Address::fromString(QStringLiteral("fd00::1")).isLan());
}

void tst_Address::isLan_ipv6_global()
{
    QVERIFY(!Address::fromString(QStringLiteral("2001:db8::1")).isLan());
}

// ===========================================================================
// Endpoint
// ===========================================================================

void tst_Address::endpoint_default_isNull()
{
    Endpoint ep;
    QVERIFY(ep.isNull());
}

void tst_Address::endpoint_fromHostOrder()
{
    auto ep = Endpoint::fromHostOrder(0x08080808u, 4661);
    QCOMPARE(ep.address().toUint32(), uint32(0x08080808u));
    QCOMPARE(ep.port(), uint16(4661));
}

void tst_Address::endpoint_fromNetworkOrder()
{
    const uint32 nbo = htonl(0x08080808u);
    auto ep = Endpoint::fromNetworkOrder(nbo, 4661);
    QCOMPARE(ep.address().toUint32(), uint32(0x08080808u));
    QCOMPARE(ep.port(), uint16(4661));
}

void tst_Address::endpoint_toString_ipv4()
{
    auto ep = Endpoint::fromHostOrder(0x01020304u, 8080);
    QCOMPARE(ep.toString(), QStringLiteral("1.2.3.4:8080"));
}

void tst_Address::endpoint_toString_ipv6()
{
    auto ep = Endpoint(Address::fromString(QStringLiteral("::1")), 443);
    QCOMPARE(ep.toString(), QStringLiteral("[::1]:443"));
}

void tst_Address::endpoint_comparison()
{
    auto a = Endpoint::fromHostOrder(1, 80);
    auto b = Endpoint::fromHostOrder(1, 81);
    auto c = Endpoint::fromHostOrder(2, 80);
    QVERIFY(a < b); // same IP, different port
    QVERIFY(a < c); // different IP
    QCOMPARE(a, Endpoint::fromHostOrder(1, 80));
}

void tst_Address::endpoint_hash()
{
    std::unordered_set<Endpoint> s;
    s.insert(Endpoint::fromHostOrder(1, 80));
    s.insert(Endpoint::fromHostOrder(1, 81));
    s.insert(Endpoint::fromHostOrder(1, 80)); // dup
    QCOMPARE(s.size(), std::size_t(2));
}

// ===========================================================================
// ipstr overloads
// ===========================================================================

void tst_Address::ipstr_address()
{
    auto a = Address::fromHostOrder(0x08080808u);
    QCOMPARE(eMule::ipstr(a), QStringLiteral("8.8.8.8"));
}

void tst_Address::ipstr_endpoint()
{
    auto ep = Endpoint::fromHostOrder(0x01020304u, 4661);
    QCOMPARE(eMule::ipstr(ep), QStringLiteral("1.2.3.4:4661"));
}

// ===========================================================================
// isPublicIP — comprehensive reserved range checks
// ===========================================================================

void tst_Address::isPublicIP_ipv4_multicast()
{
    QVERIFY(!Address::fromHostOrder(0xE0000001u).isPublicIP()); // 224.0.0.1
    QVERIFY(!Address::fromHostOrder(0xEFFFFFFFu).isPublicIP()); // 239.255.255.255
}

void tst_Address::isPublicIP_ipv4_reserved240()
{
    QVERIFY(!Address::fromHostOrder(0xF0000001u).isPublicIP()); // 240.0.0.1
    QVERIFY(!Address::fromHostOrder(0xFFFFFFFFu).isPublicIP()); // 255.255.255.255
}

void tst_Address::isPublicIP_ipv4_cgnat()
{
    QVERIFY(!Address::fromHostOrder(0x64400001u).isPublicIP()); // 100.64.0.1
    QVERIFY(!Address::fromHostOrder(0x647FFFFFu).isPublicIP()); // 100.127.255.255
    QVERIFY(Address::fromHostOrder(0x64800001u).isPublicIP());  // 100.128.0.1 (outside CGNAT)
}

void tst_Address::isPublicIP_ipv4_testNets()
{
    QVERIFY(!Address::fromHostOrder(0xC0000201u).isPublicIP()); // 192.0.2.1 (TEST-NET-1)
    QVERIFY(!Address::fromHostOrder(0xC6336401u).isPublicIP()); // 198.51.100.1 (TEST-NET-2)
    QVERIFY(!Address::fromHostOrder(0xCB007101u).isPublicIP()); // 203.0.113.1 (TEST-NET-3)
}

void tst_Address::isPublicIP_ipv4_benchmarking()
{
    QVERIFY(!Address::fromHostOrder(0xC6120001u).isPublicIP()); // 198.18.0.1
    QVERIFY(!Address::fromHostOrder(0xC6130001u).isPublicIP()); // 198.19.0.1
    QVERIFY(Address::fromHostOrder(0xC6140001u).isPublicIP());  // 198.20.0.1 (outside)
}

void tst_Address::isPublicIP_ipv6_teredo()
{
    // 2001:0000::1 — Teredo
    QVERIFY(!Address::fromString(QStringLiteral("2001::1")).isPublicIP());
}

void tst_Address::isPublicIP_ipv6_documentation()
{
    QVERIFY(!Address::fromString(QStringLiteral("2001:db8::1")).isPublicIP());
}

void tst_Address::isPublicIP_ipv6_6to4()
{
    QVERIFY(!Address::fromString(QStringLiteral("2002::1")).isPublicIP());
}

void tst_Address::isPublicIP_ipv6_multicast()
{
    QVERIFY(!Address::fromString(QStringLiteral("ff02::1")).isPublicIP());
}

void tst_Address::isPublicIP_ipv6_srv6()
{
    QVERIFY(!Address::fromString(QStringLiteral("5f00::1")).isPublicIP());
}

void tst_Address::isPublicIP_ipv6_global_ok()
{
    // 2607:f8b0:4004::1 (Google range) — should be public
    QVERIFY(Address::fromString(QStringLiteral("2607:f8b0:4004::1")).isPublicIP());
}

// ===========================================================================
// IPv4 ↔ IPv6 mapped conversion
// ===========================================================================

void tst_Address::toIPv6Mapped_basic()
{
    auto v4 = Address::fromHostOrder(0xC0A80101u); // 192.168.1.1
    auto mapped = v4.toIPv6Mapped();
    QVERIFY(mapped.isIPv6());
    QCOMPARE(mapped.toString(), QStringLiteral("::ffff:192.168.1.1"));
}

void tst_Address::toIPv6Mapped_null()
{
    QVERIFY(Address().toIPv6Mapped().isNull());
}

void tst_Address::toIPv4_fromMapped()
{
    auto mapped = Address::fromString(QStringLiteral("::ffff:10.0.0.1"));
    // fromString normalizes mapped to IPv4, so construct manually
    auto v4orig = Address::fromHostOrder(0x0A000001u);
    auto v6 = v4orig.toIPv6Mapped();
    QVERIFY(v6.isIPv6());
    auto back = v6.toIPv4();
    QVERIFY(back.isIPv4());
    QCOMPARE(back.toUint32(), 0x0A000001u);
}

void tst_Address::toIPv4_fromNonMapped()
{
    // A real IPv6 address can't be demoted
    auto v6 = Address::fromString(QStringLiteral("2001:db8::1"));
    QVERIFY(v6.toIPv4().isNull());
}

void tst_Address::toIPv4_alreadyV4()
{
    auto v4 = Address::fromHostOrder(0x08080808u);
    QCOMPARE(v4.toIPv4(), v4); // no-op, returns self
}

void tst_Address::conversion_roundTrip()
{
    auto orig = Address::fromHostOrder(0x01020304u); // 1.2.3.4
    auto mapped = orig.toIPv6Mapped();
    auto back = mapped.toIPv4();
    QCOMPARE(back, orig);
}

#include "tst_Address.moc"

QTEST_GUILESS_MAIN(tst_Address)
