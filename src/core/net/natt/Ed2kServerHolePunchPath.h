#pragma once
/// @file Ed2kServerHolePunchPath.h
/// @brief ed2k-server 0.9.84+ LowID hole punch (flag 0x20000).
///
/// TCP 0x60 <target id 4><our udp 2> -> 0x61 to both sides
/// <ip 4><tcp 2><udp 2><hash 16><role 1>[<peer id 4>], or 0x62 <target id 4><reason 1>.
/// The server reads our mapped UDP port from the 0x9F keepalive we send every 60 s
/// from the client UDP socket (it trusts it for 600 s).
///
/// Extension (server flag 0x100000, we log in with 0x100000 | 0x1000; ed2k-server#39):
/// one 0x9F with each 0x60; a target is woken by Lugdunum's 0x37, answers with 0x9F at
/// 0 / 0.5 / 1 s and punches; the 0x61 that follows is 30 bytes, <peer_crypt 1>
/// appended, and its role byte says who dials. 0x9F goes server-key obfuscated once we
/// hold the key: after the first obfuscated one the server ignores plain ones.

#include "net/natt/NatPath.h"

namespace eMule::natt {

class Ed2kServerHolePunchPath final : public NatPath {
public:
    static constexpr uint64 kKeepaliveMs = 60000;
    static constexpr uint64 kRepeatMs    = 10000;
    static constexpr uint32 kMaxRequests = 3;
    static constexpr uint32 kInfoMinSize = 25;      // pre-0.9.84 servers
    static constexpr uint32 kInfoIdSize  = 29;      // with peer_id
    static constexpr uint32 kInfoCryptSize = 30;    // extension pair: with peer_crypt
    static constexpr uint64 kBurstStepMs = 500;     // target 0x9F at 0, 0.5 and 1 s
    static constexpr uint8  kBurstCount  = 3;
    static constexpr uint64 kInfoWaitMs  = 4000;    // server waits 3 s for our 0x9F, then sends 0x61

    using NatPath::NatPath;

    [[nodiscard]] PathKind kind() const override { return PathKind::Ed2kServer; }
    [[nodiscard]] bool canServe(const PeerInfo& peer) const override;
    bool request(Attempt& attempt) override;
    void repeat(Attempt& attempt) override;
    void tick() override;
    bool fastTick() override;
    bool handleServerTcp(uint8 opcode, const QByteArray& payload) override;

private:
    void sendRequest(Attempt& attempt);
    /// One 0x9F now; false when nothing could be sent.
    bool sendKeepalive(const Server& server);
    void onInfo(const uint8* data, uint32 size);
    void onFail(const uint8* data, uint32 size);
    void onCallbackNotice(const uint8* data, uint32 size);
    [[nodiscard]] bool extensionActive() const;

    Endpoint m_session;             ///< server session the keepalive state belongs to
    uint32 m_sessionID = 0;
    uint64 m_lastKeepaliveMs = 0;
    bool m_sentObfuscated = false;  ///< plain 0x9F are ignored from here on
};

} // namespace eMule::natt
