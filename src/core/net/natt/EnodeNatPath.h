#pragma once
/// @file EnodeNatPath.h
/// @brief eNode-go PR_NAT rendezvous (flag 0x40000, UDP port from ST_NAT_PORT).
///
/// Frame <0xF1><size 4 LE = payload + 1><opcode 1><payload>, endpoints big-endian.
/// We REGISTER our user hash per family (the server stores the observed source),
/// keep it alive inside the 90 s TTL (30 s on eNode-go v0.5.1 and older), and pair with SYNC2 <our hash><connAck 4><peer hash>.
/// Both sides get SYNC (v4) / SYNC_EX / SYNC_IPV6 with the other's endpoint.
/// The register ack names the server's own address and preferred port per family (0 = keep
/// ours): later PR_NAT traffic of that family goes there, until the registration is lost.
/// Wake-up (flag 0x200000, our login bit 0x200000): no standing registration; the
/// server sends TCP OP_NAT_WAKE <families 1> when a peer asks for us, and we REGISTER
/// once per family. A requester REGISTERs right before its SYNC2.
/// Same server only: both peers must be logged in to it. Peers on another server go
/// through ForeignCallbackPath (andrey23127/ed2k-server#40).

#include "net/natt/NatPath.h"

#include <QByteArray>

namespace eMule::natt {

class EnodeNatPath final : public NatPath {
public:
    static constexpr uint16 kDefaultPort  = 2004;
    static constexpr uint64 kKeepaliveMs  = 60000;   // server TTL 90 s
    static constexpr uint64 kRegisterMs   = 5000;    // until acked
    static constexpr uint32 kRegisterTries = 6;      // unanswered at kRegisterMs (30 s), then
    static constexpr uint64 kRegisterSlowMs = 60000; // a refused REGISTER gets no answer
    static constexpr uint64 kRepeatMs     = 5000;
    static constexpr uint32 kMaxRequests  = 3;
    /// No PING for this long after an acked registration: the server lost it (or our NAT
    /// gave us a new port), so register again. One unanswered keepalive is enough: on an
    /// older server with a 30 s TTL every keepalive finds the candidate expired.
    static constexpr uint64 kPingLossMs   = kKeepaliveMs + 5000;
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
        uint32 unanswered = 0;   ///< REGISTERs since the last ack
        bool acked = false;
    };
    /// Endpoint a register ack moved us to; valid while @c from is still our default.
    struct Learned {
        Endpoint from;
        Endpoint to;
    };

    /// We would be a useful target: NAT on, firewalled, a data path.
    [[nodiscard]] bool active() const;
    /// The server wakes us (supportsNatWake): no standing registration.
    [[nodiscard]] bool wakeMode() const;
    void registerNow(bool ipv6);

    /// Learned endpoint of that family, else defaultNatEndpoint().
    [[nodiscard]] Endpoint natEndpoint(bool ipv6) const;
    /// Server address + ST_NAT_PORT (2004 when absent).
    [[nodiscard]] Endpoint defaultNatEndpoint(bool ipv6) const;
    [[nodiscard]] Endpoint learnedEndpoint(bool ipv6) const;
    void onRegisterAck(bool ipv6, const Endpoint& from, const Address& addr, uint16 port);
    [[nodiscard]] Endpoint syncEndpoint() const;   ///< where SYNC2 goes
    [[nodiscard]] bool isOwnNatServer(const Endpoint& from) const;
    void keepRegistered(bool ipv6, bool want);
    void sendSync2(Attempt& attempt);
    void onSync(const Endpoint& udp, const uint8* hash, uint32 connAck);
    void onFailed(uint8 reason, const uint8* targetHash);

    Registration m_v4;
    Registration m_v6;
    Learned m_learnedV4;
    Learned m_learnedV6;
};

} // namespace eMule::natt
