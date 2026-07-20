#pragma once

/// @file KadFirewallTester.h
/// @brief UDP firewall detection (ported from kademlia/kademlia/UDPFirewallTester.h).

#include "kademlia/KadContact.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <cstdint>
#include <list>

namespace eMule::kad {

/// Static class for UDP firewall detection.
class UDPFirewallTester {
public:
    static bool isFirewalledUDP(bool lastStateIfTesting);
    static void setUDPFWCheckResult(bool succeeded, bool testCancelled,
                                     uint32 fromIP, uint16 incomingPort);
    static void reCheckFirewallUDP(bool setUnverified);
    static bool isFWCheckUDPRunning();
    static bool isVerified();
    static void addPossibleTestContact(const UInt128& clientID, uint32 ip,
                                        uint16 udpPort, uint16 tcpPort,
                                        const UInt128& target, uint8 version,
                                        const KadUDPKey& udpKey, bool ipVerified,
                                        uint8 connectOptions = 0,
                                        const UInt128& clientHash = UInt128());
    static bool needsMoreTestContacts();
    static void reset();
    static void connected();
    static void queryNextClient();

    /// Test seam: backdate the check's start time so the 6-minute timeout can be
    /// exercised without waiting for it.
    static void debugSetTestStart(uint32 startTime);

    /// Test seam: register a client as "asked" without going through the TCP
    /// request in queryNextClient(), so result validation can be exercised.
    static void debugAddUsedTestClient(uint32 ip, uint16 udpPort);

    /// Test seam: how many firewall checks have reported so far.
    [[nodiscard]] static uint8 debugChecksFinished();
    /// Test seam: how many firewall checks are still awaiting a result.
    [[nodiscard]] static uint8 debugChecksRunning();

private:
    static bool getUDPCheckClientsNeeded();

    struct UsedClient {
        Contact contact;
        bool answered = false;
    };

    static bool s_firewalledUDP;
    static bool s_firewalledLastStateUDP;
    static bool s_isFWVerifiedUDP;
    static bool s_nodeSearchStarted;
    static bool s_timedOut;
    static uint8 s_fwChecksRunning;
    static uint8 s_fwChecksFinished;
    static uint32 s_testStart;
    static uint32 s_lastSucceededTime;
    static std::list<Contact> s_possibleTestClients;
    static std::list<UsedClient> s_usedTestClients;
};

} // namespace eMule::kad
