#include "pch.h"
/// @file ClientList.cpp
/// @brief Client list manager implementation — Phase 1.
///
/// Ported from MFC CClientList (srchybrid/ClientList.cpp).

#include "client/ClientList.h"
#include "client/ClientCredits.h"
#include "client/UpDownClient.h"
#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "net/ClientReqSocket.h"
#include "net/ListenSocket.h"
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

UpDownClient* ClientList::attachToAlreadyKnown(UpDownClient* newClient, ClientReqSocket* sender)
{
    if (!newClient)
        return nullptr;

    // MFC's two-priority scan: remember the first user-hash match, but let an address match
    // win outright and stop the search (srchybrid/ClientList.cpp:193-203).
    //
    // Divergence: newClient is skipped. MFC's throwaway is not in the list yet at this point
    // (it calls AddClient only when no match is found), which is why MFC needs its
    // "found_client == tocheck" early-return. Ours is added by handleIncomingConnection the
    // moment the socket is accepted, so an unguarded scan would always match itself on
    // address and never reach the real client. Skipping self reproduces MFC's self-match
    // outcome ("nothing to merge") and, unlike MFC's, does not depend on list order.
    UpDownClient* found = nullptr;
    for (auto* candidate : m_items) {
        if (candidate == newClient)
            continue;
        if (!found && newClient->compare(candidate, /*ignoreUserHash*/ false))
            found = candidate;                       // matching user hash
        if (newClient->compare(candidate, /*ignoreUserHash*/ true)) {
            found = candidate;                       // matching address — wins
            break;
        }
    }

    if (!found)
        return nullptr;

    if (sender) {
        if (auto* oldSocket = qobject_cast<ClientReqSocket*>(found->socket())) {
            if (oldSocket->isConnected()
                && (found->userAddress() != newClient->userAddress()
                    || found->userPort() != newClient->userPort()))
            {
                // The known client is live on a different address, so one of the two is
                // lying about its user hash. MFC ClientList.cpp:213-226.
                if (found->credits()
                    && found->credits()->currentIdentState(found->userAddress().toNetworkUint32())
                           == IdentState::Identified)
                {
                    // found is cryptographically identified, so the newcomer is the bad guy.
                    if (thePrefs.logBannedClients()) {
                        logWarning(QStringLiteral("Clients: %1 (%2), Ban reason: Userhash invalid")
                                       .arg(newClient->userName(),
                                            ipstr(newClient->connectAddress())));
                    }
                    newClient->ban(QStringLiteral("Userhash invalid"));
                } else if (thePrefs.logBannedClients()) {
                    logWarning(QStringLiteral("Found matching client, to a currently connected "
                                              "client: %1 (%2) and %3 (%4)")
                                   .arg(newClient->userName(), ipstr(newClient->connectAddress()),
                                        found->userName(), ipstr(found->connectAddress())));
                }
                return nullptr;
            }

            // The known client's socket is stale — drop it before taking the new one.
            // safeDelete() does not call removeSocket(), so the pool entry must go first
            // or ListenSocket::process() would walk a dangling pointer.
            if (theApp.listenSocket)
                theApp.listenSocket->removeSocket(oldSocket);
            // MFC clears the socket's back-pointer here (`socket->client = NULL`) so the
            // dying socket cannot report back. The Qt equivalent has to cut the signal
            // connections too, otherwise a clientDisconnected from the close below would
            // reach `found` and null out the socket we are about to give it.
            QObject::disconnect(oldSocket, nullptr, found, nullptr);
            oldSocket->setClient(nullptr);
            oldSocket->safeDelete();
            found->setSocket(nullptr);
        }

        // Re-home the socket. Disconnecting during the very emission that got us here is
        // safe in Qt — the running slot completes, later signals go to `found` instead.
        QObject::disconnect(sender, nullptr, newClient, nullptr);
        newClient->setSocket(nullptr);
        found->wireIncomingSocket(sender);
    }

    return found;
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

UpDownClient* ClientList::findByAddress(const Address& addr, uint16 port) const
{
    if (addr.isNull())
        return nullptr;
    for (auto* c : m_items) {
        if (c->userAddress() == addr && c->userPort() == port)
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

UpDownClient* ClientList::findByConnAddress(const Address& addr) const
{
    if (addr.isNull())
        return nullptr;
    for (auto* c : m_items) {
        if (c->connectAddress() == addr)
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

UpDownClient* ClientList::findByEndpoint_UDP(const Address& addr, uint16 udpPort) const
{
    if (addr.isNull())
        return nullptr;
    for (auto* c : m_items) {
        if (c->userAddress() == addr && c->udpPort() == udpPort)
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

    // Create a new client for the incoming buddy. Same reason as requestBuddy() for putting
    // the IP in the userId slot: an IncomingBuddy that we have to dial back must not look
    // Low-ID to tryToConnect().
    auto* client = new UpDownClient(tcpPort, ip, 0, 0, nullptr);
    client->setConnectAddress(Address::fromHostOrder(ip));
    client->setKadPort(udpPort);
    client->setUserHash(clientID);
    client->setKadState(KadState::IncomingBuddy);
    client->setBuddyID(buddyID);

    addClient(client);

    // No status change: MFC leaves m_nBuddyStatus alone here (srchybrid/ClientList.cpp:735-746)
    // and lets the client become ConnectedBuddy when its connection completes. Claiming
    // Connecting for an inbound request would block a genuine outgoing attempt behind a peer
    // that may never connect.
    return true;
}

void ClientList::requestBuddy(uint32 ip, uint16 tcpPort, uint16 udpPort,
                               const uint8* clientID, uint8 connectOptions)
{
    // Already have a connected buddy — skip.
    // Matches MFC ClientList.cpp:694-719.
    if (m_buddyStatus == BuddyStatus::Connected)
        return;

    // Find existing client by IP+port, or create a new one.
    //
    // The contact's IP goes in the ctor's userId slot (host order, so ed2kID stays false),
    // exactly as MFC does — `new CUpDownClient(NULL, contact->GetTCPPort(),
    // contact->GetIPAddress(), 0, 0, false)` (srchybrid/ClientList.cpp:706). Passing 0 left
    // m_userIDHybrid at 0, i.e. hasLowID() true; QueuedBuddy is not one of tryToConnect()'s
    // Low-ID bypasses, so the dial fell through to the callback branches, found no route and
    // returned false — an outgoing buddy could never be established at all, and
    // m_buddyStatus stuck at Connecting forever. A Kad contact always has a routable IP.
    auto* client = findByConnIP(ip, tcpPort);
    if (!client) {
        client = new UpDownClient(tcpPort, ip, 0, 0, nullptr);
        client->setConnectAddress(Address::fromHostOrder(ip));
        addClient(client);
    }

    // Already busy with this client in some other Kad exchange — don't disturb it.
    // MFC srchybrid/ClientList.cpp:709-710.
    if (client->kadState() != KadState::None && client->kadState() != KadState::QueuedBuddy)
        return;

    client->setKadPort(udpPort);
    client->setUserHash(clientID);
    client->setKadState(KadState::QueuedBuddy);
    client->setConnectOptions(connectOptions, true, false);

    // Deliberately no dial and no status change here. processKadList() owns the
    // QueuedBuddy -> ConnectingBuddy transition, exactly as MFC's ProcessKadList does
    // (srchybrid/ClientList.cpp:530-538) — which is what enforces "one buddy attempt at a
    // time" and what lets a second candidate be tried when the first fails. Dialling
    // inline, as this used to, both bypassed that gate and marked us Connecting even when
    // the dial had failed, so m_buddyStatus could stick at Connecting forever.
}

// ===========================================================================
// Kademlia UDP firewall check — MFC ClientList.cpp:767-784
// ===========================================================================

bool ClientList::doRequestFirewallCheckUDP(const kad::Contact& contact)
{
    // Skip if we already know this IP — the result would be biased
    if (findByIP(contact.address().toNetworkUint32()))
        return false;

    // Create a temporary client for the TCP connection. The contact's IP goes in the userId
    // slot so hasLowID() reports the truth; QueuedFwCheckUDP is separately allowed past
    // tryToConnect()'s Low-ID gate, but relying on that bypass to reach a plainly routable
    // peer is what hid the identical bug in requestBuddy().
    auto* client = new UpDownClient(contact.getTCPPort(),
                                    contact.address().toUint32(), 0, 0, nullptr);
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

    // processKadList() drives the QueuedFwCheckUDP dial from here, matching MFC
    // (srchybrid/ClientList.cpp:780-781, which only adds to the Kad list).
    addClient(client);
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
    cleanUpTrackedList();
    processConnectingClients();
    processKadList();

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
// processKadList — MFC CClientList::Process, Kad half (srchybrid/ClientList.cpp:470-620)
// ===========================================================================

void ClientList::processKadList()
{
    auto* kadInst = kad::Kademlia::instance();
    const bool kadRunning = kadInst && kadInst->isRunning();

    // What this pass observed, so buddy loss is detected from the list itself rather than
    // from whichever call site happened to clear m_buddy last.
    BuddyStatus seen = BuddyStatus::None;

    // A copy: the transitions below dial, disconnect and re-enter the queue, any of which
    // can add or remove clients. MFC iterates a dedicated m_KadList; here the Kad clients
    // are simply the ones carrying a state, which keeps a second list from drifting out of
    // sync with m_items.
    const std::vector<UpDownClient*> snapshot(m_items.begin(), m_items.end());

    for (auto* client : snapshot) {
        if (!isValidClient(client))
            continue;
        if (client->kadState() == KadState::None)
            continue;

        // Kad stopped — drop every pending Kad interaction. MFC ClientList.cpp:481-485.
        if (!kadRunning) {
            client->setKadState(KadState::None);
            continue;
        }

        switch (client->kadState()) {
        case KadState::QueuedFwCheck:
        case KadState::QueuedFwCheckUDP:
            // Somebody asked us to verify their TCP port. Direct dial only — a callback
            // would prove nothing about the port under test.
            client->tryToConnect(true, /*noCallbacks*/ true);
            break;

        case KadState::ConnectingFwCheck:
        case KadState::ConnectedFwCheck:
        case KadState::FwCheckUDP:
        case KadState::ConnectingFwCheckUDP:
            // Waiting on a result. The ConnectedFwCheck acknowledgement is sent from
            // connectionEstablished() the moment the socket comes up, so unlike MFC there
            // is nothing to do for it here.
            break;

        case KadState::IncomingBuddy:
            // A firewalled peer wants us as its buddy. If we already have one, drop it;
            // otherwise it becomes ConnectedBuddy when its connection completes.
            if (m_buddyStatus == BuddyStatus::Connected)
                client->setKadState(KadState::None);
            break;

        case KadState::QueuedBuddy:
            // We are firewalled and want this peer as our buddy — but only one attempt at a
            // time, so a second candidate waits until the first fails.
            if (m_buddyStatus == BuddyStatus::None) {
                seen = BuddyStatus::Connecting;
                m_buddyStatus = BuddyStatus::Connecting;
                client->setKadState(KadState::ConnectingBuddy);
                client->tryToConnect(true, /*noCallbacks*/ true);
            } else if (m_buddyStatus == BuddyStatus::Connected) {
                client->setKadState(KadState::None);
            }
            break;

        case KadState::ConnectingBuddy:
            if (m_buddyStatus == BuddyStatus::Connected)
                client->setKadState(KadState::None);
            else
                seen = BuddyStatus::Connecting;
            break;

        case KadState::ConnectedBuddy:
            seen = BuddyStatus::Connected;
            if (m_buddyStatus != BuddyStatus::Connected) {
                m_buddy = client;
                m_buddyStatus = BuddyStatus::Connected;
            }
            // The keep-alive. sendBuddyPingPong() is the schedule check its own comment says
            // "the caller ClientList::processKadList sends the actual OP_BUDDYPING" — this
            // is that caller, which until now did not exist, leaving an established buddy
            // link with nothing holding it open. MFC ClientList.cpp:564-569.
            if (m_buddy == client && theApp.isFirewalled() && client->sendBuddyPingPong()) {
                auto packet = std::make_unique<Packet>(OP_BUDDYPING, 0);
                packet->prot = OP_EMULEPROT;
                client->sendPacket(std::move(packet));
                client->setLastBuddyPingPongTime();
            }
            break;

        default:
            client->setKadState(KadState::None);
            break;
        }
    }

    // Never had a buddy, or just lost one. setBuddy() re-arms the Kad buddy search.
    if (seen == BuddyStatus::None && (m_buddyStatus != BuddyStatus::None || m_buddy))
        setBuddy(nullptr, BuddyStatus::None);

    // A buddy relays callbacks for firewalled peers, which only makes sense while it is
    // itself firewalled — a peer that opened its port no longer needs, or is, a relay.
    // MFC srchybrid/ClientList.cpp:614-617.
    if (m_buddy && !m_buddy->hasLowID())
        m_buddy->setKadState(KadState::None);
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
// Tracked clients — MFC CClientList::m_trackedClientsMap
// ===========================================================================

void ClientList::addTrackClient(const UpDownClient* client)
{
    if (!client)
        return;
    const Address& addr = client->userAddress();
    if (addr.isNull())
        return;

    auto& record = m_trackedClients[addr];
    // Refresh the whole record, not just the matching port: MFC keeps an address alive
    // as long as any client behind it is active.
    record.inserted = static_cast<uint32>(getTickCount());

    const uint16 port = client->userPort();
    for (auto& item : record.items) {
        if (item.port == port) {
            item.credits = client->credits();
            return;
        }
    }
    record.items.push_back({port, client->credits()});
}

int ClientList::clientsFromIP(const Address& addr) const
{
    if (addr.isNull())
        return 0;
    auto it = m_trackedClients.find(addr);
    return it == m_trackedClients.end() ? 0 : static_cast<int>(it->second.items.size());
}

bool ClientList::comparePriorUserhash(const Address& addr, uint16 port,
                                      const ClientCredits* credits) const
{
    auto it = m_trackedClients.find(addr);
    if (it == m_trackedClients.end())
        return true;   // never seen — nothing to contradict
    for (const auto& item : it->second.items) {
        if (item.port == port)
            return item.credits == credits;
    }
    return true;
}

void ClientList::trackBadRequest(const UpDownClient* client, int increaseCounter)
{
    if (!client)
        return;
    const Address& addr = client->userAddress();
    if (addr.isNull())
        return;

    auto& record = m_trackedClients[addr];
    record.inserted = static_cast<uint32>(getTickCount());

    // Saturate at 0 rather than wrapping: callers pass a negative delta to reset the
    // counter, and an unsigned underflow there would read back as a huge strike count.
    if (increaseCounter < 0) {
        const auto decrease = static_cast<uint32>(-increaseCounter);
        record.badRequests = (record.badRequests > decrease) ? record.badRequests - decrease : 0;
    } else {
        record.badRequests += static_cast<uint32>(increaseCounter);
    }
}

uint32 ClientList::badRequests(const UpDownClient* client) const
{
    if (!client)
        return 0;
    auto it = m_trackedClients.find(client->userAddress());
    return it == m_trackedClients.end() ? 0 : it->second.badRequests;
}

int ClientList::trackedCount() const
{
    return static_cast<int>(m_trackedClients.size());
}

void ClientList::removeAllTrackedClients()
{
    m_trackedClients.clear();
}

// ===========================================================================
// Direct UDP callback rate limit — MFC ClientList.cpp:904-921
// ===========================================================================

void ClientList::addTrackCallbackRequests(const Address& addr)
{
    const uint32 curTick = static_cast<uint32>(getTickCount());
    m_directCallbackRequests[addr] = curTick;

    std::erase_if(m_directCallbackRequests, [curTick](const auto& entry) {
        return curTick >= entry.second + SEC2MS(180);
    });
}

bool ClientList::allowCallbackRequest(const Address& addr) const
{
    const auto it = m_directCallbackRequests.find(addr);
    if (it == m_directCallbackRequests.end())
        return true;
    return static_cast<uint32>(getTickCount()) >= it->second + SEC2MS(180);
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

void ClientList::cleanUpTrackedList()
{
    const auto now = static_cast<uint32>(getTickCount());
    if (now - m_lastTrackedCleanUp < TRACKED_CLEANUP_TIME)
        return;

    m_lastTrackedCleanUp = now;
    std::erase_if(m_trackedClients, [now](const auto& pair) {
        return now >= pair.second.inserted + KEEPTRACK_TIME;
    });
}

} // namespace eMule
