#pragma once

/// @file KadRoutingBin.h
/// @brief Kademlia K-bucket (ported from kademlia/routing/RoutingBin.h).

#include "kademlia/KadTypes.h"

#include <cstdint>
#include <unordered_map>

namespace eMule::kad {

/// A single K-bucket in the Kademlia routing table.
/// Holds up to K contacts sorted by last-seen time (oldest first).
class RoutingBin {
public:
    RoutingBin();
    ~RoutingBin();

    RoutingBin(const RoutingBin&) = delete;
    RoutingBin& operator=(const RoutingBin&) = delete;

    bool addContact(Contact* contact);
    void setAlive(Contact* contact);
    void setTCPPort(uint32 ip, uint16 udpPort, uint16 tcpPort);
    void removeContact(Contact* contact, bool noTrackingAdjust = false);

    /// Full bin: put `newcomer` in the place of the weakest contact not in use,
    /// if that one is expired or `newcomer` is at least kReplaceMargin stronger.
    /// Every IP / subnet limit is checked as if the victim were already gone, and
    /// nothing is removed unless the newcomer goes in.
    /// @return the contact that left (caller deletes it), or null.
    [[nodiscard]] Contact* replaceWeakest(Contact* newcomer);
    static constexpr int kReplaceMargin = 120;

    [[nodiscard]] Contact* getContact(const UInt128& id);
    [[nodiscard]] Contact* getContact(uint32 ip, uint16 port, bool tcpPort);
    [[nodiscard]] Contact* getOldest();
    [[nodiscard]] Contact* getRandomContact(uint32 maxType, uint32 minKadVersion);

    [[nodiscard]] uint32 getSize() const;
    void getNumContacts(uint32& inOutContacts, uint32& inOutFilteredContacts, uint8 minVersion) const;
    [[nodiscard]] uint32 getRemaining() const;

    void getEntries(ContactArray& result, bool emptyFirst = true);
    void getClosestTo(uint32 maxType, const UInt128& target, uint32 maxRequired,
                      ContactMap& result, bool emptyFirst = true, bool setInUse = false);

    bool changeContactIPAddress(Contact* contact, uint32 newIP);
    void pushToBottom(Contact* contact);
    void setAllContactsVerified();

    static bool checkGlobalIPLimits(uint32 ip, uint16 port, bool log);
    [[nodiscard]] bool hasOnlyLANNodes() const;

    /// Reset global IP/subnet tracking maps (for testing).
    static void resetGlobalTracking();

    bool m_dontDeleteContacts = false;

private:
    static void adjustGlobalTracking(uint32 ip, bool increase);

    ContactList m_entries;

    static std::unordered_map<uint32, uint32> s_globalContactIPs;
    static std::unordered_map<uint32, uint32> s_globalContactSubnets;
};

} // namespace eMule::kad
