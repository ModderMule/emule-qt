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
    /// IPv4 the server sees our TCP session on (HighID, or OP_IDCHANGE), wire order, 0 if unknown.
    [[nodiscard]] virtual uint32 serverSeenIPv4() const = 0;
    /// IPv4 Kad nodes see our UDP socket on, wire order, 0 if unknown.
    [[nodiscard]] virtual uint32 udpSeenIPv4() const = 0;
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
    /// A source's server for the foreign callback: the server list entry, else (pref
    /// natDialUnlistedServers) an unlisted one confirmed by one plain status ping, which
    /// @p probe sends when none went out yet. Never added to the server list.
    [[nodiscard]] virtual ForeignServer foreignServer(const Address& addr, uint16 tcpPort, bool probe) = 0;
    /// Stat-ping that listed server soon: its UDP key is missing or may be stale.
    virtual void refreshForeignServerKey(const Address& addr, uint16 tcpPort) = 0;

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

    // -- QUIC NAT-T transport (docs/protocol/natt-quic.md) --
    /// The transport works here (built in, TLS up, feature on).
    [[nodiscard]] virtual bool quicAvailable() const { return false; }
    /// The peer's endpoint is known: accept its QUIC dial for a while, obfuscate for its
    /// hash, tell it our caps.
    virtual void armQuic(const Attempt& attempt) { (void)attempt; }
    /// Dial the peer of @p attempt over QUIC at its punched UDP endpoint.
    virtual void dialPeerQuic(const Attempt& attempt) { (void)attempt; }
    /// A QUIC connection to the peer is being set up or carries its session.
    [[nodiscard]] virtual bool quicActive(const Attempt& attempt) const { (void)attempt; return false; }
    /// A 0xB2 frame of the transport (0x01-0x03, 0xFF); false when not one.
    virtual bool handleQuicFrame(uint8 type, const uint8* data, uint32 size, const Endpoint& from,
                                 bool obfuscated)
    {
        (void)type; (void)data; (void)size; (void)from; (void)obfuscated;
        return false;
    }
};

} // namespace natt
} // namespace eMule
