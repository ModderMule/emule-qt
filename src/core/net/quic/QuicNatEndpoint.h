#pragma once
/// @file QuicNatEndpoint.h
/// @brief All QUIC NAT-T connections of this client, on the client UDP port (0xB2 frames).
///
/// Routes 0xB2 0x01 datagrams by connection ID, admits Initials only from peers a
/// rendezvous armed, answers caps, keeps the key-frame hash table and decides per
/// datagram whether it leaves obfuscated (docs/protocol/natt-quic.md §Obfuscation).

#include "net/Address.h"
#include "net/quic/QuicNatIo.h"
#include "net/quic/QuicNatProtocol.h"

#include <QObject>
#include <QTimer>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

#ifdef EMULE_HAVE_QUIC_NATT
#include <ngtcp2/ngtcp2.h>
#endif

namespace eMule::quic {

class QuicNatConnection;

class QuicNatEndpoint : public QObject {
    Q_OBJECT

public:
    explicit QuicNatEndpoint(QuicNatIo& io, QObject* parent = nullptr);
    ~QuicNatEndpoint() override;

    /// ngtcp2 and the TLS contexts work in this build and process.
    [[nodiscard]] static bool available();

    /// A 0xB2 frame (type byte, payload after it) from the client UDP socket.
    /// Returns false for frame types that are not ours.
    bool handleFrame(uint8 type, const uint8* data, size_t len, const Endpoint& from, bool obfuscated);

    /// Accept one connection from @p peer (any port on its address: NATs rebind) for a
    /// while. @p peerHash is what its proof must name (zero = anyone).
    void expect(const Endpoint& peer, const Hash16& peerHash);
    /// Dial @p peer; its proof must name @p peerHash (zero = anyone). onEstablished or
    /// onFailed follows. Obfuscation is learnPeerHash()'s business.
    bool dial(const Endpoint& peer, const Hash16& peerHash);
    /// EAQC caps (ack = answer) to @p peer.
    void sendCaps(const Endpoint& peer, const Hash16& peerHash, const Hash16& fileHash, bool ack);
    /// The rendezvous told us who is at @p peer: obfuscate for this hash.
    void learnPeerHash(const Endpoint& peer, const Hash16& hash);

    [[nodiscard]] size_t connectionCount() const { return m_conns.size(); }
    /// A live or starting connection to @p addr.
    [[nodiscard]] bool hasConnection(const Address& addr) const;
    /// The peer at @p peer answered since we started sending (any frame).
    [[nodiscard]] bool heardFrom(const Endpoint& peer) const;

    /// Proof accepted both ways: the stream is ready for a socket. Called queued.
    std::function<void(std::shared_ptr<QuicNatConnection> conn, bool outgoing)> onEstablished;
    /// A dial or an armed connection ended before onEstablished.
    std::function<void(const Endpoint& peer, const Hash16& hash, const QString& reason)> onFailed;
    /// CAPS or CAPS_ACK from @p peer.
    std::function<void(const Endpoint& peer, const Caps& caps, bool ack)> onCaps;

    // --- connection side ---
    [[nodiscard]] uint64 nowNs() const;
    void armTimer();
#ifdef EMULE_HAVE_QUIC_NATT
    void addCid(const ngtcp2_cid& cid, QuicNatConnection* conn);
    void removeCid(const ngtcp2_cid& cid);
#endif
    void sendDatagram(QuicNatConnection& conn, const uint8* data, size_t len, bool longHeader);
    void connectionEstablished(QuicNatConnection& conn);
    void connectionClosed(QuicNatConnection& conn, bool wasEstablished);

private:
    struct PeerState {
        Hash16 hash{};
        bool hashKnown = false;
        bool hashFromKeyFrame = false;
        uint64 lastUseMs = 0;
        uint64 firstObfuscatedMs = 0;   ///< first obfuscated frame, 0 = none yet
        bool heard = false;             ///< any 0xB2 frame from this endpoint
        bool plain = false;             ///< answered only after a plain fallback: stay plain
        bool fallbackToggle = false;
        uint64 lastKeyFrameMs = 0;
    };
    struct Expectation {
        Endpoint peer;
        Hash16 hash{};
        uint64 untilMs = 0;
    };

    void onTimer();
    void routeQuic(const uint8* data, size_t len, const Endpoint& from);
    void sendFramed(uint8 type, const uint8* data, size_t len, const Endpoint& to);
    [[nodiscard]] const Hash16* cryptHashFor(const Endpoint& to, bool quicDatagram);
    PeerState& peer(const Endpoint& ep);
    void noteHeard(const Endpoint& from);
    void reap();
    void prune(uint64 nowMs);
    [[nodiscard]] std::shared_ptr<QuicNatConnection> find(QuicNatConnection* raw) const;

    QuicNatIo& m_io;
    QTimer m_timer;
    std::vector<std::shared_ptr<QuicNatConnection>> m_conns;
    std::map<QByteArray, QuicNatConnection*> m_byCid;
    std::map<Endpoint, PeerState> m_peers;
    std::vector<Expectation> m_expect;
    std::map<Address, uint64> m_lastCapsAck;
    std::set<Address> m_droppedLogged;   ///< unexpected Initials already logged since the last prune
    bool m_reapPosted = false;
    uint64 m_lastPruneMs = 0;
};

} // namespace eMule::quic
