#pragma once
/// @file NatEnv.h
/// @brief Everything the NAT rendezvous code needs from the running app.
///
/// AppNatEnv is the real one (theApp, thePrefs, sockets); tests supply their own.

#include "net/Address.h"
#include "net/natt/NatTypes.h"
#include "utils/Types.h"

#include <QByteArray>

#include <memory>
#include <vector>

namespace eMule {

class Packet;
class Server;

namespace natt {

class NatEnv {
public:
    virtual ~NatEnv() = default;

    /// Monotonic milliseconds.
    [[nodiscard]] virtual uint64 now() const = 0;

    // -- Our side --
    [[nodiscard]] virtual Hash16 ourHash() const = 0;
    /// ED2K client ID on the current server, 0 when offline.
    [[nodiscard]] virtual uint32 ourClientID() const = 0;
    [[nodiscard]] virtual uint16 ourUdpPort() const = 0;
    [[nodiscard]] virtual uint16 ourTcpPort() const = 0;
    /// Our crypt bits (CONNECT_OPT_CRYPT_*).
    [[nodiscard]] virtual uint8 ourConnectOptions() const = 0;
    /// Our public IPv4, ED2K wire order (Address::fromNetworkOrder), 0 if unknown.
    [[nodiscard]] virtual uint32 ourPublicIPv4() const = 0;
    /// Confident global IPv6, null otherwise.
    [[nodiscard]] virtual Address ourPublicIPv6() const = 0;
    /// We cannot accept inbound TCP (no ED2K HighID, Kad not open).
    [[nodiscard]] virtual bool firewalled() const = 0;
    [[nodiscard]] virtual bool kadConnected() const = 0;
    /// Feature switch (natTraversal pref).
    [[nodiscard]] virtual bool enabled() const = 0;

    // -- Server --
    /// The server we are logged in to, or nullptr.
    [[nodiscard]] virtual const Server* currentServer() const = 0;
    /// The address the live session dialed.
    [[nodiscard]] virtual Address serverSessionAddress() const = 0;
    /// Other listed servers that pair any two PR_NAT hashes (UDP flag 0x40000), as
    /// IPv4 endpoints for a SYNC2. Never the current one. When @p peer's server is
    /// known only that one (a target registers only where it is logged in), else all.
    [[nodiscard]] virtual std::vector<Endpoint> rendezvousServers(const PeerInfo& peer) const = 0;

    // -- Policy --
    /// Good IP, not IP-filtered, not banned.
    [[nodiscard]] virtual bool admit(const Address& addr) const = 0;

    // -- I/O (all UDP leaves the client UDP socket, so the NAT mapping is the shared one) --
    /// Client datagram; @p cryptHash != nullptr obfuscates it for that user hash.
    virtual void sendPeerUdp(std::unique_ptr<Packet> packet, const Endpoint& to,
                             const uint8* cryptHash) = 0;
    /// Server datagram; @p serverKey != 0 obfuscates with the server UDP key.
    virtual void sendServerUdp(std::unique_ptr<Packet> packet, const Endpoint& to,
                               uint32 serverKey) = 0;
    /// The current server's UDP key and obfuscation port. Stat pings update the server
    /// list entry, never the session's copy of the server.
    [[nodiscard]] virtual ServerUdpCrypt serverUdpCrypt() const = 0;
    /// Stat-ping the current server soon: its UDP key is missing or may be stale.
    virtual void refreshServerKey() = 0;
    /// Pre-built bytes (PR_NAT frames).
    virtual void sendRawUdp(const QByteArray& data, const Endpoint& to) = 0;
    /// TCP packet on the live server session.
    virtual bool sendServerTcp(std::unique_ptr<Packet> packet) = 0;

    // -- Client glue --
    /// Open the TCP connection over IPv6 to the peer of @p attempt.
    virtual void dialPeer(const Attempt& attempt, const Address& ipv6) = 0;
    /// A TCP connection to the peer of @p attempt is up.
    [[nodiscard]] virtual bool peerConnected(const Attempt& attempt) const = 0;
    /// The attempt gave up; park the source (LowToLowIP).
    virtual void attemptFailed(const Attempt& attempt) = 0;
};

} // namespace natt
} // namespace eMule
