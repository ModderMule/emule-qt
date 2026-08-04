#pragma once

/// @file DefaultGateway.h
/// @brief Default next-hop router lookup for PCP / NAT-PMP.
///
/// PCP (RFC 6887) and NAT-PMP (RFC 6886) are unicast protocols addressed to the
/// gateway on UDP 5351, so unlike UPnP there is no multicast discovery to fall
/// back on — the next hop has to come from the routing table. Qt exposes no
/// route API, hence the three platform backends here.
///
/// Two rules the callers depend on:
///
///  * An empty result means "do not send". Guessing (the `.1` of our own /24 is
///    the usual folklore) would spray our internal port layout at whatever host
///    happens to own that address.
///  * IPv6 next hops are normally link-local (`fe80::…`) and are useless without
///    their zone index, so candidates carry the interface name alongside the
///    address. `Address` deliberately has no scope field (see LocalIPv6.h), so
///    the two are paired here instead and rejoined in toQHostAddress().

#include "net/Address.h"
#include "utils/Types.h"

#include <QHostAddress>
#include <QString>

#include <vector>

namespace eMule {

/// A candidate next-hop router, with the zone index an IPv6 link-local needs.
struct GatewayCandidate {
    Address address;                ///< next-hop address
    QString interfaceName;          ///< zone index; empty when not needed
    uint32  metric = 0;             ///< route metric, lower is better

    [[nodiscard]] bool isNull() const noexcept { return address.isNull(); }

    /// The address with its scope id reattached. Sending to a link-local next hop
    /// without this fails with EHOSTUNREACH.
    [[nodiscard]] QHostAddress toQHostAddress() const;

    /// "fe80::1%en0" / "192.168.178.1" — for logs.
    [[nodiscard]] QString toString() const;

    [[nodiscard]] bool operator==(const GatewayCandidate& other) const noexcept
    {
        return address == other.address && interfaceName == other.interfaceName;
    }
};

/// One routing-table row, in the minimal form the selection logic needs.
struct RouteEntry {
    Address destination;
    uint8   prefixLength = 0;
    Address gateway;
    QString interfaceName;
    uint32  metric = 0;
    bool    isUp = false;
    bool    isGateway = false;
};

/// Next-hop candidates for @p family, best first. Empty means "do not send".
///
/// More than one is normal (several default routes, or a VPN alongside the LAN
/// link); callers probe them concurrently rather than trusting the first.
[[nodiscard]] std::vector<GatewayCandidate> defaultGateways(Address::Family family);

// -- Exposed for unit tests ---------------------------------------------------
// Defined on every platform, not just Linux, so the parsers and the selection
// rule stay testable everywhere (same arrangement as LocalIPv6's parsers).

/// Parse /proc/net/route: "Iface Destination Gateway Flags RefCnt Use Metric Mask …",
/// all numeric columns little-endian hex. Default routes have Destination 00000000.
[[nodiscard]] std::vector<RouteEntry> parseProcNetRoute(QStringView text);

/// Parse /proc/net/ipv6_route: "<dst 32hex> <dstlen> <src 32hex> <srclen>
/// <nexthop 32hex> <metric 8hex> <refcnt> <use> <flags 8hex> <iface>".
/// Default routes have destination prefix length 0.
[[nodiscard]] std::vector<RouteEntry> parseProcNetIpv6Route(QStringView text);

/// Reduce routing-table rows to usable next-hop candidates, best first.
///
/// Keeps only up gateway routes of the requested family whose destination is the
/// default route, then sorts by metric. Rejects unspecified next hops and
/// link-locals with an all-zero interface identifier (`fe80::%utunN` rows, which
/// name an interface rather than a router).
[[nodiscard]] std::vector<GatewayCandidate> selectDefaultGateways(
    const std::vector<RouteEntry>& routes, Address::Family family);

} // namespace eMule
