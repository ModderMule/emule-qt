#include "pch.h"
/// @file PortMapTypes.cpp
/// @brief Display names for the port-mapping vocabulary.

#include "portmap/PortMapTypes.h"

#include <QCoreApplication>

namespace eMule {

QString portMapMethodName(PortMapMethod method)
{
    switch (method) {
    case PortMapMethod::Pcp:    return QStringLiteral("PCP");
    case PortMapMethod::NatPmp: return QStringLiteral("NAT-PMP");
    case PortMapMethod::UPnP:   return QStringLiteral("UPnP");
    case PortMapMethod::None:   break;
    }
    return QStringLiteral("none");
}

QString portMapStatusName(PortMapStatus status)
{
    switch (status) {
    case PortMapStatus::Unknown:   return QCoreApplication::translate("PortMap", "Unknown");
    case PortMapStatus::Disabled:  return QCoreApplication::translate("PortMap", "Disabled");
    case PortMapStatus::Probing:   return QCoreApplication::translate("PortMap", "Probing");
    case PortMapStatus::Mapped:    return QCoreApplication::translate("PortMap", "Forwarded");
    case PortMapStatus::Degraded:  return QCoreApplication::translate("PortMap", "Forwarded (not reachable)");
    case PortMapStatus::NotMapped: return QCoreApplication::translate("PortMap", "Not forwarded");
    case PortMapStatus::Failed:    return QCoreApplication::translate("PortMap", "Failed");
    }
    return {};
}

QString portMapPurposeName(PortMapPurpose purpose)
{
    switch (purpose) {
    case PortMapPurpose::Ed2kTcp:       return QStringLiteral("eD2K TCP");
    case PortMapPurpose::Ed2kClientUdp: return QStringLiteral("eD2K/Kad UDP");
    case PortMapPurpose::WebServer:     return QStringLiteral("Web server");
    }
    return {};
}

} // namespace eMule
