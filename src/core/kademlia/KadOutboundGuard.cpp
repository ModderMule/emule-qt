#include "pch.h"
/// @file KadOutboundGuard.cpp
/// @brief Mirror of the receivers' Kad flood buckets, per destination IP.

#include "kademlia/KadOutboundGuard.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"

#include <algorithm>

namespace eMule::kad {

namespace {
constexpr int64 kBucketMax = MIN2MS(1);   // MFC: a full minute of allowance
constexpr uint64 kMetricWindow = MIN2MS(1);

int64 costOf(uint8 opcode)
{
    const uint32 n = mfcRequestsPerMinute(opcode);
    return n ? kBucketMax / n : 0;
}
} // namespace

uint32 mfcRequestsPerMinute(uint8 opcode)
{
    // MFC PacketTracking.cpp:113-149
    switch (opcode) {
    case KADEMLIA2_BOOTSTRAP_REQ:      return 2;
    case KADEMLIA2_HELLO_REQ:          return 3;
    case KADEMLIA2_REQ:                return 10;
    case KADEMLIA2_SEARCH_NOTES_REQ:
    case KADEMLIA2_SEARCH_KEY_REQ:
    case KADEMLIA2_SEARCH_SOURCE_REQ:  return 3;
    case KADEMLIA2_PUBLISH_KEY_REQ:    return 4;
    case KADEMLIA2_PUBLISH_SOURCE_REQ: return 3;
    case KADEMLIA2_PUBLISH_NOTES_REQ:  return 2;
    case KADEMLIA_FIREWALLED2_REQ:
    case KADEMLIA_FIREWALLED_REQ:
    case KADEMLIA_FINDBUDDY_REQ:       return 2;
    case KADEMLIA_CALLBACK_REQ:        return 1;
    case KADEMLIA2_PING:               return 2;
    default:                           return 0;
    }
}

uint8 floodBucketOpcode(uint8 opcode)
{
    return opcode == KADEMLIA_FIREWALLED2_REQ ? uint8{KADEMLIA_FIREWALLED_REQ} : opcode;
}

bool OutboundGuard::allows(uint32 ip, uint8 opcode, Priority prio, uint32 count, uint64 now) const
{
    opcode = floodBucketOpcode(opcode);
    const int64 cost = costOf(opcode);
    if (cost == 0 || count == 0)
        return true;
    int64 reserve = kJitterMs;
    if (prio == Priority::Background && opcode == KADEMLIA2_REQ)
        reserve += cost * kForegroundReqReserve;

    int64 tokens = kBucketMax;   // never sent to: the receiver's bucket starts full
    if (const auto it = m_ips.find(ip); it != m_ips.end()) {
        for (const Bucket& b : it->second.buckets) {
            if (b.opcode == opcode) {
                tokens = tokensAt(b, now);
                break;
            }
        }
    }
    return tokens - cost * static_cast<int64>(count) >= reserve;
}

uint32 OutboundGuard::charge(uint32 ip, uint8 opcode, uint64 now)
{
    opcode = floodBucketOpcode(opcode);
    const int64 cost = costOf(opcode);
    if (cost == 0)
        return 0;
    PerIP& per = m_ips[ip];
    auto it = std::ranges::find(per.buckets, opcode, &Bucket::opcode);
    if (it == per.buckets.end()) {
        per.buckets.push_back(Bucket{opcode, kBucketMax, now, {}});
        it = std::prev(per.buckets.end());
    }
    // Same arithmetic as the receiver: refill, cap, charge. Ungated opcodes may go negative.
    it->tokens = tokensAt(*it, now) - cost;
    it->latest = now;
    per.fullAt = std::max(per.fullAt, now + static_cast<uint64>(kBucketMax - it->tokens));
    per.lastSent = now;

    std::erase_if(it->sent, [now](uint64 t) { return now - t >= kMetricWindow; });
    it->sent.push_back(now);
    return static_cast<uint32>(it->sent.size());
}

void OutboundGuard::cleanup(uint64 now)
{
    // Full again and no send left in the metric window: the entry says nothing new
    std::erase_if(m_ips, [now](const auto& kv) {
        return now >= kv.second.fullAt && now - kv.second.lastSent >= kMetricWindow;
    });
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int64 OutboundGuard::tokensAt(const Bucket& b, uint64 now)
{
    return std::min(kBucketMax, b.tokens + static_cast<int64>(now - b.latest));
}

} // namespace eMule::kad
