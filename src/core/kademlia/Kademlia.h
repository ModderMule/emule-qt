#pragma once

/// @file Kademlia.h
/// @brief Main Kademlia DHT engine (ported from kademlia/kademlia/Kademlia.h).
///
/// Non-static QObject-based class owned by the application. Uses QTimer for
/// periodic processing instead of being called from an external timer.

#include "kademlia/KadFastKad.h"
#include "kademlia/KadSafeKad.h"
#include "kademlia/KadTypes.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <ctime>
#include <functional>
#include <list>
#include <set>

namespace eMule {
class ClientList;
class IPFilter;
} // namespace eMule

namespace eMule::kad {

class Indexed;
class KadClientSearcher;
class KadPrefs;
class KademliaUDPListener;
class RoutingZone;

/// Outcome of a firewall re-check request, so callers can tell the user why
/// nothing happened instead of silently doing nothing.
enum class RecheckFirewallResult {
    Started,        ///< A new firewall check was started.
    NotRunning,     ///< Kad is not running.
    LanMode,        ///< Running in LAN mode — firewall checks are disabled.
    AlreadyRunning, ///< A NodeFwCheckUDP lookup is still in flight.
};

/// Main Kademlia DHT engine.
class Kademlia : public QObject {
    Q_OBJECT

public:
    explicit Kademlia(QObject* parent = nullptr);
    ~Kademlia() override;

    Kademlia(const Kademlia&) = delete;
    Kademlia& operator=(const Kademlia&) = delete;

    void start();
    void start(KadPrefs* prefs);
    void stop();

    [[nodiscard]] KadPrefs* getPrefs() const { return m_prefs; }
    [[nodiscard]] RoutingZone* getRoutingZone() const { return m_routingZone; }
    [[nodiscard]] KademliaUDPListener* getUDPListener() const { return m_udpListener; }
    [[nodiscard]] Indexed* getIndexed() const { return m_indexed; }

    [[nodiscard]] bool isRunning() const { return m_running; }
    [[nodiscard]] bool isConnected() const;
    /// True when Kad is connected AND has received enough responses for searches to be useful.
    [[nodiscard]] bool isKadReady() const;
    [[nodiscard]] bool isFirewalled() const;
    /// Starts a firewall re-check. At most one NodeFwCheckUDP lookup may run at
    /// a time; a request made while one is in flight is rejected rather than
    /// stacking another lookup.
    [[nodiscard]] RecheckFirewallResult recheckFirewalled();

    [[nodiscard]] uint32 getKademliaUsers(bool newMethod = false) const;
    [[nodiscard]] uint32 getKademliaFiles() const;
    [[nodiscard]] uint32 getTotalStoreKey() const;
    [[nodiscard]] uint32 getTotalStoreSrc() const;
    [[nodiscard]] uint32 getTotalStoreNotes() const;
    [[nodiscard]] uint32 getTotalFile() const;
    [[nodiscard]] bool getPublish() const;
    [[nodiscard]] uint32 getIPAddress() const;

    void bootstrap(uint32 ip, uint16 port);
    void bootstrap(const QString& host, uint16 port);
    void processPacket(const uint8* data, uint32 len, uint32 ip, uint16 port,
                       bool validReceiverKey, const KadUDPKey& senderKey);

    void addEvent(RoutingZone* zone);
    void removeEvent(RoutingZone* zone);

    void storeClosestDistance(const UInt128& distance);
    [[nodiscard]] bool isRunningInLANMode() const;
    [[nodiscard]] static bool shouldSkipFirewallChecks();

    bool findNodeIDByIP(KadClientSearcher& requester, uint32 ip, uint16 tcpPort, uint16 udpPort);
    bool findIPByNodeID(KadClientSearcher& requester, const uint8* nodeID);
    void cancelClientSearch(const KadClientSearcher& requester);

    static void setIPFilter(eMule::IPFilter* filter);
    static eMule::IPFilter* getIPFilter() { return s_ipFilter; }

    static void setClientList(eMule::ClientList* list) { s_clientList = list; }
    static eMule::ClientList* getClientList() { return s_clientList; }

    /// Callback type for Kad keyword search results.
    /// Parameters: searchID, fileHash (16 bytes), name, size, type, sources,
    ///   completeSources, and the imported media/format metadata tags
    ///   (FT_MEDIA_*, TAG_FILEFORMAT, …) attached to the result.
    using KadKeywordResultCallback = std::function<void(uint32 searchID,
        const uint8* fileHash, const QString& name, uint64 size,
        const QString& type, uint32 sources, uint32 completeSources,
        const TagList& metaTags)>;
    /// Callback type for Kad source results (file sources found via DHT).
    /// Parameters: searchID, fileHash (16 bytes), sourceIP, sourcePort,
    ///   buddyIP, buddyPort, cryptOptions, sourceType (1/3/4/5/6),
    ///   buddyHash (16 bytes), clientHash (16 bytes — ED2K user hash), udpPort
    using KadSourceResultCallback = std::function<void(uint32 searchID,
        const uint8* fileHash, uint32 ip, uint16 tcpPort,
        uint32 buddyIP, uint16 buddyPort, uint8 buddyCrypt,
        uint8 sourceType, const uint8* buddyHash,
        const uint8* clientHash, uint16 udpPort,
        const uint8* sourceIPv6, const uint8* buddyIPv6)>;  // 16 bytes each, or nullptr
    /// Callback type for Kad notes results.
    using KadNotesResultCallback = std::function<void(uint32 searchID,
        const uint8* fileHash, const uint8* publisherId, const QString& name,
        uint8 rating, const QString& comment)>;

    static void setKadKeywordResultCallback(KadKeywordResultCallback cb) { s_keywordResultCb = std::move(cb); }
    static void setKadSourceResultCallback(KadSourceResultCallback cb) { s_sourceResultCb = std::move(cb); }
    static void setKadNotesResultCallback(KadNotesResultCallback cb) { s_notesResultCb = std::move(cb); }
    static const KadKeywordResultCallback& kadKeywordResultCallback() { return s_keywordResultCb; }
    static const KadSourceResultCallback& kadSourceResultCallback() { return s_sourceResultCb; }
    static const KadNotesResultCallback& kadNotesResultCallback() { return s_notesResultCb; }

    static ContactList s_bootstrapList;

    static Kademlia* instance() { return s_instance; }
    static KadPrefs* getInstancePrefs() { return s_instance ? s_instance->m_prefs : nullptr; }
    static RoutingZone* getInstanceRoutingZone() { return s_instance ? s_instance->m_routingZone : nullptr; }
    static KademliaUDPListener* getInstanceUDPListener() { return s_instance ? s_instance->m_udpListener : nullptr; }
    static Indexed* getInstanceIndexed() { return s_instance ? s_instance->m_indexed : nullptr; }
    static SafeKad* getInstanceSafeKad() { return s_instance ? &s_instance->m_safeKad : nullptr; }
    static FastKad* getInstanceFastKad() { return s_instance ? &s_instance->m_fastKad : nullptr; }

signals:
    void started();
    void stopped();
    void connected();
    void firewallStatusChanged(bool firewalled);
    void statsUpdated(uint32 users, uint32 files);
    void searchesChanged();

private:
    void process();
    uint32 calculateKadUsersNew() const;

    KadPrefs* m_prefs = nullptr;
    RoutingZone* m_routingZone = nullptr;
    KademliaUDPListener* m_udpListener = nullptr;
    Indexed* m_indexed = nullptr;
    QTimer* m_processTimer = nullptr;

    std::set<RoutingZone*> m_zoneEvents;
    time_t m_nextSearchJumpStart = 0;
    time_t m_nextSelfLookup = 0;
    time_t m_nextFirewallCheck = 0;
    time_t m_nextFindBuddy = 0;
    time_t m_statusUpdate = 0;
    time_t m_bigTimer = 0;
    time_t m_consolidate = 0;
    time_t m_externPortLookup = 0;
    time_t m_bootstrap = 0;
    time_t m_lanModeCheck = 0;
    bool m_running = false;
    bool m_lanMode = false;
    bool m_bootstrapping = false;
    bool m_wasConnected = false;
    bool m_ownsPrefs = false;
    std::list<uint32> m_statsEstUsersProbes;

    SafeKad m_safeKad;
    FastKad m_fastKad;

    static Kademlia* s_instance;
    static eMule::IPFilter* s_ipFilter;
    static eMule::ClientList* s_clientList;
    static KadKeywordResultCallback s_keywordResultCb;
    static KadSourceResultCallback s_sourceResultCb;
    static KadNotesResultCallback s_notesResultCb;
};

} // namespace eMule::kad
