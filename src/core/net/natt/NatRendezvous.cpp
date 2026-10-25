#include "pch.h"
/// @file NatRendezvous.cpp
/// @brief LowID<->LowID rendezvous hub: attempts, punch, IPv6 handoff.

#include "net/natt/NatRendezvous.h"

#include "app/AppContext.h"
#include "client/UpDownClient.h"
#include "files/PartFile.h"
#include "net/Packet.h"
#include "net/natt/Ed2kServerHolePunchPath.h"
#include "net/natt/EnodeNatPath.h"
#include "net/natt/KadBuddyNatPath.h"
#include "net/natt/LugdunumNatPath.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <algorithm>
#include <cstring>

namespace eMule::natt {

namespace {

constexpr uint32 kHandoffBaseSize = 1 + 1 + 4 + 16 + 1 + 2;   // ver flags id hash opts tcp
constexpr uint32 kHandoffFullSize = kHandoffBaseSize + 16;    // + ipv6

bool sameHost(const Endpoint& ep, const Address& addr)
{
    return !ep.address().isNull() && ep.address() == addr;
}

} // namespace

// ---------------------------------------------------------------------------
// PeerInfo / names
// ---------------------------------------------------------------------------

const char* pathName(PathKind kind) noexcept
{
    switch (kind) {
    case PathKind::Ed2kServer: return "ed2k-server";
    case PathKind::Lugdunum:   return "Lugdunum";
    case PathKind::EnodeNat:   return "PR_NAT";
    case PathKind::KadBuddy:   return "Kad buddy";
    }
    return "?";
}

PeerInfo PeerInfo::fromClient(const UpDownClient& client)
{
    PeerInfo p;
    if (client.hasValidHash()) {
        std::memcpy(p.hash.data(), client.userHash(), 16);
        p.hashKnown = true;
    }
    p.clientID = client.userIDHybrid();
    p.serverAddress = client.serverAddress();
    p.serverPort = client.serverPort();
    const Address& ip = !client.userAddress().isNull() ? client.userAddress() : client.connectAddress();
    const uint16 udp = client.kadPort() != 0 ? client.kadPort() : client.udpPort();
    if (ip.isIPv4() && udp != 0)
        p.kadEndpoint = Endpoint(ip, udp);
    if (client.hasValidBuddyID() && !client.buddyAddress().isNull() && client.buddyPort() != 0) {
        p.buddyValid = true;
        p.buddyEndpoint = Endpoint(client.buddyAddress(), client.buddyPort());
        std::memcpy(p.buddyID.data(), client.buddyID(), 16);
    }
    if (const auto* file = client.reqFile())
        std::memcpy(p.fileHash.data(), file->fileHash(), 16);
    return p;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

NatRendezvous::NatRendezvous(std::unique_ptr<NatEnv> env, QObject* parent)
    : QObject(parent)
    , m_env(std::move(env))
{
    // Order = preference when several paths can serve a peer (agreed in emule-qt#39):
    // the same-server paths first, the Kad buddy last. orderedPaths() moves PR_NAT ahead
    // of Lugdunum when it can serve the peer: eNode-go speaks both, and PR_NAT is faster
    // and not IPv4-only.
    m_paths.push_back(std::make_unique<Ed2kServerHolePunchPath>(*this));
    m_paths.push_back(std::make_unique<LugdunumNatPath>(*this));
    m_paths.push_back(std::make_unique<EnodeNatPath>(*this));
    m_paths.push_back(std::make_unique<KadBuddyNatPath>(*this));

    m_fastTimer.setInterval(100);
    connect(&m_fastTimer, &QTimer::timeout, this, &NatRendezvous::processFast);
}

NatRendezvous::~NatRendezvous() = default;

bool NatRendezvous::hasDataPath()
{
    return theApp.hasConfidentPublicIPv6();
}

bool NatRendezvous::hasDataPathNow() const
{
    return !m_env->ourPublicIPv6().isNull();
}

void NatRendezvous::setPath(std::unique_ptr<NatPath> replacement)
{
    for (auto& p : m_paths) {
        if (p->kind() == replacement->kind()) {
            p = std::move(replacement);
            return;
        }
    }
    m_paths.push_back(std::move(replacement));
}

NatPath* NatRendezvous::path(PathKind kind) const
{
    for (const auto& p : m_paths) {
        if (p->kind() == kind)
            return p.get();
    }
    return nullptr;
}

QString NatRendezvous::peerKey(const PeerInfo& peer)
{
    if (peer.hashKnown)
        return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(peer.hash.data()), 16).toHex());
    return QStringLiteral("id:%1:%2").arg(peer.serverAddress.toString()).arg(peer.clientID);
}

// ---------------------------------------------------------------------------
// Client flow
// ---------------------------------------------------------------------------

bool NatRendezvous::hasFailed(const PeerInfo& peer) const
{
    return m_failedPeers.contains(peerKey(peer));
}

bool NatRendezvous::isActive(const PeerInfo& peer) const
{
    return findForPeer(peer) != nullptr;
}

bool NatRendezvous::mayKeepLowIdSource(const PeerInfo& peer) const
{
    if (!m_env->enabled() || !m_env->firewalled() || !hasDataPathNow() || hasFailed(peer))
        return false;
    return std::ranges::any_of(m_paths, [&](const auto& p) { return p->canServe(peer); });
}

bool NatRendezvous::keepLowIdSource(const PeerInfo& peer)
{
    return theApp.natRendezvous && theApp.natRendezvous->mayKeepLowIdSource(peer);
}

bool NatRendezvous::canStart(const PeerInfo& peer) const
{
    if (isActive(peer))
        return true;
    if (m_attempts.size() >= kMaxAttempts)
        return false;
    return mayKeepLowIdSource(peer);
}

bool NatRendezvous::canStart(const UpDownClient& client) const
{
    return canStart(PeerInfo::fromClient(client));
}

bool NatRendezvous::start(UpDownClient& client)
{
    const PeerInfo peer = PeerInfo::fromClient(client);
    if (!start(peer))
        return false;
    if (Attempt* a = findForPeer(peer); a && a->role == Role::Requester)
        a->client = &client;
    return true;
}

bool NatRendezvous::start(const PeerInfo& peer)
{
    if (isActive(peer))
        return true;
    if (!canStart(peer))
        return false;

    const uint64 now = m_env->now();
    for (NatPath* p : orderedPaths(peer)) {
        auto a = std::make_unique<Attempt>();
        a->id = m_nextId++;
        a->role = Role::Requester;
        a->path = p->kind();
        a->peer = peer;
        a->startedMs = now;
        a->deadlineMs = now + kAttemptTimeoutMs;
        a->triedPaths = static_cast<uint8>(1u << static_cast<unsigned>(p->kind()));
        if (!p->request(*a))
            continue;
        logDebug(QStringLiteral("NAT-T: rendezvous #%1 via %2 for %3")
                     .arg(a->id).arg(QLatin1StringView(pathName(a->path)), peerKey(peer)));
        m_attempts.push_back(std::move(a));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Attempts
// ---------------------------------------------------------------------------

Attempt* NatRendezvous::findAttempt(const std::function<bool(const Attempt&)>& pred) const
{
    for (const auto& a : m_attempts) {
        if (!a->finished && pred(*a))
            return a.get();
    }
    return nullptr;
}

std::vector<Attempt*> NatRendezvous::findAll(const std::function<bool(const Attempt&)>& pred) const
{
    std::vector<Attempt*> out;
    for (const auto& a : m_attempts) {
        if (!a->finished && pred(*a))
            out.push_back(a.get());
    }
    return out;
}

Attempt* NatRendezvous::findForPeer(const PeerInfo& peer) const
{
    return findAttempt([&](const Attempt& a) {
        if (peer.hashKnown && a.peer.hashKnown)
            return a.peer.hash == peer.hash;
        return peer.clientID != 0 && a.peer.clientID == peer.clientID
            && a.peer.serverAddress == peer.serverAddress;
    });
}

std::vector<const Attempt*> NatRendezvous::attempts() const
{
    std::vector<const Attempt*> out;
    for (const auto& a : m_attempts)
        out.push_back(a.get());
    return out;
}

bool NatRendezvous::noticeAllowed(const PeerInfo& peer, const Address& from)
{
    const uint64 now = m_env->now();
    if (auto it = m_recentDone.constFind(peerKey(peer));
        it != m_recentDone.cend() && now - *it < kRepeatNoticeMs)
        return false;
    if (auto it = m_noticeFailed.constFind(peerKey(peer));
        it != m_noticeFailed.cend() && now - *it < kNoticeRetryMs)
        return false;
    const QString ipKey = from.toString();
    if (auto it = m_noticeByIp.constFind(ipKey);
        it != m_noticeByIp.cend() && now - *it < kNoticePerIpMs)
        return false;
    m_noticeByIp.insert(ipKey, now);
    return true;
}

Attempt* NatRendezvous::acceptNotice(PathKind kind, const PeerInfo& peer, const Endpoint& peerUdp,
                                     uint16 peerTcpPort)
{
    if (!m_env->enabled())
        return nullptr;
    if (Attempt* existing = findForPeer(peer)) {
        endpointKnown(*existing, peerUdp, peerTcpPort);
        return existing;
    }
    if (peerUdp.port() == 0 || !m_env->admit(peerUdp.address())) {
        logDebug(QStringLiteral("NAT-T: %1 notice for %2 dropped (endpoint %3)")
                     .arg(QLatin1StringView(pathName(kind)), peerKey(peer), peerUdp.toString()));
        return nullptr;
    }
    if (m_attempts.size() >= kMaxAttempts || !noticeAllowed(peer, peerUdp.address()))
        return nullptr;

    const uint64 now = m_env->now();
    auto a = std::make_unique<Attempt>();
    a->id = m_nextId++;
    a->role = Role::Target;
    a->path = kind;
    a->peer = peer;
    a->startedMs = now;
    a->deadlineMs = now + kAttemptTimeoutMs;
    Attempt* raw = a.get();
    m_attempts.push_back(std::move(a));
    logDebug(QStringLiteral("NAT-T: %1 notice #%2: %3 at %4")
                 .arg(QLatin1StringView(pathName(kind))).arg(raw->id)
                 .arg(peerKey(peer), peerUdp.toString()));
    endpointKnown(*raw, peerUdp, peerTcpPort);
    return raw;
}

void NatRendezvous::endpointKnown(Attempt& a, const Endpoint& udp, uint16 tcpPort)
{
    if (a.finished)
        return;
    if (udp.port() == 0 || !m_env->admit(udp.address())) {
        fail(a, QStringLiteral("bad peer endpoint %1").arg(udp.toString()));
        return;
    }
    if (tcpPort != 0)
        a.peerTcpPort = tcpPort;
    if (udp.address().isIPv6())
        a.peerUdp6 = udp;
    a.peerUdp = udp;

    const uint64 now = m_env->now();
    if (a.state == Attempt::State::Requested)
        a.state = Attempt::State::Punching;
    a.punchUntilMs = now + kPunchWindowMs;
    a.deadlineMs = std::max(a.deadlineMs, now + kPunchWindowMs + kRequesterDialDelay);
    sendPunch(a);
    if (!a.peerHandoffSeen)
        sendHandoff(a, false);

    if (!a.endpointReported) {
        a.endpointReported = true;
        logDebug(QStringLiteral("NAT-T: #%1 peer endpoint %2 (%3)")
                     .arg(a.id).arg(udp.toString(), QLatin1StringView(pathName(a.path))));
        emit peerEndpointReady(a.peer.hash, udp, a.role);
    }
}

void NatRendezvous::fail(Attempt& a, const QString& why)
{
    if (a.finished)
        return;
    a.finished = true;
    a.state = Attempt::State::Failed;
    logDebug(QStringLiteral("NAT-T: #%1 %2 failed: %3")
                 .arg(a.id).arg(peerKey(a.peer), why));
    const uint64 now = m_env->now();
    m_recentDone.insert(peerKey(a.peer), now);
    if (a.role == Role::Requester) {
        m_failedPeers.insert(peerKey(a.peer), now);
        m_env->attemptFailed(a);
    } else {
        m_noticeFailed.insert(peerKey(a.peer), now);
    }
    emit attemptFinished(a.id, false);
}

void NatRendezvous::failOrNext(Attempt& a, const QString& why)
{
    if (!tryNextPath(a))
        fail(a, why);
}

void NatRendezvous::finish(Attempt& a, bool connected)
{
    if (a.finished)
        return;
    a.finished = true;
    m_recentDone.insert(peerKey(a.peer), m_env->now());
    logDebug(QStringLiteral("NAT-T: #%1 %2 connected").arg(a.id).arg(peerKey(a.peer)));
    emit attemptFinished(a.id, connected);
}

void NatRendezvous::removeFinished()
{
    std::erase_if(m_attempts, [](const auto& a) { return a->finished; });
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void NatRendezvous::process()
{
    for (const auto& p : m_paths)
        p->tick();
    processFast();   // also without an event loop (tests)

    const uint64 now = m_env->now();
    // Index loop: paths may add attempts while we run.
    for (size_t i = 0; i < m_attempts.size(); ++i) {
        Attempt& a = *m_attempts[i];
        if (a.finished)
            continue;
        if (now >= a.deadlineMs) {
            if (!tryNextPath(a))
                fail(a, QStringLiteral("timeout"));
            continue;
        }
        if (NatPath* p = path(a.path))
            p->repeat(a);
        if (a.finished || a.state == Attempt::State::Requested)
            continue;

        if (now < a.punchUntilMs && now - a.lastPunchMs >= kPunchIntervalMs)
            sendPunch(a);
        if (!a.peerHandoffSeen && now - a.lastHandoffMs >= kHandoffIntervalMs)
            sendHandoff(a, false);
        evaluateDial(a);
        if (!a.finished && m_env->peerConnected(a))
            finish(a, true);
    }
    removeFinished();

    // Rate-cap bookkeeping only needs the recent past.
    for (auto it = m_noticeByIp.begin(); it != m_noticeByIp.end();)
        it = (now - *it >= kNoticePerIpMs) ? m_noticeByIp.erase(it) : std::next(it);
    for (auto it = m_noticeFailed.begin(); it != m_noticeFailed.end();)
        it = (now - *it >= kNoticeRetryMs) ? m_noticeFailed.erase(it) : std::next(it);
    for (auto it = m_recentDone.begin(); it != m_recentDone.end();)
        it = (now - *it >= kRepeatNoticeMs) ? m_recentDone.erase(it) : std::next(it);
}

void NatRendezvous::processFast()
{
    bool wanted = false;
    for (const auto& p : m_paths)
        wanted = p->fastTick() || wanted;
    if (!wanted)
        m_fastTimer.stop();
}

void NatRendezvous::wantFastTick()
{
    if (!m_fastTimer.isActive())
        m_fastTimer.start();
}

bool NatRendezvous::applyPeerCrypt(Attempt& a, uint8 cryptOptions)
{
    a.peer.cryptOptions = cryptOptions;
    a.peer.cryptKnown = true;
    if (cryptCompatible(m_env->ourConnectOptions(), cryptOptions))
        return true;
    fail(a, QStringLiteral("crypt mismatch (peer 0x%1)").arg(cryptOptions, 0, 16));
    return false;
}

// ---------------------------------------------------------------------------
// Punch + handoff
// ---------------------------------------------------------------------------

const uint8* NatRendezvous::peerCryptHash(const Attempt& a, bool plainWhenUnknown) const
{
    if (!a.peer.hashKnown)
        return nullptr;
    // Crypt bits from the server: obfuscate from the first packet iff the peer can read it.
    if (a.peer.cryptKnown)
        return (a.peer.cryptOptions & CONNECT_OPT_CRYPT_SUPPORTED) ? a.peer.hash.data() : nullptr;
    return plainWhenUnknown ? nullptr : a.peer.hash.data();
}

void NatRendezvous::sendPunch(Attempt& a)
{
    a.lastPunchMs = m_env->now();
    m_env->sendPeerUdp(std::make_unique<Packet>(OP_HOLEPUNCH, 0, OP_EMULEPROT), a.peerUdp,
                       peerCryptHash(a, true));
}

void NatRendezvous::sendHandoff(Attempt& a, bool answer)
{
    a.lastHandoffMs = m_env->now();
    const Address v6 = m_env->ourPublicIPv6();
    const bool withV6 = v6.isIPv6();
    auto packet = std::make_unique<Packet>(OP_NATT_FRAME_IPV6HANDOFF,
                                           withV6 ? kHandoffFullSize : kHandoffBaseSize,
                                           OP_UDPRESERVEDPROT2);
    auto* out = reinterpret_cast<uint8*>(packet->pBuffer);
    out[0] = kHandoffVersion;
    out[1] = static_cast<uint8>((withV6 ? kHandoffHasIPv6 : 0) | (answer ? kHandoffAnswer : 0));
    pokeUInt32(out + 2, m_env->ourClientID());
    const Hash16 hash = m_env->ourHash();
    std::memcpy(out + 6, hash.data(), 16);
    out[22] = m_env->ourConnectOptions();
    pokeUInt16(out + 23, m_env->ourTcpPort());
    if (withV6)
        std::memcpy(out + 25, v6.ipv6Bytes().data(), 16);
    m_env->sendPeerUdp(std::move(packet), a.peerUdp, peerCryptHash(a, false));
}

void NatRendezvous::evaluateDial(Attempt& a)
{
    if (a.finished || a.dialed || !a.peerHandoffSeen || a.awaitingInfo)
        return;
    if (a.peerIPv6.isNull() || m_env->ourPublicIPv6().isNull()) {
        // ToDo: hand the punched endpoint to the QUIC NAT-T transport instead of failing.
        fail(a, QStringLiteral("no IPv6 on one side"));
        return;
    }
    // A 30-byte 0x61 names the dialer (ed2k-server#39); both sides of such a pair obey it.
    if (a.serverRole == 1)
        return;
    if (a.role == Role::Target || a.serverRole == 0) {
        dial(a);
        return;
    }
    const uint64 now = m_env->now();
    if (a.dialAtMs == 0)
        a.dialAtMs = now + kRequesterDialDelay;
    else if (now >= a.dialAtMs && !m_env->peerConnected(a))
        dial(a);
}

void NatRendezvous::dial(Attempt& a)
{
    a.dialed = true;
    a.state = Attempt::State::Dialed;
    logDebug(QStringLiteral("NAT-T: #%1 dialing [%2]:%3")
                 .arg(a.id).arg(a.peerIPv6.toString()).arg(a.peerTcpPort));
    m_env->dialPeer(a, a.peerIPv6);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool NatRendezvous::handleServerTcp(uint8 opcode, const QByteArray& payload)
{
    if (!m_env->enabled())
        return false;
    return std::ranges::any_of(m_paths, [&](const auto& p) { return p->handleServerTcp(opcode, payload); });
}

bool NatRendezvous::handleServerDatagram(const Endpoint& from, const uint8* data, qsizetype len)
{
    if (!m_env->enabled() || len < 2)
        return false;
    return std::ranges::any_of(m_paths, [&](const auto& p) { return p->handleServerUdp(from, data, len); });
}

void NatRendezvous::handlePeerPacket(uint8 opcode, const uint8* data, uint32 size, const Endpoint& from)
{
    if (!m_env->enabled())
        return;
    for (const auto& p : m_paths) {
        if (p->handlePeerPacket(opcode, data, size, from))
            return;
    }
    if (opcode == OP_HOLEPUNCH)
        handleHolePunch(from);
}

void NatRendezvous::handleFrame(uint8 frameType, const uint8* data, uint32 size, const Endpoint& from,
                                bool /*obfuscated*/)
{
    if (!m_env->enabled())
        return;
    if (frameType == OP_NATT_FRAME_IPV6HANDOFF)
        handleHandoff(data, size, from);
    // ToDo: OP_NATT_FRAME_QUIC / _CAPS / _CAPS_ACK / _KEY once the QUIC transport exists.
}

void NatRendezvous::handleBuddyRelay(const Endpoint& requester, const uint8* data, uint32 size,
                                     const UpDownClient& served)
{
    if (!m_env->enabled())
        return;
    if (auto* kad = dynamic_cast<KadBuddyNatPath*>(path(PathKind::KadBuddy)))
        kad->onBuddyRelay(requester, data, size, KadBuddyNatPath::ServedPeer::fromClient(served));
}

bool NatRendezvous::handleRendezvousFromBuddy(const Endpoint& requester, const uint8* data, uint32 size)
{
    if (!m_env->enabled())
        return false;
    if (auto* kad = dynamic_cast<KadBuddyNatPath*>(path(PathKind::KadBuddy)))
        return kad->onRendezvous(requester, data, size);
    return false;
}

void NatRendezvous::handleHolePunch(const Endpoint& from)
{
    Attempt* a = findAttempt([&](const Attempt& x) {
        return sameHost(x.peerUdp, from.address()) || sameHost(x.peerUdp6, from.address())
            || sameHost(x.peer.kadEndpoint, from.address());
    });
    if (!a)
        return;
    if (a->state == Attempt::State::Requested) {
        endpointKnown(*a, from);
        return;
    }
    // The datagram's source is the mapping that actually works.
    if (sameHost(a->peerUdp, from.address()) && a->peerUdp.port() != from.port()) {
        logDebug(QStringLiteral("NAT-T: #%1 peer port %2 -> %3")
                     .arg(a->id).arg(a->peerUdp.port()).arg(from.port()));
        a->peerUdp = from;
    }
}

void NatRendezvous::handleHandoff(const uint8* data, uint32 size, const Endpoint& from)
{
    if (size < kHandoffBaseSize || data[0] < kHandoffVersion)
        return;
    const uint8 flags = data[1];
    const uint32 clientID = peekUInt32(data + 2);
    Hash16 hash{};
    std::memcpy(hash.data(), data + 6, 16);
    const uint16 tcpPort = peekUInt16(data + 23);

    // Only for an attempt we started or accepted, and only from its peer.
    Attempt* a = findAttempt([&](const Attempt& x) {
        const bool fromPeer = sameHost(x.peerUdp, from.address()) || sameHost(x.peerUdp6, from.address())
                           || sameHost(x.peer.kadEndpoint, from.address());
        return fromPeer && (!x.peer.hashKnown || x.peer.hash == hash);
    });
    if (!a) {
        // A late repeat for an attempt that just ended is expected; anything else is noise.
        const QString key = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(hash.data()), 16).toHex());
        if (!m_recentDone.contains(key))
            logDebug(QStringLiteral("NAT-T: unsolicited handoff from %1 dropped").arg(from.toString()));
        return;
    }
    if (a->state == Attempt::State::Requested)
        endpointKnown(*a, from, tcpPort);
    if (a->finished)
        return;

    if (!a->peer.hashKnown) {
        a->peer.hash = hash;
        a->peer.hashKnown = true;
    }
    if (a->peer.clientID == 0)
        a->peer.clientID = clientID;
    if (tcpPort != 0)
        a->peerTcpPort = tcpPort;
    if (sameHost(a->peerUdp, from.address()))
        a->peerUdp = from;
    a->peerHandoffSeen = true;

    a->peerIPv6 = Address();
    a->peerSentNoAddress = true;
    if ((flags & kHandoffHasIPv6) && size >= kHandoffFullSize) {
        const Address v6 = Address::fromIPv6Bytes(data + 25);
        if (v6.isIPv6() && isGoodIP(v6) && m_env->admit(v6)) {
            a->peerIPv6 = v6;
            a->peerSentNoAddress = false;
        }
    }
    if (!(flags & kHandoffAnswer))
        sendHandoff(*a, true);
    evaluateDial(*a);
}

// ---------------------------------------------------------------------------
// Path choice
// ---------------------------------------------------------------------------

std::vector<NatPath*> NatRendezvous::orderedPaths(const PeerInfo& peer) const
{
    std::vector<NatPath*> out;
    for (const auto& p : m_paths) {
        if (p->canServe(peer))
            out.push_back(p.get());
    }
    // PR_NAT ahead of Lugdunum when both can serve: only an eNode-go server offers both.
    const auto lug = std::ranges::find_if(out, [](NatPath* p) { return p->kind() == PathKind::Lugdunum; });
    const auto enode = std::ranges::find_if(out, [](NatPath* p) { return p->kind() == PathKind::EnodeNat; });
    if (lug != out.end() && enode != out.end() && enode > lug)
        std::rotate(lug, enode, enode + 1);
    return out;
}

bool NatRendezvous::tryNextPath(Attempt& a)
{
    // Only a path that never answered is worth replacing; once an endpoint is known the
    // data path failed, and another introduction would not change that.
    if (a.role != Role::Requester || a.state != Attempt::State::Requested)
        return false;
    const uint64 now = m_env->now();
    for (NatPath* p : orderedPaths(a.peer)) {
        const auto bit = static_cast<uint8>(1u << static_cast<unsigned>(p->kind()));
        if (a.triedPaths & bit)
            continue;
        a.triedPaths |= bit;
        const PathKind previous = a.path;
        a.path = p->kind();
        a.requestCount = 0;
        a.lastRequestMs = 0;
        a.connAck = 0;
        a.deadlineMs = now + kAttemptTimeoutMs;
        if (!p->request(a))
            continue;
        logDebug(QStringLiteral("NAT-T: #%1 %2 timed out, trying %3")
                     .arg(a.id)
                     .arg(QLatin1StringView(pathName(previous)), QLatin1StringView(pathName(a.path))));
        return true;
    }
    return false;
}

} // namespace eMule::natt
