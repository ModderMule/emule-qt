#include "pch.h"
/// @file EnodeNatPath.cpp
/// @brief eNode-go PR_NAT: REGISTER/KEEPALIVE, SYNC2 -> SYNC/SYNC_EX/SYNC_IPV6/FAILED.

#include "net/natt/EnodeNatPath.h"

#include "net/natt/NatEnv.h"
#include "net/natt/NatRendezvous.h"
#include "server/Server.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QtEndian>

#include <cstring>

namespace eMule::natt {

namespace {

constexpr qsizetype kFrameHeader = 6;   // prot + size + opcode

QByteArray hashBytes(const Hash16& hash)
{
    return {reinterpret_cast<const char*>(hash.data()), 16};
}

} // namespace

QByteArray EnodeNatPath::frame(uint8 opcode, const QByteArray& payload)
{
    QByteArray out(kFrameHeader + payload.size(), Qt::Uninitialized);
    auto* p = reinterpret_cast<uint8*>(out.data());
    p[0] = PR_NAT;
    pokeUInt32(p + 1, static_cast<uint32>(payload.size() + 1));
    p[5] = opcode;
    if (!payload.isEmpty())
        std::memcpy(p + kFrameHeader, payload.constData(), static_cast<size_t>(payload.size()));
    return out;
}

Endpoint EnodeNatPath::natEndpoint(bool ipv6) const
{
    const Server* server = env().currentServer();
    if (!server || !server->supportsNatRendezvous())
        return {};
    const uint16 port = server->natPort() != 0 ? server->natPort() : kDefaultPort;
    const Address addr = ipv6 ? server->ipv6Address() : server->ipv4Address();
    return addr.isNull() ? Endpoint() : Endpoint(addr, port);
}

Endpoint EnodeNatPath::syncEndpoint() const
{
    // The acked family first: an IPv6 session has no IPv4 registration the server accepts.
    if (m_v4.acked && !m_v4.to.address().isNull())
        return m_v4.to;
    if (m_v6.acked && !m_v6.to.address().isNull())
        return m_v6.to;
    const Endpoint v4 = natEndpoint(false);
    return v4.address().isNull() ? natEndpoint(true) : v4;
}

bool EnodeNatPath::isNatServer(const Endpoint& from) const
{
    const Server* server = env().currentServer();
    if (!server || !server->supportsNatRendezvous() || !server->hasAddress(from.address()))
        return false;
    // The handler also answers on the main UDP port.
    const uint16 natPort = server->natPort() != 0 ? server->natPort() : kDefaultPort;
    return from.port() == natPort || from.port() == server->port() + 4;
}

bool EnodeNatPath::canServe(const PeerInfo& peer) const
{
    return peer.hashKnown && env().ourUdpPort() != 0 && !syncEndpoint().address().isNull()
        && peerOnOurServer(peer);
}

bool EnodeNatPath::request(Attempt& attempt)
{
    attempt.connAck = getRandomUInt32() | 1u;
    sendSync2(attempt);
    return true;
}

void EnodeNatPath::repeat(Attempt& attempt)
{
    if (attempt.role == Role::Requester && attempt.state == Attempt::State::Requested
        && repeatDue(attempt, kRepeatMs, kMaxRequests))
        sendSync2(attempt);
}

void EnodeNatPath::sendSync2(Attempt& attempt)
{
    QByteArray payload = hashBytes(env().ourHash());
    char ack[4];
    qToBigEndian(attempt.connAck, ack);
    payload.append(ack, 4);
    payload.append(hashBytes(attempt.peer.hash));
    attempt.lastRequestMs = env().now();
    ++attempt.requestCount;
    // SYNC2 registers the family it arrives on; the server picks IPv6 when both sides
    // registered one (keepRegistered does that in tick()).
    env().sendRawUdp(frame(OP_NAT_SYNC2, payload), syncEndpoint());
}

void EnodeNatPath::keepRegistered(Registration& reg, const Endpoint& to)
{
    if (to.address().isNull()) {
        reg = {};
        return;
    }
    if (!(to == reg.to)) {
        reg = {};
        reg.to = to;
    }
    const uint64 now = env().now();
    const uint64 interval = reg.acked ? kKeepaliveMs : kRegisterMs;
    if (reg.lastSentMs != 0 && now - reg.lastSentMs < interval)
        return;
    reg.lastSentMs = now;
    if (reg.acked)
        env().sendRawUdp(frame(OP_NAT_KEEPALIVE, {}), to);
    else
        env().sendRawUdp(frame(OP_NAT_REGISTER, hashBytes(env().ourHash())), to);
}

void EnodeNatPath::tick()
{
    // Registered only while we could use it: a register makes us pairable by hash.
    const bool active = env().enabled() && env().ourUdpPort() != 0 && env().firewalled()
                     && hub().hasDataPathNow();
    keepRegistered(m_v4, active ? natEndpoint(false) : Endpoint());
    keepRegistered(m_v6, active && env().ourPublicIPv6().isIPv6() ? natEndpoint(true) : Endpoint());
}

bool EnodeNatPath::handleServerUdp(const Endpoint& from, const uint8* data, qsizetype len)
{
    if (data[0] != PR_NAT || !isNatServer(from))
        return false;
    if (len < kFrameHeader)
        return true;
    const uint32 size = peekUInt32(data + 1);
    if (size < 1 || kFrameHeader - 1 + static_cast<qsizetype>(size) > len) {
        log(QStringLiteral("bad frame size %1 of %2").arg(size).arg(len));
        return true;
    }
    const uint8 opcode = data[5];
    const uint8* p = data + kFrameHeader;
    const uint32 plen = size - 1;

    switch (opcode) {
    case OP_NAT_REGISTER:          // v4 ack: <port BE><ip4 BE>
        if (plen >= 6 && from.address().isIPv4())
            m_v4.acked = true;
        break;
    case OP_NAT_REGISTER_IPV6:     // v6 ack: <port BE><ip6>
        if (plen >= 18)
            m_v6.acked = true;
        break;
    case OP_NAT_PING:
        break;
    case OP_NAT_SYNC:
    case OP_NAT_SYNC_EX:
        if (plen >= 26) {
            const Endpoint udp(Address::fromHostOrder(qFromBigEndian<quint32>(p)),
                               qFromBigEndian<quint16>(p + 4));
            onSync(udp, p + 6, qFromBigEndian<quint32>(p + 22));
        }
        break;
    case OP_NAT_SYNC_IPV6:
        if (plen >= 38) {
            const Endpoint udp(Address::fromIPv6Bytes(p), qFromBigEndian<quint16>(p + 16));
            onSync(udp, p + 18, qFromBigEndian<quint32>(p + 34));
        }
        break;
    case OP_NAT_FAILED:
        if (plen >= 17)
            onFailed(p[0], p + 1);
        break;
    default:
        log(QStringLiteral("unknown opcode 0x%1").arg(opcode, 2, 16, QLatin1Char('0')));
        break;
    }
    return true;
}

void EnodeNatPath::onSync(const Endpoint& udp, const uint8* hash, uint32 connAck)
{
    PeerInfo peer;
    std::memcpy(peer.hash.data(), hash, 16);
    peer.hashKnown = true;
    log(QStringLiteral("SYNC peer %1 connAck %2").arg(udp.toString()).arg(connAck));

    if (Attempt* mine = hub().findAttempt([&](const Attempt& a) {
            return a.path == kind() && a.role == Role::Requester && a.peer.hashKnown
                && a.peer.hash == peer.hash;
        })) {
        hub().endpointKnown(*mine, udp);
        return;
    }
    if (const Server* server = env().currentServer()) {
        peer.serverAddress = server->ipAddress();
        peer.serverPort = server->port();
    }
    if (Attempt* a = hub().acceptNotice(kind(), peer, udp, 0))
        a->connAck = connAck;
}

void EnodeNatPath::onFailed(uint8 reason, const uint8* targetHash)
{
    Hash16 hash{};
    std::memcpy(hash.data(), targetHash, 16);
    if (Attempt* a = hub().findAttempt([&](const Attempt& x) {
            return x.path == kind() && x.role == Role::Requester && x.peer.hash == hash;
        }))
        hub().fail(*a, QStringLiteral("PR_NAT FAILED reason %1").arg(reason));
}

} // namespace eMule::natt
