#pragma once
/// @file Ed2kServerHolePunchPath.h
/// @brief ed2k-server 0.9.84+ LowID hole punch (flag 0x20000).
///
/// TCP 0x60 <target id 4><our udp 2> -> 0x61 to both sides
/// <ip 4><tcp 2><udp 2><hash 16><role 1>[<peer id 4>], or 0x62 <target id 4><reason 1>.
/// The server reads our mapped UDP port from the 0x9F keepalive we send every 60 s
/// from the client UDP socket (it trusts it for 600 s).

#include "net/natt/NatPath.h"

namespace eMule::natt {

class Ed2kServerHolePunchPath final : public NatPath {
public:
    static constexpr uint64 kKeepaliveMs = 60000;
    static constexpr uint64 kRepeatMs    = 10000;
    static constexpr uint32 kMaxRequests = 3;
    static constexpr uint32 kInfoMinSize = 25;      // pre-0.9.84 servers
    static constexpr uint32 kInfoIdSize  = 29;      // with peer_id

    using NatPath::NatPath;

    [[nodiscard]] PathKind kind() const override { return PathKind::Ed2kServer; }
    [[nodiscard]] bool canServe(const PeerInfo& peer) const override;
    bool request(Attempt& attempt) override;
    void repeat(Attempt& attempt) override;
    void tick() override;
    bool handleServerTcp(uint8 opcode, const QByteArray& payload) override;

private:
    void sendRequest(Attempt& attempt);
    void onInfo(const uint8* data, uint32 size);
    void onFail(const uint8* data, uint32 size);

    Endpoint m_keepaliveTo;
    uint64 m_lastKeepaliveMs = 0;
};

} // namespace eMule::natt
