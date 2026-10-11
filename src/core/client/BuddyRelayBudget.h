#pragma once

/// @file BuddyRelayBudget.h
/// @brief Requester-side mirror of the limits a Kad buddy relay applies. Not MFC.
///
/// A relay drops requests over its limits without an answer, so the requester counts
/// too and does not send what would be dropped. See docs/protocol/kad-buddy-search.md.

#include "net/Address.h"
#include "utils/Types.h"

#include <array>
#include <deque>
#include <map>
#include <unordered_map>

namespace eMule {

/// Relayed packets per served buddy per minute — relay: UpDownClient::allowBuddyRelay().
inline constexpr uint32 kMaxBuddyRelaysPerMin = 60;

/// Default of the maxServedBuddies preference. A relay accepts that many
/// KADEMLIA_CALLBACK_REQ per minute from one IP; the requester assumes the default.
inline constexpr uint16 kDefaultMaxServedBuddies = 8;

enum class RelayRequest : uint8 {
    KadCallback,    ///< KADEMLIA_CALLBACK_REQ, relayed as OP_CALLBACK
    ReaskCallback   ///< OP_REASKCALLBACKUDP, relayed as OP_REASKCALLBACKTCP
};

class BuddyRelayBudget {
public:
    /// Checks and charges one request to `relay` for the node with `buddyID` (16 bytes).
    /// False: the relay would drop it, do not send. A refusal charges nothing.
    [[nodiscard]] bool tryCharge(const Address& relay, const uint8* buddyID,
                                 RelayRequest kind, uint64 now);

    void clear();

private:
    using BuddyID = std::array<uint8, 16>;

    // Same bucket as PacketTracking::inTrackListIsAllowedPacket(): tokens are ms
    struct CallerBucket {
        int64 tokens = 0;
        uint64 latest = 0;
    };

    void prune(uint64 now);

    std::unordered_map<Address, CallerBucket> m_callbackBuckets;  // by relay IP
    std::map<BuddyID, std::deque<uint64>> m_relaySends;           // send ticks, last minute
    uint64 m_lastPrune = 0;
};

} // namespace eMule
