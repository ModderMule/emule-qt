#include "pch.h"
/// @file BuddyRelayBudget.cpp
/// @brief Requester-side mirror of the Kad buddy relay limits.

#include "client/BuddyRelayBudget.h"

#include "utils/Opcodes.h"

#include <algorithm>
#include <cstring>

namespace eMule {

namespace {

constexpr uint64 kWindow = MIN2MS(1);
constexpr int64 kBucketMax = MIN2MS(1);                               // full bucket
constexpr int64 kCallbackCost = kBucketMax / kAssumedRelayServedBuddies;

} // namespace

bool BuddyRelayBudget::tryCharge(const Address& relay, const uint8* buddyID,
                                 RelayRequest kind, uint32 reaskSize, uint64 now)
{
    if (!buddyID)
        return false;

    if (now - m_lastPrune >= kWindow)
        prune(now);

    BuddyID id;
    std::memcpy(id.data(), buddyID, id.size());

    // The relayed size. A reask is charged in its IPv6 form: which family the relay
    // sees us on is unknown here, and the larger one always fits.
    const uint32 bytes = kind == RelayRequest::KadCallback
                             ? kRelayedCallbackSize
                             : relayedReaskSize(std::max<uint32>(reaskSize, 17), true);

    // Per served buddy: a sliding window. The relay counts in a fixed one whose start is
    // unknown here, so only "64 KiB in any minute" fits into it for certain.
    auto& buddy = m_relaySends[id];
    while (!buddy.sends.empty() && now - buddy.sends.front().tick >= kWindow) {
        buddy.bytes -= buddy.sends.front().bytes;
        buddy.sends.pop_front();
    }
    if (buddy.bytes + bytes > kBuddyRelayBytesPerMin)
        return false;

    // Per caller IP: the relay limits only the Kad request
    if (kind == RelayRequest::KadCallback) {
        const Address key = relay.peerKey();
        auto it = m_callbackBuckets.find(key);
        if (it == m_callbackBuckets.end()) {
            it = m_callbackBuckets.emplace(key, CallerBucket{kBucketMax, now}).first;
        } else {
            auto& bucket = it->second;
            bucket.tokens = std::min<int64>(kBucketMax,
                                            bucket.tokens + static_cast<int64>(now - bucket.latest));
            bucket.latest = now;
        }
        if (it->second.tokens < kCallbackCost)
            return false;
        it->second.tokens -= kCallbackCost;
    }

    buddy.sends.push_back({now, bytes});
    buddy.bytes += bytes;
    return true;
}

void BuddyRelayBudget::clear()
{
    m_callbackBuckets.clear();
    m_relaySends.clear();
    m_lastPrune = 0;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void BuddyRelayBudget::prune(uint64 now)
{
    m_lastPrune = now;
    // A bucket untouched for a minute is full again
    std::erase_if(m_callbackBuckets, [now](const auto& entry) {
        return now - entry.second.latest >= kWindow;
    });
    std::erase_if(m_relaySends, [now](const auto& entry) {
        return entry.second.sends.empty() || now - entry.second.sends.back().tick >= kWindow;
    });
}

} // namespace eMule
