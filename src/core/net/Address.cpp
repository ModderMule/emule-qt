/// @file Address.cpp
/// @brief Non-inline implementations for Address and Endpoint.

#include "net/Address.h"
#include "utils/OtherFunctions.h"   // the uint32 isGoodIP the IPv4 branch delegates to

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <QHostAddress>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>

namespace eMule {

// ============================================================================
// Address — factory methods
// ============================================================================

Address Address::fromNetworkOrder(uint32 ip) noexcept
{
    if (ip == 0)
        return {};
    Address a;
    a.m_v4 = ntohl(ip);
    a.m_family = Family::IPv4;
    return a;
}

Address Address::fromQHostAddress(const QHostAddress& addr)
{
    if (addr.isNull())
        return {};

    // Normalize IPv4-mapped IPv6 (::ffff:a.b.c.d) to plain IPv4
    bool ok = false;
    const uint32 v4 = addr.toIPv4Address(&ok);
    if (ok)
        return fromHostOrder(v4);

    if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
        Address a;
        a.m_family = Family::IPv6;
        const Q_IPV6ADDR raw = addr.toIPv6Address();
        std::memcpy(a.m_v6.data(), raw.c, 16);
        return a;
    }

    return {};
}

Address Address::fromString(const QString& str)
{
    QHostAddress ha;
    if (!ha.setAddress(str))
        return {};
    return fromQHostAddress(ha);
}

Address Address::fromIPv6Bytes(const uint8* bytes) noexcept
{
    Address a;
    a.m_family = Family::IPv6;
    std::memcpy(a.m_v6.data(), bytes, 16);
    return a;
}

// ============================================================================
// Address — extraction
// ============================================================================

uint32 Address::toNetworkUint32() const noexcept
{
    if (m_family != Family::IPv4)
        return 0;
    return htonl(m_v4);
}

QHostAddress Address::toQHostAddress() const
{
    switch (m_family) {
    case Family::IPv4:
        return QHostAddress(m_v4); // QHostAddress(quint32) expects host byte order
    case Family::IPv6: {
        Q_IPV6ADDR raw;
        std::memcpy(raw.c, m_v6.data(), 16);
        return QHostAddress(raw);
    }
    default:
        return {};
    }
}

QString Address::toString() const
{
    switch (m_family) {
    case Family::IPv4:
        return toQHostAddress().toString();
    case Family::IPv6:
        return toQHostAddress().toString();
    default:
        return {};
    }
}

// ============================================================================
// Address — validation
// ============================================================================

namespace {
/// Set once at startup from !thePrefs.filterLANIPs(); read from every isPublicIP() call,
/// including ones on the socket threads, hence atomic. net/ must not depend on prefs/, so
/// the value is pushed in rather than pulled.
std::atomic<bool> g_labNetworkMode{false};
} // namespace

void Address::setLabNetworkMode(bool enabled)
{
    g_labNetworkMode.store(enabled, std::memory_order_relaxed);
}

bool Address::labNetworkMode()
{
    return g_labNetworkMode.load(std::memory_order_relaxed);
}

bool Address::isPublicIP() const
{
    // Comprehensive check against all IANA reserved ranges.
    // https://en.wikipedia.org/wiki/Reserved_IP_addresses

    if (m_family == Family::None)
        return false;

    if (m_family == Family::IPv4) {
        const auto a = static_cast<uint8>(m_v4 >> 24);
        const auto b = static_cast<uint8>(m_v4 >> 16);
        const auto c = static_cast<uint8>(m_v4 >> 8);

        if (a == 0 || a == 10 || a == 127)       return false; // 0.0.0.0/8, 10/8, loopback
        if (a == 192 && b == 168)                 return false; // 192.168.0.0/16
        if (a == 172 && b >= 16 && b <= 31)       return false; // 172.16.0.0/12
        if (a == 169 && b == 254)                 return false; // 169.254.0.0/16 link-local
        if (a >= 224 && a <= 239)                 return false; // 224.0.0.0/4 multicast
        if (a >= 240)                             return false; // 240.0.0.0/4 reserved + broadcast
        if (a == 100 && b >= 64 && b <= 127)      return false; // 100.64.0.0/10 CGNAT (RFC 6598)
        if (a == 192 && b == 0 && (c == 0 || c == 2)) return false; // 192.0.0.0/24, 192.0.2.0/24 (doc)
        if (a == 192 && b == 88 && c == 99)       return false; // 192.88.99.0/24 (6to4 relay anycast)
        if (a == 198 && (b == 18 || b == 19))     return false; // 198.18.0.0/15 (benchmarking)
        if (a == 198 && b == 51 && c == 100)      return false; // 198.51.100.0/24 (TEST-NET-2)
        if (a == 203 && b == 0 && c == 113)       return false; // 203.0.113.0/24 (TEST-NET-3)
        if (a == 233 && b == 252 && c == 0)       return false; // 233.252.0.0/24 (MCAST-TEST-NET)

        return true;
    }

    if (m_family == Family::IPv6) {
        if (labNetworkMode()) {
            // Lab mode: the operator has told us this is a private/test network by clearing
            // filterLANIPs, so accept every unicast IPv6 — loopback for a single-host rig, ULA
            // and link-local for a LAN rig, and 2001:db8::/32 because interop harnesses run on
            // the documentation prefix. Multicast and the unspecified address stay rejected:
            // they are not peer addresses under any configuration.
            //
            // Deliberately IPv6-only. The IPv4 acceptance set is the classic ed2k one and is
            // relaxed for LAN through isGoodIP(forceCheck) / isRoutable(!filterLANIPs) instead;
            // widening it here would silently change which IPv4 sources we accept.
            const bool unspecified =
                std::all_of(m_v6.begin(), m_v6.end(), [](uint8 b) { return b == 0; });
            return !unspecified && m_v6[0] != 0xFF;
        }

        // ::/128 unspecified, ::1/128 loopback
        if (m_v6[0] == 0 && m_v6[1] == 0 && m_v6[2] == 0 && m_v6[3] == 0 &&
            m_v6[4] == 0 && m_v6[5] == 0 && m_v6[6] == 0 && m_v6[7] == 0 &&
            m_v6[8] == 0 && m_v6[9] == 0 && m_v6[10] == 0 && m_v6[11] == 0 &&
            m_v6[12] == 0 && m_v6[13] == 0 && m_v6[14] == 0 && m_v6[15] <= 1)
            return false;

        // fe80::/10 link-local
        if (m_v6[0] == 0xFE && (m_v6[1] & 0xC0) == 0x80)
            return false;

        // fc00::/7 unique local (ULA)
        if ((m_v6[0] & 0xFE) == 0xFC)
            return false;

        // ::ffff:0:0/96 IPv4-mapped IPv6
        if (m_v6[0] == 0 && m_v6[1] == 0 && m_v6[2] == 0 && m_v6[3] == 0 &&
            m_v6[4] == 0 && m_v6[5] == 0 && m_v6[6] == 0 && m_v6[7] == 0 &&
            m_v6[8] == 0 && m_v6[9] == 0 && m_v6[10] == 0xFF && m_v6[11] == 0xFF)
            return false;

        // 64:ff9b::/96 NAT64 well-known prefix
        if (m_v6[0] == 0x00 && m_v6[1] == 0x64 && m_v6[2] == 0xFF && m_v6[3] == 0x9B)
            return false;

        // 100::/64 discard prefix (RFC 6666)
        if (m_v6[0] == 0x01 && m_v6[1] == 0x00)
            return false;

        // 2001::/32 Teredo tunneling
        if (m_v6[0] == 0x20 && m_v6[1] == 0x01 && m_v6[2] == 0x00 && m_v6[3] == 0x00)
            return false;

        // 2001:20::/28 ORCHIDv2
        if (m_v6[0] == 0x20 && m_v6[1] == 0x01 && (m_v6[2] & 0xF0) == 0x20)
            return false;

        // 2001:db8::/32 documentation
        if (m_v6[0] == 0x20 && m_v6[1] == 0x01 && m_v6[2] == 0x0D && m_v6[3] == 0xB8)
            return false;

        // 2002::/16 6to4 (deprecated)
        if (m_v6[0] == 0x20 && m_v6[1] == 0x02)
            return false;

        // 5f00::/16 SRv6 Segment Routing
        if (m_v6[0] == 0x5F && m_v6[1] == 0x00)
            return false;

        // ff00::/8 multicast
        if (m_v6[0] == 0xFF)
            return false;

        return true;
    }

    return false;
}

bool Address::isRoutable(bool allowLan) const
{
    if (allowLan) {
        // Only reject null and 0.x.x.x
        if (isNull())
            return false;
        if (m_family == Family::IPv4 && static_cast<uint8>(m_v4 >> 24) == 0)
            return false;
        return true;
    }
    return isPublicIP();
}

bool Address::isLan() const
{
    if (m_family == Family::IPv4) {
        const auto a = static_cast<uint8>(m_v4 >> 24);
        const auto b = static_cast<uint8>(m_v4 >> 16);

        if (a == 10) return true;                          // 10.0.0.0/8
        if (a == 172 && b >= 16 && b <= 31) return true;   // 172.16.0.0/12
        if (a == 192 && b == 168) return true;              // 192.168.0.0/16
        if (a == 127) return true;                          // loopback
        if (a == 169 && b == 254) return true;              // link-local
        return false;
    }

    if (m_family == Family::IPv6) {
        // ::1 loopback
        static constexpr std::array<uint8, 16> loopback =
            {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1};
        if (m_v6 == loopback)
            return true;

        // fe80::/10 link-local
        if (m_v6[0] == 0xFE && (m_v6[1] & 0xC0) == 0x80)
            return true;

        // fc00::/7 unique local
        if ((m_v6[0] & 0xFE) == 0xFC)
            return true;

        return false;
    }

    return false;
}

// ============================================================================
// Address — IPv4 ↔ IPv6 mapped conversion
// ============================================================================

Address Address::toIPv4() const
{
    if (m_family == Family::IPv4)
        return *this; // already IPv4

    if (m_family == Family::IPv6) {
        // Check if this is an IPv4-mapped IPv6 address (::ffff:a.b.c.d)
        if (m_v6[0] == 0 && m_v6[1] == 0 && m_v6[2] == 0 && m_v6[3] == 0 &&
            m_v6[4] == 0 && m_v6[5] == 0 && m_v6[6] == 0 && m_v6[7] == 0 &&
            m_v6[8] == 0 && m_v6[9] == 0 && m_v6[10] == 0xFF && m_v6[11] == 0xFF) {
            // Extract IPv4 from bytes [12..15] (network byte order)
            uint32 nbo = 0;
            std::memcpy(&nbo, &m_v6[12], 4);
            return fromNetworkOrder(nbo);
        }
    }

    return {}; // not convertible
}

Address Address::toIPv6Mapped() const
{
    if (m_family == Family::IPv6)
        return *this; // already IPv6

    if (m_family == Family::IPv4) {
        Address a;
        a.m_family = Family::IPv6;
        // Build ::ffff:a.b.c.d
        // m_v6[0..9] = 0 (already zeroed)
        a.m_v6[10] = 0xFF;
        a.m_v6[11] = 0xFF;
        // Store IPv4 bytes in network byte order at [12..15]
        const uint32 nbo = htonl(m_v4);
        std::memcpy(&a.m_v6[12], &nbo, 4);
        return a;
    }

    return {}; // null → null
}

// ============================================================================
// Address — hashing
// ============================================================================

std::size_t Address::hash() const noexcept
{
    if (m_family == Family::IPv4)
        return std::hash<uint32>{}(m_v4);

    if (m_family == Family::IPv6) {
        // FNV-1a over 16 bytes
        std::size_t h = 14695981039346656037ULL;
        for (auto byte : m_v6) {
            h ^= byte;
            h *= 1099511628211ULL;
        }
        return h;
    }

    return 0;
}

// ============================================================================
// Endpoint
// ============================================================================

Endpoint Endpoint::fromNetworkOrder(uint32 ip, uint16 port) noexcept
{
    return Endpoint(Address::fromNetworkOrder(ip), port);
}

std::optional<Endpoint> Endpoint::fromString(QStringView text, uint16 defaultPort)
{
    const auto hp = parseHostPort(text, defaultPort);
    if (!hp || !hp->hostIsLiteral)
        return std::nullopt;
    return Endpoint(Address::fromString(hp->host), hp->port);
}

QString Endpoint::toString() const
{
    if (m_address.isIPv6())
        return QStringLiteral("[%1]:%2").arg(m_address.toString()).arg(m_port);
    return QStringLiteral("%1:%2").arg(m_address.toString()).arg(m_port);
}

std::size_t Endpoint::hash() const noexcept
{
    auto h = m_address.hash();
    h ^= std::hash<uint16>{}(m_port) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

// ============================================================================
// Textual host:port parsing / formatting
// ============================================================================

namespace {

/// Parse a decimal port. Returns nullopt unless every character is a digit and the
/// value fits a uint16 (0 is rejected by the caller, not here).
std::optional<uint16> parsePortDigits(QStringView text)
{
    if (text.isEmpty() || text.size() > 5)
        return std::nullopt;
    uint32 value = 0;
    for (const QChar ch : text) {
        if (ch < u'0' || ch > u'9')
            return std::nullopt;
        value = value * 10 + static_cast<uint32>(ch.unicode() - u'0');
    }
    if (value > 65535)
        return std::nullopt;
    return static_cast<uint16>(value);
}

} // namespace

std::optional<HostPort> parseHostPort(QStringView text, uint16 defaultPort)
{
    const QStringView trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return std::nullopt;

    QStringView hostPart;
    uint16 port = defaultPort;

    if (trimmed.startsWith(u'[')) {
        // "[v6]" or "[v6]:port" — the bracketed text must be an IPv6 literal, so
        // "[example.com]:80" is rejected rather than silently treated as a hostname.
        const qsizetype close = trimmed.indexOf(u']');
        if (close < 0)
            return std::nullopt;
        hostPart = trimmed.sliced(1, close - 1);
        const QStringView tail = trimmed.sliced(close + 1);
        if (!tail.isEmpty()) {
            if (!tail.startsWith(u':'))
                return std::nullopt;
            const auto parsed = parsePortDigits(tail.sliced(1));
            if (!parsed)
                return std::nullopt;
            port = *parsed;
        }
        if (!Address::fromString(hostPart.toString()).isIPv6())
            return std::nullopt;
    } else if (trimmed.count(u':') >= 2) {
        // Two or more colons and no brackets: this can only be a bare IPv6 literal.
        // Splitting at the last colon would be a coin flip — "2001:db8::1:4662" is
        // itself a valid address — so require the whole string to parse as one.
        if (!Address::fromString(trimmed.toString()).isIPv6())
            return std::nullopt;
        hostPart = trimmed;
    } else {
        const qsizetype colon = trimmed.indexOf(u':');
        if (colon < 0) {
            hostPart = trimmed;
        } else {
            hostPart = trimmed.first(colon);
            const auto parsed = parsePortDigits(trimmed.sliced(colon + 1));
            if (!parsed)
                return std::nullopt;
            port = *parsed;
        }
    }

    if (hostPart.isEmpty() || port == 0)
        return std::nullopt;

    HostPort result;
    result.host = hostPart.toString();
    result.port = port;
    result.hostIsLiteral = !Address::fromString(result.host).isNull();
    return result;
}

QString formatHostPort(QStringView host, uint16 port)
{
    if (host.contains(u':'))
        return QStringLiteral("[%1]:%2").arg(host).arg(port);
    return QStringLiteral("%1:%2").arg(host).arg(port);
}

// ============================================================================
// ipstr overloads
// ============================================================================

QString ipstr(const Address& addr)
{
    return addr.toString();
}

QString ipstr(const Endpoint& ep)
{
    return ep.toString();
}

// ============================================================================
// isGoodIP / isGoodIPPort — Address-typed forms
// ============================================================================

bool isGoodIP(const Address& addr, bool forceCheck)
{
    if (addr.isIPv4()) {
        // Delegate so the IPv4 acceptance set stays byte-identical to the classic rule.
        return eMule::isGoodIP(addr.toNetworkUint32(), forceCheck);
    }
    if (addr.isIPv6()) {
        // forceCheck mirrors the IPv4 rule: skip only the LAN test. Multicast and the
        // unspecified address stay rejected, since the IPv4 form keeps rejecting
        // 0.x.x.x and 224+ regardless of forceCheck. isPublicIP() and isLan() are
        // disjoint and together cover exactly the unicast space we want.
        if (addr.isPublicIP())
            return true;
        return forceCheck && addr.isLan();
    }
    return false;   // null address
}

bool isGoodIPPort(const Address& addr, uint16 port)
{
    return isGoodIP(addr) && port != 0;
}

} // namespace eMule
