#include "pch.h"

#ifdef EMULE_HAVE_QUIC_NATT

#include "net/quic/QuicNatConnection.h"
#include "net/quic/QuicNatEndpoint.h"
#include "net/quic/QuicTls.h"
#include "utils/Log.h"

#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <QMetaObject>

#include <cstring>

namespace eMule::quic {

namespace {

constexpr uint64 kMs = 1'000'000;   // ns per ms
/// Packets written per writeOut() before it yields to the event loop.
constexpr int kMaxBurstPackets = 64;
/// How long a closed-by-us connection may flush what it still has.
constexpr uint64 kCloseLingerMs = 3000;
/// Application error code of a proof failure (any value: peers only log it).
constexpr uint64 kAppErrProof = 0x1;

QuicNatConnection* self(void* userData)
{
    return static_cast<QuicNatConnection*>(userData);
}

ngtcp2_conn* getConn(ngtcp2_crypto_conn_ref* ref)
{
    return static_cast<QuicNatConnection*>(ref->user_data)->conn();
}

void randomBytes(uint8* dest, size_t len)
{
    if (RAND_bytes(dest, static_cast<int>(len)) != 1) {
        for (size_t i = 0; i < len; ++i)
            dest[i] = static_cast<uint8>(std::rand());
    }
}

void toSockaddr(const Endpoint& ep, sockaddr_storage& ss, ngtcp2_addr& out)
{
    std::memset(&ss, 0, sizeof(ss));
    if (ep.address().isIPv6()) {
        auto* sa = reinterpret_cast<sockaddr_in6*>(&ss);
        sa->sin6_family = AF_INET6;
        sa->sin6_port = htons(ep.port());
        std::memcpy(&sa->sin6_addr, ep.address().ipv6Bytes().data(), 16);
        out.addrlen = sizeof(sockaddr_in6);
    } else {
        auto* sa = reinterpret_cast<sockaddr_in*>(&ss);
        sa->sin_family = AF_INET;
        sa->sin_port = htons(ep.port());
        sa->sin_addr.s_addr = htonl(ep.address().toUint32());
        out.addrlen = sizeof(sockaddr_in);
    }
    out.addr = reinterpret_cast<ngtcp2_sockaddr*>(&ss);
}

// --- ngtcp2 trampolines ---
int onHandshakeCompleted(ngtcp2_conn*, void* ud) { return self(ud)->cbHandshakeCompleted(); }
int onRecvStreamData(ngtcp2_conn*, uint32_t flags, int64_t sid, uint64_t, const uint8_t* data,
                     size_t len, void* ud, void*)
{
    return self(ud)->cbRecvStreamData(flags, sid, data, len);
}
int onAcked(ngtcp2_conn*, int64_t sid, uint64_t off, uint64_t len, void* ud, void*)
{
    return self(ud)->cbAckedStreamData(sid, off, len);
}
int onStreamClose(ngtcp2_conn*, uint32_t, int64_t sid, uint64_t, uint64_t, void* ud, void*)
{
    return self(ud)->cbStreamClose(sid);
}
int onExtendStreamsBidi(ngtcp2_conn*, uint64_t, void* ud) { return self(ud)->cbExtendMaxLocalStreamsBidi(); }
void onRand(uint8_t* dest, size_t len, const ngtcp2_rand_ctx*) { randomBytes(dest, len); }
int onNewCid(ngtcp2_conn*, ngtcp2_cid* cid, ngtcp2_stateless_reset_token* token, size_t cidlen, void* ud)
{
    return self(ud)->cbNewConnectionId(cid, token, cidlen);
}
int onRemoveCid(ngtcp2_conn*, const ngtcp2_cid* cid, void* ud) { return self(ud)->cbRemoveConnectionId(cid); }

ngtcp2_callbacks makeCallbacks(bool server)
{
    ngtcp2_callbacks cb{};
    if (server)
        cb.recv_client_initial = ngtcp2_crypto_recv_client_initial_cb;
    else {
        cb.client_initial = ngtcp2_crypto_client_initial_cb;
        cb.recv_retry = ngtcp2_crypto_recv_retry_cb;
    }
    cb.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
    cb.handshake_completed = onHandshakeCompleted;
    cb.encrypt = ngtcp2_crypto_encrypt_cb;
    cb.decrypt = ngtcp2_crypto_decrypt_cb;
    cb.hp_mask = ngtcp2_crypto_hp_mask_cb;
    cb.recv_stream_data = onRecvStreamData;
    cb.acked_stream_data_offset = onAcked;
    cb.extend_max_local_streams_bidi = onExtendStreamsBidi;
    cb.rand = onRand;
    cb.update_key = ngtcp2_crypto_update_key_cb;
    cb.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
    cb.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
    cb.version_negotiation = ngtcp2_crypto_version_negotiation_cb;
    cb.get_new_connection_id2 = onNewCid;
    cb.remove_connection_id = onRemoveCid;
    cb.get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb;
    cb.stream_close2 = onStreamClose;
    return cb;
}

} // namespace

QuicNatConnection::QuicNatConnection(QuicNatEndpoint& endpoint, bool server, const Endpoint& peer,
                                     const Endpoint& local, const Hash16& ourHash, const Hash16& peerHash)
    : m_ep(&endpoint)
    , m_server(server)
    , m_peer(peer)
    , m_local(local)
    , m_ourHash(ourHash)
    , m_peerHash(peerHash)
    , m_poster(std::make_unique<QObject>())
{
    m_ref.get_conn = getConn;
    m_ref.user_data = this;
}

QuicNatConnection::~QuicNatConnection()
{
    if (m_conn)
        ngtcp2_conn_del(m_conn);
    if (m_ssl) {
        SSL_set_app_data(m_ssl, nullptr);
        SSL_free(m_ssl);
    }
    if (m_ossl)
        ngtcp2_crypto_ossl_ctx_del(m_ossl);
}

// ---------------------------------------------------------------------------
// StreamTransport
// ---------------------------------------------------------------------------

qint64 QuicNatConnection::read(char* data, qint64 maxLen)
{
    if (m_recv.isEmpty())
        return (m_state == State::Closed || m_peerFin) ? -1 : 0;
    const qint64 n = std::min<qint64>(maxLen, m_recv.size());
    std::memcpy(data, m_recv.constData(), static_cast<size_t>(n));
    m_recv.remove(0, n);
    // the peer may send this much more
    if (m_conn && m_remoteStream >= 0 && m_state != State::Closed) {
        ngtcp2_conn_extend_max_stream_offset(m_conn, m_remoteStream, static_cast<uint64>(n));
        ngtcp2_conn_extend_max_offset(m_conn, static_cast<uint64>(n));
        postFlush();
    }
    if (m_recv.isEmpty() && m_peerFin)
        notifyLost(QStringLiteral("peer finished the stream"));
    return n;
}

void QuicNatConnection::close()
{
    m_sink = nullptr;
    if (m_state == State::Closed || m_closeRequested)
        return;
    m_closeRequested = true;
    m_closeDeadlineNs = (m_ep ? m_ep->nowNs() : 0) + kCloseLingerMs * kMs;
    postFlush();
}

qint64 QuicNatConnection::setSendBufferLimit(qint64 bytes)
{
    std::lock_guard lock(m_sendMx);
    m_limit = std::max(m_limit, bytes);
    return m_limit;
}

qint64 QuicNatConnection::write(const char* data, qint64 len)
{
    if (len <= 0)
        return 0;
    std::lock_guard lock(m_sendMx);
    if (m_state == State::Closed || m_closeRequested)
        return -1;
    const auto pending = static_cast<qint64>(m_end - m_acked);
    if (pending >= m_limit)
        return 0;
    const qint64 take = std::min(len, m_limit - pending);
    qint64 done = 0;
    while (done < take) {
        const size_t inBlock = static_cast<size_t>((m_end - m_base) % kBlock);
        if (inBlock == 0 && (m_end - m_base) / kBlock == m_blocks.size())
            m_blocks.push_back(std::make_unique<Block>());
        Block& b = *m_blocks.back();
        const size_t n = std::min<size_t>(kBlock - inBlock, static_cast<size_t>(take - done));
        std::memcpy(b.data() + inBlock, data + done, n);
        done += static_cast<qint64>(n);
        m_end += n;
    }
    postFlush();
    return done;
}

qint64 QuicNatConnection::pendingSend() const
{
    std::lock_guard lock(m_sendMx);
    return static_cast<qint64>(m_end - m_acked);
}

bool QuicNatConnection::blocksWrite() const
{
    std::lock_guard lock(m_sendMx);
    return static_cast<qint64>(m_end - m_acked) >= m_limit;
}

// ---------------------------------------------------------------------------
// Endpoint side
// ---------------------------------------------------------------------------

bool QuicNatConnection::startClient(uint64 nowNs)
{
    m_startNs = nowNs;
    if (!initTls())
        return false;
    ngtcp2_cid dcid{};
    dcid.datalen = kCidLen;
    randomBytes(dcid.data, dcid.datalen);
    m_scid.datalen = kCidLen;
    randomBytes(m_scid.data, m_scid.datalen);

    ngtcp2_settings settings;
    ngtcp2_transport_params params;
    if (!initCommon(settings, params, nowNs))
        return false;

    sockaddr_storage rs{}, ls{};
    const ngtcp2_path path = makePath(m_peer, rs, ls);
    const ngtcp2_callbacks cb = makeCallbacks(false);
    if (ngtcp2_conn_client_new(&m_conn, &dcid, &m_scid, &path, NGTCP2_PROTO_VER_V1, &cb, &settings,
                               &params, nullptr, this) != 0)
        return false;
    ngtcp2_conn_set_tls_native_handle(m_conn, m_ossl);
    if (m_ep)
        m_ep->addCid(m_scid, this);
    writeOut(nowNs);
    return m_state != State::Closed;
}

bool QuicNatConnection::startServer(const ngtcp2_pkt_hd& hd, uint64 nowNs)
{
    m_startNs = nowNs;
    m_heardPeer = true;
    if (!initTls())
        return false;
    m_scid.datalen = kCidLen;
    randomBytes(m_scid.data, m_scid.datalen);
    m_odcid = hd.dcid;

    ngtcp2_settings settings;
    ngtcp2_transport_params params;
    if (!initCommon(settings, params, nowNs))
        return false;
    params.original_dcid = hd.dcid;
    params.original_dcid_present = 1;

    sockaddr_storage rs{}, ls{};
    const ngtcp2_path path = makePath(m_peer, rs, ls);
    const ngtcp2_callbacks cb = makeCallbacks(true);
    if (ngtcp2_conn_server_new(&m_conn, &hd.scid, &m_scid, &path, hd.version, &cb, &settings, &params,
                               nullptr, this) != 0)
        return false;
    ngtcp2_conn_set_tls_native_handle(m_conn, m_ossl);
    if (m_ep)
        m_ep->addCid(m_scid, this);
    return true;
}

void QuicNatConnection::onDatagram(const uint8* data, size_t len, const Endpoint& from, uint64 nowNs)
{
    if (!m_conn || m_state == State::Closed)
        return;
    m_heardPeer = true;
    m_resendLeft = 0;
    closeBurstBefore(nowNs);

    sockaddr_storage rs{}, ls{};
    const ngtcp2_path path = makePath(from, rs, ls);
    ngtcp2_pkt_info pi{};
    const int rv = ngtcp2_conn_read_pkt(m_conn, &path, &pi, data, len, nowNs);
    if (rv != 0) {
        handleError(rv, "read_pkt");
        return;
    }
    if (from != m_peer && m_state == State::Established)
        m_peer = from;   // the peer's NAT rebound; ngtcp2 validated the path
    writeOut(nowNs);
    if (!m_recv.isEmpty() && m_sink && m_state == State::Established)
        m_sink->transportReadable();
}

void QuicNatConnection::onTimer(uint64 nowNs)
{
    if (!m_conn || m_state == State::Closed)
        return;
    if (m_closeRequested && nowNs >= m_closeDeadlineNs) {
        finishClose(QStringLiteral("closed"), false);
        return;
    }
    if (ngtcp2_conn_get_expiry2(m_conn) <= nowNs) {
        closeBurstBefore(nowNs);
        const int rv = ngtcp2_conn_handle_expiry(m_conn, nowNs);
        if (rv != 0) {
            handleError(rv, "expiry");
            return;
        }
    }
    // the first flight again while the punch settles: our Initial may have hit a closed NAT
    if (!m_heardPeer && m_resendLeft > 0 && nowNs >= m_nextResendNs && !m_lastLong.isEmpty()) {
        --m_resendLeft;
        m_nextResendNs = nowNs + kHandshakeResendMs * kMs;
        // a shared copy: sendPacket() reassigns m_lastLong, which would free the bytes it sends
        const QByteArray again = m_lastLong;
        sendPacket(reinterpret_cast<const uint8*>(again.constData()), static_cast<size_t>(again.size()));
    }
    if (m_state == State::Handshaking && nowNs - m_startNs > kHandshakeTimeoutMs * kMs) {
        fail(QStringLiteral("handshake timed out"));
        return;
    }
    writeOut(nowNs);
}

void QuicNatConnection::writeOut(uint64 nowNs)
{
    if (!m_conn || m_state == State::Closed)
        return;
    closeBurstBefore(nowNs);

    if (m_closeRequested) {
        bool drained;
        {
            std::lock_guard lock(m_sendMx);
            drained = m_acked >= m_end;
        }
        if (drained || nowNs >= m_closeDeadlineNs) {
            finishClose(QStringLiteral("closed"), false);
            return;
        }
    }

    std::array<uint8, kMaxSendDatagram> buf;
    for (int packets = 0; packets < kMaxBurstPackets; ++packets) {
        ngtcp2_vec vec[2]{};
        size_t vcnt = 0;
        int64_t sid = -1;
        if (m_localStream >= 0) {
            std::lock_guard lock(m_sendMx);
            if (m_submitted < m_end) {
                sid = m_localStream;
                uint64 off = m_submitted;
                for (; vcnt < 2 && off < m_end; ++vcnt) {
                    const size_t idx = static_cast<size_t>((off - m_base) / kBlock);
                    const size_t in = static_cast<size_t>((off - m_base) % kBlock);
                    const size_t n = static_cast<size_t>(std::min<uint64>(kBlock - in, m_end - off));
                    vec[vcnt].base = reinterpret_cast<uint8*>(m_blocks[idx]->data() + in);
                    vec[vcnt].len = n;
                    off += n;
                }
            }
        }

        ngtcp2_path_storage ps;
        ngtcp2_path_storage_zero(&ps);
        ngtcp2_pkt_info pi{};
        ngtcp2_ssize datalen = -1;
        ngtcp2_ssize n = ngtcp2_conn_writev_stream(m_conn, &ps.path, &pi, buf.data(), buf.size(), &datalen,
                                                   NGTCP2_WRITE_STREAM_FLAG_NONE, sid, vcnt ? vec : nullptr,
                                                   vcnt, nowNs);
        if (n == NGTCP2_ERR_STREAM_DATA_BLOCKED || n == NGTCP2_ERR_STREAM_SHUT_WR
            || n == NGTCP2_ERR_STREAM_NOT_FOUND) {
            // flow control: acks and the rest still go out
            n = ngtcp2_conn_writev_stream(m_conn, &ps.path, &pi, buf.data(), buf.size(), nullptr,
                                          NGTCP2_WRITE_STREAM_FLAG_NONE, -1, nullptr, 0, nowNs);
            datalen = -1;
        }
        if (n < 0) {
            handleError(static_cast<int>(n), "writev_stream");
            return;
        }
        if (datalen > 0) {
            std::lock_guard lock(m_sendMx);
            m_submitted += static_cast<uint64>(datalen);
        }
        if (n == 0)
            break;
        sendPacket(buf.data(), static_cast<size_t>(n));
        m_burstOpen = true;
        m_burstTs = nowNs;
        if (packets + 1 == kMaxBurstPackets)
            postFlush();   // more next turn; other sockets get the loop meanwhile
    }
}

uint64 QuicNatConnection::nextDeadlineNs() const
{
    if (!m_conn || m_state == State::Closed)
        return UINT64_MAX;
    uint64 t = ngtcp2_conn_get_expiry2(m_conn);
    if (!m_heardPeer && m_resendLeft > 0)
        t = std::min(t, m_nextResendNs);
    if (m_state == State::Handshaking)
        t = std::min(t, m_startNs + kHandshakeTimeoutMs * kMs);
    if (m_closeRequested)
        t = std::min(t, m_closeDeadlineNs);
    return t;
}

void QuicNatConnection::detach()
{
    m_ep = nullptr;
    if (m_state != State::Closed) {
        m_state = State::Closed;
        m_closeReason = QStringLiteral("transport shut down");
        if (m_sink)
            m_sink->transportLost(QAbstractSocket::RemoteHostClosedError, m_closeReason);
        m_sink = nullptr;
    }
}

// ---------------------------------------------------------------------------
// ngtcp2 callbacks
// ---------------------------------------------------------------------------

int QuicNatConnection::cbHandshakeCompleted()
{
    if (!QuicTls::alpnMatches(m_ssl))
        return failLater(QStringLiteral("wrong ALPN"));
    m_state = State::Proving;
    ngtcp2_conn_set_keep_alive_timeout(m_conn, kKeepAliveMs * kMs);
    openLocalStream();
    return 0;
}

int QuicNatConnection::cbRecvStreamData(uint32 flags, int64_t streamId, const uint8* data, size_t len)
{
    // the first stream the peer delivers on is the one it writes: ours (aMule shape) or its own
    if (m_remoteStream < 0)
        m_remoteStream = streamId;
    if (streamId != m_remoteStream) {
        ngtcp2_conn_extend_max_stream_offset(m_conn, streamId, len);
        ngtcp2_conn_extend_max_offset(m_conn, len);
        return 0;
    }
    size_t used = 0;
    if (!m_proofAccepted) {
        const size_t need = kProofSize - static_cast<size_t>(m_proofIn.size());
        used = std::min(need, len);
        m_proofIn.append(reinterpret_cast<const char*>(data), static_cast<qsizetype>(used));
        ngtcp2_conn_extend_max_stream_offset(m_conn, streamId, used);
        ngtcp2_conn_extend_max_offset(m_conn, used);
        if (static_cast<size_t>(m_proofIn.size()) == kProofSize) {
            const ProofCheck check = checkProof(reinterpret_cast<const uint8*>(m_proofIn.constData()),
                                                kProofSize, m_ourHash, m_peerHash);
            if (!check.ok)
                return failLater(QString::fromLatin1(check.reason), kAppErrProof);
            m_peerHash = check.sender;
            m_proofAccepted = true;
            maybeEstablish();
        }
    }
    if (used < len)
        m_recv.append(reinterpret_cast<const char*>(data + used), static_cast<qsizetype>(len - used));
    if (flags & NGTCP2_STREAM_DATA_FLAG_FIN)
        m_peerFin = true;
    if (m_peerFin && m_recv.isEmpty() && m_state == State::Established)
        notifyLost(QStringLiteral("peer finished the stream"));
    return 0;
}

int QuicNatConnection::cbAckedStreamData(int64_t streamId, uint64 offset, uint64 len)
{
    if (streamId != m_localStream)
        return 0;
    {
        std::lock_guard lock(m_sendMx);
        m_acked = std::max(m_acked, offset + len);
        trimAcked();
    }
    if (m_sink && len > 0)
        m_sink->transportWritten(static_cast<qint64>(len));
    return 0;
}

int QuicNatConnection::cbStreamClose(int64_t streamId)
{
    if (streamId == m_remoteStream || streamId == m_localStream) {
        if (m_state == State::Established)
            notifyLost(QStringLiteral("peer closed the stream"));
        else if (m_state == State::Proving)
            return failLater(QStringLiteral("stream closed before the proof"));
    }
    return 0;
}

int QuicNatConnection::cbExtendMaxLocalStreamsBidi()
{
    if (m_state == State::Proving)
        openLocalStream();
    return 0;
}

int QuicNatConnection::cbNewConnectionId(ngtcp2_cid* cid, ngtcp2_stateless_reset_token* token, size_t cidlen)
{
    cid->datalen = cidlen;
    randomBytes(cid->data, cidlen);
    randomBytes(token->data, sizeof(token->data));
    if (m_ep)
        m_ep->addCid(*cid, this);
    return 0;
}

int QuicNatConnection::cbRemoveConnectionId(const ngtcp2_cid* cid)
{
    if (m_ep)
        m_ep->removeCid(*cid);
    return 0;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool QuicNatConnection::initTls()
{
    QuicTls* tls = QuicTls::instance();
    if (!tls)
        return false;
    m_ssl = SSL_new(m_server ? tls->serverContext() : tls->clientContext());
    if (!m_ssl || ngtcp2_crypto_ossl_ctx_new(&m_ossl, m_ssl) != 0)
        return false;
    const int rv = m_server ? ngtcp2_crypto_ossl_configure_server_session(m_ssl)
                            : ngtcp2_crypto_ossl_configure_client_session(m_ssl);
    if (rv != 0)
        return false;
    SSL_set_app_data(m_ssl, &m_ref);
    if (m_server) {
        SSL_set_accept_state(m_ssl);
    } else {
        SSL_set_connect_state(m_ssl);
        QuicTls::setClientAlpn(m_ssl);
        SSL_set_tlsext_host_name(m_ssl, kSni);
    }
    return true;
}

bool QuicNatConnection::initCommon(ngtcp2_settings& settings, ngtcp2_transport_params& params, uint64 nowNs)
{
    ngtcp2_settings_default(&settings);
    settings.initial_ts = nowNs;
    settings.cc_algo = NGTCP2_CC_ALGO_BBR;
    settings.max_tx_udp_payload_size = kMaxSendDatagram;
    settings.no_tx_udp_payload_size_shaping = 1;
    settings.no_pmtud = 1;
    settings.handshake_timeout = kHandshakeTimeoutMs * kMs;

    ngtcp2_transport_params_default(&params);
    params.initial_max_streams_bidi = 1;
    params.initial_max_streams_uni = 0;
    params.initial_max_stream_data_bidi_local = kStreamWindow;
    params.initial_max_stream_data_bidi_remote = kStreamWindow;
    params.initial_max_stream_data_uni = 0;
    params.initial_max_data = kConnWindow;
    params.max_idle_timeout = kIdleTimeoutMs * kMs;
    params.max_udp_payload_size = kMaxRecvDatagram;
    params.active_connection_id_limit = 2;
    return true;
}

void QuicNatConnection::handleError(int rv, const char* where)
{
    if (m_pendingFail) {
        const auto pending = std::exchange(m_pendingFail, std::nullopt);
        fail(pending->first, pending->second);
        return;
    }
    if (rv == NGTCP2_ERR_DRAINING || rv == NGTCP2_ERR_CLOSING) {
        finishClose(QStringLiteral("peer closed the connection"), true);
        return;
    }
    if (rv == NGTCP2_ERR_IDLE_CLOSE) {
        finishClose(QStringLiteral("idle timeout"), true);
        return;
    }
    if (rv == NGTCP2_ERR_DROP_CONN) {
        finishClose(QStringLiteral("dropped"), true);
        return;
    }
    if (m_state == State::Closed)
        return;
    // ngtcp2 knows the right CONNECTION_CLOSE for its own errors
    ngtcp2_ccerr ccerr;
    ngtcp2_ccerr_default(&ccerr);
    ngtcp2_ccerr_set_liberr(&ccerr, rv, nullptr, 0);
    std::array<uint8, kMaxSendDatagram> buf;
    ngtcp2_path_storage ps;
    ngtcp2_path_storage_zero(&ps);
    ngtcp2_pkt_info pi{};
    const ngtcp2_ssize n = ngtcp2_conn_write_connection_close(m_conn, &ps.path, &pi, buf.data(), buf.size(),
                                                              &ccerr, m_ep ? m_ep->nowNs() : 0);
    if (n > 0)
        sendPacket(buf.data(), static_cast<size_t>(n));
    finishClose(QStringLiteral("%1: %2").arg(QLatin1String(where), QLatin1String(ngtcp2_strerror(rv))), true);
}

void QuicNatConnection::fail(const QString& reason, uint64 appError)
{
    if (m_state == State::Closed || !m_conn)
        return;
    ngtcp2_ccerr ccerr;
    ngtcp2_ccerr_default(&ccerr);
    ngtcp2_ccerr_set_application_error(&ccerr, appError, nullptr, 0);
    std::array<uint8, kMaxSendDatagram> buf;
    ngtcp2_path_storage ps;
    ngtcp2_path_storage_zero(&ps);
    ngtcp2_pkt_info pi{};
    const ngtcp2_ssize n = ngtcp2_conn_write_connection_close(m_conn, &ps.path, &pi, buf.data(), buf.size(),
                                                              &ccerr, m_ep ? m_ep->nowNs() : 0);
    if (n > 0)
        sendPacket(buf.data(), static_cast<size_t>(n));
    finishClose(reason, true);
}

void QuicNatConnection::finishClose(const QString& reason, bool notify)
{
    if (m_state == State::Closed)
        return;
    if (!notify && m_conn && !ngtcp2_conn_in_closing_period2(m_conn) && !ngtcp2_conn_in_draining_period2(m_conn)) {
        // our own close: tell the peer, then stop
        ngtcp2_ccerr ccerr;
        ngtcp2_ccerr_default(&ccerr);
        ngtcp2_ccerr_set_application_error(&ccerr, 0, nullptr, 0);
        std::array<uint8, kMaxSendDatagram> buf;
        ngtcp2_path_storage ps;
        ngtcp2_path_storage_zero(&ps);
        ngtcp2_pkt_info pi{};
        const ngtcp2_ssize n = ngtcp2_conn_write_connection_close(m_conn, &ps.path, &pi, buf.data(),
                                                                  buf.size(), &ccerr, m_ep ? m_ep->nowNs() : 0);
        if (n > 0)
            sendPacket(buf.data(), static_cast<size_t>(n));
    }
    const State was = m_state;
    m_state = State::Closed;
    m_closeReason = reason;
    if (notify && was == State::Established)
        notifyLost(reason);
    if (m_ep)
        m_ep->connectionClosed(*this, was == State::Established);
}

int QuicNatConnection::failLater(const QString& reason, uint64 appError)
{
    if (!m_pendingFail)
        m_pendingFail = std::make_pair(reason, appError);
    return NGTCP2_ERR_CALLBACK_FAILURE;
}

void QuicNatConnection::closeBurstBefore(uint64 nowNs)
{
    if (m_burstOpen && nowNs > m_burstTs && m_conn) {
        ngtcp2_conn_update_pkt_tx_time(m_conn, m_burstTs);
        m_burstOpen = false;
    }
}

void QuicNatConnection::sendPacket(const uint8* data, size_t len)
{
    if (!m_ep)
        return;
    const bool longHeader = isLongHeader(data, len);
    if (longHeader && !m_heardPeer && m_state == State::Handshaking) {
        m_lastLong = QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(len));
        if (m_resendLeft == 0 && !m_server) {
            m_resendLeft = kHandshakeResendMax;
            m_nextResendNs = m_ep->nowNs() + kHandshakeResendMs * kMs;
        }
    }
    m_ep->sendDatagram(*this, data, len, longHeader);
}

void QuicNatConnection::openLocalStream()
{
    if (m_localStream >= 0 || !m_conn)
        return;
    int64_t sid = -1;
    if (ngtcp2_conn_open_bidi_stream(m_conn, &sid, nullptr) != 0)
        return;   // the peer's limit opens later: cbExtendMaxLocalStreamsBidi
    m_localStream = sid;
    queueProof();
}

void QuicNatConnection::queueProof()
{
    if (m_proofQueued)
        return;
    const auto proof = encodeProof(m_ourHash, m_peerHash);
    {
        std::lock_guard lock(m_sendMx);
        // nothing else is queued before the proof: the socket is attached after it
        if (m_blocks.empty())
            m_blocks.push_back(std::make_unique<Block>());
        std::memcpy(m_blocks.back()->data() + (m_end - m_base), proof.data(), proof.size());
        m_end += proof.size();
    }
    m_proofQueued = true;
    maybeEstablish();
}

void QuicNatConnection::maybeEstablish()
{
    if (m_state != State::Proving || !m_proofQueued || !m_proofAccepted)
        return;
    m_state = State::Established;
    if (m_ep)
        m_ep->connectionEstablished(*this);
}

void QuicNatConnection::trimAcked()
{
    // caller holds m_sendMx
    while (!m_blocks.empty() && m_acked >= m_base + kBlock && m_submitted >= m_base + kBlock) {
        m_blocks.pop_front();
        m_base += kBlock;
    }
}

void QuicNatConnection::postFlush()
{
    if (m_flushPosted.exchange(true))
        return;
    std::weak_ptr<QuicNatConnection> weak = weak_from_this();
    QMetaObject::invokeMethod(m_poster.get(), [weak] {
        if (auto c = weak.lock()) {
            c->m_flushPosted = false;
            if (c->m_ep) {
                c->writeOut(c->m_ep->nowNs());
                c->m_ep->armTimer();
            }
        }
    }, Qt::QueuedConnection);
}

void QuicNatConnection::notifyLost(const QString& reason)
{
    if (m_lostNotified)
        return;
    m_lostNotified = true;
    // queued: never inside ngtcp2 or the socket's own read
    std::weak_ptr<QuicNatConnection> weak = weak_from_this();
    QMetaObject::invokeMethod(m_poster.get(), [weak, reason] {
        if (auto c = weak.lock()) {
            if (auto* sink = std::exchange(c->m_sink, nullptr))
                sink->transportLost(QAbstractSocket::RemoteHostClosedError, reason);
        }
    }, Qt::QueuedConnection);
}

ngtcp2_path QuicNatConnection::makePath(const Endpoint& remote, sockaddr_storage& rs, sockaddr_storage& ls) const
{
    ngtcp2_path path{};
    toSockaddr(remote, rs, path.remote);
    toSockaddr(m_local, ls, path.local);
    return path;
}

} // namespace eMule::quic

#endif // EMULE_HAVE_QUIC_NATT
