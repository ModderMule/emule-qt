#include "pch.h"
/// @file Ed2kServerHolePunchPath.cpp
/// @brief ed2k-server LowID hole punch: 0x60/0x61/0x62 + 0x9F keepalive.

#include "net/natt/Ed2kServerHolePunchPath.h"

#include "net/Packet.h"
#include "net/natt/NatEnv.h"
#include "net/natt/NatRendezvous.h"
#include "server/Server.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <cstring>

namespace eMule::natt {

bool Ed2kServerHolePunchPath::canServe(const PeerInfo& peer) const
{
    const Server* server = env().currentServer();
    return server && server->supportsLowIdHolePunch() && env().ourUdpPort() != 0
        && peerOnOurServer(peer);
}

bool Ed2kServerHolePunchPath::request(Attempt& attempt)
{
    sendRequest(attempt);
    return true;
}

void Ed2kServerHolePunchPath::repeat(Attempt& attempt)
{
    // The server re-sends 0x61 itself; re-ask only when nothing came back.
    if (attempt.role == Role::Requester && attempt.state == Attempt::State::Requested
        && repeatDue(attempt, kRepeatMs, kMaxRequests))
        sendRequest(attempt);
}

void Ed2kServerHolePunchPath::sendRequest(Attempt& attempt)
{
    auto packet = std::make_unique<Packet>(OP_LOWID_HOLEPUNCH_REQUEST, 6, OP_EDONKEYPROT);
    auto* out = reinterpret_cast<uint8*>(packet->pBuffer);
    pokeUInt32(out, attempt.peer.clientID);
    pokeUInt16(out + 4, env().ourUdpPort());
    attempt.lastRequestMs = env().now();
    ++attempt.requestCount;
    if (!env().sendServerTcp(std::move(packet)))
        log(QStringLiteral("0x60 not sent (no server session)"));
}

void Ed2kServerHolePunchPath::tick()
{
    const Server* server = env().currentServer();
    if (!server || !server->supportsLowIdHolePunch() || !env().enabled() || env().ourUdpPort() == 0) {
        m_keepaliveTo = {};
        return;
    }
    // From the client UDP socket, to the server's UDP port (TCP + 4), plain.
    const Endpoint to(env().serverSessionAddress(), static_cast<uint16>(server->port() + 4));
    const uint64 now = env().now();
    if (to == m_keepaliveTo && now - m_lastKeepaliveMs < kKeepaliveMs)
        return;
    m_keepaliveTo = to;
    m_lastKeepaliveMs = now;
    auto packet = std::make_unique<Packet>(OP_SERVER_NATT_KEEPALIVE, 16, OP_EDONKEYPROT);
    const Hash16 hash = env().ourHash();
    std::memcpy(packet->pBuffer, hash.data(), 16);
    env().sendServerUdp(std::move(packet), to, 0);
}

bool Ed2kServerHolePunchPath::handleServerTcp(uint8 opcode, const QByteArray& payload)
{
    const auto* data = reinterpret_cast<const uint8*>(payload.constData());
    const auto size = static_cast<uint32>(payload.size());
    if (opcode == OP_LOWID_HOLEPUNCH_INFO) {
        onInfo(data, size);
        return true;
    }
    if (opcode == OP_LOWID_HOLEPUNCH_FAIL) {
        onFail(data, size);
        return true;
    }
    return false;
}

void Ed2kServerHolePunchPath::onInfo(const uint8* data, uint32 size)
{
    if (size < kInfoMinSize) {
        log(QStringLiteral("short 0x61 (%1 bytes)").arg(size));
        return;
    }
    const Endpoint udp(Address::fromNetworkOrder(peekUInt32(data)), peekUInt16(data + 6));
    const uint16 tcpPort = peekUInt16(data + 4);
    Hash16 hash{};
    std::memcpy(hash.data(), data + 8, 16);
    const uint8 role = data[24];
    // Fields may be appended later, never inserted: read peer_id only when present.
    const uint32 peerID = size >= kInfoIdSize ? peekUInt32(data + 25) : 0;
    log(QStringLiteral("0x61 peer %1 tcp %2 id %3 role %4").arg(udp.toString()).arg(tcpPort).arg(peerID).arg(role));

    const Server* server = env().currentServer();
    PeerInfo peer;
    peer.hash = hash;
    peer.hashKnown = true;
    peer.clientID = peerID;
    if (server) {
        peer.serverAddress = server->ipAddress();
        peer.serverPort = server->port();
    }

    // Our own request: matched by the target ID the server echoes (0.9.84+), else by hash,
    // else the single request still waiting.
    Attempt* mine = hub().findAttempt([&](const Attempt& a) {
        if (a.path != kind() || a.role != Role::Requester)
            return false;
        if (peerID != 0)
            return a.peer.clientID == peerID;
        return a.peer.hashKnown && a.peer.hash == hash;
    });
    if (!mine && peerID == 0) {
        const auto waiting = hub().findAll([&](const Attempt& a) {
            return a.path == kind() && a.role == Role::Requester && a.state == Attempt::State::Requested;
        });
        if (waiting.size() == 1)
            mine = waiting.front();
    }
    if (mine) {
        mine->peer.hash = hash;
        mine->peer.hashKnown = true;
        hub().endpointKnown(*mine, udp, tcpPort);
        return;
    }
    hub().acceptNotice(kind(), peer, udp, tcpPort);
}

void Ed2kServerHolePunchPath::onFail(const uint8* data, uint32 size)
{
    if (size < 5)
        return;
    const uint32 target = peekUInt32(data);
    const uint8 reason = data[4];
    if (Attempt* a = hub().findAttempt([&](const Attempt& x) {
            return x.path == kind() && x.role == Role::Requester && x.peer.clientID == target;
        }))
        hub().fail(*a, QStringLiteral("server refused (0x62 reason %1)").arg(reason));
}

} // namespace eMule::natt
