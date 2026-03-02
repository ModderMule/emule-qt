#pragma once

/// @file ClientList.h
/// @brief Client list manager — tracks all connected/known peers.
///
/// Ported from MFC CClientList (srchybrid/ClientList.h).
/// Phase 1 covers add/remove/find and banned IP tracking.

#include "client/DeadSourceList.h"
#include "utils/Types.h"

#include <QObject>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace eMule::kad { class Contact; }

namespace eMule {

class ClientReqSocket;
class UpDownClient;

/// Buddy connection state for Kademlia firewall traversal.
enum class BuddyStatus : uint8_t {
    None,        ///< No buddy
    Connecting,  ///< Buddy request in progress
    Connected    ///< Buddy is active
};

class ClientList : public QObject {
    Q_OBJECT

public:
    explicit ClientList(QObject* parent = nullptr);
    ~ClientList() override;

    // Non-copyable (QObject)
    ClientList(const ClientList&) = delete;
    ClientList& operator=(const ClientList&) = delete;

    // -- Incoming connection handling ----------------------------------------

    /// Handle a new incoming TCP connection from ListenSocket.
    /// Creates an UpDownClient and wires packet signals.
    void handleIncomingConnection(ClientReqSocket* socket);

    // -- Client management --------------------------------------------------

    void addClient(UpDownClient* client, bool skipDupTest = false);
    void removeClient(UpDownClient* client, const QString& reason = {});
    [[nodiscard]] bool isValidClient(const UpDownClient* client) const;
    [[nodiscard]] int clientCount() const;
    void deleteAll();

    // -- Find operations (linear scan, matching MFC) ------------------------

    [[nodiscard]] UpDownClient* findByIP(uint32 ip) const;
    [[nodiscard]] UpDownClient* findByIP(uint32 ip, uint16 port) const;
    [[nodiscard]] UpDownClient* findByConnIP(uint32 ip, uint16 port) const;
    [[nodiscard]] UpDownClient* findByUserHash(const uint8* hash,
                                                uint32 ip = 0, uint16 port = 0) const;
    [[nodiscard]] UpDownClient* findByIP_UDP(uint32 ip, uint16 udpPort) const;
    [[nodiscard]] UpDownClient* findByServerID(uint32 serverIP, uint32 ed2kUserID) const;
    [[nodiscard]] UpDownClient* findByUserID_KadPort(uint32 clientID, uint16 kadPort) const;
    [[nodiscard]] UpDownClient* findByIP_KadPort(uint32 ip, uint16 kadPort) const;

    // -- Buddy management (Kademlia) ----------------------------------------

    [[nodiscard]] UpDownClient* getBuddy() const { return m_buddy; }
    [[nodiscard]] BuddyStatus buddyStatus() const { return m_buddyStatus; }
    void setBuddy(UpDownClient* buddy, BuddyStatus status);

    /// Called when a remote node wants to become our buddy.
    /// Returns true if accepted.
    /// Matches MFC CClientList::IncomingBuddy (srchybrid/ClientList.cpp:721).
    bool incomingBuddy(uint32 ip, uint16 tcpPort, uint16 udpPort,
                       const uint8* clientID, const uint8* buddyID);

    /// Initiate a buddy request to a remote node found via Kad.
    /// Matches MFC CClientList::RequestBuddy (srchybrid/ClientList.cpp:694).
    void requestBuddy(uint32 ip, uint16 tcpPort, uint16 udpPort,
                      const uint8* clientID, uint8 connectOptions);

    /// Create a temporary client to request a UDP firewall check via TCP.
    /// Matches MFC CClientList::DoRequestFirewallCheckUDP (srchybrid/ClientList.cpp:767).
    bool doRequestFirewallCheckUDP(const kad::Contact& contact);

    // -- Banned clients -----------------------------------------------------

    /// Iterate over all known clients.
    void forEachClient(const std::function<void(UpDownClient*)>& callback) const;

    /// Periodic cleanup — removes idle clients that serve no purpose.
    /// Called every ~1s from CoreSession::onTimer().
    /// Matches MFC CClientList::Process() (srchybrid/ClientList.cpp).
    void process();

    void addBannedClient(uint32 ip);
    [[nodiscard]] bool isBannedClient(uint32 ip) const;
    void removeBannedClient(uint32 ip);
    [[nodiscard]] int bannedCount() const;
    void removeAllBannedClients();

    // -- Public member (matches MFC pattern) --------------------------------

    DeadSourceList globalDeadSourceList;

signals:
    void clientAdded(UpDownClient* client);
    void clientRemoved(UpDownClient* client);

private:
    void cleanUpBannedList();

    std::vector<UpDownClient*> m_clients;
    std::unordered_map<uint32, uint32> m_bannedList;  // IP -> ban tick
    uint32 m_lastBanCleanUp = 0;
    UpDownClient* m_buddy = nullptr;
    BuddyStatus m_buddyStatus = BuddyStatus::None;
};

} // namespace eMule
