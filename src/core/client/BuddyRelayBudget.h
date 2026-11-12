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
#include <utility>

namespace eMule {

/// Relayed bytes per served buddy per minute — relay: UpDownClient::allowBuddyRelay().
/// Bytes, not packets: a packet count high enough for a full queue let a flood of large
/// reasks through. Same budget as aMule (#1809).
inline constexpr uint32 kBuddyRelayBytesPerMin = 64 * 1024;

/// OP_CALLBACK body a relayed KADEMLIA_CALLBACK_REQ becomes: BuddyID 16, file hash 16,
/// caller IP 4, caller TCP port 2.
inline constexpr uint32 kRelayedCallbackSize = 16 + 16 + 4 + 2;

/// Largest OP_REASKCALLBACKUDP a relay forwards, the most the format can carry: BuddyID 16,
/// file hash 16, part count 2, part bits for 65,535 parts 8,192, complete sources 2.
/// Anything larger is no reask. Same cap as aMule (#1809).
inline constexpr uint32 kMaxReaskCallbackUdpSize = 16 + 16 + 2 + (0xFFFF + 7) / 8 + 2;
static_assert(kMaxReaskCallbackUdpSize == 8228);

/// OP_REASKCALLBACKTCP body an OP_REASKCALLBACKUDP of `udpSize` becomes: BuddyID dropped,
/// caller address added, 6 bytes for IPv4, 22 for IPv6 (0xFFFFFFFF, address 16, port 2).
[[nodiscard]] constexpr uint32 relayedReaskSize(uint32 udpSize, bool ipv6)
{
    return udpSize - 16 + (ipv6 ? 22 : 6);
}

/// Served buddies the requester assumes a relay allows: the auto value at the default
/// upload capacity (Preferences::autoServedBuddies()). A relay accepts that many
/// KADEMLIA_CALLBACK_REQ per minute from one IP.
inline constexpr uint16 kAssumedRelayServedBuddies = 8;

/// Relay: KADEMLIA_CALLBACK_REQ relayed per sender IP to one served buddy per minute.
/// Above it only while the combined budget of all buddies is under half used
/// (ClientList::servedRelayBelowHalf()). A relayed callback makes
/// the buddy dial the sender's address, which UDP can forge, so it stays bounded per sender.
inline constexpr uint32 kCallbacksPerSenderPerMin = 100;

/// Passes per key in a fixed minute that starts with the key's first pass. With a limit of
/// 1 this is "one in any minute". Relay: KADEMLIA_CALLBACK_REQ per sender IP and served
/// buddy; requester: the mirror per relay IP and buddy ID.
template <typename Key>
class MinuteGate {
public:
    static constexpr uint64 kWindowMs = 60 * 1000;

    /// Passes of @p key in its current minute, 0 when it has none.
    [[nodiscard]] uint32 count(const Key& key, uint64 now) const
    {
        const auto it = m_windows.find(key);
        return it == m_windows.end() || now - it->second.start >= kWindowMs ? 0 : it->second.count;
    }
    [[nodiscard]] bool isOpen(const Key& key, uint64 now, uint32 limit = 1) const
    {
        return count(key, now) < limit;
    }
    /// Record a pass; windows older than a minute are pruned once a minute.
    void pass(const Key& key, uint64 now)
    {
        if (now - m_lastPrune >= kWindowMs) {
            m_lastPrune = now;
            std::erase_if(m_windows, [now](const auto& e) { return now - e.second.start >= kWindowMs; });
        }
        auto& w = m_windows[key];
        if (w.count == 0 || now - w.start >= kWindowMs)
            w = {now, 0};
        ++w.count;
    }
    void clear()
    {
        m_windows.clear();
        m_lastPrune = 0;
    }
    [[nodiscard]] size_t size() const { return m_windows.size(); }

private:
    struct Window {
        uint64 start = 0;
        uint32 count = 0;
    };
    std::map<Key, Window> m_windows;
    uint64 m_lastPrune = 0;
};

enum class RelayRequest : uint8 {
    KadCallback,    ///< KADEMLIA_CALLBACK_REQ, relayed as OP_CALLBACK
    ReaskCallback   ///< OP_REASKCALLBACKUDP, relayed as OP_REASKCALLBACKTCP
};

class BuddyRelayBudget {
public:
    /// Checks and charges one request to `relay` for the node with `buddyID` (16 bytes).
    /// `reaskSize`: the OP_REASKCALLBACKUDP size, for a reask only. False: the relay would
    /// drop it, do not send. A refusal charges nothing.
    [[nodiscard]] bool tryCharge(const Address& relay, const uint8* buddyID,
                                 RelayRequest kind, uint32 reaskSize, uint64 now);

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
    MinuteGate<std::pair<Address, BuddyID>> m_callbackPerBuddy;   // by relay IP and buddy
    struct RelaySend {
        uint64 tick = 0;
        uint32 bytes = 0;
    };
    struct BuddySends {
        std::deque<RelaySend> sends;  // last minute
        uint32 bytes = 0;             // sum of sends
    };

    std::map<BuddyID, BuddySends> m_relaySends;
    uint64 m_lastPrune = 0;
};

} // namespace eMule
