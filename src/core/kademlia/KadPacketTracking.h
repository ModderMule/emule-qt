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
    struct TrackedRequest {
        uint32 latest = 0;
        int tokens = 0;
        uint8 opcode = 0;
        bool dbgLogged = false;
    };
    uint32 lastExpire = 0;
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
    int inTrackListIsAllowedPacket(uint32 ip, uint8 opcode, bool validReceiverKey);
    void inTrackListCleanup();
    void addLegacyChallenge(const UInt128& contactID, const UInt128& challengeID,
                            uint32 ip, uint8 opcode);
    bool isLegacyChallenge(const UInt128& challengeID, uint32 ip, uint8 opcode,
                           UInt128& outContactID);
    bool hasActiveLegacyChallenge(uint32 ip) const;

private:
    static bool isTrackedOutListRequestPacket(uint8 opcode);

    std::list<TrackedPacket> m_trackedRequests;
    std::list<TrackedChallenge> m_challengeRequests;
    std::unordered_map<uint32, TrackedPacketsIn*> m_trackPacketsIn;
    uint32 m_lastTrackInCleanup = 0;
};

} // namespace eMule::kad
