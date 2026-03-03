/// @file CoreSession.cpp
/// @brief Lightweight timer driver — calls process() on core managers.
///
/// Creates and wires the upload pipeline components on start().

#include "app/CoreSession.h"
#include "app/AppContext.h"
#include "ipfilter/IPFilter.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFileList.h"
#include "friends/FriendList.h"
#include "files/SharedFileList.h"
#include "kademlia/Kademlia.h"
#include "search/SearchList.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUDPListener.h"
#include "kademlia/KadUInt128.h"
#include "net/ClientUDPSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/Scheduler.h"
#include "net/LastCommonRouteFinder.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadDiskIOThread.h"
#include "transfer/UploadQueue.h"
#include "upnp/UPnPManager.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

#include <cstring>

namespace eMule {

CoreSession::CoreSession(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &CoreSession::onTimer);
}

CoreSession::~CoreSession()
{
    stop();
    shutdownScheduler();
    shutdownUPnP();
    shutdownKademlia();
    shutdownServerConnect();
    shutdownClientInfra();
    shutdownUSS();
    shutdownUploadPipeline();
    shutdownSearch();
}

void CoreSession::start()
{
    logInfo(QStringLiteral("Starting core — TCP port %1, UDP port %2")
                .arg(thePrefs.port()).arg(thePrefs.udpPort()));
    initUploadPipeline();
    initUSS();
    initClientInfra();
    initSearch();
    initServerConnect();
    initKademlia();
    initUPnP();
    initScheduler();
    m_tickCounter = 0;
    m_timer.start();
}

void CoreSession::stop()
{
    m_timer.stop();
}

// ---------------------------------------------------------------------------
// initUploadPipeline — create and wire upload components
// ---------------------------------------------------------------------------

void CoreSession::initUploadPipeline()
{
    // Create KnownFileList if not already set
    if (!theApp.knownFileList) {
        m_knownFileList = std::make_unique<KnownFileList>();
        m_knownFileList->init(thePrefs.configDir());
        theApp.knownFileList = m_knownFileList.get();
    }

    // Create SharedFileList if not already set
    if (!theApp.sharedFileList) {
        m_sharedFileList = std::make_unique<SharedFileList>(theApp.knownFileList);
        theApp.sharedFileList = m_sharedFileList.get();

        // Wire server connect if available
        if (theApp.serverConnect)
            m_sharedFileList->setServerConnect(theApp.serverConnect);
    }

    // Create UploadDiskIOThread
    if (!m_uploadDiskIO) {
        m_uploadDiskIO = std::make_unique<UploadDiskIOThread>();
        m_uploadDiskIO->start();
    }

    // Create UploadBandwidthThrottler
    if (!m_uploadThrottler && !theApp.uploadBandwidthThrottler) {
        m_uploadThrottler = std::make_unique<UploadBandwidthThrottler>();
        theApp.uploadBandwidthThrottler = m_uploadThrottler.get();
        m_uploadThrottler->start();
    }

    // Create UploadQueue if not already set
    if (!theApp.uploadQueue) {
        m_uploadQueue = std::make_unique<UploadQueue>();
        theApp.uploadQueue = m_uploadQueue.get();

        // Wire components
        m_uploadQueue->setDiskIOThread(m_uploadDiskIO.get());
        m_uploadQueue->setThrottler(theApp.uploadBandwidthThrottler);
        m_uploadQueue->setSharedFileList(theApp.sharedFileList);
    }

    // Initial scan of shared files
    if (theApp.sharedFileList)
        theApp.sharedFileList->reload();
}

// ---------------------------------------------------------------------------
// shutdownUploadPipeline — stop threads and release components
// ---------------------------------------------------------------------------

void CoreSession::shutdownUploadPipeline()
{
    // Stop disk IO thread
    if (m_uploadDiskIO) {
        m_uploadDiskIO->endThread();
        m_uploadDiskIO->wait();
    }

    // Stop bandwidth throttler
    if (m_uploadThrottler) {
        m_uploadThrottler->endThread();
        m_uploadThrottler->wait();
    }

    // Clear theApp pointers before destroying owned objects
    if (m_uploadQueue && theApp.uploadQueue == m_uploadQueue.get())
        theApp.uploadQueue = nullptr;
    if (m_sharedFileList && theApp.sharedFileList == m_sharedFileList.get())
        theApp.sharedFileList = nullptr;
    if (m_knownFileList && theApp.knownFileList == m_knownFileList.get())
        theApp.knownFileList = nullptr;
    if (m_uploadThrottler && theApp.uploadBandwidthThrottler == m_uploadThrottler.get())
        theApp.uploadBandwidthThrottler = nullptr;

    m_uploadQueue.reset();
    m_uploadDiskIO.reset();
    m_uploadThrottler.reset();
    m_sharedFileList.reset();
    m_knownFileList.reset();
}

// ---------------------------------------------------------------------------
// onTimer — called every 100ms
// ---------------------------------------------------------------------------

void CoreSession::onTimer()
{
    ++m_tickCounter;

    // Fast path — every 100ms tick
    if (theApp.downloadQueue)
        theApp.downloadQueue->process();
    if (theApp.uploadQueue)
        theApp.uploadQueue->process();

    // Slow path — every 10th tick (~1s)
    if (m_tickCounter % 10 == 0) {
        updateUSSParams();
        if (theApp.clientCredits) {
            const QString creditsPath = QDir(thePrefs.configDir()).filePath(
                QStringLiteral("clients.met"));
            theApp.clientCredits->process(creditsPath);  // auto-save every 13 min
        }
        if (theApp.listenSocket)
            theApp.listenSocket->process();
        if (theApp.knownFileList)
            theApp.knownFileList->process();
        if (theApp.sharedFileList)
            theApp.sharedFileList->process();
        if (theApp.statistics)
            theApp.statistics->updateConnectionStats(0.0f, 0.0f);
        if (theApp.clientList)
            theApp.clientList->process();
        if (theApp.serverConnect) {
            theApp.serverConnect->checkForTimeout();
            theApp.serverConnect->keepConnectionAlive();
        }
        if (theApp.scheduler && thePrefs.schedulerEnabled())
            theApp.scheduler->check();

        // UPnP refresh every ~30s (300 ticks)
        if (m_upnpManager && m_tickCounter % 300 == 0)
            m_upnpManager->checkAndRefresh();
    }
}

// ---------------------------------------------------------------------------
// initUSS — create and start LastCommonRouteFinder (Upload SpeedSense)
// ---------------------------------------------------------------------------

void CoreSession::initUSS()
{
    if (m_lastCommonRouteFinder)
        return;

    m_lastCommonRouteFinder = std::make_unique<LastCommonRouteFinder>();
    theApp.lastCommonRouteFinder = m_lastCommonRouteFinder.get();

    connect(m_lastCommonRouteFinder.get(), &LastCommonRouteFinder::needMoreHosts,
            this, [this] {
        std::vector<uint32> ips;
        if (theApp.serverList) {
            for (const auto& srv : theApp.serverList->servers()) {
                if (srv->ip() != 0)
                    ips.push_back(srv->ip());
            }
        }
        if (theApp.clientList) {
            theApp.clientList->forEachClient([&ips](UpDownClient* c) {
                uint32 ip = c->connectIP();
                if (ip != 0)
                    ips.push_back(ip);
            });
        }
        if (!ips.empty())
            m_lastCommonRouteFinder->addHostsToCheck(ips);
    });

    m_lastCommonRouteFinder->start();
    logInfo(QStringLiteral("Upload SpeedSense (USS) thread started"));
}

// ---------------------------------------------------------------------------
// shutdownUSS — stop and release LastCommonRouteFinder
// ---------------------------------------------------------------------------

void CoreSession::shutdownUSS()
{
    if (!m_lastCommonRouteFinder)
        return;

    m_lastCommonRouteFinder->endThread();
    m_lastCommonRouteFinder->wait();

    if (theApp.lastCommonRouteFinder == m_lastCommonRouteFinder.get())
        theApp.lastCommonRouteFinder = nullptr;
    m_lastCommonRouteFinder.reset();
}

// ---------------------------------------------------------------------------
// updateUSSParams — feed current prefs to the USS thread each second
// ---------------------------------------------------------------------------

void CoreSession::updateUSSParams()
{
    if (!m_lastCommonRouteFinder)
        return;

    USSParams p;
    p.enabled = thePrefs.dynUpEnabled();
    p.pingTolerance = thePrefs.dynUpPingTolerance() / 100.0;
    p.pingToleranceMilliseconds = static_cast<uint32>(thePrefs.dynUpPingToleranceMs());
    p.useMillisecondPingTolerance = thePrefs.dynUpUseMillisecondPingTolerance();
    p.goingUpDivider = static_cast<uint32>(thePrefs.dynUpGoingUpDivider());
    p.goingDownDivider = static_cast<uint32>(thePrefs.dynUpGoingDownDivider());
    p.numberOfPingsForAverage = static_cast<uint32>(thePrefs.dynUpNumberOfPings());
    p.minUpload = thePrefs.minUpload() * 1024;  // KB/s → bytes/s
    p.maxUpload = (thePrefs.maxUpload() == 0) ? UINT32_MAX : thePrefs.maxUpload() * 1024;
    p.curUpload = (thePrefs.maxUpload() == 0) ? UINT32_MAX : thePrefs.maxUpload() * 1024;
    m_lastCommonRouteFinder->setPrefs(p);
}

// ---------------------------------------------------------------------------
// initServerConnect — create ServerList + ServerConnect, load server.met
// ---------------------------------------------------------------------------

void CoreSession::initServerConnect()
{
    if (m_serverList || m_serverConnect)
        return;

    // 1. Create ServerList and load server.met
    m_serverList = std::make_unique<ServerList>();
    theApp.serverList = m_serverList.get();

    const QString serverMetPath = QDir(thePrefs.configDir()).filePath(
        QStringLiteral("server.met"));
    if (QFile::exists(serverMetPath)) {
        if (m_serverList->loadServerMet(serverMetPath))
            logInfo(QStringLiteral("Loaded %1 servers from server.met")
                        .arg(m_serverList->serverCount()));
        else
            logWarning(QStringLiteral("Failed to load server.met"));
    } else {
        logInfo(QStringLiteral("No server.met found — server list is empty"));
    }

    // 1b. Auto-update server list from URL if configured
    if (thePrefs.autoUpdateServerList() && !thePrefs.serverListURL().isEmpty())
        autoUpdateServerList();

    // 2. Create ServerConnect
    m_serverConnect = std::make_unique<ServerConnect>(*m_serverList);
    theApp.serverConnect = m_serverConnect.get();

    // 3. Build config from preferences
    ServerConnectConfig cfg;
    cfg.safeServerConnect     = thePrefs.safeServerConnect();
    cfg.autoConnectStaticOnly = thePrefs.autoConnectStaticOnly();
    cfg.useServerPriorities   = thePrefs.useServerPriorities();
    cfg.reconnectOnDisconnect = thePrefs.reconnect();
    cfg.addServersFromServer  = thePrefs.addServersFromServer();
    cfg.cryptLayerPreferred   = thePrefs.cryptLayerRequested();
    cfg.cryptLayerRequired    = thePrefs.cryptLayerRequired();
    cfg.cryptLayerEnabled     = thePrefs.cryptLayerSupported();
    cfg.serverKeepAliveTimeout = thePrefs.serverKeepAliveTimeout();
    cfg.userHash              = thePrefs.userHash();
    cfg.userNick              = thePrefs.nick();
    cfg.listenPort            = thePrefs.port();
    cfg.smartLowIdCheck       = thePrefs.smartLowIdCheck();
    m_serverConnect->setConfig(cfg);

    // 4. Wire SharedFileList to ServerConnect for shared-file announcements
    if (theApp.sharedFileList)
        theApp.sharedFileList->setServerConnect(theApp.serverConnect);

    // 5. Auto-connect to a server if enabled
    if (thePrefs.autoConnect()) {
        logInfo(QStringLiteral("Auto-connecting to eD2K server..."));
        m_serverConnect->connectToAnyServer();
    }
}

// ---------------------------------------------------------------------------
// autoUpdateServerList — download server.met from configured URL and merge
// ---------------------------------------------------------------------------

void CoreSession::autoUpdateServerList()
{
    const QString url = thePrefs.serverListURL();
    logInfo(QStringLiteral("Auto-updating server list from %1").arg(url));

    QNetworkAccessManager nam;
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(10000); // 10 seconds

    auto* reply = nam.get(request);

    // Block briefly with event loop (same pattern as MFC's modal download)
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit); // safety timeout
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        logWarning(QStringLiteral("Failed to download server list: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (data.isEmpty()) {
        logWarning(QStringLiteral("Downloaded server list is empty"));
        return;
    }

    // Save to temp file and merge
    const QString downloadPath = QDir(thePrefs.configDir()).filePath(
        QStringLiteral("server_met.download"));
    QFile file(downloadPath);
    if (!file.open(QIODevice::WriteOnly)) {
        logWarning(QStringLiteral("Failed to save downloaded server list"));
        return;
    }
    file.write(data);
    file.close();

    const size_t before = m_serverList->serverCount();
    m_serverList->addServerMetToList(downloadPath, true);
    const size_t added = m_serverList->serverCount() - before;
    logInfo(QStringLiteral("Server list updated: %1 new servers added").arg(added));

    QFile::remove(downloadPath);
}

// ---------------------------------------------------------------------------
// shutdownServerConnect — disconnect, save, and release
// ---------------------------------------------------------------------------

void CoreSession::shutdownServerConnect()
{
    if (m_serverConnect) {
        if (m_serverConnect->isConnected() || m_serverConnect->isConnecting())
            m_serverConnect->disconnect();
    }

    // Save server.met
    if (m_serverList) {
        const QString serverMetPath = QDir(thePrefs.configDir()).filePath(
            QStringLiteral("server.met"));
        m_serverList->saveServerMet(serverMetPath);
    }

    // Unwire SharedFileList
    if (theApp.sharedFileList && theApp.serverConnect == m_serverConnect.get())
        theApp.sharedFileList->setServerConnect(nullptr);

    // Clear theApp pointers before destroying
    if (m_serverConnect && theApp.serverConnect == m_serverConnect.get())
        theApp.serverConnect = nullptr;
    if (m_serverList && theApp.serverList == m_serverList.get())
        theApp.serverList = nullptr;

    m_serverConnect.reset();
    m_serverList.reset();
}

// ---------------------------------------------------------------------------
// initClientInfra — create ClientList and ListenSocket
// ---------------------------------------------------------------------------

void CoreSession::initClientInfra()
{
    // Create and load IP filter
    if (!theApp.ipFilter) {
        m_ipFilter = std::make_unique<IPFilter>();
        int count = m_ipFilter->loadFromDefaultFile(thePrefs.configDir());
        theApp.ipFilter = m_ipFilter.get();
        if (count > 0)
            logInfo(QStringLiteral("IP filter loaded: %1 entries").arg(count));
    }

    if (!theApp.clientList) {
        m_clientList = std::make_unique<ClientList>(this);
        theApp.clientList = m_clientList.get();
    }

    if (!theApp.friendList) {
        m_friendList = std::make_unique<FriendList>(this);
        m_friendList->load(thePrefs.configDir());
        theApp.friendList = m_friendList.get();
    }

    if (!theApp.listenSocket) {
        m_listenSocket = std::make_unique<ListenSocket>(this);
        if (m_listenSocket->startListening(thePrefs.port())) {
            theApp.listenSocket = m_listenSocket.get();
            logInfo(QStringLiteral("TCP listen socket bound on port %1")
                        .arg(m_listenSocket->connectedPort()));
        } else {
            logWarning(QStringLiteral("Failed to bind TCP listen socket on port %1")
                           .arg(thePrefs.port()));
            m_listenSocket.reset();
        }
    }

    if (theApp.listenSocket && theApp.clientList) {
        connect(theApp.listenSocket, &ListenSocket::newClientConnection,
                theApp.clientList, &ClientList::handleIncomingConnection);
    }
}

// ---------------------------------------------------------------------------
// shutdownClientInfra — release ClientList and ListenSocket
// ---------------------------------------------------------------------------

void CoreSession::shutdownClientInfra()
{
    if (m_friendList) {
        m_friendList->save(thePrefs.configDir());
        if (theApp.friendList == m_friendList.get())
            theApp.friendList = nullptr;
        m_friendList.reset();
    }

    if (m_listenSocket && theApp.listenSocket == m_listenSocket.get())
        theApp.listenSocket = nullptr;
    m_listenSocket.reset();

    if (m_clientList && theApp.clientList == m_clientList.get())
        theApp.clientList = nullptr;
    m_clientList.reset();

    if (m_ipFilter) {
        theApp.ipFilter = nullptr;
        m_ipFilter.reset();
    }
}

// ---------------------------------------------------------------------------
// initKademlia — create and start Kademlia if enabled
// ---------------------------------------------------------------------------

void CoreSession::initKademlia()
{
    if (!thePrefs.kadEnabled() || m_kademlia)
        return;

    // 1. Create and bind the shared UDP socket (client + Kad traffic).
    m_clientUDP = std::make_unique<ClientUDPSocket>();
    const uint16 udpPort = static_cast<uint16>(thePrefs.udpPort());
    if (!m_clientUDP->rebind(udpPort)) {
        logError(QStringLiteral("Failed to bind client UDP socket on port %1 — Kademlia disabled")
                     .arg(udpPort));
        m_clientUDP.reset();
        return;
    }
    theApp.clientUDP = m_clientUDP.get();
    logInfo(QStringLiteral("Client UDP socket bound on port %1").arg(udpPort));

    // 2. Create and start Kademlia (no internal socket binding).
    m_kademlia = std::make_unique<kad::Kademlia>();
    kad::Kademlia::setClientList(theApp.clientList);

    // Wire Kad keyword result callback → SearchList
    kad::Kademlia::setKadKeywordResultCallback(
        [](uint32 searchID, const uint8* fileHash, const QString& name,
           uint64 size, const QString& type, uint32 sources, uint32 completeSources) {
            if (theApp.searchList)
                theApp.searchList->addKadKeywordResult(searchID, fileHash, name, size,
                                                       type, sources, completeSources);
        });

    m_kademlia->start();

    if (!m_kademlia->isRunning()) {
        m_kademlia.reset();
        theApp.clientUDP = nullptr;
        m_clientUDP.reset();
        return;
    }

    auto* listener = m_kademlia->getUDPListener();
    auto* udp = m_clientUDP.get();

    // 3. Receive bridge: ClientUDPSocket → KademliaUDPListener
    //    Reconstruct [opcode][payload] buffer for processPacket().
    connect(udp, &ClientUDPSocket::kadPacketReceived,
        listener, [listener](uint8 opcode, const uint8* data, uint32 size,
                             uint32 senderIP, uint16 senderPort,
                             bool validReceiverKey, uint32 receiverVerifyKey) {
            QByteArray buf(1 + static_cast<qsizetype>(size), Qt::Uninitialized);
            buf[0] = static_cast<char>(opcode);
            if (size > 0)
                std::memcpy(buf.data() + 1, data, size);
            listener->processPacket(reinterpret_cast<const uint8*>(buf.constData()),
                                    static_cast<uint32>(buf.size()),
                                    senderIP, senderPort,
                                    validReceiverKey,
                                    kad::KadUDPKey(receiverVerifyKey, senderIP));
        });

    // 4. Send bridge: KademliaUDPListener → ClientUDPSocket
    //    Build a Packet from the raw [opcode][payload] and queue it for sending.
    connect(listener, &kad::KademliaUDPListener::packetToSend,
        udp, [udp](QByteArray data, uint32 destIP, uint16 destPort,
                    kad::KadUDPKey targetKey, kad::UInt128 cryptTargetID) {
            if (data.isEmpty())
                return;

            auto pkt = std::make_unique<Packet>(OP_KADEMLIAHEADER);
            pkt->opcode = static_cast<uint8>(data[0]);
            if (data.size() > 1) {
                pkt->size = static_cast<uint32>(data.size() - 1);
                pkt->pBuffer = new char[pkt->size];
                std::memcpy(pkt->pBuffer, data.constData() + 1, pkt->size);
            }

            // Determine encryption parameters
            const uint8* targetHash = nullptr;
            uint32 receiverVerifyKey = 0;
            bool hasTarget = !(cryptTargetID == kad::UInt128());
            if (hasTarget) {
                targetHash = cryptTargetID.getData();
            } else {
                auto* prefs = kad::Kademlia::getInstancePrefs();
                receiverVerifyKey = targetKey.getKeyValue(
                    prefs ? prefs->ipAddress() : 0);
            }

            udp->sendPacket(std::move(pkt), destIP, destPort,
                            hasTarget || (receiverVerifyKey != 0),
                            hasTarget ? targetHash : nullptr,
                            true, receiverVerifyKey);
        });

    logInfo(QStringLiteral("Kademlia started."));
}

// ---------------------------------------------------------------------------
// shutdownKademlia — stop and destroy Kademlia
// ---------------------------------------------------------------------------

void CoreSession::shutdownKademlia()
{
    kad::Kademlia::setKadKeywordResultCallback(nullptr);
    kad::Kademlia::setClientList(nullptr);
    if (m_kademlia)
        m_kademlia->stop();
    m_kademlia.reset();

    if (theApp.clientUDP == m_clientUDP.get())
        theApp.clientUDP = nullptr;
    m_clientUDP.reset();
}

// ---------------------------------------------------------------------------
// initSearch — create SearchList
// ---------------------------------------------------------------------------

void CoreSession::initSearch()
{
    if (!theApp.searchList) {
        m_searchList = std::make_unique<SearchList>(this);
        theApp.searchList = m_searchList.get();
    }
}

// ---------------------------------------------------------------------------
// shutdownSearch — release SearchList
// ---------------------------------------------------------------------------

void CoreSession::shutdownSearch()
{
    if (m_searchList && theApp.searchList == m_searchList.get())
        theApp.searchList = nullptr;
    m_searchList.reset();
}

// ---------------------------------------------------------------------------
// initScheduler — create Scheduler, load schedules, save originals
// ---------------------------------------------------------------------------

void CoreSession::initScheduler()
{
    if (m_scheduler)
        return;

    m_scheduler = std::make_unique<Scheduler>(this);
    theApp.scheduler = m_scheduler.get();

    if (theApp.downloadQueue)
        m_scheduler->setDownloadQueue(theApp.downloadQueue);

    int loaded = m_scheduler->loadFromFile(thePrefs.configDir());
    if (loaded > 0)
        logInfo(QStringLiteral("Loaded %1 scheduler entries").arg(loaded));

    m_scheduler->saveOriginals();
}

// ---------------------------------------------------------------------------
// shutdownScheduler — restore originals, save, release
// ---------------------------------------------------------------------------

void CoreSession::shutdownScheduler()
{
    if (!m_scheduler)
        return;

    m_scheduler->restoreOriginals();
    m_scheduler->saveToFile(thePrefs.configDir());
    theApp.scheduler = nullptr;
    m_scheduler.reset();
}

// ---------------------------------------------------------------------------
// initUPnP — create and start UPnP port mapping if enabled
// ---------------------------------------------------------------------------

void CoreSession::initUPnP()
{
    if (!thePrefs.enableUPnP() || m_upnpManager)
        return;

    m_upnpManager = std::make_unique<UPnPManager>(this);
    theApp.upnpManager = m_upnpManager.get();

    connect(m_upnpManager.get(), &UPnPManager::discoveryComplete,
            this, [](bool success) {
        if (success)
            logInfo(QStringLiteral("UPnP: port mapping successful"));
        else
            logWarning(QStringLiteral("UPnP: port mapping failed — check router settings"));
    });

    m_upnpManager->startDiscovery(
        static_cast<uint16>(thePrefs.port()),
        static_cast<uint16>(thePrefs.udpPort()));

    logInfo(QStringLiteral("UPnP: discovery started for TCP %1 / UDP %2")
                .arg(thePrefs.port()).arg(thePrefs.udpPort()));
}

// ---------------------------------------------------------------------------
// shutdownUPnP — remove port mappings and release manager
// ---------------------------------------------------------------------------

void CoreSession::shutdownUPnP()
{
    if (!m_upnpManager)
        return;

    if (thePrefs.closeUPnPOnExit())
        m_upnpManager->deletePorts();

    if (theApp.upnpManager == m_upnpManager.get())
        theApp.upnpManager = nullptr;
    m_upnpManager.reset();
}

} // namespace eMule
