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
// ipstr overloads for Address / Endpoint
// ============================================================================

[[nodiscard]] QString ipstr(const Address& addr);
[[nodiscard]] QString ipstr(const Endpoint& ep);

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
