#pragma once

/// @file RouterLocator.h
/// @brief Find the LAN router's address for its web page, and spot an active VPN.
///
/// Best effort, for the "Open Router" button. The router is the next hop of a
/// default route on a non-tunnel interface. A VPN counts as active when traffic
/// to the internet leaves through a tunnel interface. Merely having one is not
/// enough: macOS keeps several idle utunN interfaces up at all times.

#include "net/DefaultGateway.h"

#include <QHostAddress>
#include <QNetworkInterface>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace eMule {

struct RouterInfo {
    QHostAddress router;         ///< null when no router was found
    QString routerInterface;     ///< interface the router sits on, for display
    QString vpnInterface;        ///< non-empty when internet traffic leaves through a tunnel
};

/// Look up the router and the VPN state on this machine. Blocks briefly (< 1 s).
[[nodiscard]] RouterInfo locateRouter();

// -- Exposed for unit tests ---------------------------------------------------

/// True when an interface looks like a VPN or other tunnel, by type or name.
[[nodiscard]] bool looksLikeTunnel(QStringView name, QStringView humanName,
                                   QNetworkInterface::InterfaceType type);

/// Pick the router among default-route next hops. Skips tunnel interfaces and
/// IPv6 link-locals (browsers cannot open a zoned address); IPv4 wins over IPv6.
[[nodiscard]] std::optional<GatewayCandidate> pickRouter(
    const std::vector<GatewayCandidate>& candidates,
    const std::function<bool(const QString& interfaceName)>& isTunnel);

} // namespace eMule
