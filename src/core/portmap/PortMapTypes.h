#pragma once

/// @file PortMapTypes.h
/// @brief Protocol-independent vocabulary for the port-mapping subsystem.
///
/// Deliberately free of Qt objects and I/O so the codecs, the facade and the
/// tests can all share it without pulling in a backend.

#include "net/Address.h"
#include "utils/Types.h"

#include <QString>

#include <tuple>

namespace eMule {

/// Transport a mapping applies to. Values are the IANA protocol numbers PCP
/// puts on the wire (RFC 6887 section 11.1), so no translation table is needed.
enum class PortMapProtocol : uint8 { Tcp = 6, Udp = 17 };

enum class PortMapFamily : uint8 { IPv4, IPv6 };

/// Which protocol produced a mapping. The ordinal *is* the preference order —
/// lower wins a tie in the backend race — so do not reorder casually.
enum class PortMapMethod : uint8 { None = 0, Pcp = 1, NatPmp = 2, UPnP = 3 };

/// What a mapping is for.
///
/// There is deliberately no `Ed2kServerUdp`: UDPSocket only ever receives
/// OP_GLOBSEARCHRES / OP_GLOBFOUNDSOURCES / OP_GLOBSERVSTATRES /
/// OP_SERVER_DESC_RES (UDPSocket.cpp:315-335), all of them replies to a request
/// we sent first. Our own outbound datagram opens the NAT binding, so an inbound
/// mapping would add attack surface and buy nothing.
enum class PortMapPurpose : uint8 { Ed2kTcp, Ed2kClientUdp, WebServer };

/// Lifecycle of the subsystem as a whole, and of one mapping.
///
/// `Degraded` is load-bearing rather than cosmetic: a mapping can be granted and
/// still be unreachable, either because the external port differs from the
/// internal one (eD2K advertises thePrefs.port() and has no external-port tag,
/// so a mismatch is a silent LowID) or because the external address is CGNAT
/// space. Reporting either as `Mapped` would tell the user "forwarded" next to a
/// permanently firewalled client.
enum class PortMapStatus : uint8 {
    Unknown,        ///< not started
    Disabled,       ///< switched off by preference
    Probing,        ///< backend race in flight
    Mapped,         ///< every mapping granted, ports and address usable
    Degraded,       ///< granted, but not usable from the Internet
    NotMapped,      ///< no backend could map
    Failed,         ///< a backend answered and refused
};

[[nodiscard]] QString portMapMethodName(PortMapMethod method);
[[nodiscard]] QString portMapStatusName(PortMapStatus status);
[[nodiscard]] QString portMapPurposeName(PortMapPurpose purpose);

/// One mapping we want to exist. Desired state, not an outcome.
struct PortMapRequest {
    PortMapPurpose  purpose = PortMapPurpose::Ed2kTcp;
    PortMapProtocol protocol = PortMapProtocol::Tcp;
    PortMapFamily   family = PortMapFamily::IPv4;
    uint16          internalPort = 0;
    Address         internalClient;   ///< null: backend fills in (LAN v4 / GUA v6)
    QString         description;      ///< UPnP only; ignored by PCP and NAT-PMP

    /// Identity of the mapping, independent of which backend serves it.
    [[nodiscard]] auto key() const noexcept
    {
        return std::tie(purpose, protocol, family);
    }

    [[nodiscard]] bool operator==(const PortMapRequest& other) const noexcept
    {
        return key() == other.key() && internalPort == other.internalPort;
    }
};

/// One mapping that currently exists, as the router described it.
struct PortMapping {
    PortMapRequest request;
    uint16        externalPort = 0;
    Address       externalAddress;
    uint32        lifetimeSecs = 0;     ///< 0 = indefinite (IGD1 permanent lease)
    qint64        grantedAtMs = 0;      ///< monotonic
    PortMapMethod method = PortMapMethod::None;
    QByteArray    opaqueId;             ///< PCP nonce / IGD2 pinhole UniqueID

    /// True when the router honoured the port we asked for. eD2K cannot
    /// advertise anything else, so a false here means LowID.
    [[nodiscard]] bool portMatches() const noexcept
    {
        return externalPort != 0 && externalPort == request.internalPort;
    }

    /// True when the mapping is actually reachable from the Internet.
    /// A CGNAT address (100.64.0.0/10) fails isPublicIP() and lands here.
    [[nodiscard]] bool isUsable() const
    {
        return portMatches() && externalAddress.isPublicIP();
    }
};

} // namespace eMule

// Needed so these cross a thread boundary in queued signals (UPnPWorker runs on
// its own thread). Address/Endpoint are already declared in net/Address.h.
Q_DECLARE_METATYPE(eMule::PortMapRequest)
Q_DECLARE_METATYPE(eMule::PortMapping)
