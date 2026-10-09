#pragma once

/// @file CoreSession.h
/// @brief Lightweight timer driver that calls process() on core managers.
///
/// Drives DownloadQueue, UploadQueue, ListenSocket, KnownFileList,
/// SharedFileList, and Statistics at the correct intervals.
/// Creates and owns core upload pipeline components.

#include "net/BindAddress.h"
#include "portmap/PortMapTypes.h"
#include "utils/Types.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <vector>

namespace eMule {

class Address;
class ClientCreditsList;
class ClientList;
class ClientUDPSocket;
class CollectionKeys;
class DownloadQueue;
class Endpoint;
class FriendList;
class IPFilter;
class IP2Country;
class GeoIpUpdater;
class LastCommonRouteFinder;
class KnownFileList;
class ListenSocket;
class GlobalSearchScheduler;
class SearchList;
class SeenFileIndex;
class ClientCensus;
namespace kad { class KadNodeCensus; }
class ServerConnect;
class ServerList;
class SharedFileList;
class Scheduler;
class Statistics;
class StatsHistory;
class UDPSocket;
class PortMapper;
struct PortMapRequest;
class UploadBandwidthThrottler;
class UploadDiskIOThread;
class PartFileWriteThread;
class AICHSyncThread;
class HttpCacheManager;
class UploadQueue;

namespace kad { class Kademlia; }

class CoreSession : public QObject {
    Q_OBJECT

public:
    explicit CoreSession(QObject* parent = nullptr);
    ~CoreSession() override;

    void start();
    void stop();

    [[nodiscard]] kad::Kademlia* kademlia() const { return m_kademlia.get(); }
    [[nodiscard]] CollectionKeys* collectionKeys() const { return m_collectionKeys.get(); }

    /// Re-declare the desired port mappings. Call after anything that changes
    /// which ports need forwarding — notably the web server starting or
    /// stopping, which is what finally gives the webServerUPnP pref an effect.
    void updatePortMappings();

    /// Follow the enableUPnP pref on a running session: start or drop the mapper,
    /// else re-declare the mappings. Call after a preference save.
    void applyPortMapPreferences();

    /// Move the listen sockets to the configured ports — only while no server, Kad
    /// or peer is connected. Call after a preference save.
    PortApplyResult applyListenPorts();
    /// Ports the sockets are bound to (0 = closed).
    [[nodiscard]] uint16 boundTcpPort() const;
    [[nodiscard]] uint16 boundUdpPort() const;

    /// Skip the auto-connect at start(): the GUI's first start wizard is about to
    /// ask for the ports. Set before start().
    void setConnectHold(bool hold) { m_connectHold = hold; }
    [[nodiscard]] bool isConnectHeld() const { return m_connectHold; }
    /// End the hold and do the auto-connect that start() skipped.
    void releaseConnectHold();

    /// The bound-interface selection changed (preference edit) or may have (watchdog):
    /// re-resolve it, and when it differs close every socket and reopen on the new one.
    /// While it does not resolve the networks stay down; the daemon keeps running.
    void applyBindSelection();
    [[nodiscard]] bool isNetworkSuspended() const { return m_netSuspended; }

    /// Listen ports that did not open at start(): portBindFailed fires before anyone
    /// can be connected to it.
    [[nodiscard]] const QList<int>& failedBindPorts() const { return m_failedBindPorts; }

    // -- Protocol handlers installed on the shared sockets ---------------------
    // Static and state-free, so the wiring below stays a one-liner and the
    // behaviour is reachable without standing up a whole session.

    /// OP_DIRECTCALLBACKREQ receive handler: a firewalled peer asks us to open the
    /// TCP connection. Guards on Kad running + us firewalled, the 19-byte minimum
    /// and the sender passing isGoodIP / ban / IP-filter, then creates or refreshes
    /// the client and dials it. Wired to ClientUDPSocket::directCallbackReceived.
    static void handleDirectCallbackRequest(const Endpoint& senderEP,
                                            const uint8* data, uint32 size);

    /// Our effective public IPv6 changed: queue an OP_CHANGE_CLIENT_IP for every
    /// connected peer that can use it. Marks only — the packet goes out when the
    /// upload queue or a source list next walks the client, so an address rotation
    /// never fans a write out to every socket at once. Installed as
    /// AppContext::onPublicIPv6Changed.
    static void markPeersForIPChange(const Address& effective);

signals:
    /// Port-mapping status, whichever mapper instance is alive. Disabled when the
    /// pref switches it off.
    void portMapStatusChanged(eMule::PortMapStatus status);

    /// A listen port could not be opened (MFC IDS_MAIN_SOCKETERROR).
    void portBindFailed(int port);

private slots:
    void onTimer();

private:
    /// `<ConfigDir>/uploadqueue.met` — the Upload Queue Storage file.
    [[nodiscard]] static QString uploadQueuePath();
    /// Force-write the upload queue. Must run before any shutdownXxx(): shutdownClientInfra()
    /// destroys the ClientCredits objects the waiting clients read their wait times from.
    static void saveUploadQueueStore();

    void initUploadPipeline();
    void shutdownUploadPipeline();

    QTimer m_timer;
    uint32 m_tickCounter = 0;
    uint32 m_lastStatsFlushTick = 0;

    void initClientInfra();
    void shutdownClientInfra();
    void initDownloadQueue();
    void shutdownDownloadQueue();
    void initClientUDP();
    void shutdownClientUDP();
    void initKademlia();
    void wireKadListener();
    void shutdownKademlia();
    void initUSS();
    void shutdownUSS();
    void updateUSSParams();
    void initScheduler();
    void shutdownScheduler();
    void initStatistics();
    void shutdownStatistics();
    void initSearch();
    /// The seen-files store; needed by the share scan and by searches, whichever
    /// comes first.
    void ensureSeenFileIndex();
    void shutdownSearch();
    void initServerConnect();
    void shutdownServerConnect();
    /// Select our public IPv6 and emit the privacy-address advisory. The advisory is
    /// emitted here and nowhere else, which is what makes it once-per-run — the later
    /// refresh in ServerConnect::initLocalIP() runs on every reconnect and stays silent.
    void initLocalIPv6();
    void autoUpdateServerList();
    void initPortMapper();
    void shutdownPortMapper();
    /// Collect the mappings that should currently exist. Reads the ports the
    /// sockets are actually bound to, not the preference values — with a random
    /// or zero configured port those differ, and mapping the pref would forward
    /// a port nothing is listening on.
    [[nodiscard]] std::vector<PortMapRequest> buildPortMapRequests() const;
    void stopWorkerThreads();
    void suspendNetworking();
    void resumeNetworking();
    /// No server, no Kad, no peer socket.
    [[nodiscard]] bool isNetworkIdle() const;
    void rememberAppliedPorts();
    void reportBindFailure(int port);
    void publishListenPorts();

    BindAddress::Resolution m_appliedBind;   ///< what the open sockets were bound on
    bool m_netSuspended = false;
    bool m_resumeEd2k = false;   ///< reconnect to a server on resume
    bool m_resumeKad = false;    ///< restart Kad on resume
    bool m_connectHold = false;  ///< auto-connect deferred until releaseConnectHold()
    // Preference values the sockets were last bound with (not the socket ports:
    // a configured 0 gets an OS-assigned one).
    QList<int> m_failedBindPorts;
    uint16 m_appliedTcpPort = 0;
    uint16 m_appliedUdpPort = 0;
    uint16 m_appliedServerUdpPort = 0;

    // Owned components
    std::unique_ptr<DownloadQueue> m_downloadQueue;
    std::unique_ptr<IPFilter> m_ipFilter;
    std::unique_ptr<IP2Country> m_ip2Country;
    std::unique_ptr<GeoIpUpdater> m_geoIpUpdater;
    std::unique_ptr<KnownFileList> m_knownFileList;
    std::unique_ptr<SharedFileList> m_sharedFileList;
    std::unique_ptr<UploadQueue> m_uploadQueue;
    std::unique_ptr<HttpCacheManager> m_httpCache;
    std::unique_ptr<UploadBandwidthThrottler> m_uploadThrottler;
    std::unique_ptr<UploadDiskIOThread> m_uploadDiskIO;
    std::unique_ptr<PartFileWriteThread> m_partFileWriter;
    std::unique_ptr<AICHSyncThread> m_aichSync;
    std::unique_ptr<kad::Kademlia> m_kademlia;
    std::unique_ptr<ClientUDPSocket> m_clientUDP;
    std::unique_ptr<ClientCreditsList> m_clientCredits;
    std::unique_ptr<ClientList> m_clientList;
    std::unique_ptr<FriendList> m_friendList;
    std::unique_ptr<ListenSocket> m_listenSocket;
    std::unique_ptr<SearchList> m_searchList;
    std::unique_ptr<SeenFileIndex> m_seenFileIndex;
    std::unique_ptr<kad::KadNodeCensus> m_kadNodeCensus;
    std::unique_ptr<ClientCensus> m_clientCensus;
    std::unique_ptr<GlobalSearchScheduler> m_globalSearch;
    std::unique_ptr<ServerList> m_serverList;
    std::unique_ptr<ServerConnect> m_serverConnect;
    std::unique_ptr<UDPSocket> m_serverUDP;
    std::unique_ptr<LastCommonRouteFinder> m_lastCommonRouteFinder;
    uint32 m_lastUSSMaxUpload = UINT32_MAX;   ///< last limit handed to it; a rise restarts its ramp
    std::unique_ptr<Scheduler> m_scheduler;
    std::unique_ptr<Statistics> m_statistics;
    std::unique_ptr<StatsHistory> m_statsHistory;
    std::unique_ptr<PortMapper> m_portMapper;
    std::unique_ptr<CollectionKeys> m_collectionKeys;
};

} // namespace eMule
