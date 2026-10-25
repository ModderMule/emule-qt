#include "pch.h"
/// @file Ed2kServerHolePunchPath.cpp
/// @brief ed2k-server LowID hole punch: 0x60/0x61/0x62 + 0x9F keepalive, and the
///        0x100000 extension (0x37 wake-up, 0x9F burst, 30-byte 0x61).

#include "net/natt/Ed2kServerHolePunchPath.h"

#include "net/natt/LugdunumNatPath.h"

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
    // Woken by 0x37 and no 0x61 followed: it came from a plain 0xA6 (Lugdunum-only
    // requester), so answer it Lugdunum's way with the reciprocal 0xA6. A foreign
    // requester (ID 0) has no ID to name: its server sends it our endpoint, we punch on.
    if (attempt.role == Role::Target && attempt.awaitingInfo && env().now() >= attempt.infoDeadlineMs) {
        attempt.awaitingInfo = false;
        if (attempt.foreignWake) {
            log(QStringLiteral("#%1: no 0x61 after a foreign 0x37, punching on").arg(attempt.id));
            return;
        }
        if (NatPath* lugdunum = hub().path(PathKind::Lugdunum)) {
            log(QStringLiteral("#%1: no 0x61 after 0x37, answering with 0xA6").arg(attempt.id));
            attempt.path = PathKind::Lugdunum;
            attempt.requestCount = 0;
            attempt.lastRequestMs = 0;
            lugdunum->request(attempt);
        }
        return;
    }
    // The server re-sends 0x61 itself; re-ask only when nothing came back.
    if (attempt.role == Role::Requester && attempt.state == Attempt::State::Requested
        && repeatDue(attempt, kRepeatMs, kMaxRequests))
        sendRequest(attempt);
}

void Ed2kServerHolePunchPath::sendRequest(Attempt& attempt)
{
    // Extension: a fresh 0x9F with each 0x60, so the 0x37 carries our current port.
    if (extensionActive())
        sendKeepalive(*env().currentServer());
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
        m_session = {};
        m_sessionID = 0;
        m_sentObfuscated = false;
        return;
    }
    const Endpoint session(env().serverSessionAddress(), server->port());
    const bool fresh = session != m_session || env().ourClientID() != m_sessionID;
    if (fresh) {
        m_session = session;
        m_sessionID = env().ourClientID();
        m_sentObfuscated = false;
    }
    if (fresh || env().now() - m_lastKeepaliveMs >= kKeepaliveMs)
        sendKeepalive(*server);
}

bool Ed2kServerHolePunchPath::fastTick()
{
    const Server* server = env().currentServer();
    const auto pending = hub().findAll([&](const Attempt& a) {
        return a.path == kind() && a.role == Role::Target && a.burstLeft > 0;
    });
    if (!server)
        return false;
    const uint64 now = env().now();
    bool more = false;
    for (Attempt* a : pending) {
        const uint64 due = a->burstStartMs + (kBurstCount - a->burstLeft) * kBurstStepMs;
        if (now >= due) {
            sendKeepalive(*server, a->foreignWake);
            --a->burstLeft;
        }
        more = more || a->burstLeft > 0;
    }
    return more;
}

bool Ed2kServerHolePunchPath::sendKeepalive(const Server& server, bool obfuscatedOnly)
{
    m_lastKeepaliveMs = env().now();
    // From the client UDP socket. Extension servers take it server-key obfuscated
    // (IPv4 only); the others get it plain, to the server's UDP port (TCP + 4). The
    // burst after a foreign 0x37 goes obfuscated only: the key proves our address.
    const Address address = env().serverSessionAddress();
    const ServerUdpCrypt crypt = env().serverUdpCrypt();
    const bool obfuscate = (server.supportsHolePunchExtension() || obfuscatedOnly) && address.isIPv4()
        && crypt.key != 0 && crypt.port != 0 && (env().ourConnectOptions() & CONNECT_OPT_CRYPT_SUPPORTED);
    if (!obfuscate && (m_sentObfuscated || obfuscatedOnly)) {
        // The server ignores plain ones now; they would not refresh our port.
        env().refreshServerKey();
        log(QStringLiteral("0x9F skipped: no server key"));
        return false;
    }
    auto packet = std::make_unique<Packet>(OP_SERVER_NATT_KEEPALIVE, 16, OP_EDONKEYPROT);
    const Hash16 hash = env().ourHash();
    std::memcpy(packet->pBuffer, hash.data(), 16);
    if (obfuscate) {
        m_sentObfuscated = true;
        env().sendServerUdp(std::move(packet), Endpoint(address, crypt.port), crypt.key);
    } else {
        env().sendServerUdp(std::move(packet), Endpoint(address, static_cast<uint16>(server.port() + 4)), 0);
    }
    return true;
}

bool Ed2kServerHolePunchPath::extensionActive() const
{
    const Server* server = env().currentServer();
    return server && server->supportsHolePunchExtension() && env().enabled() && env().ourUdpPort() != 0;
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
    // Ahead of LugdunumNatPath: on an extension server a 0x37 is our wake-up call, and
    // so is one naming requester ID 0 (foreign callback) on any server.
    if (opcode == OP_NAT_CALLBACKREQUESTED
        && (extensionActive() || (size >= LugdunumNatPath::kNoticeSize && peekUInt32(data + 6) == 0))) {
        onCallbackNotice(data, size);
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
    // Extension pair: the peer's crypt bits, and the role byte is binding for both.
    const bool extended = size >= kInfoCryptSize;
    const auto peerCrypt = static_cast<uint8>(extended ? (data[29] & 0x07) : 0);
    log(QStringLiteral("0x61 peer %1 tcp %2 id %3 role %4%5")
            .arg(udp.toString()).arg(tcpPort).arg(peerID).arg(role)
            .arg(extended ? QStringLiteral(" crypt 0x%1").arg(peerCrypt, 0, 16) : QString()));

    const Server* server = env().currentServer();
    PeerInfo peer;
    peer.hash = hash;
    peer.hashKnown = true;
    peer.clientID = peerID;
    if (server) {
        peer.serverAddress = server->ipAddress();
        peer.serverPort = server->port();
    }
    if (extended) {
        peer.cryptOptions = peerCrypt;
        peer.cryptKnown = true;
    }
    const auto apply = [&](Attempt& a) {
        a.peer.hash = hash;
        a.peer.hashKnown = true;
        a.awaitingInfo = false;
        if (extended) {
            a.serverRole = role == 0 ? 0 : 1;
            if (!hub().applyPeerCrypt(a, peerCrypt))
                return;
        }
        hub().endpointKnown(a, udp, tcpPort);
    };

    // Woken by 0x37 (extension): the 0x61 that releases us names the requester's ID.
    // A foreign requester's 0x37 had none: match its address.
    if (Attempt* woken = hub().findAttempt([&](const Attempt& a) {
            return a.path == kind() && a.role == Role::Target && a.awaitingInfo
                && ((peerID != 0 && a.peer.clientID == peerID) || (a.peer.hashKnown && a.peer.hash == hash)
                    || (a.foreignWake && a.peerUdp.address() == udp.address()));
        })) {
        apply(*woken);
        return;
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
        apply(*mine);
        return;
    }
    if (extended && !cryptCompatible(env().ourConnectOptions(), peerCrypt)) {
        log(QStringLiteral("0x61 dropped: crypt mismatch"));
        return;
    }
    if (Attempt* a = hub().acceptNotice(kind(), peer, udp, tcpPort); a && extended)
        a->serverRole = role == 0 ? 0 : 1;
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

void Ed2kServerHolePunchPath::onCallbackNotice(const uint8* data, uint32 size)
{
    const auto n = LugdunumNatPath::parseNotice(env(), data, size);
    if (!n) {
        log(QStringLiteral("short 0x37 (%1 bytes)").arg(size));
        return;
    }
    PeerInfo peer = n->peer;
    const bool foreign = peer.clientID == 0;
    if (!foreign && peer.clientID == ourID())
        return;
    log(QStringLiteral("0x37: id %1 at %2 flags 0x%3")
            .arg(peer.clientID).arg(n->udp.toString()).arg(n->flags, 2, 16, QLatin1Char('0')));
    if (foreign) {
        // Logged in elsewhere: no hash, and crypt bits only when it sent them.
        peer.kadEndpoint = n->udp;   // keys the attempt by address (NatRendezvous::peerKey)
        peer.cryptKnown = (n->flags & 0x07) != 0;
        // The requester re-sends its 0xA6: a repeat 0x37 only refreshes the port.
        if (Attempt* same = hub().findAttempt([&](const Attempt& x) {
                return x.foreignWake && x.peerUdp.address() == n->udp.address();
            })) {
            hub().endpointKnown(*same, n->udp);
            return;
        }
    }
    if (peer.cryptKnown && !cryptCompatible(env().ourConnectOptions(), peer.cryptOptions)) {
        log(QStringLiteral("0x37 dropped: crypt mismatch"));
        return;
    }
    const bool known = !foreign && hub().findForPeer(peer) != nullptr;
    // Punching starts at once, towards the requester's freshest port.
    Attempt* a = hub().acceptNotice(kind(), peer, n->udp, 0);
    if (!a || known || a->role != Role::Target)
        return;
    a->foreignWake = foreign;
    // 0x9F at 0, 0.5 and 1 s releases the 0x61 with our fresh port; dial only as it says.
    a->awaitingInfo = true;
    a->infoDeadlineMs = env().now() + kInfoWaitMs;
    a->burstLeft = kBurstCount;
    a->burstStartMs = env().now();
    if (fastTick())
        hub().wantFastTick();
}

} // namespace eMule::natt
