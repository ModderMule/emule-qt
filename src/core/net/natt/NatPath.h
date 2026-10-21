#pragma once
/// @file NatPath.h
/// @brief Base class of the four NAT rendezvous paths (one per introducer).
///
/// A path knows one introducer's wire format: it sends the request, parses the
/// introducer's answer and hands the peer's UDP endpoint to the hub. Punching,
/// the IPv6 handoff, timeouts and failure marks are the hub's.

#include "net/Address.h"
#include "net/natt/NatTypes.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QString>

namespace eMule {

class Server;

namespace natt {

class NatEnv;
class NatRendezvous;

class NatPath {
public:
    explicit NatPath(NatRendezvous& hub) : m_hub(hub) {}
    virtual ~NatPath() = default;

    NatPath(const NatPath&) = delete;
    NatPath& operator=(const NatPath&) = delete;

    [[nodiscard]] virtual PathKind kind() const = 0;

    /// This path can introduce us to @p peer right now.
    [[nodiscard]] virtual bool canServe(const PeerInfo& peer) const = 0;

    /// Send the first request for a requester attempt. False = nothing sent.
    virtual bool request(Attempt& attempt) = 0;

    /// Per tick for each live attempt of this path; resend when due.
    virtual void repeat(Attempt& /*attempt*/) {}

    /// Per tick (keepalives, registrations).
    virtual void tick() {}

    /// Notice on the server TCP session. True when consumed.
    virtual bool handleServerTcp(uint8 /*opcode*/, const QByteArray& /*payload*/) { return false; }

    /// Datagram from a server on the client UDP socket. True when consumed.
    virtual bool handleServerUdp(const Endpoint& /*from*/, const uint8* /*data*/, qsizetype /*len*/)
    {
        return false;
    }

    /// OP_EMULEPROT client datagram (OP_NATT_ENDPOINT_HINT). True when consumed.
    virtual bool handlePeerPacket(uint8 /*opcode*/, const uint8* /*data*/, uint32 /*size*/,
                                  const Endpoint& /*from*/)
    {
        return false;
    }

protected:
    [[nodiscard]] NatRendezvous& hub() const { return m_hub; }
    [[nodiscard]] NatEnv& env() const;

    /// Logged in to a server and @p peer is a LowID on that same server.
    [[nodiscard]] bool peerOnOurServer(const PeerInfo& peer) const;

    /// Server-logged ID of ours on the current server, 0 when offline.
    [[nodiscard]] uint32 ourID() const;

    void log(const QString& text) const;

    /// Request repeat helper: due when @p intervalMs passed and fewer than @p maxCount sent.
    [[nodiscard]] bool repeatDue(const Attempt& attempt, uint64 intervalMs, uint32 maxCount) const;

private:
    NatRendezvous& m_hub;
};

} // namespace natt
} // namespace eMule
