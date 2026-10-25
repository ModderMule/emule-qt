#pragma once
/// @file EnodeNatPath.h
/// @brief eNode-go PR_NAT rendezvous (flag 0x40000, UDP port from ST_NAT_PORT).
///
/// Frame <0xF1><size 4 LE = payload + 1><opcode 1><payload>, endpoints big-endian.
/// We REGISTER our user hash per family (the server stores the observed source),
/// keep it alive inside the 90 s TTL (30 s on eNode-go v0.5.1 and older), and pair with SYNC2 <our hash><connAck 4><peer hash>.
/// Both sides get SYNC (v4) / SYNC_EX / SYNC_IPV6 with the other's endpoint.
/// Wake-up (flag 0x200000, our login bit 0x200000): no standing registration; the
/// server sends TCP OP_NAT_WAKE <families 1> when a peer asks for us, and we REGISTER
/// once per family. A requester REGISTERs right before its SYNC2.
/// Cross-server: we register only on our login server. As a requester for a peer
/// elsewhere we SYNC2 our own server first, then listed servers with UDP flag 0x40000
/// one at a time; when none pairs it, that hash is not swept again this session.
/// ToDo: publish (hash, rendezvous server) in Kad, so a requester needs no guessing.

#include "net/natt/NatPath.h"

#include <QByteArray>
#include <QHash>

#include <vector>

namespace eMule::natt {

class EnodeNatPath final : public NatPath {
public:
    static constexpr uint16 kDefaultPort  = 2004;
    static constexpr uint64 kKeepaliveMs  = 60000;   // server TTL 90 s
    static constexpr uint64 kRegisterMs   = 5000;    // until acked
    static constexpr uint64 kRepeatMs     = 5000;
    static constexpr uint32 kMaxRequests  = 3;
    /// No PING for this long after an acked registration: the server lost it (or our NAT
    /// gave us a new port), so register again. One unanswered keepalive is enough: on an
    /// older server with a 30 s TTL every keepalive finds the candidate expired.
    static constexpr uint64 kPingLossMs   = kKeepaliveMs + 5000;
    static constexpr uint64 kSweepStepMs  = 4000;    // per server; a wake's FAILED takes 3 s
    static constexpr uint32 kSweepSends   = 2;       // SYNC2s per server
    static constexpr size_t kMaxSweepServers = 8;
    static constexpr size_t kMaxRemembered   = 4096; // m_noRendezvous / m_pairedVia
    static constexpr uint8  kWakeIPv4     = 0x01;   ///< OP_NAT_WAKE family bits
    static constexpr uint8  kWakeIPv6     = 0x02;

    using NatPath::NatPath;

    [[nodiscard]] PathKind kind() const override { return PathKind::EnodeNat; }
    [[nodiscard]] bool canServe(const PeerInfo& peer) const override;
    bool request(Attempt& attempt) override;
    void repeat(Attempt& attempt) override;
    void tick() override;
    bool handleServerTcp(uint8 opcode, const QByteArray& payload) override;
    bool handleServerUdp(const Endpoint& from, const uint8* data, qsizetype len) override;

    /// <0xF1><size LE><opcode><payload>
    [[nodiscard]] static QByteArray frame(uint8 opcode, const QByteArray& payload);

private:
    struct Registration {
        Endpoint to;
        uint64 lastSentMs = 0;
        uint64 lastPingMs = 0;   ///< last ack or PING
        bool acked = false;
    };

    /// We would be a useful target: NAT on, firewalled, a data path.
    [[nodiscard]] bool active() const;
    /// The server wakes us (supportsNatWake): no standing registration.
    [[nodiscard]] bool wakeMode() const;
    void registerNow(bool ipv6);

    [[nodiscard]] Endpoint natEndpoint(bool ipv6) const;
    [[nodiscard]] Endpoint syncEndpoint() const;   ///< where SYNC2 goes
    [[nodiscard]] bool isNatServer(const Endpoint& from) const;   ///< our server or a sweep one
    [[nodiscard]] bool isOwnNatServer(const Endpoint& from) const;
    void keepRegistered(Registration& reg, const Endpoint& to);
    void sendSync2(Attempt& attempt);
    /// Our server (when it pairs) then the other capable ones, for a peer not on ours.
    [[nodiscard]] std::vector<Endpoint> sweepServers(const PeerInfo& peer) const;
    /// SYNC2 target of @p attempt: its sweep server, or our own.
    [[nodiscard]] Endpoint attemptEndpoint(const Attempt& attempt) const;
    /// Sweep: on to the next server; none left = remember the hash and fall through.
    void nextServer(Attempt& attempt, const QString& why);
    void onSync(const Endpoint& from, const Endpoint& udp, const uint8* hash, uint32 connAck);
    void onFailed(const Endpoint& from, uint8 reason, const uint8* targetHash);

    Registration m_v4;
    Registration m_v6;
    QHash<QByteArray, uint64> m_noRendezvous;   ///< hash -> when no server paired it (session)
    struct Paired { Endpoint server; uint64 atMs = 0; };
    QHash<QByteArray, Paired> m_pairedVia;      ///< hash -> server that paired it last
};

} // namespace eMule::natt
