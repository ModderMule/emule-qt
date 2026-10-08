#pragma once

/// @file PortMapStatusText.h
/// @brief User-facing wording for the daemon's port-mapping status.
///
/// Takes the map PushPortMapStatus carries, which is also GetNetworkInfo's
/// `portmap` section. Widget-free; shared by the wizard and the Options dialog.

#include "portmap/PortMapTypes.h"

#include <QCborMap>
#include <QCoreApplication>
#include <QString>

namespace eMule {

enum class PortMapOutcome { Pending, Ok, Warning, Failed };

struct PortMapSummary {
    QString text;
    PortMapOutcome outcome = PortMapOutcome::Pending;
};

[[nodiscard]] inline PortMapSummary portMapStatusSummary(const QCborMap& info)
{
    const auto status = static_cast<PortMapStatus>(
        info.value(QLatin1StringView("status")).toInteger());
    const QString method = info.value(QLatin1StringView("methodText")).toString();
    const QString address = info.value(QLatin1StringView("externalAddress")).toString();

    // Literal translate() calls: lupdate cannot see a string passed through a helper
    using App = QCoreApplication;
    QString text;
    switch (status) {
    case PortMapStatus::Mapped:
        text = address.isEmpty()
            ? App::translate("PortMapStatus", "Ports forwarded via %1.").arg(method)
            : App::translate("PortMapStatus", "Ports forwarded via %1 (external address %2).")
                  .arg(method, address);
        return {text, PortMapOutcome::Ok};
    case PortMapStatus::Degraded:
        // Granted but unreachable: the router itself sits behind another NAT
        text = address.isEmpty()
            ? App::translate("PortMapStatus",
                             "The router granted the ports, but they are not reachable "
                             "from the Internet.")
            : App::translate("PortMapStatus",
                             "The router granted the ports, but its address %1 is not "
                             "public, so they stay unreachable over IPv4.").arg(address);
        return {text, PortMapOutcome::Warning};
    case PortMapStatus::NotMapped:
        return {App::translate("PortMapStatus",
                               "No router answered PCP, NAT-PMP or UPnP. Forward the ports manually."),
                PortMapOutcome::Failed};
    case PortMapStatus::Failed:
        return {App::translate("PortMapStatus", "The router refused the port forwarding."),
                PortMapOutcome::Failed};
    case PortMapStatus::Disabled:
        return {App::translate("PortMapStatus", "Automatic port forwarding is switched off."),
                PortMapOutcome::Failed};
    case PortMapStatus::Unknown:
    case PortMapStatus::Probing:
        break;
    }
    return {App::translate("PortMapStatus", "Asking the router..."), PortMapOutcome::Pending};
}

} // namespace eMule
