/// @file ClientList.cpp
/// @brief Client list manager implementation — Phase 1.
///
/// Ported from MFC CClientList (srchybrid/ClientList.cpp).

#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "utils/OtherFunctions.h"
#include "utils/TimeUtils.h"
#include "utils/Opcodes.h"

#include <algorithm>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>  // ntohl
#endif

namespace eMule {

// ===========================================================================
// Construction / Destruction
// ===========================================================================

ClientList::ClientList(QObject* parent)
    : QObject(parent)
    , m_lastBanCleanUp(static_cast<uint32>(getTickCount()))
{
    globalDeadSourceList.init(true);
}

ClientList::~ClientList() = default;

// ===========================================================================
// Client management
// ===========================================================================

void ClientList::addClient(UpDownClient* client, bool skipDupTest)
{
    if (!client)
        return;

    if (!skipDupTest) {
        // Check for duplicate pointer (matches MFC list.Find())
        if (std::find(m_clients.begin(), m_clients.end(), client) != m_clients.end())
            return;
    }

    m_clients.push_back(client);
    emit clientAdded(client);
}

void ClientList::removeClient(UpDownClient* client, const QString& reason)
{
    Q_UNUSED(reason);
    if (!client)
        return;

    auto it = std::find(m_clients.begin(), m_clients.end(), client);
    if (it != m_clients.end()) {
        m_clients.erase(it);
        emit clientRemoved(client);
    }
}

bool ClientList::isValidClient(const UpDownClient* client) const
{
    return std::find(m_clients.begin(), m_clients.end(), client) != m_clients.end();
}

int ClientList::clientCount() const
{
    return static_cast<int>(m_clients.size());
}

void ClientList::deleteAll()
{
    m_clients.clear();
}

// ===========================================================================
// Find operations (linear scan, matching MFC)
// ===========================================================================

UpDownClient* ClientList::findByIP(uint32 ip) const
{
    for (auto* c : m_clients) {
        if (c->userIP() == ip)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByIP(uint32 ip, uint16 port) const
{
    for (auto* c : m_clients) {
        if (c->userIP() == ip && c->userPort() == port)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByConnIP(uint32 ip, uint16 port) const
{
    for (auto* c : m_clients) {
        if (c->connectIP() == ip && c->userPort() == port)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByUserHash(const uint8* hash, uint32 ip, uint16 port) const
{
    // Two-pass: prefer exact match (hash+IP+port), fallback to hash-only
    UpDownClient* hashOnlyMatch = nullptr;

    for (auto* c : m_clients) {
        if (md4equ(c->userHash(), hash)) {
            if (ip != 0 && port != 0
                && c->userIP() == ip && c->userPort() == port)
            {
                return c;  // exact match
            }
            if (!hashOnlyMatch)
                hashOnlyMatch = c;
        }
    }

    return hashOnlyMatch;
}

UpDownClient* ClientList::findByIP_UDP(uint32 ip, uint16 udpPort) const
{
    for (auto* c : m_clients) {
        if (c->userIP() == ip && c->udpPort() == udpPort)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByServerID(uint32 serverIP, uint32 ed2kUserID) const
{
    // Convert ED2K user ID to hybrid format (matches MFC ntohl conversion)
    const uint32 hybridID = ntohl(ed2kUserID);

    for (auto* c : m_clients) {
        if (c->serverIP() == serverIP && c->userIDHybrid() == hybridID)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByUserID_KadPort(uint32 clientID, uint16 kadPort) const
{
    for (auto* c : m_clients) {
        if (c->userIDHybrid() == clientID && c->kadPort() == kadPort)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByIP_KadPort(uint32 ip, uint16 kadPort) const
{
    for (auto* c : m_clients) {
        if (c->userIP() == ip && c->kadPort() == kadPort)
            return c;
    }
    return nullptr;
}

// ===========================================================================
// Buddy management (Kademlia)
// ===========================================================================

void ClientList::setBuddy(UpDownClient* buddy, BuddyStatus status)
{
    m_buddy = buddy;
    m_buddyStatus = status;
}

bool ClientList::incomingBuddy(uint32 buddyIP, uint16 buddyPort, const uint8* buddyID)
{
    Q_UNUSED(buddyID);

    // Accept if we don't already have a buddy
    if (m_buddyStatus == BuddyStatus::Connected && m_buddy)
        return false;

    // For now, just log the incoming buddy request.
    // Full implementation requires creating an UpDownClient from the IP/port
    // and establishing a TCP connection for the buddy relay.
    m_buddyStatus = BuddyStatus::Connecting;
    return true;
}

void ClientList::requestBuddy(uint32 ip, uint16 port, uint8 connectOptions)
{
    Q_UNUSED(connectOptions);

    if (m_buddyStatus == BuddyStatus::Connected)
        return;

    // Mark as connecting. Full implementation requires creating an UpDownClient
    // and initiating a TCP connection for the buddy relay.
    m_buddyStatus = BuddyStatus::Connecting;
}

// ===========================================================================
// Banned clients
// ===========================================================================

void ClientList::addBannedClient(uint32 ip)
{
    m_bannedList[ip] = static_cast<uint32>(getTickCount());
}

bool ClientList::isBannedClient(uint32 ip) const
{
    auto it = m_bannedList.find(ip);
    if (it == m_bannedList.end())
        return false;
    return (static_cast<uint32>(getTickCount()) < it->second + CLIENTBANTIME);
}

void ClientList::removeBannedClient(uint32 ip)
{
    m_bannedList.erase(ip);
}

int ClientList::bannedCount() const
{
    return static_cast<int>(m_bannedList.size());
}

void ClientList::removeAllBannedClients()
{
    m_bannedList.clear();
}

// ===========================================================================
// Private helpers
// ===========================================================================

void ClientList::cleanUpBannedList()
{
    const auto now = static_cast<uint32>(getTickCount());
    if (now - m_lastBanCleanUp < CLIENTBANTIME)
        return;

    m_lastBanCleanUp = now;
    std::erase_if(m_bannedList, [now](const auto& pair) {
        return now >= pair.second + CLIENTBANTIME;
    });
}

} // namespace eMule
