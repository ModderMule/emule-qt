#pragma once

/// @file KadOutboundGuard.h
/// @brief Our side of the receivers' Kad flood limits, per destination IP.
///
/// Not MFC. Every eMule node limits requests per sender IP and opcode with a token
/// bucket (MFC PacketTracking.cpp:99): a full minute of allowance, then drops, and a
/// 3-minute deficit bans the sender's IP for 2 h. A large share publishes far faster
/// than MFC, so its lookups can pile onto one contact. The guard mirrors each
/// receiver's bucket for us and holds a request back that it would drop.

#include "utils/Types.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace eMule::kad {

/// MFC's allowance per sender IP and minute for a Kad request; 0 = not a limited request.
/// KADEMLIA_CALLBACK_REQ is MFC's value, not what we accept ourselves.
[[nodiscard]] uint32 mfcRequestsPerMinute(uint8 opcode);
/// The bucket an opcode counts in: FIREWALLED2 shares FIREWALLED's, as in MFC.
[[nodiscard]] uint8 floodBucketOpcode(uint8 opcode);

class OutboundGuard {
public:
    /// Background = publishing and routing upkeep; it leaves part of the lookup
    /// allowance to the user's searches and downloads.
    enum class Priority : uint8 { Foreground, Background };

    /// Margin kept in every bucket, ms: the receiver refills by arrival time, and
    /// network jitter can bunch packets we spaced out
    static constexpr int64 kJitterMs = 1000;
    /// Share of the KADEMLIA2_REQ allowance background lookups leave free: 3 of 10/min
    static constexpr uint32 kForegroundReqReserve = 3;

    /// True if @p count requests of @p opcode to @p ip stay inside its MFC budget now.
    /// Opcodes MFC does not limit are always allowed.
    [[nodiscard]] bool allows(uint32 ip, uint8 opcode, Priority prio, uint32 count, uint64 now) const;
    /// Charge one sent request, gated or not (HELLO, PING and the rest count too).
    /// @return requests of this opcode sent to @p ip in the last 60 s, 0 if not limited
    uint32 charge(uint32 ip, uint8 opcode, uint64 now);
    /// Drop IPs whose buckets are full again. Cheap; call on a timer.
    void cleanup(uint64 now);

    [[nodiscard]] size_t trackedIPs() const { return m_ips.size(); }

private:
    struct Bucket {
        uint8 opcode = 0;
        int64 tokens = 0;          ///< ms of allowance at @c latest
        uint64 latest = 0;
        std::vector<uint64> sent;  ///< send times within the last 60 s, for the metrics
    };
    struct PerIP {
        std::vector<Bucket> buckets;
        uint64 fullAt = 0;         ///< tick all buckets are full again
        uint64 lastSent = 0;
    };

    [[nodiscard]] static int64 tokensAt(const Bucket& b, uint64 now);

    std::unordered_map<uint32, PerIP> m_ips;
};

} // namespace eMule::kad
