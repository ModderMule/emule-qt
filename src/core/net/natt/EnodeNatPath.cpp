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

#include <algorithm>
#include <cstring>

namespace eMule::natt {

namespace {

constexpr qsizetype kFrameHeader = 6;   // prot + size + opcode

QByteArray hashBytes(const Hash16& hash)
{
    return {reinterpret_cast<const char*>(hash.data()), 16};
}

/// Makes room for one more entry: drops the oldest once @p map holds @p cap.
template <typename V, typename AtMs>
void dropOldest(QHash<QByteArray, V>& map, size_t cap, AtMs atMs)
{
    if (static_cast<size_t>(map.size()) < cap || map.isEmpty())
        return;
    auto oldest = map.begin();
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (atMs(it.value()) < atMs(oldest.value()))
            oldest = it;
    }
    map.erase(oldest);
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
    const Endpoint learned = learnedEndpoint(ipv6);
    return learned.address().isNull() ? defaultNatEndpoint(ipv6) : learned;
}

Endpoint EnodeNatPath::syncEndpoint() const
{
    // Wake mode: nothing is registered, and the SYNC2 registers the family it travels on.
    // IPv6 first, as the server pairs: our IPv4 is registered just before (request()).
    if (wakeMode()) {
        if (env().ourPublicIPv6().isIPv6()) {
            if (const Endpoint v6 = natEndpoint(true); !v6.address().isNull())
                return v6;
        }
        return natEndpoint(false);
    }
    // The acked family first: the server holds that candidate. Either family registers on
    // any session the server has an address for (an IPv6 session has no IPv4 to check).
    if (m_v4.acked && !m_v4.to.address().isNull())
        return m_v4.to;
    if (m_v6.acked && !m_v6.to.address().isNull())
        return m_v6.to;
    const Endpoint v4 = natEndpoint(false);
    return v4.address().isNull() ? natEndpoint(true) : v4;
}

bool EnodeNatPath::isNatServer(const Endpoint& from) const
{
    if (isOwnNatServer(from))
        return true;
    // A server swept for a peer elsewhere answers from where the SYNC2 went. Late
    // answers from an earlier one in the list still count.
    return hub().findAttempt([&](const Attempt& a) {
        return a.path == kind() && a.role == Role::Requester
            && std::ranges::find(a.natServers, from) != a.natServers.end();
    }) != nullptr;
}

bool EnodeNatPath::isOwnNatServer(const Endpoint& from) const
{
    const Server* server = env().currentServer();
    if (!server || (!server->supportsNatRendezvous() && !server->supportsNatWake()))
        return false;
    // Where a register ack moved us
    if (const Endpoint learned = learnedEndpoint(from.address().isIPv6());
        !learned.address().isNull() && from == learned)
        return true;
    if (!server->hasAddress(from.address()))
        return false;
    // The handler also answers on the main UDP port.
    const uint16 natPort = server->natPort() != 0 ? server->natPort() : kDefaultPort;
    return from.port() == natPort || from.port() == server->port() + 4;
}

bool EnodeNatPath::canServe(const PeerInfo& peer) const
{
    if (!peer.hashKnown || env().ourUdpPort() == 0)
        return false;
    if (peerOnOurServer(peer))
        return !syncEndpoint().address().isNull();
    // Elsewhere (or unknown, as for Kad sources): a sweep, unless one found nothing.
    return !m_noRendezvous.contains(hashBytes(peer.hash)) && !sweepServers(peer).empty();
}

bool EnodeNatPath::request(Attempt& attempt)
{
    attempt.natServers.clear();
    attempt.natServerIdx = 0;
    if (!peerOnOurServer(attempt.peer)) {
        attempt.natServers = sweepServers(attempt.peer);
        if (attempt.natServers.empty())
            return false;
        log(QStringLiteral("#%1 sweeping %2 server(s) for a peer elsewhere")
                .arg(attempt.id).arg(attempt.natServers.size()));
    }
    attempt.connAck = getRandomUInt32() | 1u;
    // Wake mode: the SYNC2 registers only its own family; register the other one too, so
    // the server can pair on whichever the target has.
    const Endpoint own = syncEndpoint();
    if (wakeMode() && own.address().isIPv6() && attemptEndpoint(attempt) == own)
        registerNow(false);
    sendSync2(attempt);
    return true;
}

void EnodeNatPath::repeat(Attempt& attempt)
{
    if (attempt.role != Role::Requester || attempt.state != Attempt::State::Requested)
        return;
    if (attempt.natServers.empty()) {
        if (repeatDue(attempt, kRepeatMs, kMaxRequests))
            sendSync2(attempt);
        return;
    }
    if (repeatDue(attempt, kSweepStepMs, kSweepSends))
        sendSync2(attempt);
    else if (attempt.requestCount >= kSweepSends && env().now() - attempt.lastRequestMs >= kSweepStepMs)
        nextServer(attempt, QStringLiteral("no answer"));
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
    env().sendRawUdp(frame(OP_NAT_SYNC2, payload), attemptEndpoint(attempt));
}

std::vector<Endpoint> EnodeNatPath::sweepServers(const PeerInfo& peer) const
{
    // Our own server first, as agreed: a Kad source may well be logged in here.
    std::vector<Endpoint> others = env().rendezvousServers(peer);
    // A peer whose server is known is registered there or nowhere.
    const bool serverKnown = peer.serverPort != 0 || !peer.serverAddress.isNull();
    if (serverKnown && others.empty())
        return {};
    std::vector<Endpoint> out;
    if (const Endpoint own = syncEndpoint(); !own.address().isNull())
        out.push_back(own);
    // The server that paired this hash last time goes next.
    if (const auto it = m_pairedVia.constFind(hashBytes(peer.hash)); it != m_pairedVia.cend()) {
        if (const auto hit = std::ranges::find(others, it->server); hit != others.end())
            std::rotate(others.begin(), hit, hit + 1);
    }
    for (const Endpoint& ep : others) {
        if (out.size() >= kMaxSweepServers)
            break;
        if (std::ranges::find(out, ep) == out.end())
            out.push_back(ep);
    }
    return out;
}

Endpoint EnodeNatPath::attemptEndpoint(const Attempt& attempt) const
{
    return attempt.natServerIdx < attempt.natServers.size()
        ? attempt.natServers[attempt.natServerIdx] : syncEndpoint();
}

void EnodeNatPath::keepRegistered(bool ipv6, bool want)
{
    Registration& reg = ipv6 ? m_v6 : m_v4;
    Endpoint to = want ? natEndpoint(ipv6) : Endpoint();
    if (to.address().isNull()) {
        reg = {};
        return;
    }
    if (!(to == reg.to)) {
        reg = {};
        reg.to = to;
    }
    const uint64 now = env().now();
    // Keepalives that stop being answered: the server lost the registration or our NAT
    // moved us to a new port, and a keepalive from there matches nothing. Register again,
    // at the default endpoint: the learned one may be what broke.
    if (reg.acked && now - reg.lastPingMs > kPingLossMs) {
        log(QStringLiteral("no PING from %1 for %2 s, registering again")
                .arg(to.toString()).arg((now - reg.lastPingMs) / 1000));
        (ipv6 ? m_learnedV6 : m_learnedV4) = {};
        to = defaultNatEndpoint(ipv6);
        reg = {};
        reg.to = to;
    }
    // A refused REGISTER gets no answer: slow down after kRegisterTries.
    const uint64 interval = reg.acked ? kKeepaliveMs
                          : reg.unanswered >= kRegisterTries ? kRegisterSlowMs : kRegisterMs;
    if (reg.lastSentMs != 0 && now - reg.lastSentMs < interval)
        return;
    reg.lastSentMs = now;
    if (reg.acked) {
        env().sendRawUdp(frame(OP_NAT_KEEPALIVE, {}), to);
        return;
    }
    if (++reg.unanswered == kRegisterTries)
        log(QStringLiteral("REGISTER to %1 unanswered %2 times, next every %3 s")
                .arg(to.toString()).arg(kRegisterTries).arg(kRegisterSlowMs / 1000));
    env().sendRawUdp(frame(OP_NAT_REGISTER, hashBytes(env().ourHash())), to);
}

bool EnodeNatPath::active() const
{
    return env().enabled() && env().ourUdpPort() != 0 && env().firewalled() && hub().hasDataPathNow();
}

bool EnodeNatPath::wakeMode() const
{
    const Server* server = env().currentServer();
    return server && server->supportsNatWake();
}

void EnodeNatPath::registerNow(bool ipv6)
{
    const Endpoint to = natEndpoint(ipv6);
    if (!to.address().isNull())
        env().sendRawUdp(frame(OP_NAT_REGISTER, hashBytes(env().ourHash())), to);
}

void EnodeNatPath::tick()
{
    // Registered only while we could use it: a register makes us pairable by hash. A
    // server that wakes us needs no standing registration at all.
    const bool standing = active() && !wakeMode();
    // Learned endpoints belong to the server (and session) they came from.
    for (Learned* l : {&m_learnedV4, &m_learnedV6}) {
        if (!(l->from == defaultNatEndpoint(l == &m_learnedV6)) || ourID() == 0)
            *l = {};
    }
    keepRegistered(false, standing);
    keepRegistered(true, standing && env().ourPublicIPv6().isIPv6());
}

bool EnodeNatPath::handleServerTcp(uint8 opcode, const QByteArray& payload)
{
    if (opcode != OP_NAT_WAKE)
        return false;
    const uint8 families = payload.isEmpty() ? 0 : uint8(payload[0]);
    if (!active()) {
        log(QStringLiteral("OP_NAT_WAKE 0x%1 ignored: not a usable target")
                .arg(families, 2, 16, QLatin1Char('0')));
        return true;
    }
    log(QStringLiteral("OP_NAT_WAKE 0x%1: registering").arg(families, 2, 16, QLatin1Char('0')));
    // IPv6 first: the server pairs on the first REGISTER in a family the requester has.
    if ((families & kWakeIPv6) && env().ourPublicIPv6().isIPv6())
        registerNow(true);
    if (families & kWakeIPv4)
        registerNow(false);
    return true;
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
        if (plen >= 6 && from.address().isIPv4() && isOwnNatServer(from))
            onRegisterAck(false, from, Address::fromHostOrder(qFromBigEndian<quint32>(p + 2)),
                          qFromBigEndian<quint16>(p));
        break;
    case OP_NAT_REGISTER_IPV6:     // v6 ack: <port BE><ip6>
        if (plen >= 18 && from.address().isIPv6() && isOwnNatServer(from))
            onRegisterAck(true, from,
                          std::all_of(p + 2, p + 18, [](uint8 b) { return b == 0; })
                              ? Address() : Address::fromIPv6Bytes(p + 2),
                          qFromBigEndian<quint16>(p));
        break;
    case OP_NAT_PING:
        if (isOwnNatServer(from))
            (from.address().isIPv4() ? m_v4 : m_v6).lastPingMs = env().now();
        break;
    case OP_NAT_SYNC:
    case OP_NAT_SYNC_EX:
        if (plen >= 26) {
            const Endpoint udp(Address::fromHostOrder(qFromBigEndian<quint32>(p)),
                               qFromBigEndian<quint16>(p + 4));
            onSync(from, udp, p + 6, qFromBigEndian<quint32>(p + 22));
        }
        break;
    case OP_NAT_SYNC_IPV6:
        if (plen >= 38) {
            const Endpoint udp(Address::fromIPv6Bytes(p), qFromBigEndian<quint16>(p + 16));
            onSync(from, udp, p + 18, qFromBigEndian<quint32>(p + 34));
        }
        break;
    case OP_NAT_FAILED:
        if (plen >= 17)
            onFailed(from, p[0], p + 1);
        break;
    default:
        log(QStringLiteral("unknown opcode 0x%1").arg(opcode, 2, 16, QLatin1Char('0')));
        break;
    }
    return true;
}

void EnodeNatPath::onSync(const Endpoint& from, const Endpoint& udp, const uint8* hash, uint32 connAck)
{
    PeerInfo peer;
    std::memcpy(peer.hash.data(), hash, 16);
    peer.hashKnown = true;
    log(QStringLiteral("SYNC peer %1 connAck %2").arg(udp.toString()).arg(connAck));

    if (Attempt* mine = hub().findAttempt([&](const Attempt& a) {
            return a.path == kind() && a.role == Role::Requester && a.peer.hashKnown
                && a.peer.hash == peer.hash;
        })) {
        if (!mine->natServers.empty()) {
            log(QStringLiteral("#%1 paired via %2").arg(mine->id).arg(from.toString()));
            dropOldest(m_pairedVia, kMaxRemembered, [](const Paired& v) { return v.atMs; });
            m_pairedVia.insert(hashBytes(peer.hash), Paired{from, env().now()});
        }
        hub().endpointKnown(*mine, udp);
        return;
    }
    // Only our own server introduces us as a target (one-server rule).
    if (!isOwnNatServer(from))
        return;
    if (const Server* server = env().currentServer()) {
        peer.serverAddress = server->ipAddress();
        peer.serverPort = server->port();
    }
    if (Attempt* a = hub().acceptNotice(kind(), peer, udp, 0))
        a->connAck = connAck;
}

void EnodeNatPath::onFailed(const Endpoint& from, uint8 reason, const uint8* targetHash)
{
    Hash16 hash{};
    std::memcpy(hash.data(), targetHash, 16);
    Attempt* a = hub().findAttempt([&](const Attempt& x) {
        return x.path == kind() && x.role == Role::Requester && x.peer.hash == hash;
    });
    if (!a)
        return;
    const QString why = QStringLiteral("PR_NAT FAILED reason %1").arg(reason);
    if (a->natServers.empty()) {
        hub().failOrNext(*a, why);
        return;
    }
    // A late answer from a server we already left changes nothing.
    if (from == attemptEndpoint(*a))
        nextServer(*a, why);
}

void EnodeNatPath::nextServer(Attempt& attempt, const QString& why)
{
    log(QStringLiteral("#%1 %2 from %3").arg(attempt.id).arg(why, attemptEndpoint(attempt).toString()));
    if (++attempt.natServerIdx < attempt.natServers.size()) {
        attempt.requestCount = 0;
        attempt.lastRequestMs = 0;
        attempt.connAck = getRandomUInt32() | 1u;
        attempt.deadlineMs = std::max(attempt.deadlineMs, env().now() + NatRendezvous::kAttemptTimeoutMs);
        sendSync2(attempt);
        return;
    }
    // Not on any capable server we know: don't sweep for it again this session.
    dropOldest(m_noRendezvous, kMaxRemembered, [](uint64 at) { return at; });
    m_noRendezvous.insert(hashBytes(attempt.peer.hash), env().now());
    log(QStringLiteral("#%1 no rendezvous server pairs %2")
            .arg(attempt.id).arg(QString::fromLatin1(hashBytes(attempt.peer.hash).toHex())));
    hub().failOrNext(attempt, QStringLiteral("PR_NAT: no rendezvous server pairs the peer"));
}

Endpoint EnodeNatPath::defaultNatEndpoint(bool ipv6) const
{
    const Server* server = env().currentServer();
    if (!server || (!server->supportsNatRendezvous() && !server->supportsNatWake()))
        return {};
    const uint16 port = server->natPort() != 0 ? server->natPort() : kDefaultPort;
    const Address addr = ipv6 ? server->ipv6Address() : server->ipv4Address();
    return addr.isNull() ? Endpoint() : Endpoint(addr, port);
}

Endpoint EnodeNatPath::learnedEndpoint(bool ipv6) const
{
    const Learned& l = ipv6 ? m_learnedV6 : m_learnedV4;
    if (l.to.address().isNull() || !(l.from == defaultNatEndpoint(ipv6)))
        return {};
    return l.to;
}

void EnodeNatPath::onRegisterAck(bool ipv6, const Endpoint& from, const Address& addr, uint16 port)
{
    // Server's own address and preferred port; zero = keep what we sent to.
    const Endpoint sentTo = natEndpoint(ipv6);
    if (sentTo.address().isNull())
        return;
    const bool useAddr = !addr.isNull() && addr.isIPv6() == ipv6;
    const Endpoint to(useAddr ? addr : sentTo.address(), port != 0 ? port : sentTo.port());
    if (!(to == sentTo)) {
        log(QStringLiteral("register ack from %1: PR_NAT now to %2").arg(from.toString(), to.toString()));
        (ipv6 ? m_learnedV6 : m_learnedV4) = Learned{defaultNatEndpoint(ipv6), to};
    }
    Registration& reg = ipv6 ? m_v6 : m_v4;
    reg.to = to;
    reg.acked = true;
    reg.unanswered = 0;
    reg.lastPingMs = env().now();
}

} // namespace eMule::natt
