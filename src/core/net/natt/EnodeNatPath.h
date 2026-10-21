#pragma once
/// @file EnodeNatPath.h
/// @brief eNode-go PR_NAT rendezvous (flag 0x40000, UDP port from ST_NAT_PORT).
///
/// Frame <0xF1><size 4 LE = payload + 1><opcode 1><payload>, endpoints big-endian.
/// We REGISTER our user hash per family (the server stores the observed source),
/// keep it alive inside the 30 s TTL, and pair with SYNC2 <our hash><connAck 4><peer hash>.
/// Both sides get SYNC (v4) / SYNC_EX / SYNC_IPV6 with the other's endpoint.
/// Same-server only: cross-server use needs (hash, rendezvous server) carried in
/// Kad or source exchange, which nothing defines yet (ToDo).

#include "net/natt/NatPath.h"

#include <QByteArray>

namespace eMule::natt {

class EnodeNatPath final : public NatPath {
public:
    static constexpr uint16 kDefaultPort  = 2004;
    static constexpr uint64 kKeepaliveMs  = 20000;   // server TTL 30 s
    static constexpr uint64 kRegisterMs   = 5000;    // until acked
    static constexpr uint64 kRepeatMs     = 5000;
    static constexpr uint32 kMaxRequests  = 3;

    using NatPath::NatPath;

    [[nodiscard]] PathKind kind() const override { return PathKind::EnodeNat; }
    [[nodiscard]] bool canServe(const PeerInfo& peer) const override;
    bool request(Attempt& attempt) override;
    void repeat(Attempt& attempt) override;
    void tick() override;
    bool handleServerUdp(const Endpoint& from, const uint8* data, qsizetype len) override;

    /// <0xF1><size LE><opcode><payload>
    [[nodiscard]] static QByteArray frame(uint8 opcode, const QByteArray& payload);

private:
    struct Registration {
        Endpoint to;
        uint64 lastSentMs = 0;
        bool acked = false;
    };

    [[nodiscard]] Endpoint natEndpoint(bool ipv6) const;
    [[nodiscard]] Endpoint syncEndpoint() const;   ///< where SYNC2 goes
    [[nodiscard]] bool isNatServer(const Endpoint& from) const;
    void keepRegistered(Registration& reg, const Endpoint& to);
    void sendSync2(Attempt& attempt);
    void onSync(const Endpoint& udp, const uint8* hash, uint32 connAck);
    void onFailed(uint8 reason, const uint8* targetHash);

    Registration m_v4;
    Registration m_v6;
};

} // namespace eMule::natt
