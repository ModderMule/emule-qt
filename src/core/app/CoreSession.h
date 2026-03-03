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

namespace eMule {

class ClientList;
class ClientUDPSocket;
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
class UPnPManager;
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

private slots:
    void onTimer();

private:
    void initUploadPipeline();
    void shutdownUploadPipeline();

    QTimer m_timer;
    uint32 m_tickCounter = 0;

    void initClientInfra();
    void shutdownClientInfra();
    void initKademlia();
    void shutdownKademlia();
    void initUSS();
    void shutdownUSS();
    void updateUSSParams();
    void initScheduler();
    void shutdownScheduler();
    void initSearch();
    void shutdownSearch();
    void initServerConnect();
    void shutdownServerConnect();
    void autoUpdateServerList();
    void initUPnP();
    void shutdownUPnP();

    // Owned components
    std::unique_ptr<IPFilter> m_ipFilter;
    std::unique_ptr<KnownFileList> m_knownFileList;
    std::unique_ptr<SharedFileList> m_sharedFileList;
    std::unique_ptr<UploadQueue> m_uploadQueue;
    std::unique_ptr<UploadBandwidthThrottler> m_uploadThrottler;
    std::unique_ptr<UploadDiskIOThread> m_uploadDiskIO;
    std::unique_ptr<kad::Kademlia> m_kademlia;
    std::unique_ptr<ClientUDPSocket> m_clientUDP;
    std::unique_ptr<ClientList> m_clientList;
    std::unique_ptr<FriendList> m_friendList;
    std::unique_ptr<ListenSocket> m_listenSocket;
    std::unique_ptr<SearchList> m_searchList;
    std::unique_ptr<ServerList> m_serverList;
    std::unique_ptr<ServerConnect> m_serverConnect;
    std::unique_ptr<LastCommonRouteFinder> m_lastCommonRouteFinder;
    std::unique_ptr<Scheduler> m_scheduler;
    std::unique_ptr<UPnPManager> m_upnpManager;
};

} // namespace eMule
