#include "pch.h"
/// @file ClientList.cpp
/// @brief Client list manager implementation — Phase 1.
///
/// Ported from MFC CClientList (srchybrid/ClientList.cpp).

#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "app/AppContext.h"
#include "utils/OtherFunctions.h"
#include "net/ClientReqSocket.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadPrefs.h"
#include "transfer/UploadQueue.h"
#include "utils/TimeUtils.h"



namespace eMule {

// ===========================================================================
// Construction / Destruction
// ===========================================================================

ClientList::ClientList(QObject* parent)
    : EntityList<UpDownClient>(parent)
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
    // EntityList::addEntity: null-check -> pointer dup-check -> append ->
    // onEntityAdded() (emits clientAdded). Matches MFC list.Find() semantics.
    addEntity(client, skipDupTest);
}

void ClientList::removeClient(UpDownClient* client, const QString& reason)
{
    Q_UNUSED(reason);
    // EntityList::removeEntity: find -> erase -> onEntityRemoved() (emits clientRemoved).
    removeEntity(client);
}

bool ClientList::isValidClient(const UpDownClient* client) const
{
    return contains(client);
}

int ClientList::clientCount() const
{
    return count();
}

void ClientList::deleteAll()
{
    m_items.clear();
}

// ===========================================================================
// Incoming connection handling
// ===========================================================================

void ClientList::handleIncomingConnection(ClientReqSocket* socket)
{
    auto* client = new UpDownClient(this);
    client->wireIncomingSocket(socket);
    addClient(client);
}

// ===========================================================================
// Find operations (linear scan, matching MFC)
// ===========================================================================

UpDownClient* ClientList::findByIP(uint32 ip) const
{
    for (auto* c : m_items) {
        if (c->userAddress().toNetworkUint32() == ip)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByIP(uint32 ip, uint16 port) const
{
    for (auto* c : m_items) {
        if (c->userAddress().toNetworkUint32() == ip && c->userPort() == port)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByConnIP(uint32 ip, uint16 port) const
{
    for (auto* c : m_items) {
        if (c->connectAddress().toNetworkUint32() == ip && c->userPort() == port)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByUserHash(const uint8* hash, uint32 ip, uint16 port) const
{
    // Two-pass: prefer exact match (hash+IP+port), fallback to hash-only
    UpDownClient* hashOnlyMatch = nullptr;

    for (auto* c : m_items) {
        if (md4equ(c->userHash(), hash)) {
            if (ip != 0 && port != 0
                && c->userAddress().toNetworkUint32() == ip && c->userPort() == port)
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
    for (auto* c : m_items) {
        if (c->userAddress().toNetworkUint32() == ip && c->udpPort() == udpPort)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByServerID(uint32 serverIP, uint32 ed2kUserID) const
{
    // Convert ED2K user ID to hybrid format (matches MFC ntohl conversion)
    const uint32 hybridID = ntohl(ed2kUserID);

    for (auto* c : m_items) {
        if (c->serverAddress().toNetworkUint32() == serverIP && c->userIDHybrid() == hybridID)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByUserID_KadPort(uint32 clientID, uint16 kadPort) const
{
    for (auto* c : m_items) {
        if (c->userIDHybrid() == clientID && c->kadPort() == kadPort)
            return c;
    }
    return nullptr;
}

UpDownClient* ClientList::findByIP_KadPort(uint32 ip, uint16 kadPort) const
{
    for (auto* c : m_items) {
        if (c->userAddress().toNetworkUint32() == ip && c->kadPort() == kadPort)
            return c;
    }
    return nullptr;
}

// ===========================================================================
// Buddy management (Kademlia)
// ===========================================================================

void ClientList::setBuddy(UpDownClient* buddy, BuddyStatus status)
{
    // Losing our buddy while firewalled — instantly re-trigger buddy search.
    // Matches MFC ClientList.cpp:578-585.
    if (status == BuddyStatus::None && (m_buddyStatus != BuddyStatus::None || m_buddy)) {
        if (auto* kadInst = kad::Kademlia::instance()) {
            if (kadInst->isRunning() && kadInst->isFirewalled()
                && kad::UDPFirewallTester::isFirewalledUDP(true))
            {
                if (auto* prefs = kad::Kademlia::getInstancePrefs())
                    prefs->setFindBuddy(true);
            }
        }
    }

    m_buddy = buddy;
    m_buddyStatus = status;
}

bool ClientList::incomingBuddy(uint32 ip, uint16 tcpPort, uint16 udpPort,
                               const uint8* clientID, const uint8* buddyID)
{
    // Already have a connected buddy — reject.
    // Matches MFC ClientList.cpp:721-747.
    if (m_buddyStatus == BuddyStatus::Connected && m_buddy)
        return false;

    // Check if we already know this client
    if (findByConnIP(ip, tcpPort))
        return false;

    // Create a new client for the incoming buddy
    auto* client = new UpDownClient(tcpPort, 0, 0, 0, nullptr);
    client->setConnectAddress(Address::fromHostOrder(ip));
    client->setKadPort(udpPort);
    client->setUserHash(clientID);
    client->setKadState(KadState::IncomingBuddy);
    client->setBuddyID(buddyID);

    addClient(client);

    m_buddyStatus = BuddyStatus::Connecting;
    return true;
}

void ClientList::requestBuddy(uint32 ip, uint16 tcpPort, uint16 udpPort,
                               const uint8* clientID, uint8 connectOptions)
{
    // Already have a connected buddy — skip.
    // Matches MFC ClientList.cpp:694-719.
    if (m_buddyStatus == BuddyStatus::Connected)
        return;

    // Find existing client by IP+port, or create a new one
    auto* client = findByConnIP(ip, tcpPort);
    if (!client) {
        client = new UpDownClient(tcpPort, 0, 0, 0, nullptr);
        client->setConnectAddress(Address::fromHostOrder(ip));
        addClient(client);
    }

    client->setKadPort(udpPort);
    client->setUserHash(clientID);
    client->setKadState(KadState::QueuedBuddy);
    client->setConnectOptions(connectOptions, true, false);
    client->tryToConnect();

    m_buddyStatus = BuddyStatus::Connecting;
}

// ===========================================================================
// Kademlia UDP firewall check — MFC ClientList.cpp:767-784
// ===========================================================================

bool ClientList::doRequestFirewallCheckUDP(const kad::Contact& contact)
{
    // Skip if we already know this IP — the result would be biased
    if (findByIP(contact.address().toNetworkUint32()))
        return false;

    // Create a temporary client for the TCP connection
    auto* client = new UpDownClient(contact.getTCPPort(), 0, 0, 0, nullptr);
    client->setConnectAddress(contact.address());
    client->setKadVersion(contact.getVersion());
    client->setKadPort(contact.getUDPPort());
    client->setKadState(KadState::QueuedFwCheckUDP);

    // Propagate crypto info from the Kad contact so the TCP connection can be encrypted.
    // NOTE: The client hash (ED2K user hash) is usually unavailable here because FW check
    // contacts come from KADEMLIA2_RES responses which only carry basic info (KadID, IP,
    // ports, version) — no tags, no FT_USER_COUNT hash.  The hash is only exchanged in
    // HELLO_REQ/RES packets, but FW check contacts are deliberately never HELLO'd (they
    // must remain un-contacted for the test to be valid).
    // MFC has the same limitation (ClientList.cpp:773-776) and never sets the hash
    // here at all.  We improve on MFC by using the hash when it happens to be available,
    // but in practice most UDP FW check TCP connections will be unencrypted.
    client->setConnectOptions(contact.connectOptions(), true, false);
    uint8 hashBytes[16];
    contact.clientHash().toByteArray(hashBytes);
    if (!isnulmd4(hashBytes))
        client->setUserHash(hashBytes);

    addClient(client);
    client->tryToConnect();
    return true;
}

void ClientList::addKadFirewallRequest(uint32 ipNet)
{
    const uint32 now = static_cast<uint32>(getTickCount());
    m_kadFirewallRequests.push_front({ipNet, now});
    // Drop entries older than the 180 s window (oldest live at the back).
    while (!m_kadFirewallRequests.empty()
           && now >= m_kadFirewallRequests.back().second + SEC2MS(180))
        m_kadFirewallRequests.pop_back();
}

bool ClientList::isKadFirewallCheckIP(uint32 ipNet) const
{
    const uint32 now = static_cast<uint32>(getTickCount());
    // Newest first: once we reach an expired entry, all older ones are expired too.
    for (const auto& [ip, inserted] : m_kadFirewallRequests) {
        if (now >= inserted + SEC2MS(180))
            break;
        if (ip == ipNet)
            return true;
    }
    return false;
}

// ===========================================================================
// Iteration
// ===========================================================================

void ClientList::forEachClient(const std::function<void(UpDownClient*)>& callback) const
{
    for (auto* client : m_items)
        callback(client);
}

// ===========================================================================
// Periodic cleanup — matches MFC CClientList::Process()
// ===========================================================================

void ClientList::process()
{
    cleanUpBannedList();
    processConnectingClients();

    // Remove clients that serve no purpose — matches MFC CClientList::Process()
    for (auto it = m_items.begin(); it != m_items.end(); ) {
        auto* client = *it;

        // Keep clients that are still useful
        if (client->socket()
            || client->downloadState() != DownloadState::None
            || client->uploadState() != UploadState::None
            || client->reqFile() != nullptr
            || client->kadState() != KadState::None
            || client == m_buddy
            || client->friendPtr() != nullptr
            || (client->chatState() != ChatState::None
                && client->chatState() != ChatState::UnableToConnect))
        {
            ++it;
            continue;
        }

        // Also keep if on upload waiting queue
        if (theApp.uploadQueue && theApp.uploadQueue->isOnUploadQueue(client)) {
            ++it;
            continue;
        }

        it = m_items.erase(it);
        emit clientRemoved(client);
        client->deleteLater();
    }
}

// ===========================================================================
// Connecting client timeout — MFC CClientList (srchybrid/ClientList.cpp:865-901)
// ===========================================================================

void ClientList::addConnectingClient(UpDownClient* client)
{
    // Don't add duplicates
    for (const auto& cc : m_connectingClients) {
        if (cc.client == client)
            return;
    }
    m_connectingClients.push_back({client, static_cast<uint32>(getTickCount())});
}

void ClientList::removeConnectingClient(const UpDownClient* client)
{
    std::erase_if(m_connectingClients, [client](const ConnectingClient& cc) {
        return cc.client == client;
    });
}

void ClientList::processConnectingClients()
{
    // Time out clients that have been connecting for > 45 seconds.
    // Matches MFC ProcessConnectingClientsList() (srchybrid/ClientList.cpp:877-891).
    const uint32 curTick = static_cast<uint32>(getTickCount());
    for (auto it = m_connectingClients.begin(); it != m_connectingClients.end(); ) {
        if (curTick >= it->insertedTick + 45000u) {
            auto* client = it->client;
            it = m_connectingClients.erase(it);
            if (isValidClient(client))
                client->disconnected(QStringLiteral("Connection try timeout"));
        } else {
            ++it;
        }
    }
}

// ===========================================================================
// Banned clients
// ===========================================================================

void ClientList::addBannedClient(const Address& addr)
{
    if (addr.isNull()) return;
    m_bannedList[addr] = static_cast<uint32>(getTickCount());
}

bool ClientList::isBannedClient(const Address& addr) const
{
    if (addr.isNull()) return false;
    auto it = m_bannedList.find(addr);
    if (it == m_bannedList.end())
        return false;
    return (static_cast<uint32>(getTickCount()) < it->second + CLIENTBANTIME);
}

void ClientList::removeBannedClient(const Address& addr)
{
    m_bannedList.erase(addr);
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
