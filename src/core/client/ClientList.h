#pragma once

/// @file ClientList.h
/// @brief Client list manager — tracks all connected/known peers.
///
/// Ported from MFC CClientList (srchybrid/ClientList.h).
/// Phase 1 covers add/remove/find and banned IP tracking.

#include "client/DeadSourceList.h"
#include "net/Address.h"
#include "utils/EntityList.h"
#include "utils/Types.h"

#include <QObject>

#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eMule::kad { class Contact; }

namespace eMule {

class ClientCredits;
class ClientReqSocket;
class UpDownClient;

/// Buddy connection state for Kademlia firewall traversal.
enum class BuddyStatus : uint8_t {
    None,        ///< No buddy
    Connecting,  ///< Buddy request in progress
    Connected    ///< Buddy is active
};

class ClientList : public EntityList<UpDownClient> {
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

    /// Find the client an inbound socket really belongs to, once its OP_HELLO has been
    /// parsed onto the throwaway @p newClient that handleIncomingConnection() created.
    /// Returns that already-known client, or nullptr when the peer is genuinely new (or
    /// when the merge is refused because the identity looks spoofed).
    ///
    /// When a survivor is found and @p sender is non-null, @p sender is re-homed onto the
    /// survivor and detached from @p newClient — this is what lets a LowID source that
    /// answers our OP_CALLBACKREQUEST resume its download instead of stranding a second,
    /// empty client object.
    ///
    /// Divergence from MFC CClientList::AttachToAlreadyKnown (srchybrid/ClientList.cpp:189):
    /// this never deletes @p newClient and never removes it from the list. MFC deletes it
    /// inline, which is not possible here because the only caller runs inside newClient's
    /// own signal handler. The caller owns the teardown; the split also keeps the matching
    /// logic unit-testable without any sockets.
    [[nodiscard]] UpDownClient* attachToAlreadyKnown(UpDownClient* newClient,
                                                     ClientReqSocket* sender);

    // -- Client management --------------------------------------------------

    void addClient(UpDownClient* client, bool skipDupTest = false);
    void removeClient(UpDownClient* client, const QString& reason = {});
    [[nodiscard]] bool isValidClient(const UpDownClient* client) const;
    [[nodiscard]] int clientCount() const;
    void deleteAll();

    // -- Find operations (linear scan, matching MFC) ------------------------

    [[nodiscard]] UpDownClient* findByIP(uint32 ip) const;
    [[nodiscard]] UpDownClient* findByIP(uint32 ip, uint16 port) const;
    /// Find by user address + TCP port, for either family. Prefer this over the uint32
    /// findByIP(): that one projects through toNetworkUint32(), which is 0 for every
    /// IPv6 address, so an IPv6 peer both fails to match itself and falsely matches any
    /// client that has no user address yet. A null @p addr never matches.
    [[nodiscard]] UpDownClient* findByAddress(const Address& addr, uint16 port) const;
    [[nodiscard]] UpDownClient* findByConnIP(uint32 ip, uint16 port) const;
    [[nodiscard]] UpDownClient* findByUserHash(const uint8* hash,
                                                uint32 ip = 0, uint16 port = 0) const;
    /// Find by UDP endpoint. Address-typed on purpose: the previous uint32 form was
    /// byte-order ambiguous (it compared network order while every caller passed the
    /// host-order value out of an Endpoint, so it never matched) and could not
    /// represent an IPv6 peer at all. Do not reintroduce a uint32 overload.
    [[nodiscard]] UpDownClient* findByEndpoint_UDP(const Address& addr, uint16 udpPort) const;
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

    /// Record an IP we asked to firewall-check us, so EncryptedStreamSocket accepts its
    /// unencrypted callback under require-encryption (and so a firewall-check ACK is only
    /// counted from a requested IP). `ipNet` is network order; entries expire after 180 s.
    /// Matches MFC CClientList::AddKadFirewallRequest (srchybrid/ClientList.cpp:844).
    void addKadFirewallRequest(uint32 ipNet);
    /// True if `ipNet` (network order) is a still-live Kad firewall-check request.
    /// Matches MFC CClientList::IsKadFirewallCheckIP (srchybrid/ClientList.cpp:852).
    [[nodiscard]] bool isKadFirewallCheckIP(uint32 ipNet) const;

    // -- Connecting client timeout (MFC CClientList::ProcessConnectingClientsList) --

    /// Track a client that just started a connection attempt.
    /// 45-second timeout ensures sources in WaitCallback/Connecting states
    /// don't stay stuck forever. Matches MFC srchybrid/ClientList.cpp:865-901.
    void addConnectingClient(UpDownClient* client);
    void removeConnectingClient(const UpDownClient* client);
    void processConnectingClients();

    // -- Banned clients -----------------------------------------------------

    /// Iterate over all known clients.
    void forEachClient(const std::function<void(UpDownClient*)>& callback) const;

    /// Periodic cleanup — removes idle clients that serve no purpose.
    /// Called every ~1s from CoreSession::onTimer().
    /// Matches MFC CClientList::Process() (srchybrid/ClientList.cpp).
    void process();

    void addBannedClient(const Address& addr);
    [[nodiscard]] bool isBannedClient(const Address& addr) const;
    void removeBannedClient(const Address& addr);

    [[nodiscard]] int bannedCount() const;
    void removeAllBannedClients();

    // -- Tracked clients ----------------------------------------------------
    // MFC CClientList::m_trackedClientsMap. Keeps a few attributes of clients that
    // have already consumed or abused an upload slot for KEEPTRACK_TIME, so the limits
    // below survive the UpDownClient object being destroyed — a leecher must not be
    // able to reset its record just by reconnecting.

    /// Record this client's (address, TCP port, credits) and refresh the address's TTL.
    /// Re-recording the same port updates it in place rather than growing the list.
    void addTrackClient(const UpDownClient* client);

    /// Number of DISTINCT TCP ports seen from this address inside the TTL window.
    /// Note this is not "clients currently queued" — a peer that reconnects from a
    /// fresh source port counts again.
    [[nodiscard]] int clientsFromIP(const Address& addr) const;

    /// False when a prior client at this address:port carried different credits, i.e.
    /// the user hash behind the endpoint changed. True when unknown or unchanged.
    [[nodiscard]] bool comparePriorUserhash(const Address& addr, uint16 port,
                                            const ClientCredits* credits) const;

    /// Per-address strike counter behind the two-strikes ban (see
    /// UpDownClient::registerBadRequest). Negative deltas are used to reset it.
    void trackBadRequest(const UpDownClient* client, int increaseCounter);
    [[nodiscard]] uint32 badRequests(const UpDownClient* client) const;

    [[nodiscard]] int trackedCount() const;
    void removeAllTrackedClients();

    // -- Public member (matches MFC pattern) --------------------------------

    DeadSourceList globalDeadSourceList;

signals:
    void clientAdded(UpDownClient* client);
    void clientRemoved(UpDownClient* client);

private:
    void cleanUpBannedList();
    void cleanUpTrackedList();

    // EntityList hooks — emit the list's signals on add/remove.
    void onEntityAdded(UpDownClient* client) override { emit clientAdded(client); }
    void onEntityRemoved(UpDownClient* client) override { emit clientRemoved(client); }

    struct ConnectingClient {
        UpDownClient* client;
        uint32 insertedTick;
    };

    /// One record per tracked address. `items` grows per distinct TCP port; `inserted`
    /// is refreshed for the whole record on any touch, so an address stays alive as
    /// long as *any* client behind it is active.
    struct TrackedClient {
        struct PortAndCredits {
            uint16 port = 0;
            const ClientCredits* credits = nullptr;
        };
        std::vector<PortAndCredits> items;
        uint32 inserted = 0;
        uint32 badRequests = 0;
    };

    std::vector<ConnectingClient> m_connectingClients;
    std::unordered_map<Address, uint32> m_bannedList;  // Address -> ban tick
    std::unordered_map<Address, TrackedClient> m_trackedClients;
    uint32 m_lastBanCleanUp = 0;
    uint32 m_lastTrackedCleanUp = 0;
    // Kad firewall-check requests (MFC listFirewallCheckRequests): newest at front,
    // (ipNet, insertedSec). Purged in addKadFirewallRequest; the query never removes.
    mutable std::deque<std::pair<uint32, uint32>> m_kadFirewallRequests;
    UpDownClient* m_buddy = nullptr;
    BuddyStatus m_buddyStatus = BuddyStatus::None;
};

} // namespace eMule
