/// @file KadFirewallTester.cpp
/// @brief UDP firewall detection implementation.

#include "kademlia/KadFirewallTester.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPListener.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <ctime>

namespace eMule::kad {

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

bool UDPFirewallTester::s_firewalledUDP = false;
bool UDPFirewallTester::s_firewalledLastStateUDP = false;
bool UDPFirewallTester::s_isFWVerifiedUDP = false;
bool UDPFirewallTester::s_nodeSearchStarted = false;
bool UDPFirewallTester::s_timedOut = false;
uint8 UDPFirewallTester::s_fwChecksRunning = 0;
uint8 UDPFirewallTester::s_fwChecksFinished = 0;
uint32 UDPFirewallTester::s_testStart = 0;
uint32 UDPFirewallTester::s_lastSucceededTime = 0;
std::list<Contact> UDPFirewallTester::s_possibleTestClients;
std::list<UDPFirewallTester::UsedClient> UDPFirewallTester::s_usedTestClients;

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

bool UDPFirewallTester::isFirewalledUDP(bool lastStateIfTesting)
{
    if (lastStateIfTesting && isFWCheckUDPRunning())
        return s_firewalledLastStateUDP;
    return s_firewalledUDP;
}

void UDPFirewallTester::setUDPFWCheckResult(bool succeeded, bool testCancelled,
                                             uint32 /*fromIP*/, uint16 /*incomingPort*/)
{
    if (testCancelled) {
        logDebug(QStringLiteral("Kad: UDP FW check cancelled"));
        return;
    }

    ++s_fwChecksFinished;

    if (succeeded) {
        s_firewalledUDP = false;
        s_isFWVerifiedUDP = true;
        s_lastSucceededTime = static_cast<uint32>(time(nullptr));
        logDebug(QStringLiteral("Kad: UDP FW check succeeded — not firewalled"));
    }

    // Check if all tests are finished
    if (s_fwChecksFinished >= KADEMLIAFIREWALLCHECKS) {
        if (!s_isFWVerifiedUDP)
            s_firewalledUDP = true;
        s_fwChecksRunning = 0;
        s_firewalledLastStateUDP = s_firewalledUDP;
        logDebug(QStringLiteral("Kad: UDP FW check complete — firewalled: %1")
                     .arg(s_firewalledUDP ? "yes" : "no"));
    }
}

void UDPFirewallTester::reCheckFirewallUDP(bool setUnverified)
{
    if (setUnverified)
        s_isFWVerifiedUDP = false;
    s_fwChecksRunning = 0;
    s_fwChecksFinished = 0;
    s_possibleTestClients.clear();
    s_usedTestClients.clear();
    s_nodeSearchStarted = false;
    s_timedOut = false;
}

bool UDPFirewallTester::isFWCheckUDPRunning()
{
    return s_fwChecksRunning > 0 || s_nodeSearchStarted;
}

bool UDPFirewallTester::isVerified()
{
    return s_isFWVerifiedUDP;
}

void UDPFirewallTester::addPossibleTestContact(const UInt128& clientID, uint32 ip,
                                                uint16 udpPort, uint16 tcpPort,
                                                const UInt128& target, uint8 version,
                                                const KadUDPKey& udpKey, bool ipVerified)
{
    // Only accept Kad2 contacts with sufficient version
    if (version < KADEMLIA_VERSION8_49b)
        return;

    // Don't add if already enough clients
    if (s_possibleTestClients.size() >= 20)
        return;

    Contact contact(clientID, ip, udpPort, tcpPort, target, version, udpKey, ipVerified);
    s_possibleTestClients.push_back(std::move(contact));
}

void UDPFirewallTester::reset()
{
    s_firewalledUDP = false;
    s_firewalledLastStateUDP = false;
    s_isFWVerifiedUDP = false;
    s_nodeSearchStarted = false;
    s_timedOut = false;
    s_fwChecksRunning = 0;
    s_fwChecksFinished = 0;
    s_testStart = 0;
    s_lastSucceededTime = 0;
    s_possibleTestClients.clear();
    s_usedTestClients.clear();
}

void UDPFirewallTester::connected()
{
    if (!s_nodeSearchStarted && getUDPCheckClientsNeeded()) {
        s_nodeSearchStarted = true;
        s_testStart = static_cast<uint32>(time(nullptr));
        SearchManager::findNodeFWCheckUDP();
    }
}

void UDPFirewallTester::queryNextClient()
{
    if (!getUDPCheckClientsNeeded() || s_possibleTestClients.empty())
        return;

    auto* prefs = Kademlia::getInstancePrefs();
    auto* routingZone = Kademlia::getInstanceRoutingZone();

    while (!s_possibleTestClients.empty()) {
        Contact testContact = std::move(s_possibleTestClients.front());
        s_possibleTestClients.pop_front();

        // Skip if this is our own ID
        if (prefs && testContact.getClientID() == prefs->kadId())
            continue;

        // Skip if already in routing table (they might know our IP already)
        if (routingZone && routingZone->getContact(testContact.getClientID()))
            continue;

        // Skip if already tested
        bool alreadyTested = false;
        for (const auto& used : s_usedTestClients) {
            if (used.contact.getClientID() == testContact.getClientID()) {
                alreadyTested = true;
                break;
            }
        }
        if (alreadyTested)
            continue;

        UsedClient used;
        used.contact = testContact;
        used.answered = false;
        s_usedTestClients.push_back(std::move(used));

        ++s_fwChecksRunning;

        // Send the UDP firewall check request
        auto* udpListener = Kademlia::getInstanceUDPListener();
        if (udpListener) {
            UInt128 clientID = testContact.getClientID();
            udpListener->sendNullPacket(KADEMLIA2_FIREWALLUDP,
                testContact.getIPAddress(), testContact.getUDPPort(),
                testContact.getUDPKey(), &clientID);
        }

        logDebug(QStringLiteral("Kad: Sent UDP FW check to %1")
                     .arg(testContact.getClientID().toHexString()));
        break;
    }
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

bool UDPFirewallTester::getUDPCheckClientsNeeded()
{
    return s_fwChecksRunning + s_fwChecksFinished < KADEMLIAFIREWALLCHECKS;
}

} // namespace eMule::kad
