#include "pch.h"
/// @file KadRoutingBin.cpp
/// @brief Kademlia K-bucket implementation.

#include "kademlia/KadRoutingBin.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadLog.h"



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
        adjustGlobalTracking(contact->address().toUint32(), false);
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
    const uint32 ip = contact->address().toUint32();
    uint32 sameSubnets = 0;

    for (const auto* c : m_entries) {
        if (contact->getClientID() == c->getClientID())
            return false;
        sameSubnets += static_cast<uint32>(((ip ^ c->address().toUint32()) & ~0xFFu) == 0);
    }

    if (!checkGlobalIPLimits(ip, contact->getUDPPort(), true))
        return false;

    // No more than 2 IPs from the same /24 in one bin (unless LAN)
    if (sameSubnets >= 2 && !contact->address().isLan()) {
        logKad(QStringLiteral("Ignored kad contact (IP=%1:%2) - too many contacts with the same subnet in RoutingBin")
                   .arg(contact->address().toString())
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
        if (ip == contact->address().toUint32() && udpPort == contact->getUDPPort()) {
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
        adjustGlobalTracking(contact->address().toUint32(), false);
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
        if (ip == contact->address().toUint32()
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
        // Only hand out contacts whose IP we have actually verified. This list
        // feeds both our own searches and the KADEMLIA2_RES answers we serve to
        // remote peers, so without the gate we propagate spoofable contacts into
        // the network — something official eMule never does (MFC RoutingBin.cpp
        // GetClosestTo). Bootstrap still works because RoutingZone::readFile
        // bulk-verifies a nodes.dat that declared no verified contacts, and
        // process_KADEMLIA2_BOOTSTRAP_RES assumes verified when the table is empty.
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
    if (contact->address().toUint32() == newIP)
        return true;

    Q_ASSERT(getContact(contact->getClientID()) == contact);

    const auto newAddr = Address::fromHostOrder(newIP);

    // No more than 1 KadID per IP (global)
    auto itIP = s_globalContactIPs.find(newIP);
    uint32 sameIPCount = (itIP != s_globalContactIPs.end()) ? itIP->second : 0;
    if (sameIPCount >= kMaxContactsIP) {
        logKad(QStringLiteral("Rejected kad contact ip change on update (old IP=%1, requested IP=%2) - too many contacts with the same IP (global)")
                   .arg(contact->address().toString(), newAddr.toString()));
        return false;
    }

    if ((newIP ^ contact->address().toUint32()) & ~0xFFu) {
        // Different subnet — check global subnet limit
        auto itSubnet = s_globalContactSubnets.find(newIP & ~0xFFu);
        uint32 sameSubnetGlobal = (itSubnet != s_globalContactSubnets.end()) ? itSubnet->second : 0;
        if (sameSubnetGlobal >= kMaxContactsSubnet && !newAddr.isLan()) {
            logKad(QStringLiteral("Rejected kad contact ip change on update (old IP=%1, requested IP=%2) - too many contacts with the same Subnet (global)")
                       .arg(contact->address().toString(), newAddr.toString()));
            return false;
        }

        uint32 sameSubnet = 0;
        for (const auto* c : m_entries)
            sameSubnet += static_cast<uint32>(((newIP ^ c->address().toUint32()) & ~0xFFu) == 0);

        if (sameSubnet >= 2 && !newAddr.isLan()) {
            logKad(QStringLiteral("Rejected kad contact ip change on update (old IP=%1, requested IP=%2) - too many contacts with the same Subnet (local)")
                       .arg(contact->address().toString(), newAddr.toString()));
            return false;
        }
    }

    adjustGlobalTracking(contact->address().toUint32(), false);
    contact->setAddress(newAddr);
    adjustGlobalTracking(contact->address().toUint32(), true);
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
    const auto addr = Address::fromHostOrder(ip);

    auto itIP = s_globalContactIPs.find(ip);
    uint32 sameIPCount = (itIP != s_globalContactIPs.end()) ? itIP->second : 0;
    if (sameIPCount >= kMaxContactsIP) {
        if (log)
            logKad(QStringLiteral("Ignored kad contact (IP=%1:%2) - too many contacts with the same IP (global)")
                       .arg(addr.toString())
                       .arg(port));
        return false;
    }

    auto itSubnet = s_globalContactSubnets.find(ip & ~0xFFu);
    uint32 sameSubnetCount = (itSubnet != s_globalContactSubnets.end()) ? itSubnet->second : 0;
    if (sameSubnetCount >= kMaxContactsSubnet && !addr.isLan()) {
        if (log)
            logKad(QStringLiteral("Ignored kad contact (IP=%1:%2) - too many contacts with the same Subnet (global)")
                       .arg(addr.toString())
                       .arg(port));
        return false;
    }
    return true;
}

bool RoutingBin::hasOnlyLANNodes() const
{
    for (const auto* contact : m_entries)
        if (!contact->address().isLan())
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
    const auto addr = Address::fromHostOrder(ip);

    // -- IP tracking --
    auto itIP = s_globalContactIPs.find(ip);
    uint32 sameIPCount = (itIP != s_globalContactIPs.end()) ? itIP->second : 0;

    if (increase) {
        if (sameIPCount >= kMaxContactsIP) {
            Q_ASSERT(false);
            logKad(QStringLiteral("RoutingBin Global IP Tracking inconsistency on increase (%1)")
                       .arg(addr.toString()));
        }
        ++sameIPCount;
    } else if (sameIPCount == 0) {
        Q_ASSERT(false);
        logKad(QStringLiteral("RoutingBin Global IP Tracking inconsistency on decrease (%1)")
                   .arg(addr.toString()));
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
        if (sameSubnetCount >= kMaxContactsSubnet && !addr.isLan()) {
            Q_ASSERT(false);
            logKad(QStringLiteral("RoutingBin Global Subnet Tracking inconsistency on increase (%1)")
                       .arg(addr.toString()));
        }
        ++sameSubnetCount;
    } else if (sameSubnetCount == 0) {
        Q_ASSERT(false);
        logKad(QStringLiteral("RoutingBin Global IP Subnet inconsistency on decrease (%1)")
                   .arg(addr.toString()));
    } else {
        --sameSubnetCount;
    }

    if (sameSubnetCount != 0)
        s_globalContactSubnets[subnet] = sameSubnetCount;
    else
        s_globalContactSubnets.erase(subnet);
}

} // namespace eMule::kad
