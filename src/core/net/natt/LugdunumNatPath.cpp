#include "pch.h"
/// @file LugdunumNatPath.cpp
/// @brief Lugdunum NAT callback: 0xA6 out, 0xA7 / 0x37 in, mutual callback.

#include "net/natt/LugdunumNatPath.h"

#include "net/EncryptedDatagramSocket.h"
#include "net/Packet.h"
#include "net/natt/NatEnv.h"
#include "net/natt/NatRendezvous.h"
#include "server/Server.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <cstring>

namespace eMule::natt {

namespace {

/// The server's IPv4: Lugdunum is IPv4-only.
Address serverIPv4(const NatEnv& env, const Server& server)
{
    const Address session = env.serverSessionAddress();
    return session.isIPv4() ? session : server.ipv4Address();
}

} // namespace

bool LugdunumNatPath::canServe(const PeerInfo& peer) const
{
    // Callback-capable targets always get an odd LowID (Lugdunum and ed2k-server 0.9.85+),
    // so an even one would only be dropped silently (andrey23127/ed2k-server#39).
    if ((peer.clientID & 1) == 0)
        return false;
    // The callback is IPv4 only. On an IPv6 session (eNode-go dual-stack) the server
    // cannot tie a 0xA6 from our IPv4 to the session, and PR_NAT covers that case.
    if (env().serverSessionAddress().isIPv6())
        return false;
    const Server* server = env().currentServer();
    // The UDP key and obfuscation port may still be missing: sources arrive before the
    // first stat ping answers, and sendRequest() waits for it.
    return server && server->supportsLugdunumNat() && !serverIPv4(env(), *server).isNull()
        && peerOnOurServer(peer);
}

bool LugdunumNatPath::request(Attempt& attempt)
{
    if (ourID() == 0 || attempt.peer.clientID == 0)
        return false;
    sendRequest(attempt);   // without a key yet, repeat() sends once it arrives
    return true;
}

void LugdunumNatPath::repeat(Attempt& attempt)
{
    // Requester: until the target's 0xA7 arrives. Target: the reciprocal request keeps
    // the server's 19 s UDP window open until the requester has answered us.
    const bool waiting = attempt.role == Role::Requester
        ? attempt.state == Attempt::State::Requested
        : !attempt.peerHandoffSeen;
    if (waiting && repeatDue(attempt, kRepeatMs, kMaxRequests))
        sendRequest(attempt);
}

bool LugdunumNatPath::sendRequest(Attempt& attempt)
{
    const Server* server = env().currentServer();
    const uint32 ourId = ourID();
    if (!server || ourId == 0 || attempt.peer.clientID == 0)
        return false;
    // eserver ignores a plaintext 0xA6: wait for the key and port from a stat ping
    const ServerUdpCrypt crypt = env().serverUdpCrypt();
    if (crypt.key == 0 || crypt.port == 0) {
        env().refreshServerKey();
        return false;
    }
    auto packet = std::make_unique<Packet>(OP_NAT_CALLBACKREQUEST, 8, OP_EDONKEYPROT);
    auto* out = reinterpret_cast<uint8*>(packet->pBuffer);
    pokeUInt32(out, attempt.peer.clientID);
    pokeUInt32(out + 4, ourId);
    attempt.lastRequestMs = env().now();
    ++attempt.requestCount;
    env().sendServerUdp(std::move(packet),
                        Endpoint(serverIPv4(env(), *server), crypt.port), crypt.key);
    // Still unanswered: a restarted eserver drops our old key without a word
    if (attempt.requestCount == kKeyRefreshAfter)
        env().refreshServerKey();
    return true;
}

bool LugdunumNatPath::handleServerTcp(uint8 opcode, const QByteArray& payload)
{
    if (opcode != OP_NAT_CALLBACKREQUESTED)
        return false;
    onNotice(reinterpret_cast<const uint8*>(payload.constData()), static_cast<uint32>(payload.size()));
    return true;
}

bool LugdunumNatPath::handleServerUdp(const Endpoint& from, const uint8* data, qsizetype len)
{
    const Server* server = env().currentServer();
    if (!server || !server->hasAddress(from.address()))
        return false;

    if (data[0] == OP_EDONKEYPROT) {
        if (data[1] != OP_NAT_CALLBACKREQUESTED_UDP)
            return false;
        onNotice(data + 2, static_cast<uint32>(len - 2));
        return true;
    }
    const uint32 key = env().serverUdpCrypt().key;
    if (key == 0)
        return false;
    // Decrypt a copy: anything that is not our notice goes on to the client path intact.
    QByteArray copy(reinterpret_cast<const char*>(data), len);
    const DecryptResult dr = EncryptedDatagramSocket::decryptReceivedServer(
        reinterpret_cast<uint8*>(copy.data()), static_cast<int>(copy.size()), key);
    if (!dr.data || dr.length < 2 || dr.data[0] != OP_EDONKEYPROT
        || dr.data[1] != OP_NAT_CALLBACKREQUESTED_UDP)
        return false;
    onNotice(dr.data + 2, static_cast<uint32>(dr.length - 2));
    return true;
}

std::optional<CallbackNotice> LugdunumNatPath::parseNotice(const NatEnv& env, const uint8* data, uint32 size)
{
    if (size < kNoticeSize)
        return std::nullopt;
    // eserver writes the flags byte before the hash (NeoLoader's parser has it backwards).
    CallbackNotice n;
    n.udp = Endpoint(Address::fromNetworkOrder(peekUInt32(data)), peekUInt16(data + 4));
    n.flags = data[10];
    n.peer.clientID = peekUInt32(data + 6);
    // Low bits = requester's (CT_SERVER_FLAGS >> 9) & 7, the CONNECT_OPT_CRYPT_* order.
    n.peer.cryptOptions = static_cast<uint8>(n.flags & 0x07);
    n.peer.cryptKnown = true;
    if ((n.flags & kFlagHasHash) && size >= kNoticeSize + 16) {
        std::memcpy(n.peer.hash.data(), data + kNoticeSize, 16);
        n.peer.hashKnown = true;
    }
    if (const Server* server = env.currentServer()) {
        n.peer.serverAddress = server->ipAddress();
        n.peer.serverPort = server->port();
    }
    return n;
}

void LugdunumNatPath::onNotice(const uint8* data, uint32 size)
{
    const auto n = parseNotice(env(), data, size);
    if (!n) {
        log(QStringLiteral("short notice (%1 bytes)").arg(size));
        return;
    }
    const Endpoint& udp = n->udp;
    const PeerInfo& peer = n->peer;
    const uint32 requesterID = peer.clientID;
    log(QStringLiteral("notice: id %1 at %2 flags 0x%3")
            .arg(requesterID).arg(udp.toString()).arg(n->flags, 2, 16, QLatin1Char('0')));

    // A callback to ourselves only reflects our own mapped port.
    if (requesterID == ourID())
        return;

    // The mutual-callback answer to our own request.
    if (Attempt* mine = hub().findAttempt([&](const Attempt& a) {
            return a.path == kind() && a.role == Role::Requester && a.peer.clientID == requesterID;
        })) {
        if (peer.hashKnown && !mine->peer.hashKnown) {
            mine->peer.hash = peer.hash;
            mine->peer.hashKnown = true;
        }
        mine->peer.cryptOptions = peer.cryptOptions;
        mine->peer.cryptKnown = true;
        hub().endpointKnown(*mine, udp);
        return;
    }

    // Someone wants us: answer with our own request so it learns our endpoint.
    Attempt* a = hub().acceptNotice(kind(), peer, udp, 0);
    if (a && a->role == Role::Target && a->requestCount == 0)
        sendRequest(*a);
}

} // namespace eMule::natt
