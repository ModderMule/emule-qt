#pragma once
/// @file QuicNatConnection.h
/// @brief One QUIC NAT-T connection: ngtcp2 + OpenSSL, carrying one eD2K byte stream.
///
/// Each side opens its own bidi stream and writes only there; it reads the first stream
/// the peer delivers data on. The first 37 bytes each way are the EAQN1 proof.
/// Everything runs on the endpoint's thread except the StreamTransport any-thread calls.

#ifdef EMULE_HAVE_QUIC_NATT

#include "net/StreamTransport.h"
#include "net/quic/QuicNatProtocol.h"

#include <QByteArray>
#include <QObject>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>

typedef struct ssl_st SSL;
typedef struct ngtcp2_crypto_ossl_ctx ngtcp2_crypto_ossl_ctx;

namespace eMule::quic {

class QuicNatEndpoint;

class QuicNatConnection final : public StreamTransport,
                                public std::enable_shared_from_this<QuicNatConnection> {
public:
    enum class State : uint8 { Handshaking, Proving, Established, Closing, Closed };

    QuicNatConnection(QuicNatEndpoint& endpoint, bool server, const Endpoint& peer,
                      const Endpoint& local, const Hash16& ourHash, const Hash16& peerHash);
    ~QuicNatConnection() override;

    QuicNatConnection(const QuicNatConnection&) = delete;
    QuicNatConnection& operator=(const QuicNatConnection&) = delete;

    // --- StreamTransport ---
    void setSink(StreamTransportSink* sink) override { m_sink = sink; }
    [[nodiscard]] qint64 bytesAvailable() const override { return m_recv.size(); }
    qint64 read(char* data, qint64 maxLen) override;
    void close() override;
    [[nodiscard]] Endpoint peerEndpoint() const override { return m_peer; }
    [[nodiscard]] Endpoint localEndpoint() const override { return m_local; }
    qint64 setSendBufferLimit(qint64 bytes) override;
    qint64 write(const char* data, qint64 len) override;
    [[nodiscard]] qint64 pendingSend() const override;
    [[nodiscard]] bool blocksWrite() const override;

    // --- endpoint side (endpoint thread) ---
    /// Client: build and send the first Initial.
    bool startClient(uint64 nowNs);
    /// Server: from the client's first Initial (already accepted by ngtcp2_accept).
    bool startServer(const ngtcp2_pkt_hd& hd, uint64 nowNs);
    /// One QUIC datagram from the peer. @p from may differ from peerEndpoint() after a rebind.
    void onDatagram(const uint8* data, size_t len, const Endpoint& from, uint64 nowNs);
    /// Timers due: retransmits, keepalive, idle, handshake resend.
    void onTimer(uint64 nowNs);
    /// Write what can go now (stream data, acks).
    void writeOut(uint64 nowNs);
    /// Earliest time onTimer() wants to run, ns.
    [[nodiscard]] uint64 nextDeadlineNs() const;
    /// The endpoint goes away: drop without a sound.
    void detach();

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] bool isServer() const { return m_server; }
    [[nodiscard]] const Hash16& peerHash() const { return m_peerHash; }
    [[nodiscard]] bool finished() const { return m_state == State::Closed; }
    [[nodiscard]] QString closeReason() const { return m_closeReason; }
    /// The client's original destination CID (server side), for retransmitted Initials.
    [[nodiscard]] const ngtcp2_cid& originalDcid() const { return m_odcid; }
    [[nodiscard]] ngtcp2_conn* conn() const { return m_conn; }

    // ngtcp2 callback entry points (static trampolines in the .cpp)
    int cbHandshakeCompleted();
    int cbRecvStreamData(uint32 flags, int64_t streamId, const uint8* data, size_t len);
    int cbAckedStreamData(int64_t streamId, uint64 offset, uint64 len);
    int cbStreamClose(int64_t streamId);
    int cbExtendMaxLocalStreamsBidi();
    int cbNewConnectionId(ngtcp2_cid* cid, ngtcp2_stateless_reset_token* token, size_t cidlen);
    int cbRemoveConnectionId(const ngtcp2_cid* cid);

private:
    static constexpr size_t kBlock = 64 * 1024;
    using Block = std::array<char, kBlock>;

    bool initTls();
    bool initCommon(ngtcp2_settings& settings, ngtcp2_transport_params& params, uint64 nowNs);
    void handleError(int rv, const char* where);
    void fail(const QString& reason, uint64 appError = 0);
    /// From inside an ngtcp2 callback: fail once the library call returns.
    int failLater(const QString& reason, uint64 appError = 0);
    void finishClose(const QString& reason, bool notify);
    void closeBurstBefore(uint64 nowNs);
    void sendPacket(const uint8* data, size_t len);
    void openLocalStream();
    void queueProof();
    void maybeEstablish();
    void trimAcked();
    void postFlush();
    void notifyLost(const QString& reason);
    [[nodiscard]] ngtcp2_path makePath(const Endpoint& remote, struct sockaddr_storage& rs,
                                       struct sockaddr_storage& ls) const;

    QuicNatEndpoint* m_ep;
    const bool m_server;
    Endpoint m_peer;
    const Endpoint m_local;
    const Hash16 m_ourHash;
    Hash16 m_peerHash;

    ngtcp2_conn* m_conn = nullptr;
    SSL* m_ssl = nullptr;
    ngtcp2_crypto_ossl_ctx* m_ossl = nullptr;
    ngtcp2_crypto_conn_ref m_ref{};
    ngtcp2_cid m_odcid{};
    ngtcp2_cid m_scid{};

    State m_state = State::Handshaking;
    QString m_closeReason;
    StreamTransportSink* m_sink = nullptr;

    int64_t m_localStream = -1;
    int64_t m_remoteStream = -1;
    bool m_proofQueued = false;
    bool m_proofAccepted = false;
    QByteArray m_proofIn;
    QByteArray m_recv;
    bool m_peerFin = false;
    bool m_lostNotified = false;

    // send queue: stable blocks (ngtcp2 keeps pointers until acked), offsets in stream bytes
    mutable std::mutex m_sendMx;
    std::deque<std::unique_ptr<Block>> m_blocks;
    uint64 m_base = 0;        ///< stream offset of m_blocks.front()
    uint64 m_end = 0;         ///< bytes appended
    uint64 m_submitted = 0;   ///< bytes handed to ngtcp2
    uint64 m_acked = 0;       ///< bytes acked
    qint64 m_limit = 512 * 1024;
    std::atomic<bool> m_flushPosted{false};
    std::unique_ptr<QObject> m_poster;   ///< lives on the endpoint thread: target of posted flushes

    // pacing burst (ngtcp2 >= 1.13 defers a second packet at the same timestamp)
    bool m_burstOpen = false;
    uint64 m_burstTs = 0;

    // handshake datagram resend while the punch settles
    QByteArray m_lastLong;
    int m_resendLeft = 0;
    uint64 m_nextResendNs = 0;
    bool m_heardPeer = false;

    std::optional<std::pair<QString, uint64>> m_pendingFail;
    bool m_closeRequested = false;
    uint64 m_closeDeadlineNs = 0;
    uint64 m_startNs = 0;
};

} // namespace eMule::quic

#endif // EMULE_HAVE_QUIC_NATT
