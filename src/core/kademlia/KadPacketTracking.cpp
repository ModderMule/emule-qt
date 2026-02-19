/// @file KadPacketTracking.cpp
/// @brief Token bucket rate limiting and challenge tracking implementation.

#include "kademlia/KadPacketTracking.h"
#include "kademlia/KadLog.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <ctime>

namespace eMule::kad {

namespace {
constexpr uint32 kTrackTimeout = 20;       // seconds for outgoing packet tracking
constexpr uint32 kChallengeTimeout = 20;   // seconds for challenge expiry
constexpr uint32 kCleanupInterval = 300;   // 5 minutes for inbound tracking cleanup
constexpr int kMaxTokens = 3;              // max token bucket tokens
constexpr int kTokenRefillRate = 3;        // tokens per refill period
constexpr uint32 kTokenRefillInterval = 3; // seconds between refills
} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PacketTracking::PacketTracking() = default;

PacketTracking::~PacketTracking()
{
    for (auto& [ip, tracked] : m_trackPacketsIn)
        delete tracked;
    m_trackPacketsIn.clear();
}

// ---------------------------------------------------------------------------
// Protected methods
// ---------------------------------------------------------------------------

void PacketTracking::addTrackedOutPacket(uint32 ip, uint8 opcode)
{
    TrackedPacket tp;
    tp.insertedTime = static_cast<uint32>(time(nullptr));
    tp.ip = ip;
    tp.opcode = opcode;
    m_trackedRequests.push_back(tp);
}

bool PacketTracking::isOnOutTrackList(uint32 ip, uint8 opcode, bool dontRemove)
{
    uint32 now = static_cast<uint32>(time(nullptr));

    // Clean expired entries from front while iterating
    while (!m_trackedRequests.empty() && (now - m_trackedRequests.front().insertedTime) > kTrackTimeout)
        m_trackedRequests.pop_front();

    for (auto it = m_trackedRequests.begin(); it != m_trackedRequests.end(); ++it) {
        if (it->ip == ip && it->opcode == opcode) {
            if (!dontRemove)
                m_trackedRequests.erase(it);
            return true;
        }
    }
    return false;
}

int PacketTracking::inTrackListIsAllowedPacket(uint32 ip, uint8 opcode, bool validReceiverKey)
{
    // Privileged requests: bootstrap & hello always allowed
    if (opcode == KADEMLIA2_BOOTSTRAP_REQ || opcode == KADEMLIA2_HELLO_REQ)
        return 1;

    // Rate limit: check token bucket for this IP + opcode
    uint32 now = static_cast<uint32>(time(nullptr));

    // Periodic cleanup
    if ((now - m_lastTrackInCleanup) > kCleanupInterval)
        inTrackListCleanup();

    TrackedPacketsIn* tracked = nullptr;
    auto it = m_trackPacketsIn.find(ip);
    if (it != m_trackPacketsIn.end()) {
        tracked = it->second;
    } else {
        tracked = new TrackedPacketsIn();
        tracked->ip = ip;
        tracked->lastExpire = now;
        m_trackPacketsIn[ip] = tracked;
    }

    // Find or create request tracker for this opcode
    TrackedPacketsIn::TrackedRequest* req = nullptr;
    for (auto& r : tracked->trackedRequests) {
        if (r.opcode == opcode) {
            req = &r;
            break;
        }
    }

    if (!req) {
        tracked->trackedRequests.push_back({});
        req = &tracked->trackedRequests.back();
        req->opcode = opcode;
        req->tokens = kMaxTokens;
        req->latest = now;
    }

    // Refill tokens based on elapsed time
    uint32 elapsed = now - req->latest;
    if (elapsed >= kTokenRefillInterval) {
        uint32 refills = elapsed / kTokenRefillInterval;
        req->tokens = std::min(req->tokens + static_cast<int>(refills * kTokenRefillRate), kMaxTokens);
        req->latest = now;
    }

    // Check if a token is available
    if (req->tokens > 0 || validReceiverKey) {
        if (req->tokens > 0)
            --req->tokens;
        req->dbgLogged = false;
        return 1; // Allowed
    }

    if (!req->dbgLogged) {
        logKad(QStringLiteral("Kad: Rate limiting packet opcode 0x%1 from %2")
                   .arg(opcode, 2, 16, QChar(u'0'))
                   .arg(ip));
        req->dbgLogged = true;
    }
    return 0; // Rate limited
}

void PacketTracking::inTrackListCleanup()
{
    uint32 now = static_cast<uint32>(time(nullptr));
    m_lastTrackInCleanup = now;

    auto it = m_trackPacketsIn.begin();
    while (it != m_trackPacketsIn.end()) {
        if ((now - it->second->lastExpire) > kCleanupInterval) {
            delete it->second;
            it = m_trackPacketsIn.erase(it);
        } else {
            ++it;
        }
    }
}

void PacketTracking::addLegacyChallenge(const UInt128& contactID, const UInt128& challengeID,
                                         uint32 ip, uint8 opcode)
{
    TrackedChallenge tc;
    tc.insertedTime = static_cast<uint32>(time(nullptr));
    tc.ip = ip;
    tc.contactID = contactID;
    tc.challenge = challengeID;
    tc.opcode = opcode;
    m_challengeRequests.push_back(tc);
}

bool PacketTracking::isLegacyChallenge(const UInt128& challengeID, uint32 ip, uint8 opcode,
                                        UInt128& outContactID)
{
    uint32 now = static_cast<uint32>(time(nullptr));

    // Clean expired from front
    while (!m_challengeRequests.empty() && (now - m_challengeRequests.front().insertedTime) > kChallengeTimeout)
        m_challengeRequests.pop_front();

    for (auto it = m_challengeRequests.begin(); it != m_challengeRequests.end(); ++it) {
        if (it->ip == ip && it->opcode == opcode && it->challenge == challengeID) {
            outContactID = it->contactID;
            m_challengeRequests.erase(it);
            return true;
        }
    }
    return false;
}

bool PacketTracking::hasActiveLegacyChallenge(uint32 ip) const
{
    uint32 now = static_cast<uint32>(time(nullptr));
    for (const auto& tc : m_challengeRequests) {
        if (tc.ip == ip && (now - tc.insertedTime) <= kChallengeTimeout)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

bool PacketTracking::isTrackedOutListRequestPacket(uint8 opcode)
{
    switch (opcode) {
    case KADEMLIA2_BOOTSTRAP_REQ:
    case KADEMLIA2_HELLO_REQ:
    case KADEMLIA2_REQ:
    case KADEMLIA2_SEARCH_KEY_REQ:
    case KADEMLIA2_SEARCH_SOURCE_REQ:
    case KADEMLIA2_SEARCH_NOTES_REQ:
    case KADEMLIA2_PUBLISH_KEY_REQ:
    case KADEMLIA2_PUBLISH_SOURCE_REQ:
    case KADEMLIA2_PUBLISH_NOTES_REQ:
    case KADEMLIA2_PING:
        return true;
    default:
        return false;
    }
}

} // namespace eMule::kad
