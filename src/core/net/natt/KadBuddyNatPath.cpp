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
    // The source of its latest Kad packet to us (per client, never per IP): a NAT keeps
    // the outside port across destinations, not the inside one. Kad port as fallback.
    if (client.servedUdpPort() != 0 && client.servedUdpAddress().isIPv4()) {
        s.udp = Endpoint(client.servedUdpAddress(), client.servedUdpPort());
    } else {
        const uint16 port = client.kadPort() != 0 ? client.kadPort() : client.udpPort();
        if (client.userAddress().isIPv4() && port != 0)
            s.udp = Endpoint(client.userAddress(), port);
    }
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
    // Always the full 73 bytes (zeros = unknown): stock parsers read up to the hint byte,
    // so the extension block after it is safe from them.
    const Address v6 = env().ourPublicIPv6();
    const bool withUdp6 = v6.isIPv6() && env().ourUdpPort() != 0;
    const uint32 blockSize = withUdp6 ? 2u + kExtUdp6Size : 0u;
    auto packet = std::make_unique<Packet>(OP_REASKCALLBACKUDP,
                                           kEnvelopeSize + kEnvelopeTailSize + blockSize, OP_EMULEPROT);
    auto* out = reinterpret_cast<uint8*>(packet->pBuffer);
    std::memcpy(out, attempt.peer.buddyID.data(), 16);
    std::memset(out + 16, 0, 16);              // zero file hash: embedded opcode follows
    out[32] = OP_RENDEZVOUS;
    const Hash16 ours = env().ourHash();
    std::memcpy(out + 33, ours.data(), 16);
    // transports in the opts too (eMuleAI): served nodes reading only the bits see QUIC, tail or not
    uint8 opts = env().ourConnectOptions() | CONNECT_OPT_NATT_ENDPOINT_HINT;
    if (env().quicAvailable())
        opts |= CONNECT_OPT_NATT_QUIC;
    out[49] = opts;
    std::memcpy(out + 50, attempt.peer.fileHash.data(), 16);
    const uint32 publicIP = env().ourPublicIPv4();
    pokeUInt32(out + 66, publicIP);            // self-reported; the buddy prefers what it sees
    pokeUInt16(out + 70, publicIP != 0 ? env().ourUdpPort() : uint16{0});
    out[72] = env().quicAvailable() ? kTransportHintQuic : kTransportHintNone;
    if (withUdp6) {
        // ToDo: the NAT66-mapped port once we learn one; the bound port is the fallback
        out[73] = kExtUdp6;
        out[74] = kExtUdp6Size;
        std::memcpy(out + 75, v6.ipv6Bytes().data(), 16);
        pokeUInt16(out + 91, env().ourUdpPort());
        attempt.offeredUdp6 = true;
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

    SentHint h;
    h.requester = requester;
    h.servedHash = served.hash;
    std::memcpy(h.buddyID.data(), data, 16);       // buddy ID as the requester sent it
    std::memcpy(h.fileHash.data(), data + 50, 16);
    h.udp = served.udp;
    h.atMs = env().now();
    sendHint(h, served.connectOptions);
    env().sendPeerUdp(std::make_unique<Packet>(OP_HOLEPUNCH, 0, OP_EMULEPROT), requester, nullptr);
    log(QStringLiteral("hint for served client sent to %1").arg(requester.toString()));

    // Kept for a refresh the served node sends on this rendezvous (natt-rendezvous.md)
    pruneHints();
    std::erase_if(m_sentHints, [&](const SentHint& x) {
        return x.requester == requester && x.servedHash == served.hash;
    });
    m_sentHints.push_back(h);
}

void KadBuddyNatPath::onServedRefresh(const ServedPeer& served)
{
    pruneHints();
    if (served.udp.port() == 0)
        return;
    for (SentHint& h : m_sentHints) {
        if (h.servedHash != served.hash || h.udp == served.udp)
            continue;
        if (!env().admit(h.requester.address()))
            continue;
        log(QStringLiteral("served client moved %1 -> %2, hint again to %3")
                .arg(h.udp.toString(), served.udp.toString(), h.requester.toString()));
        h.udp = served.udp;
        sendHint(h, served.connectOptions);
    }
}

void KadBuddyNatPath::tick()
{
    pruneHints();
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
    const Endpoint udp6 = size > kRendezvousFull
        ? parseExtUdp6(data + kRendezvousFull, size - kRendezvousFull) : Endpoint();
    const bool haveUdp6 = udp6.port() != 0 && env().ourPublicIPv6().isIPv6();
    log(QStringLiteral("rendezvous from %1%2").arg(udp.toString(),
        udp6.port() != 0 ? QStringLiteral(" / %1").arg(udp6.toString()) : QString()));

    Attempt* a = hub().acceptNotice(kind(), peer, udp, 0);
    // no usable IPv4 endpoint: the IPv6 one alone still works
    if (!a && haveUdp6 && !isGoodIP(udp.address()))
        a = hub().acceptNotice(kind(), peer, udp6, 0);
    if (a && haveUdp6)
        hub().addPeerUdp6(*a, udp6);
    return a != nullptr;
}

Endpoint KadBuddyNatPath::parseExtUdp6(const uint8* data, uint32 size)
{
    Endpoint found;
    uint32 pos = 0;
    while (pos < size) {
        if (size - pos < 2)
            return {};
        const uint8 type = data[pos];
        const uint8 len = data[pos + 1];
        pos += 2;
        if (len > size - pos)
            return {};                         // runs past the end: ignore the whole block
        // a longer value may grow later: read our 18 bytes
        if (type == kExtUdp6 && len >= kExtUdp6Size && found.port() == 0) {
            const Address addr = Address::fromIPv6Bytes(data + pos);
            const uint16 port = peekUInt16(data + pos + 16);
            if (addr.isIPv6() && isGoodIP(addr) && port != 0)
                found = Endpoint(addr, port);
        }
        pos += len;
    }
    return found;
}

void KadBuddyNatPath::sendHint(const SentHint& h, uint8 connectOptions)
{
    auto hint = std::make_unique<Packet>(OP_NATT_ENDPOINT_HINT, kHintSize, OP_EMULEPROT);
    auto* out = reinterpret_cast<uint8*>(hint->pBuffer);
    out[0] = kHintVersion;
    std::memcpy(out + 1, h.servedHash.data(), 16);
    std::memcpy(out + 17, h.buddyID.data(), 16);
    std::memcpy(out + 33, h.fileHash.data(), 16);
    pokeUInt32(out + 49, h.udp.address().toNetworkUint32());
    pokeUInt16(out + 53, h.udp.port());
    out[55] = connectOptions;
    env().sendPeerUdp(std::move(hint), h.requester, nullptr);
}

void KadBuddyNatPath::pruneHints()
{
    const uint64 now = env().now();
    std::erase_if(m_sentHints, [now](const SentHint& h) { return now - h.atMs > kRehintMs; });
}

} // namespace eMule::natt
