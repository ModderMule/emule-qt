#include "pch.h"
/// @file KadFirewallTester.cpp
/// @brief UDP firewall detection implementation.

#include "kademlia/KadFirewallTester.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPListener.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <ctime>

namespace eMule::kad {

// MFC UDPFirewallTester.cpp — UDP_FIREWALLCHECK_CLIENTSNEEDED = 2
// Must match in both setUDPFWCheckResult() and getUDPCheckClientsNeeded().
// Note: KADEMLIAFIREWALLCHECKS (4) is the TCP constant — do NOT use it here.
static constexpr uint8 kUDPFWCheckClientsNeeded = 2;

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
        if (s_fwChecksRunning > 0)
            --s_fwChecksRunning;
        logKad(QStringLiteral("Kad: UDP FW check cancelled — running=%1, finished=%2")
                   .arg(s_fwChecksRunning).arg(s_fwChecksFinished));
        queryNextClient();
        return;
    }

    ++s_fwChecksFinished;
    logKad(QStringLiteral("Kad: UDP FW check result — succeeded=%1, finished=%2/%3")
               .arg(QLatin1StringView(succeeded ? "yes" : "no"))
               .arg(s_fwChecksFinished)
               .arg(kUDPFWCheckClientsNeeded));

    if (succeeded) {
        s_firewalledUDP = false;
        s_isFWVerifiedUDP = true;
        s_lastSucceededTime = static_cast<uint32>(time(nullptr));
        logKad(QStringLiteral("Kad: UDP FW check succeeded — not firewalled"));
    }

    // Check if all tests are finished
    if (s_fwChecksFinished >= kUDPFWCheckClientsNeeded) {
        if (!s_isFWVerifiedUDP)
            s_firewalledUDP = true;
        s_fwChecksRunning = 0;
        s_firewalledLastStateUDP = s_firewalledUDP;
        logKad(QStringLiteral("Kad: UDP FW check complete — firewalled: %1")
                   .arg(QLatin1StringView(s_firewalledUDP ? "yes" : "no")));
    }
}

void UDPFirewallTester::reCheckFirewallUDP(bool setUnverified)
{
    logKad(QStringLiteral("Kad: UDP FW recheck requested (setUnverified=%1)")
               .arg(QLatin1StringView(setUnverified ? "yes" : "no")));
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
    if (s_fwChecksRunning > 0 || s_nodeSearchStarted) {
        // Timeout after 45s — matches MFC UDPFirewallTester.cpp
        if (s_testStart != 0
            && static_cast<uint32>(time(nullptr)) - s_testStart > 45)
        {
            s_timedOut = true;
            s_fwChecksRunning = 0;
            s_fwChecksFinished = 0;
            s_nodeSearchStarted = false;
            s_possibleTestClients.clear();
            logKad(QStringLiteral("Kad: UDP FW check timed out"));
            return false;
        }
        return true;
    }
    return false;
}

bool UDPFirewallTester::isVerified()
{
    return s_isFWVerifiedUDP;
}

bool UDPFirewallTester::needsMoreTestContacts()
{
    return getUDPCheckClientsNeeded() && s_possibleTestClients.size() < 20;
}

void UDPFirewallTester::addPossibleTestContact(const UInt128& clientID, uint32 ip,
                                                uint16 udpPort, uint16 tcpPort,
                                                const UInt128& target, uint8 version,
                                                const KadUDPKey& udpKey, bool ipVerified)
{
    // Only accept Kad2 contacts with sufficient version
    if (version < KADEMLIA_VERSION8_49b) {
        logKad(QStringLiteral("Kad: UDP FW test contact rejected — version %1 too low")
                   .arg(version));
        return;
    }

    // Don't add if already enough clients
    if (s_possibleTestClients.size() >= 20)
        return;

    Contact contact(clientID, ip, udpPort, tcpPort, target, version, udpKey, ipVerified);
    s_possibleTestClients.push_back(std::move(contact));
    logKad(QStringLiteral("Kad: UDP FW test contact added — pool size %1")
               .arg(s_possibleTestClients.size()));
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
        logKad(QStringLiteral("Kad: UDP FW tester connected — starting node search for test clients"));
        SearchManager::findNodeFWCheckUDP();
    }
}

void UDPFirewallTester::queryNextClient()
{
    if (!getUDPCheckClientsNeeded() || s_possibleTestClients.empty()) {
        logKad(QStringLiteral("Kad: UDP FW queryNextClient — needed=%1, pool=%2")
                   .arg(QLatin1StringView(getUDPCheckClientsNeeded() ? "yes" : "no"))
                   .arg(s_possibleTestClients.size()));
        return;
    }

    auto* prefs = Kademlia::getInstancePrefs();
    auto* routingZone = Kademlia::getInstanceRoutingZone();

    while (!s_possibleTestClients.empty()) {
        Contact testContact = std::move(s_possibleTestClients.front());
        s_possibleTestClients.pop_front();

        // Skip if this is our own ID
        if (prefs && testContact.getClientID() == prefs->kadId()) {
            logKad(QStringLiteral("Kad: UDP FW skip contact — own ID"));
            continue;
        }

        // Skip if already in routing table (they might know our IP already)
        if (routingZone && routingZone->getContact(testContact.getClientID())) {
            logKad(QStringLiteral("Kad: UDP FW skip contact — already in routing table"));
            continue;
        }

        // Skip if we already know this IP from the client list
        if (routingZone && routingZone->getContact(testContact.getIPAddress(), 0, false)) {
            logKad(QStringLiteral("Kad: UDP FW skip contact — IP already known"));
            continue;
        }

        // Skip if already tested
        bool alreadyTested = false;
        for (const auto& used : s_usedTestClients) {
            if (used.contact.getClientID() == testContact.getClientID()) {
                alreadyTested = true;
                break;
            }
        }
        if (alreadyTested) {
            logKad(QStringLiteral("Kad: UDP FW skip contact — already tested"));
            continue;
        }

        UsedClient used;
        used.contact = testContact;
        used.answered = false;
        s_usedTestClients.push_back(std::move(used));

        ++s_fwChecksRunning;

        // Request UDP firewall check via TCP connection (matches original
        // theApp.clientlist->DoRequestFirewallCheckUDP at srchybrid/kademlia/UDPFirewallTester.cpp:255)
        if (!theApp.clientList || !theApp.clientList->doRequestFirewallCheckUDP(testContact)) {
            logKad(QStringLiteral("Kad: UDP FW check TCP request failed for %1")
                       .arg(testContact.getClientID().toHexString()));
            --s_fwChecksRunning;
            continue;
        }

        logKad(QStringLiteral("Kad: Initiated UDP FW check via TCP to %1")
                   .arg(testContact.getClientID().toHexString()));
        break;
    }
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

bool UDPFirewallTester::getUDPCheckClientsNeeded()
{
    return s_fwChecksRunning + s_fwChecksFinished < kUDPFWCheckClientsNeeded;
}

} // namespace eMule::kad
