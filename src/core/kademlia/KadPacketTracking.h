#pragma once

/// @file KadPacketTracking.h
/// @brief Token bucket rate limiting and challenge tracking for Kad UDP.
///
/// Ported from kademlia/kademlia/UDPFirewallTester.h (PacketTracking part).

#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

namespace eMule::kad {

// ---------------------------------------------------------------------------
// Tracking structures
// ---------------------------------------------------------------------------

struct TrackedPacket {
    uint32 insertedTime = 0;
    uint32 ip = 0;
    uint8 opcode = 0;
};

struct TrackedChallenge {
    uint32 insertedTime = 0;
    uint32 ip = 0;
    UInt128 contactID;
    UInt128 challenge;
    uint8 opcode = 0;
};

struct TrackedPacketsIn {
    /// Per-opcode token bucket. Tokens are denominated in **milliseconds** of
    /// allowance, matching MFC PacketTracking.cpp: a budget of N packets/minute
    /// costs MIN2MS(1)/N per packet, the bucket refills at real time and caps at
    /// MIN2MS(1). Going negative means over budget.
    struct TrackedRequest {
        uint64 latest = 0;
        int64 tokens = 0;
        uint8 opcode = 0;
        bool dbgLogged = false;
    };
    uint64 lastExpire = 0;
    uint32 ip = 0;
    std::vector<TrackedRequest> trackedRequests;
};

// ---------------------------------------------------------------------------
// PacketTracking — base class for KademliaUDPListener
// ---------------------------------------------------------------------------

class PacketTracking {
public:
    PacketTracking();
    virtual ~PacketTracking();

protected:
    void addTrackedOutPacket(uint32 ip, uint8 opcode);
    bool isOnOutTrackList(uint32 ip, uint8 opcode, bool dontRemove = false);
    /// Incoming request flood protection.
    /// @return 0 = allowed, 1 = flood (drop), 2 = massive flood (drop, ban, and
    ///         the caller should expire the contact from the routing zone).
    ///         Matches MFC PacketTracking.cpp:99-208 — note this is the
    ///         *opposite* polarity to the pre-rewrite port code, which returned
    ///         1 for "allowed".
    int inTrackListIsAllowedPacket(uint32 ip, uint8 opcode, bool validReceiverKey);
    void inTrackListCleanup();
    void addLegacyChallenge(const UInt128& contactID, const UInt128& challengeID,
                            uint32 ip, uint8 opcode);
    bool isLegacyChallenge(const UInt128& challengeID, uint32 ip, uint8 opcode,
                           UInt128& outContactID);
    bool hasActiveLegacyChallenge(uint32 ip) const;
    /// True for request opcodes whose response handler gates on isOnOutTrackList().
    static bool isTrackedOutListRequestPacket(uint8 opcode);

private:

    std::list<TrackedPacket> m_trackedRequests;
    std::list<TrackedChallenge> m_challengeRequests;
    std::unordered_map<uint32, TrackedPacketsIn*> m_trackPacketsIn;
    uint64 m_lastTrackInCleanup = 0; // getTickCount() ms, matches TrackedRequest
};

} // namespace eMule::kad
