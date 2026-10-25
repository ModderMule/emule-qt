#include "pch.h"
/// @file KadBuddyNatPath.cpp
/// @brief Kad buddy rendezvous: envelope, endpoint hint, target unwrap.

#include "net/natt/KadBuddyNatPath.h"

#include "client/UpDownClient.h"
#include "net/Packet.h"
#include "net/natt/NatEnv.h"
#include "net/natt/NatRendezvous.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <algorithm>
#include <cstring>

namespace eMule::natt {

namespace {

constexpr uint8 kTransportHintNone = 0;
constexpr uint8 kTransportHintQuic = 2;

bool isZeroHash(const uint8* p)
{
    return std::all_of(p, p + 16, [](uint8 b) { return b == 0; });
}

} // namespace

KadBuddyNatPath::ServedPeer KadBuddyNatPath::ServedPeer::fromClient(const UpDownClient& client)
{
    ServedPeer s;
    if (client.hasValidHash())
        std::memcpy(s.hash.data(), client.userHash(), 16);
    // ToDo: prefer a fresh observed external UDP port of this client (per client, never
    // per IP) once we track one; the Kad port is the agreed fallback.
    const uint16 port = client.kadPort() != 0 ? client.kadPort() : client.udpPort();
    if (client.userAddress().isIPv4() && port != 0)
        s.udp = Endpoint(client.userAddress(), port);
    s.connectOptions = static_cast<uint8>(
        (client.supportsCryptLayer() ? CONNECT_OPT_CRYPT_SUPPORTED : 0)
        | (client.requestsCryptLayer() ? CONNECT_OPT_CRYPT_REQUESTED : 0)
        | (client.requiresCryptLayer() ? CONNECT_OPT_CRYPT_REQUIRED : 0));
    return s;
}

bool KadBuddyNatPath::canServe(const PeerInfo& peer) const
{
    return peer.hashKnown && peer.buddyValid && peer.buddyEndpoint.address().isIPv4()
        && peer.buddyEndpoint.port() != 0 && env().kadConnected() && env().ourUdpPort() != 0;
}

bool KadBuddyNatPath::request(Attempt& attempt)
{
    // Open our mapping towards the target before its punch arrives.
    const Endpoint& target = attempt.peer.kadEndpoint;
    if (target.port() != 0 && env().admit(target.address())) {
        for (int i = 0; i < kInitialPunches; ++i)
            env().sendPeerUdp(std::make_unique<Packet>(OP_HOLEPUNCH, 0, OP_EMULEPROT), target, nullptr);
    }
    sendEnvelope(attempt);
    return true;
}

void KadBuddyNatPath::repeat(Attempt& attempt)
{
    if (attempt.role == Role::Requester && attempt.state == Attempt::State::Requested
        && repeatDue(attempt, kRepeatMs, kMaxRequests))
        sendEnvelope(attempt);
}

void KadBuddyNatPath::sendEnvelope(Attempt& attempt)
{
    const uint32 publicIP = env().ourPublicIPv4();
    const bool withTail = publicIP != 0;
    auto packet = std::make_unique<Packet>(OP_REASKCALLBACKUDP,
                                           kEnvelopeSize + (withTail ? kEnvelopeTailSize : 0),
                                           OP_EMULEPROT);
    auto* out = reinterpret_cast<uint8*>(packet->pBuffer);
    std::memcpy(out, attempt.peer.buddyID.data(), 16);
    std::memset(out + 16, 0, 16);              // zero file hash: embedded opcode follows
    out[32] = OP_RENDEZVOUS;
    const Hash16 ours = env().ourHash();
    std::memcpy(out + 33, ours.data(), 16);
    out[49] = static_cast<uint8>(env().ourConnectOptions() | CONNECT_OPT_NATT_ENDPOINT_HINT);
    std::memcpy(out + 50, attempt.peer.fileHash.data(), 16);
    if (withTail) {
        pokeUInt32(out + 66, publicIP);      // self-reported; the buddy prefers what it sees
        pokeUInt16(out + 70, env().ourUdpPort());
        out[72] = env().quicAvailable() ? kTransportHintQuic : kTransportHintNone;
    }
    attempt.lastRequestMs = env().now();
    ++attempt.requestCount;
    // Plain, like the stock reask via buddy: we do not know the buddy's Kad version.
    env().sendPeerUdp(std::move(packet), attempt.peer.buddyEndpoint, nullptr);
}

bool KadBuddyNatPath::handlePeerPacket(uint8 opcode, const uint8* data, uint32 size, const Endpoint& from)
{
    if (opcode != OP_NATT_ENDPOINT_HINT)
        return false;
    onEndpointHint(data, size, from);
    return true;
}

void KadBuddyNatPath::onEndpointHint(const uint8* data, uint32 size, const Endpoint& from)
{
    if (size < kHintSize || data[0] != kHintVersion)
        return;
    Hash16 target{};
    std::memcpy(target.data(), data + 1, 16);
    const uint8* buddyID = data + 17;
    const Endpoint udp(Address::fromNetworkOrder(peekUInt32(data + 49)), peekUInt16(data + 53));

    // Only from the target's own serving buddy, naming the buddy ID we sent to.
    Attempt* a = hub().findAttempt([&](const Attempt& x) {
        return x.path == kind() && x.role == Role::Requester && x.peer.hash == target
            && x.peer.buddyEndpoint == from
            && std::memcmp(x.peer.buddyID.data(), buddyID, 16) == 0;
    });
    if (!a) {
        log(QStringLiteral("endpoint hint from %1 dropped").arg(from.toString()));
        return;
    }
    log(QStringLiteral("endpoint hint: target at %1").arg(udp.toString()));
    hub().endpointKnown(*a, udp);
}

void KadBuddyNatPath::onBuddyRelay(const Endpoint& requester, const uint8* data, uint32 size,
                                   const ServedPeer& served)
{
    if (size < kEnvelopeSize || !isZeroHash(data + 16) || data[32] != OP_RENDEZVOUS)
        return;
    if (!(data[49] & CONNECT_OPT_NATT_ENDPOINT_HINT) || served.udp.port() == 0
        || !env().admit(requester.address()))
        return;

    auto hint = std::make_unique<Packet>(OP_NATT_ENDPOINT_HINT, kHintSize, OP_EMULEPROT);
    auto* out = reinterpret_cast<uint8*>(hint->pBuffer);
    out[0] = kHintVersion;
    std::memcpy(out + 1, served.hash.data(), 16);
    std::memcpy(out + 17, data, 16);           // buddy ID as the requester sent it
    std::memcpy(out + 33, data + 50, 16);      // file hash
    pokeUInt32(out + 49, served.udp.address().toNetworkUint32());
    pokeUInt16(out + 53, served.udp.port());
    out[55] = served.connectOptions;
    env().sendPeerUdp(std::move(hint), requester, nullptr);
    env().sendPeerUdp(std::make_unique<Packet>(OP_HOLEPUNCH, 0, OP_EMULEPROT), requester, nullptr);
    log(QStringLiteral("hint for served client sent to %1").arg(requester.toString()));
}

bool KadBuddyNatPath::onRendezvous(const Endpoint& requester, const uint8* data, uint32 size)
{
    if (size < kRendezvousMin)
        return false;
    PeerInfo peer;
    std::memcpy(peer.hash.data(), data, 16);
    peer.hashKnown = true;
    std::memcpy(peer.fileHash.data(), data + 17, 16);

    // The endpoint the buddy saw wins; the self-reported tail is only a fallback.
    Endpoint udp = requester;
    if (size >= kRendezvousMin + 6 && (!isGoodIP(udp.address()) || udp.port() == 0)) {
        const Endpoint hinted(Address::fromNetworkOrder(peekUInt32(data + 33)), peekUInt16(data + 37));
        if (isGoodIP(hinted.address()) && hinted.port() != 0)
            udp = hinted;
    }
    log(QStringLiteral("rendezvous from %1").arg(udp.toString()));
    return hub().acceptNotice(kind(), peer, udp, 0) != nullptr;
}

} // namespace eMule::natt
