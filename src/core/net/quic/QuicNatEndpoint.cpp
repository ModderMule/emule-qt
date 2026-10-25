#include "pch.h"

#include "net/quic/QuicNatEndpoint.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#ifdef EMULE_HAVE_QUIC_NATT
#include "net/quic/QuicNatConnection.h"
#include "net/quic/QuicTls.h"
#endif

#include <QMetaObject>

#include <chrono>

namespace eMule::quic {

namespace {
constexpr uint64 kMs = 1'000'000;
/// One CAPS_ACK per address per this.
constexpr uint64 kCapsAckIntervalMs = 1000;

#ifdef EMULE_HAVE_QUIC_NATT
QByteArray cidKey(const uint8* data, size_t len)
{
    return QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(len));
}
#endif
} // namespace

QuicNatEndpoint::QuicNatEndpoint(QuicNatIo& io, QObject* parent)
    : QObject(parent)
    , m_io(io)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &QuicNatEndpoint::onTimer);
}

QuicNatEndpoint::~QuicNatEndpoint()
{
#ifdef EMULE_HAVE_QUIC_NATT
    for (auto& c : m_conns)
        c->detach();
#endif
}

bool QuicNatEndpoint::available()
{
#ifdef EMULE_HAVE_QUIC_NATT
    return QuicTls::instance() != nullptr;
#else
    return false;
#endif
}

uint64 QuicNatEndpoint::nowNs() const
{
    return static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

bool QuicNatEndpoint::hasConnection(const Address& addr) const
{
#ifdef EMULE_HAVE_QUIC_NATT
    for (const auto& c : m_conns) {
        if (!c->finished() && c->peerEndpoint().address() == addr)
            return true;
    }
#else
    (void)addr;
#endif
    return false;
}

bool QuicNatEndpoint::heardFrom(const Endpoint& ep) const
{
    const auto it = m_peers.find(ep);
    return it != m_peers.end() && it->second.heard;
}

void QuicNatEndpoint::learnPeerHash(const Endpoint& ep, const Hash16& hash)
{
    if (isZero(hash))
        return;
    PeerState& p = peer(ep);
    // the rendezvous names the peer: beats a key frame, which anyone can send
    if (!p.hashKnown || p.hashFromKeyFrame) {
        p.hash = hash;
        p.hashKnown = true;
        p.hashFromKeyFrame = false;
    }
}

bool QuicNatEndpoint::handleFrame(uint8 type, const uint8* data, size_t len, const Endpoint& from, bool obfuscated)
{
    (void)obfuscated;
    switch (type) {
    case OP_NATT_FRAME_QUIC:
        noteHeard(from);
        if (available() && len > 0 && len <= kMaxRecvDatagram)
            routeQuic(data, len, from);
        return true;
    case OP_NATT_FRAME_KEY: {
        noteHeard(from);
        if (len < 16)
            return true;
        PeerState& p = peer(from);
        // never overwrite: a later key frame refreshes the entry only
        if (!p.hashKnown) {
            std::copy_n(data, 16, p.hash.begin());
            p.hashKnown = !isZero(p.hash);
            p.hashFromKeyFrame = true;
        }
        p.lastUseMs = m_io.nowMs();
        return true;
    }
    case OP_NATT_FRAME_CAPS:
    case OP_NATT_FRAME_CAPS_ACK: {
        noteHeard(from);
        const auto caps = decodeCaps(data, len, m_io.ourHash());
        if (!caps)
            return true;
        if (!isZero(caps->sender) && !peer(from).hashKnown)
            learnPeerHash(from, caps->sender);
        const bool ack = type == OP_NATT_FRAME_CAPS_ACK;
        if (!ack && available()) {
            const uint64 now = m_io.nowMs();
            uint64& last = m_lastCapsAck[from.address()];
            if (last == 0 || now - last >= kCapsAckIntervalMs) {
                last = now;
                sendCaps(from, caps->sender, caps->file, true);
            }
        }
        if (onCaps)
            onCaps(from, *caps, ack);
        return true;
    }
    default:
        return false;
    }
}

void QuicNatEndpoint::expect(const Endpoint& ep, const Hash16& peerHash)
{
    const uint64 until = m_io.nowMs() + kExpectTtlMs;
    for (auto& e : m_expect) {
        if (e.peer == ep) {
            e.untilMs = until;
            e.hash = peerHash;
            return;
        }
    }
    m_expect.push_back({ep, peerHash, until});
}

bool QuicNatEndpoint::dial(const Endpoint& ep, const Hash16& peerHash)
{
#ifdef EMULE_HAVE_QUIC_NATT
    if (!available() || m_conns.size() >= kMaxConnections || !m_io.admit(ep.address()))
        return false;
    // one connection per peer: a second dial would cost a fresh handshake
    for (const auto& c : m_conns) {
        if (c->peerEndpoint() == ep && !c->finished())
            return true;
    }
    auto conn = std::make_shared<QuicNatConnection>(*this, false, ep, m_io.localEndpoint(ep),
                                                    m_io.ourHash(), peerHash);
    m_conns.push_back(conn);
    if (!conn->startClient(nowNs())) {
        m_conns.pop_back();
        return false;
    }
    armTimer();
    return true;
#else
    (void)ep; (void)peerHash;
    return false;
#endif
}

void QuicNatEndpoint::sendCaps(const Endpoint& ep, const Hash16& peerHash, const Hash16& fileHash, bool ack)
{
    Caps c;
    c.options = static_cast<uint8>((m_io.connectOptions() & 0x0F) | (available() ? kCapsQuic : 0));
    c.sender = m_io.ourHash();
    c.expected = peerHash;
    c.file = fileHash;
    const QByteArray frame = encodeCaps(c);
    sendFramed(ack ? OP_NATT_FRAME_CAPS_ACK : OP_NATT_FRAME_CAPS,
               reinterpret_cast<const uint8*>(frame.constData()), static_cast<size_t>(frame.size()), ep);
}

void QuicNatEndpoint::armTimer()
{
#ifdef EMULE_HAVE_QUIC_NATT
    uint64 next = UINT64_MAX;
    for (const auto& c : m_conns)
        next = std::min(next, c->nextDeadlineNs());
    if (next == UINT64_MAX) {
        m_timer.stop();
        return;
    }
    const uint64 now = nowNs();
    const uint64 waitMs = next <= now ? 0 : (next - now + kMs - 1) / kMs;
    m_timer.start(static_cast<int>(std::min<uint64>(waitMs, 60'000)));
#endif
}

#ifdef EMULE_HAVE_QUIC_NATT
void QuicNatEndpoint::addCid(const ngtcp2_cid& cid, QuicNatConnection* conn)
{
    m_byCid[cidKey(cid.data, cid.datalen)] = conn;
}

void QuicNatEndpoint::removeCid(const ngtcp2_cid& cid)
{
    m_byCid.erase(cidKey(cid.data, cid.datalen));
}
#endif

void QuicNatEndpoint::sendDatagram(QuicNatConnection& conn, const uint8* data, size_t len, bool longHeader)
{
#ifdef EMULE_HAVE_QUIC_NATT
    const Endpoint to = conn.peerEndpoint();
    // a key frame ahead of the handshake, so the peer can obfuscate back to us
    if (longHeader) {
        PeerState& p = peer(to);
        const uint64 now = m_io.nowMs();
        if (p.lastKeyFrameMs == 0 || now - p.lastKeyFrameMs >= kKeyFrameIntervalMs) {
            p.lastKeyFrameMs = now;
            const Hash16 own = m_io.ourHash();
            sendFramed(OP_NATT_FRAME_KEY, own.data(), own.size(), to);
        }
    }
    sendFramed(OP_NATT_FRAME_QUIC, data, len, to);
#else
    (void)conn; (void)data; (void)len; (void)longHeader;
#endif
}

void QuicNatEndpoint::connectionEstablished(QuicNatConnection& conn)
{
#ifdef EMULE_HAVE_QUIC_NATT
    // queued: the socket must not be built inside an ngtcp2 callback
    std::weak_ptr<QuicNatConnection> weak = find(&conn);
    const bool outgoing = !conn.isServer();
    QMetaObject::invokeMethod(this, [this, weak, outgoing] {
        auto c = weak.lock();
        if (!c || c->finished())
            return;
        m_expect.erase(std::remove_if(m_expect.begin(), m_expect.end(),
                                      [&](const Expectation& e) {
                                          return e.peer.address() == c->peerEndpoint().address();
                                      }),
                       m_expect.end());
        if (onEstablished)
            onEstablished(c, outgoing);
        else
            c->close();
    }, Qt::QueuedConnection);
#else
    (void)conn;
#endif
}

void QuicNatEndpoint::connectionClosed(QuicNatConnection& conn, bool wasEstablished)
{
#ifdef EMULE_HAVE_QUIC_NATT
    if (!wasEstablished && onFailed) {
        const Endpoint peerEp = conn.peerEndpoint();
        const Hash16 hash = conn.peerHash();
        const QString reason = conn.closeReason();
        QMetaObject::invokeMethod(this, [this, peerEp, hash, reason] {
            if (onFailed)
                onFailed(peerEp, hash, reason);
        }, Qt::QueuedConnection);
    }
    if (!m_reapPosted) {
        m_reapPosted = true;
        QMetaObject::invokeMethod(this, [this] { reap(); }, Qt::QueuedConnection);
    }
#else
    (void)conn; (void)wasEstablished;
#endif
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void QuicNatEndpoint::onTimer()
{
#ifdef EMULE_HAVE_QUIC_NATT
    const uint64 now = nowNs();
    // a copy: onTimer() may close a connection
    const auto conns = m_conns;
    for (const auto& c : conns) {
        if (c->nextDeadlineNs() <= now)
            c->onTimer(now);
    }
    prune(m_io.nowMs());
    armTimer();
#endif
}

void QuicNatEndpoint::routeQuic(const uint8* data, size_t len, const Endpoint& from)
{
#ifdef EMULE_HAVE_QUIC_NATT
    ngtcp2_version_cid vc{};
    const int rv = ngtcp2_pkt_decode_version_cid(&vc, data, len, kCidLen);
    if (rv != 0)
        return;   // includes versions we do not speak: no Version Negotiation reply
    const uint64 now = nowNs();

    auto deliver = [&](QuicNatConnection* raw) {
        if (auto c = find(raw)) {
            c->onDatagram(data, len, from, now);
            armTimer();
        }
    };

    if (const auto it = m_byCid.find(cidKey(vc.dcid, vc.dcidlen)); it != m_byCid.end()) {
        deliver(it->second);
        return;
    }
    if (!isLongHeader(data, len))
        return;

    // a repeated Initial still names the client's own destination CID
    for (const auto& c : m_conns) {
        const ngtcp2_cid& od = c->originalDcid();
        if (c->isServer() && od.datalen == vc.dcidlen && std::memcmp(od.data, vc.dcid, vc.dcidlen) == 0
            && c->peerEndpoint().address() == from.address()) {
            deliver(c.get());
            return;
        }
    }

    ngtcp2_pkt_hd hd{};
    if (ngtcp2_accept(&hd, data, len) != 0)
        return;   // not a usable Initial
    if (hd.version != NGTCP2_PROTO_VER_V1)
        return;

    // only a peer a rendezvous told us about; the port may differ (NAT rebinding)
    const uint64 nowMs = m_io.nowMs();
    prune(nowMs);
    const Expectation* match = nullptr;
    for (const auto& e : m_expect) {
        if (e.peer == from) {
            match = &e;
            break;
        }
        if (!match && e.peer.address() == from.address())
            match = &e;
    }
    if (!match) {
        // the peer repeats its Initial every 250 ms: one line per address
        if (m_droppedLogged.insert(from.address()).second)
            logDebug(QStringLiteral("NAT-T QUIC: Initial from unexpected %1 dropped").arg(from.toString()));
        return;
    }
    if (m_conns.size() >= kMaxConnections || !m_io.admit(from.address()))
        return;

    const Hash16 hash = match->hash;
    if (from != match->peer) {
        const auto known = m_peers.find(match->peer);
        if (known != m_peers.end() && known->second.hashKnown && !known->second.hashFromKeyFrame)
            learnPeerHash(from, known->second.hash);
    }
    auto conn = std::make_shared<QuicNatConnection>(*this, true, from, m_io.localEndpoint(from),
                                                    m_io.ourHash(), hash);
    m_conns.push_back(conn);
    if (!conn->startServer(hd, now)) {
        m_conns.pop_back();
        return;
    }
    conn->onDatagram(data, len, from, now);
    armTimer();
#else
    (void)data; (void)len; (void)from;
#endif
}

void QuicNatEndpoint::sendFramed(uint8 type, const uint8* data, size_t len, const Endpoint& to)
{
    const Hash16* hash = cryptHashFor(to, type == OP_NATT_FRAME_QUIC);
    if (!hash && m_io.strictObfuscation())
        return;   // strict: a plain 0xB2 frame never leaves
    m_io.sendFrame(type, data, len, to, hash);
}

const Hash16* QuicNatEndpoint::cryptHashFor(const Endpoint& to, bool quicDatagram)
{
    PeerState& p = peer(to);
    const uint64 now = m_io.nowMs();
    p.lastUseMs = now;
    const bool strict = m_io.strictObfuscation();
    if (!p.hashKnown || !m_io.canObfuscate(to))
        return nullptr;
    if (p.plain && !strict)
        return nullptr;
    // the clock runs from our first QUIC datagram the peer has not answered: caps and key
    // frames go out long before a late dial and expect no answer
    if (p.firstObfuscatedMs == 0 && quicDatagram && !p.heard)
        p.firstObfuscatedMs = now;
    if (p.firstObfuscatedMs == 0)
        return &p.hash;
    // nothing back for a while: maybe the peer cannot decrypt us (wrong public IP, CGNAT).
    // Alternate plain and obfuscated until one gets an answer.
    if (!strict && !p.heard && now - p.firstObfuscatedMs >= kPlainFallbackMs) {
        p.fallbackToggle = !p.fallbackToggle;
        if (p.fallbackToggle)
            return nullptr;
    }
    return &p.hash;
}

QuicNatEndpoint::PeerState& QuicNatEndpoint::peer(const Endpoint& ep)
{
    return m_peers[ep];
}

void QuicNatEndpoint::noteHeard(const Endpoint& from)
{
    PeerState& p = peer(from);
    if (!p.heard) {
        p.heard = true;
        // the first answer came while we alternated: plain is what works
        if (p.firstObfuscatedMs != 0 && m_io.nowMs() - p.firstObfuscatedMs >= kPlainFallbackMs
            && !m_io.strictObfuscation())
            p.plain = true;
    }
    p.lastUseMs = m_io.nowMs();
}

void QuicNatEndpoint::reap()
{
#ifdef EMULE_HAVE_QUIC_NATT
    m_reapPosted = false;
    for (auto it = m_conns.begin(); it != m_conns.end();) {
        if ((*it)->finished()) {
            ngtcp2_conn* c = (*it)->conn();
            if (c) {
                std::vector<ngtcp2_cid> cids(ngtcp2_conn_get_scid2(c, nullptr));
                ngtcp2_conn_get_scid2(c, cids.data());
                for (const auto& cid : cids)
                    removeCid(cid);
            }
            for (auto m = m_byCid.begin(); m != m_byCid.end();)
                m = (m->second == it->get()) ? m_byCid.erase(m) : std::next(m);
            it = m_conns.erase(it);
        } else {
            ++it;
        }
    }
    armTimer();
#endif
}

void QuicNatEndpoint::prune(uint64 nowMs)
{
    if (nowMs - m_lastPruneMs < 1000)
        return;
    m_lastPruneMs = nowMs;
    m_expect.erase(std::remove_if(m_expect.begin(), m_expect.end(),
                                  [&](const Expectation& e) { return e.untilMs <= nowMs; }),
                   m_expect.end());
    for (auto it = m_peers.begin(); it != m_peers.end();)
        it = (nowMs - it->second.lastUseMs > kKeyFrameTtlMs) ? m_peers.erase(it) : std::next(it);
    m_droppedLogged.clear();
    for (auto it = m_lastCapsAck.begin(); it != m_lastCapsAck.end();)
        it = (nowMs - it->second > 60'000) ? m_lastCapsAck.erase(it) : std::next(it);
}

std::shared_ptr<QuicNatConnection> QuicNatEndpoint::find(QuicNatConnection* raw) const
{
#ifdef EMULE_HAVE_QUIC_NATT
    for (const auto& c : m_conns) {
        if (c.get() == raw)
            return c;
    }
#else
    (void)raw;
#endif
    return {};
}

} // namespace eMule::quic
