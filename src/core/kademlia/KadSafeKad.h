/// @file KadSafeKad.h
/// @brief Kademlia node reputation system — Sybil attack defense.
/// Ported from eMuleAI SafeKad.h

#pragma once

#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <map>
#include <ctime>

namespace eMule::kad {

class SafeKad
{
public:
    SafeKad() noexcept;
    ~SafeKad() noexcept;

    void trackNode(uint32 ip, uint16 port, const UInt128& id, bool verified = false) noexcept;
    void trackProblematicNode(uint32 ip, uint16 port) noexcept;
    void banIP(uint32 ip) noexcept;

    [[nodiscard]] bool isBadNode(uint32 ip, uint16 port, const UInt128& id,
                                  uint8 kadVersion, bool verified = false,
                                  bool onePerIP = true) noexcept;
    [[nodiscard]] bool isBanned(uint32 ip) noexcept;
    [[nodiscard]] bool isProblematic(uint32 ip, uint16 port) noexcept;

    void shutdownCleanup() noexcept;

private:
    struct NodeAddress
    {
        uint32 ip = 0;
        uint16 port = 0;

        auto operator<=>(const NodeAddress&) const = default;
    };

    template <typename T>
    struct AgeOf
    {
        time_t age = 0;
        T      key{};

        bool operator<(const AgeOf& o) const noexcept
        {
            return age < o.age || (age == o.age && key < o.key);
        }
    };

    struct Tracked
    {
        UInt128     lastID;
        time_t      lastIDChange = 0;
        time_t      lastReferenced = 0;
        bool        idVerified = false;
        NodeAddress node;

        operator AgeOf<NodeAddress>() const noexcept { return {lastReferenced, node}; }
    };

    struct Banned
    {
        time_t  banned = 0;
        time_t  lastReferenced = 0;
        uint32  ip = 0;

        operator AgeOf<uint32>() const noexcept { return {lastReferenced, ip}; }
    };

    struct Problematic
    {
        time_t      failed = 0;
        time_t      lastReferenced = 0;
        NodeAddress node;

        operator AgeOf<NodeAddress>() const noexcept { return {lastReferenced, node}; }
    };

    void cleanup(time_t nodeMaxAge = 3600, time_t banMaxAge = 3600,
                 time_t problemMaxAge = 300) noexcept;

    static constexpr size_t kMaxTrackedNodes       = 10000;
    static constexpr size_t kMaxProblematicNodes    = 10000;
    static constexpr size_t kMaxBannedIPs           = 1000;
    static constexpr time_t kMinIDChangeInterval    = 3600;      // 1 hour
    static constexpr time_t kMaxBanTime             = 24 * 3600; // 24 hours
    static constexpr time_t kMaxProblematicTime     = 5 * 60;    // 5 minutes

    std::map<NodeAddress, Tracked*>            m_trackedNodes;
    std::map<AgeOf<NodeAddress>, Tracked*>     m_trackedAge;
    std::map<NodeAddress, Problematic*>        m_problematicNodes;
    std::map<AgeOf<NodeAddress>, Problematic*> m_problematicAge;
    std::map<uint32, Banned*>                  m_bannedIPs;
    std::map<AgeOf<uint32>, Banned*>           m_bannedAge;
    time_t m_lastCleanup = 0;
};

} // namespace eMule::kad
