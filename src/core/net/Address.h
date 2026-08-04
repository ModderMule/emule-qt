#pragma once

/// @file Address.h
/// @brief IPv4/IPv6 address and endpoint abstractions.
///
/// Replaces raw uint32 IP usage with type-safe, byte-order-aware classes.
/// Named factory methods eliminate byte-order ambiguity at every call site.

#include "utils/Types.h"

#include <QHostAddress>
#include <QString>

#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <optional>

namespace eMule {

/// An IPv4 or IPv6 address (no port).
///
/// IPv4 is stored internally in host byte order as a uint32.
/// IPv6 is stored as 16 bytes in network byte order.
/// Use named factory methods — there is no implicit uint32 conversion.
class Address {
public:
    enum class Family : uint8 { None = 0, IPv4 = 4, IPv6 = 6 };

    // -- Construction (all explicit about byte order) --------------------------

    constexpr Address() noexcept = default;

    /// Construct IPv4 from host byte order uint32 (Kademlia convention).
    [[nodiscard]] static constexpr Address fromHostOrder(uint32 ip) noexcept;

    /// Construct IPv4 from network byte order uint32 (ED2K wire / UpDownClient convention).
    [[nodiscard]] static Address fromNetworkOrder(uint32 ip) noexcept;

    /// Construct from QHostAddress (IPv4 or IPv6).
    /// IPv4-mapped IPv6 (::ffff:a.b.c.d) is normalized to plain IPv4.
    [[nodiscard]] static Address fromQHostAddress(const QHostAddress& addr);

    /// Construct from string ("1.2.3.4" or "::1" or "2001:db8::1").
    [[nodiscard]] static Address fromString(const QString& str);

    /// Construct IPv6 from raw 16 bytes in network byte order.
    [[nodiscard]] static Address fromIPv6Bytes(const uint8* bytes) noexcept;

    // -- Queries ---------------------------------------------------------------

    [[nodiscard]] constexpr bool isNull() const noexcept { return m_family == Family::None; }
    [[nodiscard]] constexpr bool isIPv4() const noexcept { return m_family == Family::IPv4; }
    [[nodiscard]] constexpr bool isIPv6() const noexcept { return m_family == Family::IPv6; }
    [[nodiscard]] constexpr Family family() const noexcept { return m_family; }

    // -- IPv4 extraction (protocol serialization) ------------------------------

    /// Return IPv4 as host byte order uint32. Returns 0 if not IPv4.
    [[nodiscard]] constexpr uint32 toUint32() const noexcept;

    /// Return IPv4 as network byte order uint32 (for ED2K wire protocol). Returns 0 if not IPv4.
    [[nodiscard]] uint32 toNetworkUint32() const noexcept;

    // -- Generic extraction ----------------------------------------------------

    /// Convert to QHostAddress. Null address returns QHostAddress::Null.
    [[nodiscard]] QHostAddress toQHostAddress() const;

    /// Format as string ("1.2.3.4" or "2001:db8::1"). Null returns empty string.
    [[nodiscard]] QString toString() const;

    // -- Validation ------------------------------------------------------------

    /// True if this is a publicly routable IP (not reserved, not private, not null).
    /// Checks all IANA reserved ranges for both IPv4 and IPv6.
    [[nodiscard]] bool isPublicIP() const;

    /// Lab mode — set once at startup from `!thePrefs.filterLANIPs()`, i.e. the existing
    /// "this is a private network" switch. It widens isPublicIP() **for IPv6 only** to every
    /// unicast address (loopback, ULA, link-local, 2001:db8::/32), leaving only multicast and
    /// the unspecified address rejected. Every IPv6 acceptance decision in the client goes
    /// through isPublicIP(), so this one switch covers the hello tag, ExtSX, Kad results,
    /// server sources and our own advertise gate together.
    ///
    /// Needed because interop harnesses run on the documentation prefix and single-host rigs
    /// run on ::1, both of which the production rules refuse — correctly, in production.
    /// net/ cannot include prefs/, so the value is pushed in; see CoreSession startup.
    static void setLabNetworkMode(bool enabled);
    [[nodiscard]] static bool labNetworkMode();

    /// True if this is a valid routable IP (not LAN, not 0.x.x.x, not null).
    /// Legacy compat — equivalent to isPublicIP() when allowLan=false.
    [[nodiscard]] bool isRoutable(bool allowLan = false) const;

    /// True if this is a private/LAN/loopback/link-local address.
    [[nodiscard]] bool isLan() const;

    // -- Conversion ------------------------------------------------------------

    /// Convert between IPv4 and IPv4-mapped IPv6.
    /// toIPv6(): promotes IPv4 `a.b.c.d` → `::ffff:a.b.c.d` (16-byte mapped form).
    /// toIPv4(): demotes `::ffff:a.b.c.d` → plain IPv4. Returns null if not a mapped address.
    /// Useful for dual-stack socket interop.
    [[nodiscard]] Address toIPv4() const;
    [[nodiscard]] Address toIPv6Mapped() const;

    // -- Comparison (C++23) ----------------------------------------------------

    [[nodiscard]] constexpr std::strong_ordering operator<=>(const Address& other) const noexcept;
    [[nodiscard]] constexpr bool operator==(const Address& other) const noexcept;

    // -- Hashing ---------------------------------------------------------------

    [[nodiscard]] std::size_t hash() const noexcept;

    // -- IPv6 raw access -------------------------------------------------------

    /// Return pointer to 16 raw IPv6 bytes (network byte order). Only valid if isIPv6().
    [[nodiscard]] constexpr const std::array<uint8, 16>& ipv6Bytes() const noexcept { return m_v6; }

private:
    uint32                 m_v4 = 0;        // host byte order (valid when IPv4)
    std::array<uint8, 16>  m_v6{};          // network byte order (valid when IPv6)
    Family                 m_family = Family::None;
};

// ============================================================================
// Inline / constexpr implementations
// ============================================================================

constexpr Address Address::fromHostOrder(uint32 ip) noexcept
{
    Address a;
    if (ip != 0) {
        a.m_v4 = ip;
        a.m_family = Family::IPv4;
    }
    return a;
}

constexpr uint32 Address::toUint32() const noexcept
{
    return m_v4; // 0 if not IPv4
}

constexpr std::strong_ordering Address::operator<=>(const Address& other) const noexcept
{
    if (auto cmp = m_family <=> other.m_family; cmp != 0)
        return cmp;
    if (m_family == Family::IPv4)
        return m_v4 <=> other.m_v4;
    if (m_family == Family::IPv6)
        return m_v6 <=> other.m_v6;
    return std::strong_ordering::equal; // both None
}

constexpr bool Address::operator==(const Address& other) const noexcept
{
    if (m_family != other.m_family)
        return false;
    if (m_family == Family::IPv4)
        return m_v4 == other.m_v4;
    if (m_family == Family::IPv6)
        return m_v6 == other.m_v6;
    return true; // both None
}

// ============================================================================
// Endpoint: Address + port
// ============================================================================

/// An IP address paired with a port number.
class Endpoint {
public:
    constexpr Endpoint() noexcept = default;
    constexpr Endpoint(Address addr, uint16 port) noexcept
        : m_address(addr), m_port(port) {}

    /// From network byte order IP + port (UpDownClient / Server convention).
    [[nodiscard]] static Endpoint fromNetworkOrder(uint32 ip, uint16 port) noexcept;

    /// From host byte order IP + port (Kademlia convention).
    [[nodiscard]] static constexpr Endpoint fromHostOrder(uint32 ip, uint16 port) noexcept;

    /// Parse "1.2.3.4:port", "[2001:db8::1]:port", "[2001:db8::1]" or a bare IPv6
    /// literal. The inverse of toString(). Returns nullopt when the host part is not
    /// an IP literal (use parseHostPort() when a DNS name is acceptable) or when the
    /// resulting port would be 0.
    [[nodiscard]] static std::optional<Endpoint> fromString(QStringView text,
                                                            uint16 defaultPort = 0);

    [[nodiscard]] constexpr const Address& address() const noexcept { return m_address; }
    [[nodiscard]] constexpr uint16 port() const noexcept { return m_port; }

    [[nodiscard]] constexpr bool isNull() const noexcept {
        return m_address.isNull() && m_port == 0;
    }

    /// Format as "1.2.3.4:port" or "[::1]:port".
    [[nodiscard]] QString toString() const;

    [[nodiscard]] constexpr std::strong_ordering operator<=>(const Endpoint& other) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const Endpoint& other) const noexcept = default;

    [[nodiscard]] std::size_t hash() const noexcept;

private:
    Address m_address;
    uint16  m_port = 0;
};

constexpr Endpoint Endpoint::fromHostOrder(uint32 ip, uint16 port) noexcept
{
    return Endpoint(Address::fromHostOrder(ip), port);
}

// ============================================================================
// Textual host:port parsing / formatting (family-agnostic)
// ============================================================================

/// A textual host with an optional port. @p host is never bracketed.
struct HostPort {
    QString host;                   ///< IP literal (brackets stripped) or DNS name
    uint16  port = 0;
    bool    hostIsLiteral = false;  ///< true when Address::fromString(host) succeeds
};

/// Split a textual endpoint into host and port. Accepts:
///   "host", "host:port", "1.2.3.4", "1.2.3.4:port",
///   "[2001:db8::1]", "[2001:db8::1]:port", and a bare "2001:db8::1".
///
/// An unbracketed string holding two or more colons is only accepted when the WHOLE
/// string is a valid IPv6 literal — it is never split at the last colon, because
/// "2001:db8::1:4662" is itself a valid address and guessing silently corrupts it.
/// Bracketed forms require the bracketed text to be an IPv6 literal, so
/// "[example.com]:80" is rejected.
///
/// Returns nullopt on an empty host, a malformed port, or a resulting port of 0
/// (a port of 0 is never a usable endpoint).
[[nodiscard]] std::optional<HostPort> parseHostPort(QStringView text, uint16 defaultPort = 0);

/// Inverse of parseHostPort(): brackets @p host iff it contains ':' (i.e. is an IPv6
/// literal), so IPv4 and DNS output is unchanged.
[[nodiscard]] QString formatHostPort(QStringView host, uint16 port);

// ============================================================================
// ipstr overloads for Address / Endpoint
// ============================================================================

[[nodiscard]] QString ipstr(const Address& addr);
[[nodiscard]] QString ipstr(const Endpoint& ep);

// ============================================================================
// isGoodIP / isGoodIPPort — Address-typed forms
// ============================================================================
//
// These live here rather than beside the uint32 forms in OtherFunctions.h, which
// deliberately does not include this header (same split as ipstr above).
//
// The IPv4 branch delegates to the existing uint32 isGoodIP so its exact acceptance
// set is preserved — notably it is LOOSER than Address::isPublicIP(), which also
// rejects CGNAT, TEST-NETs and 6to4 relay anycast. Swapping the two would silently
// change which IPv4 sources we accept.

/// True when the address is usable as a peer address. IPv4 keeps the classic ed2k
/// rules; IPv6 rejects loopback, link-local, ULA, v4-mapped, NAT64, Teredo, 6to4,
/// documentation and multicast. @p forceCheck permits LAN/private ranges.
[[nodiscard]] bool isGoodIP(const Address& addr, bool forceCheck = false);

/// isGoodIP plus a non-zero port.
[[nodiscard]] bool isGoodIPPort(const Address& addr, uint16 port);

/// Scoped Address::setLabNetworkMode() override. Lab mode is process-wide, so a test that
/// flips it directly leaks into every test that runs after it; use this instead.
class ScopedLabNetworkMode {
public:
    explicit ScopedLabNetworkMode(bool enabled) : m_previous(Address::labNetworkMode())
    {
        Address::setLabNetworkMode(enabled);
    }
    ~ScopedLabNetworkMode() { Address::setLabNetworkMode(m_previous); }

    ScopedLabNetworkMode(const ScopedLabNetworkMode&) = delete;
    ScopedLabNetworkMode& operator=(const ScopedLabNetworkMode&) = delete;

private:
    bool m_previous;
};

} // namespace eMule

// -- std::hash specializations ------------------------------------------------

template <>
struct std::hash<eMule::Address> {
    std::size_t operator()(const eMule::Address& a) const noexcept { return a.hash(); }
};

template <>
struct std::hash<eMule::Endpoint> {
    std::size_t operator()(const eMule::Endpoint& e) const noexcept { return e.hash(); }
};

// -- Qt metatypes --------------------------------------------------------------
// Needed to carry an Address through a signal argument (IPFilter::ipBlocked) and to
// read one back out of a QSignalSpy.

Q_DECLARE_METATYPE(eMule::Address)
Q_DECLARE_METATYPE(eMule::Endpoint)
