#pragma once

/// @file CoreSession.h
/// @brief Lightweight timer driver that calls process() on core managers.
///
/// Drives DownloadQueue, UploadQueue, ListenSocket, KnownFileList,
/// SharedFileList, and Statistics at the correct intervals.
/// Creates and owns core upload pipeline components.

#include "utils/Types.h"

#include <QObject>
#include <QTimer>

#include <memory>
#include <vector>

namespace eMule {

class ClientCreditsList;
class ClientList;
class ClientUDPSocket;
class CollectionKeys;
class DownloadQueue;
class FriendList;
class IPFilter;
class LastCommonRouteFinder;
class KnownFileList;
class ListenSocket;
class SearchList;
class ServerConnect;
class ServerList;
class SharedFileList;
class Scheduler;
class Statistics;
class UDPSocket;
class PortMapper;
struct PortMapRequest;
class UploadBandwidthThrottler;
class UploadDiskIOThread;
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

private slots:
    void onTimer();

private:
    void initUploadPipeline();
    void shutdownUploadPipeline();

    QTimer m_timer;
    uint32 m_tickCounter = 0;

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

    // Owned components
    std::unique_ptr<DownloadQueue> m_downloadQueue;
    std::unique_ptr<IPFilter> m_ipFilter;
    std::unique_ptr<KnownFileList> m_knownFileList;
    std::unique_ptr<SharedFileList> m_sharedFileList;
    std::unique_ptr<UploadQueue> m_uploadQueue;
    std::unique_ptr<UploadBandwidthThrottler> m_uploadThrottler;
    std::unique_ptr<UploadDiskIOThread> m_uploadDiskIO;
    std::unique_ptr<kad::Kademlia> m_kademlia;
    std::unique_ptr<ClientUDPSocket> m_clientUDP;
    std::unique_ptr<ClientCreditsList> m_clientCredits;
    std::unique_ptr<ClientList> m_clientList;
    std::unique_ptr<FriendList> m_friendList;
    std::unique_ptr<ListenSocket> m_listenSocket;
    std::unique_ptr<SearchList> m_searchList;
    std::unique_ptr<ServerList> m_serverList;
    std::unique_ptr<ServerConnect> m_serverConnect;
    std::unique_ptr<UDPSocket> m_serverUDP;
    std::unique_ptr<LastCommonRouteFinder> m_lastCommonRouteFinder;
    std::unique_ptr<Scheduler> m_scheduler;
    std::unique_ptr<Statistics> m_statistics;
    std::unique_ptr<PortMapper> m_portMapper;
    std::unique_ptr<CollectionKeys> m_collectionKeys;
};

} // namespace eMule
