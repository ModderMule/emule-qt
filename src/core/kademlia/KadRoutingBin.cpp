/// @file KadRoutingBin.cpp
/// @brief Kademlia K-bucket implementation.

#include "kademlia/KadRoutingBin.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <random>

namespace eMule::kad {

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

std::unordered_map<uint32, uint32> RoutingBin::s_globalContactIPs;
std::unordered_map<uint32, uint32> RoutingBin::s_globalContactSubnets;

inline constexpr uint32 kMaxContactsSubnet = 10;
inline constexpr uint32 kMaxContactsIP     = 1;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

RoutingBin::RoutingBin() = default;

RoutingBin::~RoutingBin()
{
    for (auto* contact : m_entries) {
        adjustGlobalTracking(contact->getIPAddress(), false);
        if (!m_dontDeleteContacts)
            delete contact;
    }
    m_entries.clear();
}

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

bool RoutingBin::addContact(Contact* contact)
{
    Q_ASSERT(contact != nullptr);
    const uint32 ip = contact->getIPAddress();
    uint32 sameSubnets = 0;

    for (const auto* c : m_entries) {
        if (contact->getClientID() == c->getClientID())
            return false;
        sameSubnets += static_cast<uint32>(((ip ^ c->getIPAddress()) & ~0xFFu) == 0);
    }

    if (!checkGlobalIPLimits(ip, contact->getUDPPort(), true))
        return false;

    // No more than 2 IPs from the same /24 in one bin (unless LAN)
    if (sameSubnets >= 2 && !isLanIP(contact->getNetIP())) {
        logDebug(QStringLiteral("Ignored kad contact (IP=%1:%2) - too many contacts with the same subnet in RoutingBin")
                     .arg(ipstr(contact->getNetIP()))
                     .arg(contact->getUDPPort()));
        return false;
    }

    if (m_entries.size() < kK) {
        m_entries.push_back(contact);
        adjustGlobalTracking(ip, true);
        return true;
    }
    return false;
}

void RoutingBin::setAlive(Contact* contact)
{
    Q_ASSERT(contact != nullptr);
    Contact* found = getContact(contact->getClientID());
    Q_ASSERT(contact == found);
    if (found) {
        found->updateType();
        pushToBottom(found);
    }
}

void RoutingBin::setTCPPort(uint32 ip, uint16 udpPort, uint16 tcpPort)
{
    for (auto* contact : m_entries) {
        if (ip == contact->getIPAddress() && udpPort == contact->getUDPPort()) {
            contact->setTCPPort(tcpPort);
            contact->updateType();
            pushToBottom(contact);
            break;
        }
    }
}

void RoutingBin::removeContact(Contact* contact, bool noTrackingAdjust)
{
    if (!noTrackingAdjust)
        adjustGlobalTracking(contact->getIPAddress(), false);
    m_entries.remove(contact);
}

Contact* RoutingBin::getContact(const UInt128& id)
{
    for (auto* contact : m_entries)
        if (id == contact->getClientID())
            return contact;
    return nullptr;
}

Contact* RoutingBin::getContact(uint32 ip, uint16 port, bool tcpPort)
{
    for (auto* contact : m_entries) {
        if (ip == contact->getIPAddress()
            && ((!tcpPort && port == contact->getUDPPort())
                || (tcpPort && port == contact->getTCPPort())
                || port == 0))
        {
            return contact;
        }
    }
    return nullptr;
}

Contact* RoutingBin::getOldest()
{
    return m_entries.empty() ? nullptr : m_entries.front();
}

Contact* RoutingBin::getRandomContact(uint32 maxType, uint32 minKadVersion)
{
    if (m_entries.empty())
        return nullptr;

    auto& rng = randomEngine();
    Contact* lastFit = nullptr;
    int randomStartPos = static_cast<int>(std::uniform_int_distribution<std::size_t>(0, m_entries.size() - 1)(rng));

    for (auto* contact : m_entries) {
        if (contact->getType() <= maxType && contact->getVersion() >= minKadVersion) {
            if (randomStartPos <= 0)
                return contact;
            lastFit = contact;
        }
        --randomStartPos;
    }
    return lastFit;
}

uint32 RoutingBin::getSize() const
{
    return static_cast<uint32>(m_entries.size());
}

void RoutingBin::getNumContacts(uint32& inOutContacts, uint32& inOutFilteredContacts, uint8 minVersion) const
{
    for (const auto* contact : m_entries) {
        if (contact->getVersion() >= minVersion)
            ++inOutContacts;
        else
            ++inOutFilteredContacts;
    }
}

uint32 RoutingBin::getRemaining() const
{
    return static_cast<uint32>(kK - m_entries.size());
}

void RoutingBin::getEntries(ContactArray& result, bool emptyFirst)
{
    if (emptyFirst)
        result.assign(m_entries.begin(), m_entries.end());
    else
        result.insert(result.end(), m_entries.begin(), m_entries.end());
}

void RoutingBin::getClosestTo(uint32 maxType, const UInt128& target, uint32 maxRequired,
                              ContactMap& result, bool emptyFirst, bool setInUse)
{
    if (emptyFirst)
        result.clear();

    if (m_entries.empty() || maxRequired == 0)
        return;

    for (auto* contact : m_entries) {
        if (contact->getType() <= maxType && contact->isIpVerified()) {
            UInt128 targetDistance(contact->getClientID());
            targetDistance.xorWith(target);
            result[targetDistance] = contact;
            if (setInUse)
                contact->incUse();
        }
    }

    // Trim excess results (remove the furthest)
    while (result.size() > maxRequired) {
        auto it = std::prev(result.end());
        if (setInUse)
            it->second->decUse();
        result.erase(it);
    }
}

bool RoutingBin::changeContactIPAddress(Contact* contact, uint32 newIP)
{
    if (contact->getIPAddress() == newIP)
        return true;

    Q_ASSERT(getContact(contact->getClientID()) == contact);

    // No more than 1 KadID per IP (global)
    auto itIP = s_globalContactIPs.find(newIP);
    uint32 sameIPCount = (itIP != s_globalContactIPs.end()) ? itIP->second : 0;
    if (sameIPCount >= kMaxContactsIP) {
        logDebug(QStringLiteral("Rejected kad contact ip change on update (old IP=%1, requested IP=%2) - too many contacts with the same IP (global)")
                     .arg(ipstr(contact->getNetIP()), ipstr(htonl(newIP))));
        return false;
    }

    if ((newIP ^ contact->getIPAddress()) & ~0xFFu) {
        // Different subnet — check global subnet limit
        auto itSubnet = s_globalContactSubnets.find(newIP & ~0xFFu);
        uint32 sameSubnetGlobal = (itSubnet != s_globalContactSubnets.end()) ? itSubnet->second : 0;
        if (sameSubnetGlobal >= kMaxContactsSubnet && !isLanIP(ntohl(newIP))) {
            logDebug(QStringLiteral("Rejected kad contact ip change on update (old IP=%1, requested IP=%2) - too many contacts with the same Subnet (global)")
                         .arg(ipstr(contact->getNetIP()), ipstr(htonl(newIP))));
            return false;
        }

        uint32 sameSubnet = 0;
        for (const auto* c : m_entries)
            sameSubnet += static_cast<uint32>(((newIP ^ c->getIPAddress()) & ~0xFFu) == 0);

        if (sameSubnet >= 2 && !isLanIP(ntohl(newIP))) {
            logDebug(QStringLiteral("Rejected kad contact ip change on update (old IP=%1, requested IP=%2) - too many contacts with the same Subnet (local)")
                         .arg(ipstr(contact->getNetIP()), ipstr(htonl(newIP))));
            return false;
        }
    }

    adjustGlobalTracking(contact->getIPAddress(), false);
    contact->setIPAddress(newIP);
    adjustGlobalTracking(contact->getIPAddress(), true);
    return true;
}

void RoutingBin::pushToBottom(Contact* contact)
{
    Q_ASSERT(getContact(contact->getClientID()) == contact);
    removeContact(contact, true);
    m_entries.push_back(contact);
}

void RoutingBin::setAllContactsVerified()
{
    for (auto* contact : m_entries)
        contact->setIpVerified(true);
}

bool RoutingBin::checkGlobalIPLimits(uint32 ip, uint16 port, bool log)
{
    auto itIP = s_globalContactIPs.find(ip);
    uint32 sameIPCount = (itIP != s_globalContactIPs.end()) ? itIP->second : 0;
    if (sameIPCount >= kMaxContactsIP) {
        if (log)
            logDebug(QStringLiteral("Ignored kad contact (IP=%1:%2) - too many contacts with the same IP (global)")
                         .arg(ipstr(htonl(ip)))
                         .arg(port));
        return false;
    }

    auto itSubnet = s_globalContactSubnets.find(ip & ~0xFFu);
    uint32 sameSubnetCount = (itSubnet != s_globalContactSubnets.end()) ? itSubnet->second : 0;
    if (sameSubnetCount >= kMaxContactsSubnet && !isLanIP(ntohl(ip))) {
        if (log)
            logDebug(QStringLiteral("Ignored kad contact (IP=%1:%2) - too many contacts with the same Subnet (global)")
                         .arg(ipstr(htonl(ip)))
                         .arg(port));
        return false;
    }
    return true;
}

bool RoutingBin::hasOnlyLANNodes() const
{
    for (const auto* contact : m_entries)
        if (!isLanIP(contact->getNetIP()))
            return false;
    return true;
}

void RoutingBin::resetGlobalTracking()
{
    s_globalContactIPs.clear();
    s_globalContactSubnets.clear();
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

void RoutingBin::adjustGlobalTracking(uint32 ip, bool increase)
{
    // -- IP tracking --
    auto itIP = s_globalContactIPs.find(ip);
    uint32 sameIPCount = (itIP != s_globalContactIPs.end()) ? itIP->second : 0;

    if (increase) {
        if (sameIPCount >= kMaxContactsIP) {
            Q_ASSERT(false);
            logWarning(QStringLiteral("RoutingBin Global IP Tracking inconsistency on increase (%1)")
                           .arg(ipstr(htonl(ip))));
        }
        ++sameIPCount;
    } else if (sameIPCount == 0) {
        Q_ASSERT(false);
        logWarning(QStringLiteral("RoutingBin Global IP Tracking inconsistency on decrease (%1)")
                       .arg(ipstr(htonl(ip))));
    } else {
        --sameIPCount;
    }

    if (sameIPCount != 0)
        s_globalContactIPs[ip] = sameIPCount;
    else
        s_globalContactIPs.erase(ip);

    // -- Subnet tracking --
    uint32 subnet = ip & ~0xFFu;
    auto itSubnet = s_globalContactSubnets.find(subnet);
    uint32 sameSubnetCount = (itSubnet != s_globalContactSubnets.end()) ? itSubnet->second : 0;

    if (increase) {
        if (sameSubnetCount >= kMaxContactsSubnet && !isLanIP(ntohl(ip))) {
            Q_ASSERT(false);
            logWarning(QStringLiteral("RoutingBin Global Subnet Tracking inconsistency on increase (%1)")
                           .arg(ipstr(htonl(ip))));
        }
        ++sameSubnetCount;
    } else if (sameSubnetCount == 0) {
        Q_ASSERT(false);
        logWarning(QStringLiteral("RoutingBin Global IP Subnet inconsistency on decrease (%1)")
                       .arg(ipstr(htonl(ip))));
    } else {
        --sameSubnetCount;
    }

    if (sameSubnetCount != 0)
        s_globalContactSubnets[subnet] = sameSubnetCount;
    else
        s_globalContactSubnets.erase(subnet);
}

} // namespace eMule::kad
