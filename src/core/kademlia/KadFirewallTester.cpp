#include "pch.h"
/// @file KadFirewallTester.cpp
/// @brief UDP firewall detection implementation.

#include "kademlia/KadFirewallTester.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadMiscUtils.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearchManager.h"
#include "app/AppContext.h"
#include "client/ClientList.h"


namespace eMule::kad {

// MFC UDPFirewallTester.cpp — UDP_FIREWALLCHECK_CLIENTSNEEDED = 2
// Must match in both setUDPFWCheckResult() and getUDPCheckClientsNeeded().
// Note: KADEMLIAFIREWALLCHECKS (4) is the TCP constant — do NOT use it here.
static constexpr uint8 kUDPFWCheckClientsNeeded = 2;

// MFC UDPFirewallTester.cpp:63 — MIN2MS(6). A check that produces no result for
// this long reports *firewalled*; reporting "open" on timeout would advertise a
// port nobody can reach.
static constexpr uint32 kUDPFWCheckTimeoutSecs = 6 * 60;

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
    if (Kademlia::shouldSkipFirewallChecks())
        return false;

    const bool checkingFW = isFWCheckUDPRunning();
    if (s_timedOut) {
        // A check that never produced results means firewalled, not open.
        // MFC UDPFirewallTester.cpp:56-58.
        if (checkingFW)
            return true;
    } else if (checkingFW) {
        // Don't let a timeout firewall us if we already passed a test before.
        auto* kadInst = Kademlia::instance();
        if (!s_firewalledUDP && kadInst && kadInst->isFirewalled() && s_testStart != 0
            && !s_isFWVerifiedUDP
            && static_cast<uint32>(time(nullptr)) >= s_testStart + kUDPFWCheckTimeoutSecs)
        {
            logKad(QStringLiteral("Kad: UDP FW check timed out after %1 min — reporting firewalled")
                       .arg(kUDPFWCheckTimeoutSecs / 60));
            s_timedOut = true;
            return true;
        }
    }

    return (lastStateIfTesting && checkingFW) ? s_firewalledLastStateUDP : s_firewalledUDP;
}

void UDPFirewallTester::setUDPFWCheckResult(bool succeeded, bool testCancelled,
                                             uint32 fromIP, uint16 incomingPort)
{
    auto* prefs = Kademlia::getInstancePrefs();
    const uint32 now = static_cast<uint32>(time(nullptr));

    // Only accept results from a client we actually asked. Without this any peer
    // can inject a verdict. MFC UDPFirewallTester.cpp:84-116.
    bool requested = false;
    for (auto& used : s_usedTestClients) {
        if (used.contact.address().toUint32() != fromIP)
            continue;

        // Late second answer after an already-open verdict: a proper forwarded
        // internal port is more reliable than a NAT-assigned external one, so
        // prefer it. MFC :90-101.
        if (!isFWCheckUDPRunning() && !s_firewalledUDP && s_isFWVerifiedUDP
            && now < s_lastSucceededTime + 10
            && prefs && incomingPort == prefs->internKadPort() && prefs->useExternKadPort())
        {
            prefs->setUseExternKadPort(false);
            logKad(QStringLiteral("Kad: Corrected UDP FW result — using open internal port %1")
                       .arg(incomingPort));
            return;
        }
        if (used.answered) {
            // Each test produces two answer packets; count the client once.
            return;
        }
        used.answered = true;
        requested = true;
        break;
    }

    if (!requested) {
        logKad(QStringLiteral("Kad: Ignoring unrequested UDP FW check result from %1")
                   .arg(ipToString(fromIP)));
        return;
    }

    if (!isFWCheckUDPRunning())
        return; // already decided

    // Always release the slot, whatever the outcome — not doing this on failure
    // deadlocked the tester at running=1/finished=1 and the second test never ran.
    if (s_fwChecksRunning > 0)
        --s_fwChecksRunning;

    if (!testCancelled) {
        ++s_fwChecksFinished;

        if (succeeded) {
            // One positive result is enough.
            s_testStart = 0;
            s_firewalledUDP = false;
            s_isFWVerifiedUDP = true;
            s_timedOut = false;
            s_fwChecksFinished = kUDPFWCheckClientsNeeded; // no further tests
            s_fwChecksRunning = 0;                          // cancel the others
            s_possibleTestClients.clear();                  // keep used clients
            s_lastSucceededTime = now;
            s_firewalledLastStateUDP = s_firewalledUDP;
            SearchManager::cancelNodeFWCheckUDPSearch();

            // Learn which of our ports is actually reachable from outside.
            if (prefs) {
                if (incomingPort == prefs->internKadPort()) {
                    prefs->setUseExternKadPort(false);
                    logKad(QStringLiteral("Kad: UDP FW check succeeded — open, using intern port %1")
                               .arg(incomingPort));
                } else if (incomingPort != 0 && incomingPort == prefs->externalKadPort()) {
                    prefs->setUseExternKadPort(true);
                    logKad(QStringLiteral("Kad: UDP FW check succeeded — open, using extern port %1")
                               .arg(incomingPort));
                } else {
                    logKad(QStringLiteral("Kad: UDP FW check succeeded — not firewalled"));
                }
            }
            return;
        }

        if (s_fwChecksFinished >= kUDPFWCheckClientsNeeded) {
            s_testStart = 0;
            s_firewalledUDP = true;
            s_isFWVerifiedUDP = true;
            s_timedOut = false;
            s_firewalledLastStateUDP = s_firewalledUDP;
            s_possibleTestClients.clear();
            SearchManager::cancelNodeFWCheckUDPSearch();
            logKad(QStringLiteral("Kad: UDP FW check complete — firewalled"));
            return;
        }
        logKad(QStringLiteral("Kad: UDP FW check from %1 — firewalled, continue testing (%2/%3)")
                   .arg(ipToString(fromIP)).arg(s_fwChecksFinished).arg(kUDPFWCheckClientsNeeded));
    } else {
        logKad(QStringLiteral("Kad: UDP FW check from %1 cancelled").arg(ipToString(fromIP)));
    }

    queryNextClient();
}

void UDPFirewallTester::reCheckFirewallUDP(bool setUnverified)
{
    logKad(QStringLiteral("Kad: UDP FW recheck requested (setUnverified=%1)")
               .arg(QLatin1StringView(setUnverified ? "yes" : "no")));
    s_fwChecksRunning = 0;
    s_fwChecksFinished = 0;
    s_lastSucceededTime = 0;
    s_testStart = static_cast<uint32>(time(nullptr));
    s_timedOut = false;
    s_firewalledLastStateUDP = s_firewalledUDP;
    s_isFWVerifiedUDP = s_isFWVerifiedUDP && !setUnverified;
    s_possibleTestClients.clear();
    s_usedTestClients.clear();

    SearchManager::findNodeFWCheckUDP();
    s_nodeSearchStarted = true;

    // Re-learn the external Kad port alongside the firewall state.
    if (auto* prefs = Kademlia::getInstancePrefs())
        (void)prefs->findExternKadPort(true);
}

bool UDPFirewallTester::isFWCheckUDPRunning()
{
    // MFC UDPFirewallTester.cpp:193-196. Deliberately not gated on
    // s_fwChecksRunning: the check is "running" from the moment it starts until
    // enough clients have reported, including while we are between clients.
    return s_fwChecksFinished < kUDPFWCheckClientsNeeded
           && !Kademlia::shouldSkipFirewallChecks();
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
                                                const KadUDPKey& udpKey, bool ipVerified,
                                                uint8 connectOptions,
                                                const UInt128& clientHash)
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
    contact.setConnectOptions(connectOptions);
    contact.setClientHash(clientHash);
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

    // Drop any in-flight lookup too, so a stop/start cycle can't leave a stale
    // NodeFwCheckUDP search behind. MFC UDPFirewallTester.cpp:197-212.
    SearchManager::cancelNodeFWCheckUDPSearch();
}

void UDPFirewallTester::connected()
{
    // MFC :183-190 gates on IsFWCheckUDPRunning(), not GetUDPCheckClientsNeeded().
    if (!s_nodeSearchStarted && isFWCheckUDPRunning()) {
        s_nodeSearchStarted = true;
        s_testStart = static_cast<uint32>(time(nullptr));
        s_timedOut = false;
        logKad(QStringLiteral("Kad: UDP FW tester connected — starting node search for test clients"));
        SearchManager::findNodeFWCheckUDP();
    }
}

void UDPFirewallTester::debugSetTestStart(uint32 startTime)
{
    s_testStart = startTime;
}

void UDPFirewallTester::debugAddUsedTestClient(uint32 ip, uint16 udpPort)
{
    UsedClient used;
    used.contact = Contact(UInt128(uint32{0}), ip, udpPort, 0, UInt128(uint32{0}),
                           KADEMLIA_VERSION, KadUDPKey(), false);
    used.answered = false;
    s_usedTestClients.push_back(std::move(used));
    ++s_fwChecksRunning;
}

uint8 UDPFirewallTester::debugChecksFinished()
{
    return s_fwChecksFinished;
}

uint8 UDPFirewallTester::debugChecksRunning()
{
    return s_fwChecksRunning;
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
        if (routingZone && routingZone->getContact(testContact.address().toUint32(), 0, false)) {
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
