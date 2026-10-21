#pragma once
/// @file NatRendezvous.h
/// @brief LowID<->LowID rendezvous hub.
///
/// Four introducers tell two firewalled peers each other's public UDP endpoint:
/// ed2k-server (0x60-0x62), Lugdunum eserver (0xA6/0xA7/0x37), eNode-go PR_NAT (0xF1)
/// and a Kad serving buddy (OP_RENDEZVOUS). Once an endpoint is known the hub
/// punches the NAT (OP_HOLEPUNCH) and swaps IPv6 addresses over the opened path
/// (OP_UDPRESERVEDPROT2 / OP_NATT_FRAME_IPV6HANDOFF); the target then dials the
/// requester over plain TCP/IPv6. peerEndpointReady is the hook for a UDP transport.
///
/// Wire details: docs/protocol/natt-rendezvous.md.

#include "net/Address.h"
#include "net/natt/NatEnv.h"
#include "net/natt/NatPath.h"
#include "net/natt/NatTypes.h"
#include "utils/Types.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace eMule {

class UpDownClient;

namespace natt {

class NatRendezvous : public QObject {
    Q_OBJECT

public:
    static constexpr uint64 kAttemptTimeoutMs   = 45000;  // Lugdunum cold start alone is ~13 s
    static constexpr uint64 kPunchIntervalMs    = 1000;
    static constexpr uint64 kPunchWindowMs      = 20000;
    static constexpr uint64 kHandoffIntervalMs  = 2000;
    static constexpr uint64 kRequesterDialDelay = 5000;   // the target dials first
    static constexpr uint64 kNoticeRetryMs      = 30 * 60 * 1000;
    static constexpr uint64 kNoticePerIpMs      = 5000;
    static constexpr uint64 kRepeatNoticeMs     = 10000;  // servers re-send a notice for a few s
    static constexpr size_t kMaxAttempts        = 32;
    static constexpr uint8  kHandoffVersion     = 1;
    static constexpr uint8  kHandoffHasIPv6     = 0x01;
    static constexpr uint8  kHandoffAnswer      = 0x02;

    explicit NatRendezvous(std::unique_ptr<NatEnv> env, QObject* parent = nullptr);
    ~NatRendezvous() override;

    NatRendezvous(const NatRendezvous&) = delete;
    NatRendezvous& operator=(const NatRendezvous&) = delete;

    /// A data path exists after a rendezvous: today the IPv6 TCP handoff, which needs
    /// a confident public IPv6 here. ToDo: true unconditionally once the QUIC NAT-T
    /// transport (eMuleAI dialect, 0xB2/0x01) is in.
    [[nodiscard]] static bool hasDataPath();
    [[nodiscard]] bool hasDataPathNow() const;

    [[nodiscard]] NatEnv& env() const { return *m_env; }

    // -- Client flow ---------------------------------------------------------

    /// We are firewalled, the peer is a LowID, a path can introduce us and the peer did
    /// not fail before.
    [[nodiscard]] bool canStart(const PeerInfo& peer) const;
    [[nodiscard]] bool canStart(const UpDownClient& client) const;

    /// Start (or join) a requester attempt. True while one is running for the peer.
    bool start(const PeerInfo& peer);
    bool start(UpDownClient& client);

    /// An attempt for the peer is running.
    [[nodiscard]] bool isActive(const PeerInfo& peer) const;

    /// The peer failed this session; do not retry.
    [[nodiscard]] bool hasFailed(const PeerInfo& peer) const;

    /// Keep a LowID source while we are firewalled: some path could introduce us.
    [[nodiscard]] bool mayKeepLowIdSource(const PeerInfo& peer) const;
    /// Same, through theApp.natRendezvous (false when there is none).
    [[nodiscard]] static bool keepLowIdSource(const PeerInfo& peer);

    /// 1 s maintenance: repeats, punches, handoffs, dials, timeouts.
    void process();

    // -- Input ---------------------------------------------------------------

    /// Server TCP notice (0x37, 0x61, 0x62).
    bool handleServerTcp(uint8 opcode, const QByteArray& payload);
    /// Datagram on the client UDP socket that may come from a server (0xA7, PR_NAT).
    /// Not consumed (false) leaves it to the normal client path; @p data is untouched.
    bool handleServerDatagram(const Endpoint& from, const uint8* data, qsizetype len);
    /// OP_HOLEPUNCH / OP_NATT_ENDPOINT_HINT.
    void handlePeerPacket(uint8 opcode, const uint8* data, uint32 size, const Endpoint& from);
    /// OP_UDPRESERVEDPROT2 frame.
    void handleFrame(uint8 frameType, const uint8* data, uint32 size, const Endpoint& from,
                     bool obfuscated);
    /// We serve a Kad buddy and just relayed an OP_REASKCALLBACKUDP for it.
    void handleBuddyRelay(const Endpoint& requester, const uint8* data, uint32 size,
                          const UpDownClient& served);
    /// Our serving buddy relayed an OP_RENDEZVOUS. @p data starts after the opcode byte.
    bool handleRendezvousFromBuddy(const Endpoint& requester, const uint8* data, uint32 size);

    // -- For paths -------------------------------------------------------------

    [[nodiscard]] NatPath* path(PathKind kind) const;
    [[nodiscard]] Attempt* findAttempt(const std::function<bool(const Attempt&)>& pred) const;
    [[nodiscard]] std::vector<Attempt*> findAll(const std::function<bool(const Attempt&)>& pred) const;
    [[nodiscard]] Attempt* findForPeer(const PeerInfo& peer) const;

    /// A server or buddy named a peer that wants us (target role) or answered our own
    /// request. Applies the admission rules; returns the attempt or nullptr.
    Attempt* acceptNotice(PathKind kind, const PeerInfo& peer, const Endpoint& peerUdp,
                          uint16 peerTcpPort);

    /// The peer's UDP endpoint is known: start punching and the handoff.
    void endpointKnown(Attempt& attempt, const Endpoint& udp, uint16 tcpPort = 0);
    void fail(Attempt& attempt, const QString& why);

    /// Snapshot for tests and logging.
    [[nodiscard]] std::vector<const Attempt*> attempts() const;

    /// Replace a path (tests).
    void setPath(std::unique_ptr<NatPath> path);

    static QString peerKey(const PeerInfo& peer);

signals:
    /// Both mappings should be open now: the hook for a UDP transport.
    void peerEndpointReady(const eMule::natt::Hash16& peerHash, const eMule::Endpoint& udp,
                           eMule::natt::Role role);
    void attemptFinished(quint32 id, bool connected);

private:
    void sendPunch(Attempt& attempt);
    void sendHandoff(Attempt& attempt, bool answer);
    void evaluateDial(Attempt& attempt);
    void dial(Attempt& attempt);
    void finish(Attempt& attempt, bool connected);
    void handleHolePunch(const Endpoint& from);
    void handleHandoff(const uint8* data, uint32 size, const Endpoint& from);
    void removeFinished();
    [[nodiscard]] bool noticeAllowed(const PeerInfo& peer, const Address& from);

    std::unique_ptr<NatEnv> m_env;
    std::vector<std::unique_ptr<NatPath>> m_paths;
    std::vector<std::unique_ptr<Attempt>> m_attempts;
    QHash<QString, uint64> m_failedPeers;       ///< requester side, per session
    QHash<QString, uint64> m_noticeFailed;      ///< target side, retry after kNoticeRetryMs
    QHash<QString, uint64> m_noticeByIp;        ///< rate cap for new target attempts
    QHash<QString, uint64> m_recentDone;        ///< finished attempts; late repeats ignored
    uint32 m_nextId = 1;
};

} // namespace natt
} // namespace eMule
