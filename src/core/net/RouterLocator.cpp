#include "pch.h"
/// @file RouterLocator.cpp
/// @brief Router address and VPN detection for the "Open Router" button.

#include "net/RouterLocator.h"
#include "utils/Log.h"

#include <QUdpSocket>

#include <array>

namespace eMule {

namespace {
QNetworkInterface interfaceByRouteName(const QString& name);
QString vpnEgressInterface();
} // namespace

RouterInfo locateRouter()
{
    RouterInfo info;

    if (const QString vpn = vpnEgressInterface(); !vpn.isEmpty())
        info.vpnInterface = vpn;

    std::vector<GatewayCandidate> candidates = allDefaultGateways(Address::Family::IPv4);
    const auto v6 = allDefaultGateways(Address::Family::IPv6);
    candidates.insert(candidates.end(), v6.begin(), v6.end());

    const auto isTunnel = [](const QString& name) {
        const QNetworkInterface iface = interfaceByRouteName(name);
        return iface.isValid() && looksLikeTunnel(iface.name(), iface.humanReadableName(), iface.type());
    };
    if (const auto picked = pickRouter(candidates, isTunnel)) {
        info.router = picked->address.toQHostAddress();
        const QNetworkInterface iface = interfaceByRouteName(picked->interfaceName);
        info.routerInterface = iface.isValid() ? iface.humanReadableName() : picked->interfaceName;
    }

    logDebug(QStringLiteral("RouterLocator: router %1 on %2, vpn '%3'")
                 .arg(info.router.toString(), info.routerInterface, info.vpnInterface));
    return info;
}

bool looksLikeTunnel(QStringView name, QStringView humanName, QNetworkInterface::InterfaceType type)
{
    if (type == QNetworkInterface::Ppp || type == QNetworkInterface::Virtual)
        return true;

    // Unix interface names: macOS utun, Linux tun/tap/wg, vendor clients
    static constexpr std::array kNamePrefixes = {
        u"utun", u"tun", u"tap", u"wg", u"ppp", u"ipsec", u"gpd", u"nordlynx",
        u"tailscale", u"zt", u"proton", u"mullvad", u"cscotun",
    };
    for (const char16_t* prefix : kNamePrefixes) {
        if (name.startsWith(QStringView(prefix), Qt::CaseInsensitive))
            return true;
    }

    // Windows friendly names, which carry the adapter or client name
    static constexpr std::array kHumanWords = {
        u"vpn", u"wireguard", u"wintun", u"tap-windows", u"openvpn", u"tunnel",
        u"nordlynx", u"mullvad", u"proton", u"tailscale", u"zerotier",
        u"anyconnect", u"globalprotect", u"fortinet", u"forticlient",
    };
    for (const char16_t* word : kHumanWords) {
        if (humanName.contains(QStringView(word), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

std::optional<GatewayCandidate> pickRouter(
    const std::vector<GatewayCandidate>& candidates,
    const std::function<bool(const QString& interfaceName)>& isTunnel)
{
    std::optional<GatewayCandidate> ipv6Pick;
    for (const GatewayCandidate& candidate : candidates) {
        if (candidate.isNull() || isTunnel(candidate.interfaceName))
            continue;
        if (candidate.address.isIPv4())
            return candidate;
        // fe80::…%en0 cannot go in a browser URL
        const auto& b = candidate.address.ipv6Bytes();
        const bool linkLocal = b[0] == 0xFE && (b[1] & 0xC0) == 0x80;
        if (!linkLocal && !ipv6Pick)
            ipv6Pick = candidate;
    }
    return ipv6Pick;
}

// ============================================================================
// Private helpers
// ============================================================================

namespace {

/// Interface named by a route row: an index on Windows, a name elsewhere.
QNetworkInterface interfaceByRouteName(const QString& name)
{
#ifdef Q_OS_WIN
    bool ok = false;
    const int index = name.toInt(&ok);
    return ok ? QNetworkInterface::interfaceFromIndex(index) : QNetworkInterface{};
#else
    return QNetworkInterface::interfaceFromName(name);
#endif
}

/// Interface the OS would send @p probe's traffic through. A connected UDP
/// socket only selects a route and source address; nothing goes on the wire.
QNetworkInterface egressInterface(const QHostAddress& probe)
{
    QUdpSocket socket;
    socket.connectToHost(probe, 53);
    if (!socket.waitForConnected(500))
        return {};
    const QHostAddress local = socket.localAddress();
    if (local.isNull())
        return {};

    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().isEqual(local, QHostAddress::StrictConversion))
                return iface;
        }
    }
    return {};
}

/// Name of the tunnel interface internet traffic leaves through, empty when none.
/// Either family counts: a VPN often carries IPv4 only and blocks IPv6.
QString vpnEgressInterface()
{
    for (const QHostAddress& probe : {QHostAddress(QStringLiteral("1.1.1.1")),
                                      QHostAddress(QStringLiteral("2606:4700:4700::1111"))}) {
        const QNetworkInterface iface = egressInterface(probe);
        if (iface.isValid() && looksLikeTunnel(iface.name(), iface.humanReadableName(), iface.type()))
            return iface.humanReadableName();
    }
    return {};
}

} // namespace

} // namespace eMule
