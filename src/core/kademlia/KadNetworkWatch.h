#pragma once

/// @file KadNetworkWatch.h
/// @brief Tells "own network down" from "contacts gone" (not MFC).
///
/// Without it a node whose uplink fails, or that wakes from sleep, ages every
/// contact to type 4 and empties its routing table within the hour. Same rule as
/// ed3k Protocol.md §10.2 "Own network down".

#include "utils/Types.h"

#include <ctime>
#include <map>

namespace eMule::kad {

class NetworkWatch {
public:
    enum class Event {
        None,
        WentOffline,
        BackOnline,
    };

    explicit NetworkWatch(time_t now = 0) { reset(now); }

    /// Start a fresh silence window: resume from sleep. Keeps hasEvidence();
    /// Kad start builds a new watch instead.
    void reset(time_t now);

    /// A Kad packet arrived. @p ip host order. Returns BackOnline when it ends
    /// an offline phase.
    Event packetReceived(uint32 ip, time_t now);

    /// A request expecting an answer went out. @p ip host order.
    void requestSent(uint32 ip, time_t now);

    /// Call once per tick. Returns WentOffline on the tick it is detected.
    Event update(time_t now);

    /// LAN mode: private addresses count as evidence and as sites.
    void setLanMode(bool lan) { m_lanMode = lan; }

    [[nodiscard]] bool isOffline() const { return m_offline; }
    /// A counted packet arrived since construction: Kad worked once, so its
    /// offline verdict means the network, not blocked UDP.
    [[nodiscard]] bool hasEvidence() const { return m_evidence; }
    /// Last packet that counted; failures after it are blamed on the network.
    [[nodiscard]] time_t silenceSince() const { return m_lastReceived; }
    /// Sites asked since silenceSince() whose answer is overdue.
    [[nodiscard]] int failedSites(time_t now) const;

private:
    [[nodiscard]] bool counts(uint32 ip) const;
    [[nodiscard]] uint32 siteOf(uint32 ip) const;

    std::map<uint32, time_t> m_askedSites;   ///< site -> first request since silence
    time_t m_lastReceived = 0;
    bool m_offline = false;
    bool m_lanMode = false;
    bool m_evidence = false;
};

} // namespace eMule::kad
