#include "pch.h"
/// @file DefaultGateway.cpp
/// @brief Platform backends for default next-hop lookup.
///
/// Layout mirrors LocalIPv6.cpp: each platform contributes only raw
/// fact-gathering, and every decision — filtering, ranking, the link-local
/// sanity rules — lives in the pure functions at the bottom and is unit-tested
/// without any OS involvement. One .cpp rather than three because
/// src/core/CMakeLists.txt globs net/*.cpp, so per-platform files would all be
/// compiled on every platform anyway.

#include "net/DefaultGateway.h"
#include "utils/Log.h"

#include <QFile>

#include <algorithm>
#include <cstring>

#if defined(Q_OS_MACOS) || defined(Q_OS_FREEBSD)
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(Q_OS_WIN)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#endif

namespace eMule {

namespace {

/// True for an IPv6 link-local (fe80::/10).
[[nodiscard]] bool isIPv6LinkLocal(const Address& addr)
{
    if (!addr.isIPv6())
        return false;
    const auto& b = addr.ipv6Bytes();
    return b[0] == 0xFE && (b[1] & 0xC0) == 0x80;
}

/// True for :: / 0.0.0.0, and for an absent address.
///
/// Note the asymmetry this exists to paper over: `Address::fromNetworkOrder(0)`
/// yields a null Address, but `fromIPv6Bytes(all zeros)` yields a perfectly
/// valid `::`. A default route's destination arrives as one or the other
/// depending on family, and both mean "no destination".
[[nodiscard]] bool isUnspecified(const Address& addr)
{
    if (addr.isIPv4())
        return addr.toUint32() == 0;
    if (addr.isIPv6()) {
        const auto& b = addr.ipv6Bytes();
        return std::all_of(b.begin(), b.end(), [](uint8 v) { return v == 0; });
    }
    return true;
}

/// True when the low 64 bits — the interface identifier — are all zero.
/// `fe80::%utunN` rows name an interface, not a router, and must not be probed.
[[nodiscard]] bool hasZeroInterfaceId(const Address& addr)
{
    if (!addr.isIPv6())
        return false;
    const auto& b = addr.ipv6Bytes();
    return std::all_of(b.begin() + 8, b.end(), [](uint8 v) { return v == 0; });
}

} // namespace

// ============================================================================
// GatewayCandidate
// ============================================================================

QHostAddress GatewayCandidate::toQHostAddress() const
{
    QHostAddress host = address.toQHostAddress();
    if (!interfaceName.isEmpty() && address.isIPv6())
        host.setScopeId(interfaceName);
    return host;
}

QString GatewayCandidate::toString() const
{
    if (address.isNull())
        return {};
    if (!interfaceName.isEmpty() && address.isIPv6())
        return address.toString() + QLatin1Char('%') + interfaceName;
    return address.toString();
}

// ============================================================================
// Platform backend — macOS / BSD (PF_ROUTE sysctl dump)
// ============================================================================

#if defined(Q_OS_MACOS) || defined(Q_OS_FREEBSD)

namespace {

#ifdef Q_OS_MACOS
constexpr std::size_t kSockaddrAlign = sizeof(uint32_t);   // Apple network_cmds
#else
constexpr std::size_t kSockaddrAlign = sizeof(long);       // classic BSD
#endif

/// Routing messages pad each sockaddr up to the platform alignment; a zero-length
/// sockaddr (the default destination) still consumes one slot.
[[nodiscard]] std::size_t sockaddrAdvance(const sockaddr* sa) noexcept
{
    const std::size_t len = sa->sa_len;
    return len > 0 ? (1 + ((len - 1) | (kSockaddrAlign - 1))) : kSockaddrAlign;
}

/// Convert a routing-socket sockaddr to an Address, unwrapping the KAME
/// embedded scope id. The kernel hides the interface index in bytes 2-3 of a
/// link-local address rather than in sin6_scope_id; leaving it there yields a
/// bogus address such as fe80:8::… that nothing can be sent to.
[[nodiscard]] Address addressFromSockaddr(const sockaddr* sa, uint32& scopeIndexOut)
{
    if (sa == nullptr)
        return {};

    if (sa->sa_family == AF_INET) {
        sockaddr_in sin{};
        std::memcpy(&sin, sa, std::min<std::size_t>(sa->sa_len, sizeof(sin)));
        return Address::fromNetworkOrder(sin.sin_addr.s_addr);
    }

    if (sa->sa_family == AF_INET6) {
        sockaddr_in6 sin6{};
        std::memcpy(&sin6, sa, std::min<std::size_t>(sa->sa_len, sizeof(sin6)));
        auto* bytes = reinterpret_cast<uint8*>(&sin6.sin6_addr);

        if (sin6.sin6_scope_id != 0)
            scopeIndexOut = sin6.sin6_scope_id;

        const bool linkLocal = bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0x80;
        const bool multicastLinkLocal = bytes[0] == 0xFF && (bytes[1] & 0x0F) <= 0x02;
        if (linkLocal || multicastLinkLocal) {
            if (const uint32 embedded = (uint32(bytes[2]) << 8) | bytes[3]; embedded != 0) {
                scopeIndexOut = embedded;
                bytes[2] = 0;
                bytes[3] = 0;
            }
        }
        return Address::fromIPv6Bytes(bytes);
    }

    return {};
}

} // namespace

std::vector<GatewayCandidate> defaultGateways(Address::Family family)
{
    const int af = family == Address::Family::IPv6 ? AF_INET6 : AF_INET;
    int mib[6] = {CTL_NET, PF_ROUTE, 0, af, NET_RT_DUMP, 0};

    std::size_t needed = 0;
    if (sysctl(mib, 6, nullptr, &needed, nullptr, 0) < 0 || needed == 0) {
        logDebug(QStringLiteral("DefaultGateway: route dump sizing failed"));
        return {};
    }

    std::vector<char> buffer(needed);
    if (sysctl(mib, 6, buffer.data(), &needed, nullptr, 0) < 0) {
        logDebug(QStringLiteral("DefaultGateway: route dump failed"));
        return {};
    }

    std::vector<RouteEntry> routes;
    const char* const limit = buffer.data() + needed;
    for (const char* cursor = buffer.data(); cursor + sizeof(rt_msghdr) <= limit; ) {
        const auto* rtm = reinterpret_cast<const rt_msghdr*>(cursor);
        if (rtm->rtm_msglen == 0)
            break;
        cursor += rtm->rtm_msglen;
        if (rtm->rtm_version != RTM_VERSION)
            continue;

        // Not `slots` — Qt defines that as a keyword macro, which expands to
        // nothing and silently turns the declaration into a lambda.
        const sockaddr* addrSlots[RTAX_MAX] = {};
        const auto* sa = reinterpret_cast<const sockaddr*>(rtm + 1);
        for (int i = 0; i < RTAX_MAX; ++i) {
            if ((rtm->rtm_addrs & (1 << i)) == 0)
                continue;
            addrSlots[i] = sa;
            sa = reinterpret_cast<const sockaddr*>(
                reinterpret_cast<const char*>(sa) + sockaddrAdvance(sa));
        }

        uint32 scopeIndex = rtm->rtm_index;
        const Address destination = addressFromSockaddr(addrSlots[RTAX_DST], scopeIndex);
        scopeIndex = rtm->rtm_index;
        const Address gateway = addressFromSockaddr(addrSlots[RTAX_GATEWAY], scopeIndex);

        RouteEntry entry;
        entry.destination = destination;
        entry.gateway = gateway;
        // A default route has either no destination sockaddr at all or the
        // unspecified address. Testing isNull() alone is not enough: for IPv6
        // the kernel hands us a valid `::`, which is not null.
        entry.prefixLength = isUnspecified(destination) ? 0 : 128;
        entry.isUp = (rtm->rtm_flags & RTF_UP) != 0;
        entry.isGateway = (rtm->rtm_flags & RTF_GATEWAY) != 0;
        entry.metric = 0;

        char nameBuf[IF_NAMESIZE] = {};
        if (scopeIndex != 0 && if_indextoname(scopeIndex, nameBuf) != nullptr)
            entry.interfaceName = QString::fromLatin1(nameBuf);

        routes.push_back(std::move(entry));
    }

    return selectDefaultGateways(routes, family);
}

// ============================================================================
// Platform backend — Windows (GetBestRoute2)
// ============================================================================

#elif defined(Q_OS_WIN)

std::vector<GatewayCandidate> defaultGateways(Address::Family family)
{
    // Ask for the route towards a well-known off-link address rather than
    // enumerating the table: GetBestRoute2 applies the metric and policy rules
    // itself, which is exactly the decision we would otherwise reimplement.
    SOCKADDR_INET destination{};
    if (family == Address::Family::IPv6) {
        destination.si_family = AF_INET6;
        destination.Ipv6.sin6_family = AF_INET6;
        InetPtonW(AF_INET6, L"2001:4860:4860::8888", &destination.Ipv6.sin6_addr);
    } else {
        destination.si_family = AF_INET;
        destination.Ipv4.sin_family = AF_INET;
        InetPtonW(AF_INET, L"8.8.8.8", &destination.Ipv4.sin_addr);
    }

    MIB_IPFORWARD_ROW2 row{};
    SOCKADDR_INET bestSource{};
    if (GetBestRoute2(nullptr, 0, nullptr, &destination, 0, &row, &bestSource) != NO_ERROR) {
        logDebug(QStringLiteral("DefaultGateway: GetBestRoute2 found no route"));
        return {};
    }

    RouteEntry entry;
    entry.isUp = true;
    entry.isGateway = true;
    entry.metric = row.Metric;
    entry.prefixLength = 0;

    if (row.NextHop.si_family == AF_INET6) {
        entry.gateway = Address::fromIPv6Bytes(
            reinterpret_cast<const uint8*>(&row.NextHop.Ipv6.sin6_addr));
    } else if (row.NextHop.si_family == AF_INET) {
        entry.gateway = Address::fromNetworkOrder(row.NextHop.Ipv4.sin_addr.S_un.S_addr);
    }

    // Windows zone ids are numeric, and QHostAddress::setScopeId() accepts the
    // index rendered as a string — no name lookup needed.
    if (row.InterfaceIndex != 0)
        entry.interfaceName = QString::number(row.InterfaceIndex);

    return selectDefaultGateways({entry}, family);
}

// ============================================================================
// Platform backend — Linux and everything else (/proc/net)
// ============================================================================

#else

std::vector<GatewayCandidate> defaultGateways(Address::Family family)
{
    const QString path = family == Address::Family::IPv6
                             ? QStringLiteral("/proc/net/ipv6_route")
                             : QStringLiteral("/proc/net/route");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logDebug(QStringLiteral("DefaultGateway: cannot read %1").arg(path));
        return {};
    }
    const QString text = QString::fromLatin1(file.readAll());

    const std::vector<RouteEntry> routes = family == Address::Family::IPv6
                                               ? parseProcNetIpv6Route(text)
                                               : parseProcNetRoute(text);
    return selectDefaultGateways(routes, family);
}

#endif

// ============================================================================
// Pure helpers — compiled on every platform so the tests run everywhere
// ============================================================================

namespace {

constexpr uint32 kRouteFlagUp = 0x0001;        // RTF_UP
constexpr uint32 kRouteFlagGateway = 0x0002;   // RTF_GATEWAY

/// Little-endian hex column -> host-order IPv4.
/// The kernel prints a __be32 with %08X, so the digits come out byte-reversed
/// relative to dotted notation: "0102A8C0" is 192.168.2.1. The swap is written
/// arithmetically so the parse is identical on any host we run the tests on.
[[nodiscard]] Address ipv4FromProcHex(QStringView hex, bool* okOut)
{
    bool ok = false;
    const uint32 raw = hex.toUInt(&ok, 16);
    if (okOut != nullptr)
        *okOut = ok;
    if (!ok)
        return {};
    const uint32 hostOrder = ((raw & 0x000000FFu) << 24) | ((raw & 0x0000FF00u) << 8)
                             | ((raw & 0x00FF0000u) >> 8) | ((raw & 0xFF000000u) >> 24);
    return Address::fromHostOrder(hostOrder);
}

/// Contiguous 32-hex-digit column -> IPv6 address.
[[nodiscard]] Address ipv6FromProcHex(QStringView hex)
{
    if (hex.size() != 32)
        return {};
    std::array<uint8, 16> bytes{};
    for (int i = 0; i < 16; ++i) {
        bool ok = false;
        const uint32 value = hex.mid(i * 2, 2).toUInt(&ok, 16);
        if (!ok)
            return {};
        bytes[static_cast<std::size_t>(i)] = static_cast<uint8>(value);
    }
    return Address::fromIPv6Bytes(bytes.data());
}

/// Count leading one-bits of a netmask, so an IPv4 mask column becomes a prefix length.
[[nodiscard]] uint8 prefixLengthFromMask(uint32 hostOrderMask)
{
    uint8 bits = 0;
    for (int i = 31; i >= 0; --i) {
        if ((hostOrderMask & (1u << i)) == 0)
            break;
        ++bits;
    }
    return bits;
}

} // namespace

std::vector<RouteEntry> parseProcNetRoute(QStringView text)
{
    std::vector<RouteEntry> routes;
    for (const QStringView line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto columns = line.split(QLatin1Char('\t'), Qt::SkipEmptyParts);
        if (columns.size() < 8)
            continue;

        bool destOk = false;
        const Address destination = ipv4FromProcHex(columns[1].trimmed(), &destOk);
        if (!destOk)
            continue;   // the header row

        bool gatewayOk = false;
        const Address gateway = ipv4FromProcHex(columns[2].trimmed(), &gatewayOk);
        if (!gatewayOk)
            continue;

        bool flagsOk = false;
        const uint32 flags = columns[3].trimmed().toUInt(&flagsOk, 16);
        if (!flagsOk)
            continue;

        bool maskOk = false;
        const Address mask = ipv4FromProcHex(columns[7].trimmed(), &maskOk);

        RouteEntry entry;
        entry.destination = destination;
        entry.gateway = gateway;
        entry.interfaceName = columns[0].trimmed().toString();
        entry.metric = columns[6].trimmed().toUInt();
        entry.isUp = (flags & kRouteFlagUp) != 0;
        entry.isGateway = (flags & kRouteFlagGateway) != 0;
        entry.prefixLength = maskOk ? prefixLengthFromMask(mask.toUint32()) : 0;
        routes.push_back(std::move(entry));
    }
    return routes;
}

std::vector<RouteEntry> parseProcNetIpv6Route(QStringView text)
{
    std::vector<RouteEntry> routes;
    for (const QStringView line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto columns = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (columns.size() < 10)
            continue;

        bool prefixOk = false;
        const uint32 prefixLength = columns[1].toUInt(&prefixOk, 16);
        if (!prefixOk || prefixLength > 128)
            continue;

        bool flagsOk = false;
        const uint32 flags = columns[8].toUInt(&flagsOk, 16);
        if (!flagsOk)
            continue;

        const Address destination = ipv6FromProcHex(columns[0]);
        const Address gateway = ipv6FromProcHex(columns[4]);
        if (gateway.isNull())
            continue;

        RouteEntry entry;
        entry.destination = destination;
        entry.gateway = gateway;
        entry.prefixLength = static_cast<uint8>(prefixLength);
        entry.interfaceName = columns[9].trimmed().toString();
        entry.metric = columns[5].toUInt(nullptr, 16);
        entry.isUp = (flags & kRouteFlagUp) != 0;
        entry.isGateway = (flags & kRouteFlagGateway) != 0;
        routes.push_back(std::move(entry));
    }
    return routes;
}

std::vector<GatewayCandidate> selectDefaultGateways(const std::vector<RouteEntry>& routes,
                                                    Address::Family family)
{
    std::vector<GatewayCandidate> candidates;

    for (const RouteEntry& route : routes) {
        if (!route.isUp || !route.isGateway)
            continue;
        if (route.prefixLength != 0)
            continue;                              // not a default route
        if (route.gateway.family() != family)
            continue;
        if (isUnspecified(route.gateway))
            continue;
        // fe80:: with no interface identifier names an interface, not a router.
        if (isIPv6LinkLocal(route.gateway) && hasZeroInterfaceId(route.gateway))
            continue;
        // A link-local next hop is unreachable without its zone index, so a row
        // that failed to yield an interface name is unusable rather than merely
        // lower quality.
        if (isIPv6LinkLocal(route.gateway) && route.interfaceName.isEmpty())
            continue;

        GatewayCandidate candidate;
        candidate.address = route.gateway;
        candidate.interfaceName = route.interfaceName;
        candidate.metric = route.metric;

        if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
            candidates.push_back(std::move(candidate));
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const GatewayCandidate& a, const GatewayCandidate& b) {
                         return a.metric < b.metric;
                     });
    return candidates;
}

} // namespace eMule
