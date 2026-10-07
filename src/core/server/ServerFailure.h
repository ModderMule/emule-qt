#pragma once

/// @file ServerFailure.h
/// @brief Where and why a server connection failed.
///
/// One value per failed attempt, for the log and for ServerConnect's decision
/// whether the failure says something about the server.

#include <QLatin1StringView>

namespace eMule {

struct ServerFailure {
    enum class Phase {
        None,          ///< no failure recorded
        Resolve,       ///< dynIP hostname lookup
        SocketSetup,   ///< local socket / bind / proxy, before the dial
        Connect,       ///< TCP connect
        Handshake,     ///< obfuscation handshake and login
        Established    ///< logged in
    };

    enum class Reason {
        None,
        ConnectionRefused,
        DnsResolution,
        LocalBindInterface,     ///< our interface, route, bind or proxy
        TimeoutUnreachable,
        ProtocolRejection,      ///< closed on us, garbage, or login refused
        EstablishedDisconnect,
        TransportOther
    };

    Phase phase = Phase::None;
    Reason reason = Reason::None;

    [[nodiscard]] bool isSet() const { return phase != Phase::None; }

    [[nodiscard]] QLatin1StringView phaseName() const
    {
        using namespace Qt::StringLiterals;
        switch (phase) {
        case Phase::Resolve:     return "resolve"_L1;
        case Phase::SocketSetup: return "socket-setup"_L1;
        case Phase::Connect:     return "connect"_L1;
        case Phase::Handshake:   return "handshake"_L1;
        case Phase::Established: return "established"_L1;
        case Phase::None:        break;
        }
        return "none"_L1;
    }

    [[nodiscard]] QLatin1StringView reasonName() const
    {
        using namespace Qt::StringLiterals;
        switch (reason) {
        case Reason::ConnectionRefused:     return "connection-refused"_L1;
        case Reason::DnsResolution:         return "dns-resolution"_L1;
        case Reason::LocalBindInterface:    return "local-bind-interface"_L1;
        case Reason::TimeoutUnreachable:    return "timeout-unreachable"_L1;
        case Reason::ProtocolRejection:     return "protocol-rejection"_L1;
        case Reason::EstablishedDisconnect: return "established-disconnect"_L1;
        case Reason::TransportOther:        return "transport-other"_L1;
        case Reason::None:                  break;
        }
        return "none"_L1;
    }

    friend bool operator==(const ServerFailure&, const ServerFailure&) = default;
};

} // namespace eMule
