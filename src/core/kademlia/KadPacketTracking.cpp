#include "pch.h"
/// @file KadPacketTracking.cpp
/// @brief Token bucket rate limiting and challenge tracking implementation.

#include "kademlia/KadPacketTracking.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadMiscUtils.h"
#include "kademlia/Kademlia.h"
#include "net/Address.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/TimeUtils.h"

#include <algorithm>
#include <cstdlib>


namespace eMule::kad {

namespace {
// MFC uses SEC2MS(180) for both the out-track list (PacketTracking.cpp:52) and
// legacy challenges (:245,:267). The port previously used 20s, which would drop
// a legitimate but slow response — notably a publish answer from a loaded node.
constexpr uint32 kTrackTimeout = 180;     // seconds for outgoing packet tracking
constexpr uint32 kChallengeTimeout = 180; // seconds for challenge expiry

// Inbound flood tracking. Tokens are milliseconds of allowance; a budget of N
// packets/minute costs kTokenBucketMax/N per packet. MFC PacketTracking.cpp:99.
constexpr int64 kTokenBucketMax = MIN2MS(1);       // full bucket
constexpr int64 kMassiveFloodTokens = -MIN2MS(3);  // ban threshold
constexpr uint64 kInCleanupInterval = MIN2MS(12);  // MFC :158
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

int PacketTracking::inTrackListIsAllowedPacket(uint32 ip, uint8 opcode,
                                               bool /*validReceiverKey*/)
{
    // Flood protection for _incoming_ requests: drops too-frequent requests from
    // a single IP, which saves CPU, avoids being used as a response amplifier,
    // and slows index scanning / fake-publish floods.
    //
    // MFC PacketTracking.cpp:99-208. Note validReceiverKey is deliberately
    // ignored (MFC comments the parameter out): a valid key proves the sender is
    // not spoofed, not that it is behaving, so letting it bypass the limiter —
    // as this port previously did — hands unlimited request rate to any peer
    // that completed a handshake.
    //
    // Budgets are packets/minute, expressed as a per-packet millisecond cost.
    int64 token;
    const uint8 dbgOrigOpcode = opcode;
    switch (opcode) {
    case KADEMLIA2_BOOTSTRAP_REQ:
        token = kTokenBucketMax / 2;
        break;
    case KADEMLIA2_HELLO_REQ:
        token = kTokenBucketMax / 3;
        break;
    case KADEMLIA2_REQ:
        token = kTokenBucketMax / 10;
        break;
    case KADEMLIA2_SEARCH_NOTES_REQ:
    case KADEMLIA2_SEARCH_KEY_REQ:
    case KADEMLIA2_SEARCH_SOURCE_REQ:
        token = kTokenBucketMax / 3;
        break;
    case KADEMLIA2_PUBLISH_KEY_REQ:
        token = kTokenBucketMax / 4;
        break;
    case KADEMLIA2_PUBLISH_SOURCE_REQ:
        token = kTokenBucketMax / 3;
        break;
    case KADEMLIA2_PUBLISH_NOTES_REQ:
        token = kTokenBucketMax / 2;
        break;
    case KADEMLIA_FIREWALLED2_REQ:
        opcode = KADEMLIA_FIREWALLED_REQ; // share one bucket with the v1 form
        [[fallthrough]];
    case KADEMLIA_FIREWALLED_REQ:
    case KADEMLIA_FINDBUDDY_REQ:
        token = kTokenBucketMax / 2;
        break;
    case KADEMLIA_CALLBACK_REQ:
        token = kTokenBucketMax / 1;
        break;
    case KADEMLIA2_PING:
        token = kTokenBucketMax / 2;
        break;
    default:
        // Not a request — a response. Never throttled, otherwise a busy search
        // would rate-limit its own answers.
        return 0;
    }

    const uint64 now = getTickCount();

    if (now >= m_lastTrackInCleanup + kInCleanupInterval)
        inTrackListCleanup();

    TrackedPacketsIn* tracked = nullptr;
    auto it = m_trackPacketsIn.find(ip);
    if (it != m_trackPacketsIn.end()) {
        tracked = it->second;
    } else {
        tracked = new TrackedPacketsIn();
        tracked->ip = ip;
        m_trackPacketsIn[ip] = tracked;
    }

    TrackedPacketsIn::TrackedRequest* req = nullptr;
    for (auto& r : tracked->trackedRequests) {
        if (r.opcode == opcode) {
            req = &r;
            break;
        }
    }

    if (!req) {
        // First request with this opcode from this IP — start the bucket full,
        // minus this packet's cost.
        tracked->trackedRequests.push_back(
            TrackedPacketsIn::TrackedRequest{now, kTokenBucketMax - token, opcode, false});
        return 0;
    }

    // Refill by elapsed real time, cap at a full bucket, then charge this packet.
    req->tokens += static_cast<int64>(now - req->latest);
    if (req->tokens > kTokenBucketMax)
        req->tokens = kTokenBucketMax;
    req->tokens -= token;
    req->latest = now;
    // Kept only so cleanup knows when this entry is safe to drop. Recomputing it
    // on every packet is what stops an active flooder's counters from being
    // reset out from under the limiter.
    tracked->lastExpire =
        std::max(tracked->lastExpire, now + static_cast<uint64>(std::abs(req->tokens) + token));

    auto* kadInst = Kademlia::instance();
    if (kadInst && kadInst->isRunningInLANMode() && isLanIP(htonl(ip)))
        return 0; // no flood detection in LAN mode

    if (req->tokens < 0) {
        if (req->tokens < kMassiveFloodTokens) {
            // So far over the limit that it can only be deliberate.
            logKad(QStringLiteral("Kad: Massive request flood for opcode 0x%1 (0x%2) from %3 — banning IP")
                       .arg(opcode, 2, 16, QChar(u'0'))
                       .arg(dbgOrigOpcode, 2, 16, QChar(u'0'))
                       .arg(ipToString(ip)));
            if (theApp.clientList)
                theApp.clientList->addBannedClient(Address::fromHostOrder(ip));
            return 2; // drop, ban, and let the caller expire the contact
        }
        if (!req->dbgLogged) {
            req->dbgLogged = true;
            logKad(QStringLiteral("Kad: Request flood for opcode 0x%1 (0x%2) from %3 — dropping this opcode")
                       .arg(opcode, 2, 16, QChar(u'0'))
                       .arg(dbgOrigOpcode, 2, 16, QChar(u'0'))
                       .arg(ipToString(ip)));
        }
        return 1; // drop
    }
    req->dbgLogged = false;
    return 0;
}

void PacketTracking::inTrackListCleanup()
{
    const uint64 now = getTickCount();
    m_lastTrackInCleanup = now;

    // Drop only entries whose bucket has fully refilled (lastExpire is the tick
    // at which that happens). Previously this compared against a creation-time
    // stamp that was never updated, so an actively flooding IP had its counters
    // wiped every 5 minutes and the limit could never accumulate.
    auto it = m_trackPacketsIn.begin();
    while (it != m_trackPacketsIn.end()) {
        if (now >= it->second->lastExpire) {
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
        if (it->ip != ip || it->opcode != opcode)
            continue;
        // A stored challenge of zero is a wildcard, used by the v7 PING
        // verification path where the answer (a PONG) carries no challenge to
        // echo back — receiving it at all is the proof. MFC PacketTracking.cpp
        // IsLegacyChallenge().
        if (it->challenge == 0u || it->challenge == challengeID) {
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
    // Mirrors MFC PacketTracking.cpp:57-76 (Kad2 opcodes only — this port does
    // not speak Kad1). Only opcodes whose *response* handler calls
    // isOnOutTrackList() belong here; anything else is pure list churn.
    //
    // KADEMLIA2_SEARCH_KEY_REQ / _SEARCH_SOURCE_REQ are deliberately absent, as
    // in MFC: a keyword search fans out to many nodes and no response handler
    // gates on them, so tracking them would only cost entries.
    //
    // KADEMLIA_FIREWALLED{,2}_REQ are also absent on purpose — firewalledCheck()
    // tracks them itself under the single KADEMLIA_FIREWALLED_REQ key so that
    // both the v1 and v2 request forms are accepted by one response check.
    switch (opcode) {
    case KADEMLIA2_BOOTSTRAP_REQ:
    case KADEMLIA2_HELLO_REQ:
    case KADEMLIA2_HELLO_RES: // needed by process_KADEMLIA2_HELLO_RES_ACK
    case KADEMLIA2_REQ:
    case KADEMLIA2_SEARCH_NOTES_REQ:
    case KADEMLIA2_PUBLISH_KEY_REQ:
    case KADEMLIA2_PUBLISH_SOURCE_REQ:
    case KADEMLIA2_PUBLISH_NOTES_REQ:
    case KADEMLIA_FINDBUDDY_REQ:
    case KADEMLIA_CALLBACK_REQ:
    case KADEMLIA2_PING:
        return true;
    default:
        return false;
    }
}

} // namespace eMule::kad
