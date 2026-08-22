#include "pch.h"
/// @file CoreSession.cpp
/// @brief Lightweight timer driver — calls process() on core managers.
///
/// Creates and wires the upload pipeline components on start().

#include "app/CoreSession.h"
#include "app/AppContext.h"
#include "ipfilter/IPFilter.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "files/CollectionKeys.h"
#include "client/UpDownClient.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "friends/FriendList.h"
#include "files/SharedFileList.h"
#include "kademlia/Kademlia.h"
#include "search/GlobalSearchScheduler.h"
#include "search/SearchList.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUDPListener.h"
#include "net/Address.h"
#include "net/ClientUDPSocket.h"
#include "net/HttpFileDownload.h"
#include "net/ListenSocket.h"
#include "net/LocalIPv6.h"
#include "net/UDPSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "stats/StatsHistory.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "stats/StatsSnapshot.h"
#include "transfer/DownloadQueue.h"
#include "transfer/Scheduler.h"
#include "net/LastCommonRouteFinder.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadDiskIOThread.h"
#include "httpcache/HttpCacheManager.h"
#include "transfer/UploadQueue.h"
#include "transfer/UploadQueueStore.h"
#include "portmap/PortMapper.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTimer>


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
    // Before every shutdownXxx(): shutdownClientInfra() destroys ClientCredits, and the
    // waiting clients point straight at those objects to compute their wait times. Saving
    // any later than this reads freed memory.
    saveUploadQueueStore();
    stopWorkerThreads();
    shutdownScheduler();
    shutdownPortMapper();
    shutdownKademlia();
    shutdownClientUDP();
    shutdownServerConnect();
    shutdownDownloadQueue();
    shutdownClientInfra();
    shutdownUSS();
    shutdownUploadPipeline();
    shutdownSearch();
    shutdownStatistics();
}

void CoreSession::start()
{
    logInfo(QStringLiteral("Starting core — TCP port %1, UDP port %2")
                .arg(thePrefs.port()).arg(thePrefs.udpPort()));
    // Before initClientUDP (obfuscation key material), initServerConnect (CT_MOD_IP_V6
    // in the login packet) and initKademlia — all three read theApp.publicIPv6().
    initLocalIPv6();
    initStatistics();
    initUploadPipeline();
    initUSS();
    initClientInfra();
    initDownloadQueue();
    initSearch();
    initClientUDP();
    initServerConnect();
    initKademlia();
    initPortMapper();
    initScheduler();
    m_tickCounter = 0;
    m_timer.start();
}

void CoreSession::stop()
{
    m_timer.stop();
}

void CoreSession::stopWorkerThreads()
{
    // Stop all worker threads BEFORE destroying any objects they reference.
    // The throttler thread calls sendFileAndControlData()/sendControlData() on
    // sockets; if those sockets are destroyed first we get use-after-free.
    if (m_uploadThrottler) {
        m_uploadThrottler->endThread();
        m_uploadThrottler->wait();
    }
    if (m_uploadDiskIO) {
        m_uploadDiskIO->endThread();
        m_uploadDiskIO->wait();
    }
    if (m_lastCommonRouteFinder) {
        m_lastCommonRouteFinder->endThread();
        m_lastCommonRouteFinder->wait();
    }
}

// ---------------------------------------------------------------------------
// initStatistics — create Statistics so global rates are tracked
// ---------------------------------------------------------------------------

void CoreSession::initStatistics()
{
    if (theApp.statistics)
        return;
    m_statistics = std::make_unique<Statistics>(this);
    m_statistics->init(thePrefs);
    theApp.statistics = m_statistics.get();

    // Graph history belongs to the daemon, not to whichever GUI happens to be
    // attached: it has to outlive a GUI restart and read the same for two GUIs.
    m_statsHistory = std::make_unique<StatsHistory>(this);
    theApp.statsHistory = m_statsHistory.get();
}

// ---------------------------------------------------------------------------
// shutdownStatistics — release Statistics
// ---------------------------------------------------------------------------

void CoreSession::shutdownStatistics()
{
    if (m_statsHistory && theApp.statsHistory == m_statsHistory.get())
        theApp.statsHistory = nullptr;
    m_statsHistory.reset();

    if (m_statistics && theApp.statistics == m_statistics.get())
        theApp.statistics = nullptr;
    m_statistics.reset();
}

// ---------------------------------------------------------------------------
// initUploadPipeline — create and wire upload components
// ---------------------------------------------------------------------------

QString CoreSession::uploadQueuePath()
{
    return QDir(thePrefs.configDir()).filePath(QString::fromLatin1(kUploadQueueFileName));
}

void CoreSession::saveUploadQueueStore()
{
    if (theApp.uploadQueue)
        static_cast<void>(theApp.uploadQueue->saveStoreNow(uploadQueuePath()));
}

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

    // HTTP Cache needs the upload queue to scan and the download queue to write
    // into, so it comes up last. It is always constructed — the preferences gate
    // what it does, not whether it exists, so a peer that flips the switch at
    // runtime does not need a restart.
    if (!theApp.httpCache) {
        m_httpCache = std::make_unique<HttpCacheManager>();
        theApp.httpCache = m_httpCache.get();
        m_httpCache->start();
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
    // Stop HTTP Cache first: it holds fetch clients that point into the download
    // queue and reads the upload queue on a timer, so nothing below may go away
    // while its tick can still fire.
    if (m_httpCache)
        m_httpCache->stop();
    if (m_httpCache && theApp.httpCache == m_httpCache.get())
        theApp.httpCache = nullptr;
    m_httpCache.reset();

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
    if (m_knownFileList)
        m_knownFileList->save();
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
        if (theApp.uploadQueue) {
            // One-shot load once ED2K or Kad is up, then a 10-minute autosave. Placed after
            // sharedFileList->process() because a restored waiter is dropped unless the file
            // it queued for is currently shared.
            theApp.uploadQueue->processStore(uploadQueuePath());
        }
        if (theApp.serverList) {
            const QString serverMetPath = QDir(thePrefs.configDir()).filePath(
                QStringLiteral("server.met"));
            theApp.serverList->process(serverMetPath);  // auto-save every 17 min (#28)
        }
        if (theApp.statistics) {
            float downRate = (theApp.downloadQueue && theApp.downloadQueue->hasActiveTransfers())
                ? static_cast<float>(theApp.downloadQueue->datarate()) / 1024.0f : 0.0f;
            float upRate = (theApp.uploadQueue && theApp.uploadQueue->hasActiveUploads())
                ? static_cast<float>(theApp.uploadQueue->datarate()) / 1024.0f : 0.0f;
            theApp.statistics->updateConnectionStats(upRate, downRate);
            theApp.statistics->compUpDatarateOverhead();
            theApp.statistics->compDownDatarateOverhead();

            // After the rates are recomputed, so a sample never carries last
            // second's figures. MFC drives its graphs from the same ladder
            // (srchybrid/UploadQueue.cpp:939-951).
            if (theApp.statsHistory)
                theApp.statsHistory->sample(
                    static_cast<uint32>(QDateTime::currentSecsSinceEpoch()));
        }
        if (theApp.clientList)
            theApp.clientList->process();
        if (theApp.serverConnect) {
            theApp.serverConnect->checkForTimeout();
            theApp.serverConnect->keepConnectionAlive();
        }
        if (theApp.scheduler && thePrefs.schedulerEnabled())
            theApp.scheduler->check();

        // Server stat ping — one server per UDPSERVERSTATTIME (5s); each server
        // is re-asked at most every UDPSERVSTATREASKTIME (4.5h). This is what
        // populates serverKeyUDP / obfuscation ports for non-connected servers.
        if (theApp.serverList && theApp.serverConnect
            && theApp.serverConnect->isUDPSocketAvailable()
            && m_tickCounter % (UDPSERVERSTATTIME / 100) == 0) {
            theApp.serverList->serverStats();
        }

        // No port-mapping work on the tick ladder. Renewal is driven by one
        // timer per mapping, sized from the lifetime the router actually
        // granted. The old 30 s checkAndRefresh() ran synchronous SOAP right
        // here, so an unreachable router stalled the whole event loop.

        // Re-scan local IPv6 every ~5 min (3000 ticks). Without this the only triggers are
        // startup and a server connect, so a prefix renumber or a privacy-address rotation
        // goes unnoticed for as long as the session stays connected — and we keep
        // advertising an address we no longer hold. updatePublicIPv6() funnels into
        // noteEffectiveIPv6Change(), which is a no-op when the effective address is
        // unchanged, so the common case costs one interface scan.
        if (m_tickCounter % 3000 == 0)
            updatePublicIPv6(scanLocalIPv6());

        // Bank the cumulative statistics periodically, so an unclean exit costs at
        // most one interval instead of the whole session. MFC does the same from
        // its upload timer (srchybrid/UploadQueue.cpp:1021). The flush is absolute
        // rather than additive, so repeating it changes nothing.
        if (const uint32 interval = thePrefs.statsSaveInterval();
            interval > 0 && theApp.statistics
            && m_tickCounter - m_lastStatsFlushTick >= interval * 10) {
            m_lastStatsFlushTick = m_tickCounter;
            flushCumulativeStats(thePrefs);
            thePrefs.save();
        }
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
                if (!srv->ipAddress().isNull())
                    ips.push_back(srv->ipAddress().toNetworkUint32());
            }
        }
        if (theApp.clientList) {
            theApp.clientList->forEachClient([&ips](UpDownClient* c) {
                uint32 ip = c->connectAddress().toNetworkUint32();
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
    p.minUpload = thePrefs.minUpload();  // KB/s — setPrefs() converts to bytes/s
    // maxUploadLimit() already maps "no limit" (0) onto the UNLIMITED sentinel USSParams uses.
    p.maxUpload = thePrefs.maxUploadLimit();
    p.curUpload = (p.maxUpload == UNLIMITED) ? UNLIMITED : p.maxUpload * 1024;
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
        if (m_serverList->loadServerMet(serverMetPath)) {
            if (m_serverList->serverCount() == 0)
                logWarning(QStringLiteral("server.met loaded but contains 0 servers"));
            else
                logInfo(QStringLiteral("Loaded %1 servers from server.met")
                            .arg(m_serverList->serverCount()));
        } else {
            logWarning(QStringLiteral("Failed to load server.met at %1").arg(serverMetPath));
        }
    } else {
        logWarning(QStringLiteral("No server.met found at %1 — server list is empty").arg(serverMetPath));
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
    cfg.emuleVersionTag       = (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17)
                              | (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10)
                              | (static_cast<uint32>(SEND_EMULE_VERSION_UPD) <<  7);
    m_serverConnect->setConfig(cfg);

    // 4. Wire SharedFileList and DownloadQueue to ServerConnect
    if (theApp.sharedFileList)
        theApp.sharedFileList->setServerConnect(theApp.serverConnect);
    if (theApp.downloadQueue)
        theApp.downloadQueue->setServerConnect(theApp.serverConnect);

    // 5. Create and bind server UDP socket — skip when serverUDPPort==0 (disabled).
    //    Matches MFC CServerConnect (srchybrid/ServerConnect.cpp:466).
    if (thePrefs.serverUDPPort() != 0) {
        m_serverUDP = std::make_unique<UDPSocket>(this);
        if (!m_serverUDP->create())
            logWarning(QStringLiteral("Failed to bind server UDP socket"));
        m_serverConnect->setUDPSocket(m_serverUDP.get());
    }

    // 6. Wire TCP search results → SearchList
    connect(m_serverConnect.get(), &ServerConnect::searchResultReceived,
            this, [](const uint8* data, uint32 size, bool /*moreResults*/) {
                if (!theApp.searchList)
                    return;
                auto* srv = theApp.serverConnect ? theApp.serverConnect->currentServer() : nullptr;
                theApp.searchList->processSearchAnswer(data, size, true,
                    srv ? srv->ipAddress().toNetworkUint32() : 0, srv ? srv->port() : 0);

                // The local server has answered — a global search may now start
                // walking the rest of the list. MFC: CSearchResultsWnd::LocalEd2kSearchEnd
                // (srchybrid/SearchResultsWnd.cpp:455-470).
                if (theApp.globalSearch)
                    theApp.globalSearch->onLocalAnswerReceived();
            });

    // Losing the server connection ends a running global search, as in MFC
    // CServerConnect::ConnectionFailed (srchybrid/ServerConnect.cpp:346).
    connect(m_serverConnect.get(), &ServerConnect::disconnectedFromServer,
            this, [] {
                if (theApp.globalSearch)
                    theApp.globalSearch->cancel();
            });

    // 7. Wire UDP global search results → SearchList
    connect(m_serverUDP.get(), &UDPSocket::globalSearchResult,
            this, [](const uint8* data, uint32 size, const Endpoint& server) {
                if (theApp.searchList) {
                    uint32 ip = server.address().toNetworkUint32();
                    // The answer arrives from the server's UDP port (TCP+4); the
                    // SearchFile must record the real TCP port so it keys against
                    // the server-list entry. MFC: CUDPSocket::ProcessPacket() —
                    // UDPSocket.cpp:237 (passes nUDPPort - 4).
                    uint16 port = static_cast<uint16>(server.port() - 4);
                    theApp.searchList->processUDPSearchAnswer(data, size, true, ip, port);
                }
            });

    // 8. Wire UDP server status replies → ServerList
    // Carries the server's UDP obfuscation key and ports, which we need before
    // global UDP search can be obfuscated against that server.
    connect(m_serverUDP.get(), &UDPSocket::serverStatusResult,
            this, [](const uint8* data, uint32 size, const Endpoint& server) {
                if (theApp.serverList)
                    theApp.serverList->processStatusResponse(data, size, server);
            });

    // 8b. Wire UDP global-source replies (OP_GLOBFOUNDSOURCES) → DownloadQueue.
    // Sources are attributed to the answering server (the `from` endpoint), not
    // the currently-connected one, since any queried server may reply.
    connect(m_serverUDP.get(), &UDPSocket::globalFoundSources,
            this, [](const uint8* data, uint32 size, const Endpoint& from) {
                if (theApp.downloadQueue)
                    theApp.downloadQueue->addUDPGlobalSources(data, size, from);
            });

    // 8c. Wire UDP server-description replies (OP_SERVER_DESC_RES) → ServerList,
    // refreshing name/description/version learned over UDP.
    connect(m_serverUDP.get(), &UDPSocket::serverDescResult,
            this, [](const uint8* data, uint32 size, const Endpoint& from) {
                if (theApp.serverList)
                    theApp.serverList->processDescResponse(data, size, from);
            });

    // 9. Auto-connect to a server at startup, if enabled.
    // MFC: CemuleDlg::StartConnection() — emuleDlg.cpp:1978, reached from
    // DoAutoConnect() (EmuleDlg.cpp:742). autoConnect gates *connecting*;
    // networkED2K keeps the default Kad-only profile Kad-only. The Kad arm has
    // the mirror of this in initKademlia().
    if (thePrefs.networkED2K() && thePrefs.autoConnect())
        m_serverConnect->connectToAnyServer();
}

// ---------------------------------------------------------------------------
// autoUpdateServerList — download server.met from configured URL and merge
// ---------------------------------------------------------------------------

void CoreSession::autoUpdateServerList()
{
    const QString url = thePrefs.serverListURL();
    logInfo(QStringLiteral("Auto-updating server list from %1").arg(url));

    // Deliberately blocking, same as MFC's modal download: our caller connects to a
    // server immediately afterwards, so the merge below has to have happened first.
    // Compressed mirrors are unwrapped transparently.
    HttpFileDownload::Options opts;
    opts.preferredNames = {QStringLiteral("server.met")};
    opts.timeoutMs = 10000;

    QByteArray data;
    QString entryName;
    QString error;
    if (!HttpFileDownload::getBlocking(QUrl(url), opts, data, entryName, error)) {
        logWarning(QStringLiteral("Failed to download server list: %1").arg(error));
        return;
    }

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
        m_serverConnect->setUDPSocket(nullptr);
    }
    m_serverUDP.reset();

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

    if (!theApp.clientCredits) {
        m_clientCredits = std::make_unique<ClientCreditsList>();
        const QString creditsPath = QDir(thePrefs.configDir()).filePath(
            QStringLiteral("clients.met"));
        if (QFile::exists(creditsPath))
            m_clientCredits->loadList(creditsPath);
        theApp.clientCredits = m_clientCredits.get();
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

    // Initialize collection signing keys
    if (!m_collectionKeys) {
        m_collectionKeys = std::make_unique<CollectionKeys>(thePrefs.configDir());
        m_collectionKeys->initialize();
    }
}

// ---------------------------------------------------------------------------
// shutdownClientInfra — release ClientList and ListenSocket
// ---------------------------------------------------------------------------

void CoreSession::shutdownClientInfra()
{
    if (m_clientCredits) {
        const QString creditsPath = QDir(thePrefs.configDir()).filePath(
            QStringLiteral("clients.met"));
        m_clientCredits->saveList(creditsPath);
        if (theApp.clientCredits == m_clientCredits.get())
            theApp.clientCredits = nullptr;
        m_clientCredits.reset();
    }

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
// initDownloadQueue — create DownloadQueue and load existing .part files
// ---------------------------------------------------------------------------

void CoreSession::initDownloadQueue()
{
    if (theApp.downloadQueue)
        return;

    m_downloadQueue = std::make_unique<DownloadQueue>(this);
    theApp.downloadQueue = m_downloadQueue.get();

    // Wire dependencies
    m_downloadQueue->setSharedFileList(theApp.sharedFileList);
    m_downloadQueue->setKnownFileList(theApp.knownFileList);
    m_downloadQueue->setIPFilter(theApp.ipFilter);
    m_downloadQueue->setClientList(theApp.clientList);
    // No setServerConnect() here: initServerConnect() runs after us and creates it,
    // so the pointer would still be null. It wires the queue itself (step 4).

    // The "Source Lists" subfolder must exist before the first save (MorphXT does this in
    // CPreferences::Init, Preferences.cpp:1084-1092). A category can still point a download
    // at some other directory, which is why SourceListFile::write() also creates it.
    if (thePrefs.useSaveLoadSources())
        SourceSaver::ensureDirectories(thePrefs.tempDirs());

    // Load existing .part files from configured temp directories
    m_downloadQueue->init(thePrefs.tempDirs());

    logInfo(QStringLiteral("DownloadQueue initialized — %1 files loaded")
                .arg(m_downloadQueue->fileCount()));
}

// ---------------------------------------------------------------------------
// shutdownDownloadQueue — save state and release DownloadQueue
// ---------------------------------------------------------------------------

void CoreSession::shutdownDownloadQueue()
{
    // Save all active downloads before destruction (matches MFC EmuleDlg::OnClose)
    if (m_downloadQueue) {
        const bool saveSources = thePrefs.useSaveLoadSources();
        for (auto* file : m_downloadQueue->files()) {
            if (file->status() != PartFileStatus::Complete) {
                file->flushBuffer();
                file->savePartFile();
                // Capture the live source list now: the 10-minute timer would otherwise
                // leave up to ten minutes of connections unrecorded, and the PartFile
                // destructor clears m_srcList before anything else could read it.
                if (saveSources)
                    file->sourceSaver().saveNow(file);
            }
        }
    }

    if (m_downloadQueue && theApp.downloadQueue == m_downloadQueue.get())
        theApp.downloadQueue = nullptr;
    m_downloadQueue.reset();
}

// ---------------------------------------------------------------------------
// initClientUDP — create the shared client UDP socket and wire its non-Kad
// handlers. Always created (peer reasks, firewalled callbacks and the port
// test all need it), independent of the Kad/autoConnect gate.
// MFC: clientudp is created in the app ctor (Emule.cpp:610) and Create()d at
// boot regardless of autoconnect (EmuleDlg.cpp:725).
// ---------------------------------------------------------------------------

void CoreSession::initClientUDP()
{
    if (m_clientUDP)
        return;

    // Create and bind the shared UDP socket (client + Kad traffic).
    m_clientUDP = std::make_unique<ClientUDPSocket>();
    const uint16 udpPort = static_cast<uint16>(thePrefs.udpPort());
    if (!m_clientUDP->rebind(udpPort)) {
        logError(QStringLiteral("Failed to bind client UDP socket on port %1")
                     .arg(udpPort));
        m_clientUDP.reset();
        return;
    }
    theApp.clientUDP = m_clientUDP.get();
    logInfo(QStringLiteral("Client UDP socket bound on port %1").arg(udpPort));

    // Wire client UDP reask signals to upload queue
    if (theApp.uploadQueue) {
        connect(m_clientUDP.get(), &ClientUDPSocket::reaskFilePingReceived,
                theApp.uploadQueue, &UploadQueue::onReaskFilePing);
    }

    // Wire download-side UDP reask response signals.
    // MFC handles these in CClientUDPSocket::ProcessPacket (srchybrid/ClientUDPSocket.cpp:324-355).
    // The Qt ClientUDPSocket emits signals but they were never connected.

    // OP_REASKACK — remote source confirms our queue position via UDP.
    connect(m_clientUDP.get(), &ClientUDPSocket::reaskAckReceived,
        this, [](const Endpoint& senderEP, const uint8* data, uint32 size) {
            if (!theApp.clientList)
                return;
            auto* sender = theApp.clientList->findByEndpoint_UDP(senderEP.address(),
                                                                 senderEP.port());
            if (!sender || !sender->reaskPending())
                return;
            SafeMemFile io(data, size);
            // If UDPv4+, response contains part status first
            if (sender->udpVer() > 3 && sender->reqFile())
                sender->processFileStatus(true, io, sender->reqFile());
            uint16 rank = io.readUInt16();
            sender->setRemoteQueueFull(false);
            sender->udpReaskACK(rank);
            sender->incDownAskedCount();
        });

    // OP_FILENOTFOUND — remote source no longer has the file.
    connect(m_clientUDP.get(), &ClientUDPSocket::fileNotFoundReceived,
        this, [](const Endpoint& senderEP) {
            if (!theApp.clientList)
                return;
            auto* sender = theApp.clientList->findByEndpoint_UDP(senderEP.address(),
                                                                 senderEP.port());
            if (sender && sender->reaskPending())
                sender->udpReaskFNF(); // may delete sender
        });

    // OP_QUEUEFULL — remote source's upload queue is full.
    connect(m_clientUDP.get(), &ClientUDPSocket::queueFullReceived,
        this, [](const Endpoint& senderEP) {
            if (!theApp.clientList)
                return;
            auto* sender = theApp.clientList->findByEndpoint_UDP(senderEP.address(),
                                                                 senderEP.port());
            if (sender && sender->reaskPending()) {
                sender->setRemoteQueueFull(true);
                sender->udpReaskACK(0);
            }
        });

    // OP_REASKCALLBACKUDP — firewalled client asks us to relay reask to our buddy.
    // MFC: srchybrid/ClientUDPSocket.cpp:201-224
    connect(m_clientUDP.get(), &ClientUDPSocket::reaskCallbackReceived,
        this, [](const Endpoint& senderEP, const uint8* data, uint32 size) {
            if (!theApp.clientList)
                return;
            auto* buddy = theApp.clientList->getBuddy();
            if (!buddy || !buddy->socket() || size < 17)
                return;
            // First 16 bytes = buddy ID that must match our buddy
            if (!md4equ(data, buddy->buddyID()))
                return;
            // Strip the 16-byte buddy ID and prepend the requester's address, producing the
            // OP_REASKCALLBACKTCP body processReaskCallbackTCP expects:
            //
            //   IPv4:  <ip 4><port 2><rest>                            header 6
            //   IPv6:  <0xFFFFFFFF 4><ipv6 16><port 2><rest>           header 22
            //
            // Stock builds the IPv4 form by poking the address over buddy-ID bytes 10-15 and
            // slicing from 10, which works only because 6 bytes happen to fit. There is no
            // room for 16, so both forms are rebuilt here instead; the IPv4 bytes come out
            // identical to the in-place version. The 0xFFFFFFFF sentinel and the trailing
            // 16-byte address are the compatibility target's layout, so its buddies and ours
            // relay for each other.
            //
            // Network byte order for the IPv4 field: the buddy forwards these bytes verbatim
            // and processReaskCallbackTCP reads them as an ED2K wire IP. Writing host order
            // here made the two ends of the relay disagree about the requester's address.
            constexpr uint32 kBuddyIdLen = 16;
            const Address& requester = senderEP.address();
            const bool useIPv6 = requester.isIPv6();
            const uint32 headerLen = useIPv6 ? (4u + 16u + 2u) : 6u;
            const uint32 tailLen = size - kBuddyIdLen;
            const uint32 relaySize = tailLen + headerLen;

            auto packet = std::make_unique<Packet>(OP_REASKCALLBACKTCP, relaySize, OP_EMULEPROT);
            auto* out = reinterpret_cast<uint8*>(packet->pBuffer);
            if (useIPv6) {
                pokeUInt32(out, IPV6_SOURCE_SENTINEL);
                std::memcpy(out + 4, requester.ipv6Bytes().data(), 16);
                pokeUInt16(out + 20, senderEP.port());
            } else {
                pokeUInt32(out, requester.toNetworkUint32());
                pokeUInt16(out + 4, senderEP.port());
            }
            std::memcpy(out + headerLen, data + kBuddyIdLen, tailLen);
            buddy->sendPacket(std::move(packet));
        });

    // Direct callback: remote firewalled client asks us to connect back via UDP.
    // The handler self-guards on Kad running + firewalled, so it is safe to wire
    // even when Kad has not been started yet.
    connect(m_clientUDP.get(), &ClientUDPSocket::directCallbackReceived,
            this, &CoreSession::handleDirectCallbackRequest);

    // UDP port test → send reply on TCP port-test connection.
    connect(m_clientUDP.get(), &ClientUDPSocket::portTestReceived,
        this, [this](const Endpoint&) {
            if (m_listenSocket)
                m_listenSocket->sendPortTestReply('1', true);
        });
}

// ---------------------------------------------------------------------------
// handleDirectCallbackRequest — OP_DIRECTCALLBACKREQ receive handler.
// Kept as a static next to the wiring above rather than inline in it, so the
// guard ladder is reachable from a test without a whole CoreSession.
// ---------------------------------------------------------------------------

void CoreSession::handleDirectCallbackRequest(const Endpoint& senderEP,
                                              const uint8* data, uint32 size)
{
    if (!theApp.clientList)
        return;
    // Only accept if we're firewalled and Kad is running
    auto* kadInst = kad::Kademlia::instance();
    if (!kadInst || !kadInst->isRunning() || !kadInst->isFirewalled())
        return;
    // tcpPort(2) + userHash(16) + connectOptions(1) = 19 bytes minimum
    if (size < 19)
        return;

    SafeMemFile io(data, size);
    uint16 tcpPort = io.readUInt16();
    uint8 userHash[16];
    io.readHash16(userHash);
    uint8 connectOptions = io.readUInt8();

    // Everything here is keyed on the full sender Address. The previous code
    // projected it through toNetworkUint32(), which is 0 for IPv6: the lookup then
    // matched on hash alone and, worse, the else-branch overwrote a good connect
    // address with a null one. It also passed the sender in the ctor's *serverIP*
    // slot with userId = 0, so the address landed in m_serverAddress and
    // m_connectAddress was never set at all — an IPv4 bug of its own.
    const Address& senderAddr = senderEP.address();
    if (!isGoodIP(senderAddr) || theApp.clientList->isBannedClient(senderAddr))
        return;
    if (theApp.ipFilter && theApp.ipFilter->isFiltered(senderAddr))
        return;

    auto* client = theApp.clientList->findByEndpoint_UDP(senderAddr, senderEP.port());
    if (!client)
        client = theApp.clientList->findByAddress(senderAddr, tcpPort);

    if (!client) {
        client = new UpDownClient(tcpPort, 0, 0, 0, nullptr);
        client->setUserHash(userHash);
        // setUserAddress fills m_userAddress *and* m_connectAddress; the former is
        // what findByEndpoint_UDP matches, so the next datagram finds this client.
        client->setUserAddress(senderAddr);
        if (senderAddr.isIPv6()) {
            client->setUserIPv6(senderAddr);
            client->setOpenIPv6(true);
        }
        theApp.clientList->addClient(client);
    } else {
        client->setConnectAddress(senderAddr);
        client->setUserPort(tcpPort);
        if (senderAddr.isIPv6()) {
            client->setUserIPv6(senderAddr);
            client->setOpenIPv6(true);
        }
    }
    client->setConnectOptions(connectOptions, true, false);
    client->tryToConnect();
}

// ---------------------------------------------------------------------------
// shutdownClientUDP — release the shared client UDP socket. Called after
// shutdownKademlia() so Kad has already stopped using the socket.
// ---------------------------------------------------------------------------

void CoreSession::shutdownClientUDP()
{
    if (theApp.clientUDP == m_clientUDP.get())
        theApp.clientUDP = nullptr;
    m_clientUDP.reset();
}

// ---------------------------------------------------------------------------
// initKademlia — create Kademlia when Kad is enabled, and start it when
// autoConnect is on. Construction is independent of autoConnect so that
// Kademlia::instance() is addressable for a manual connect (GUI/IPC), matching
// MFC's always-addressable static CKademlia. The shared UDP socket is created
// separately in initClientUDP().
// ---------------------------------------------------------------------------

void CoreSession::initKademlia()
{
    // Construct Kademlia regardless of the kadEnabled pref, so a manual connect
    // (GUI/IPC BootstrapKad) can start it later even when Kad is not auto-enabled.
    // The pref gates *auto*-connect only (see the auto-start check below), matching
    // MFC's always-addressable static CKademlia. Construction binds no socket.
    if (m_kademlia)
        return;

    // 1. Create Kademlia (no internal socket binding; uses m_clientUDP).
    m_kademlia = std::make_unique<kad::Kademlia>();
    kad::Kademlia::setClientList(theApp.clientList);

    // Wire Kad keyword result callback → SearchList
    kad::Kademlia::setKadKeywordResultCallback(
        [](uint32 searchID, const uint8* fileHash, const QString& name,
           uint64 size, const QString& type, uint32 sources, uint32 completeSources,
           const kad::TagList& metaTags) {
            if (theApp.searchList)
                theApp.searchList->addKadKeywordResult(searchID, fileHash, name, size,
                                                       type, sources, completeSources, metaTags);
        });

    // Wire Kad source result callback → DownloadQueue
    kad::Kademlia::setKadSourceResultCallback(
        [](uint32 searchID, const uint8* fileHash, uint32 ip, uint16 tcpPort,
           uint32 buddyIP, uint16 buddyPort, uint8 buddyCrypt,
           uint8 sourceType, const uint8* buddyHash, const uint8* clientHash,
           uint16 udpPort, const uint8* sourceIPv6, const uint8* buddyIPv6) {
            if (theApp.downloadQueue)
                theApp.downloadQueue->addKadSourceResult(
                    searchID, fileHash, ip, tcpPort,
                    buddyIP, buddyPort, buddyCrypt,
                    sourceType, buddyHash, clientHash, udpPort,
                    sourceIPv6, buddyIPv6);
        });

    // Wire Kad notes result callback. A notes search is the only Kad lookup that
    // returns filenames (and comments/ratings) for a given file hash, so this
    // populates the File Names + Comments tabs of the detail dialogs.
    //
    // Both sinks are offered the note, as in MFC
    // (srchybrid/kademlia/kademlia/Search.cpp:1015-1032): the hash may belong to a
    // download or shared file, to a plain search hit, or to both at once — the
    // user can ask for comments on a result that is not local in any way.
    kad::Kademlia::setKadNotesResultCallback(
        [](uint32 /*searchID*/, const uint8* fileHash, const uint8* publisherId,
           const QString& name, uint8 rating, const QString& comment) {
            if (theApp.searchList && publisherId) {
                const QByteArray pub(reinterpret_cast<const char*>(publisherId), 16);
                theApp.searchList->addNotes(fileHash, pub, rating, comment);
            }
            if (theApp.downloadQueue)
                theApp.downloadQueue->addKadNoteResult(
                    fileHash, publisherId, name, rating, comment);
        });

    // Re-wire UDP↔listener bridges each time Kad starts (including restarts).
    connect(m_kademlia.get(), &kad::Kademlia::started,
            this, &CoreSession::wireKadListener);

    // Start now only when Kad is enabled AND auto-connect is on. Otherwise the
    // object stays constructed and addressable via Kademlia::instance(), so a
    // manual connect from the GUI/IPC (BootstrapKad) can start it later. MFC gates
    // Start() the same way (StartConnection, emuleDlg.cpp:1983) while CKademlia
    // stays an always-addressable static. A failed start is left constructed too,
    // so a retry does not hit a null instance.
    if (thePrefs.kadEnabled() && thePrefs.autoConnect()) {
        m_kademlia->start();
        if (m_kademlia->isRunning())
            logInfo(QStringLiteral("Kademlia started."));
        else
            logWarning(QStringLiteral("Kademlia failed to start."));
    }
}

// ---------------------------------------------------------------------------
// wireKadListener — connect ClientUDPSocket ↔ KademliaUDPListener bridges
// ---------------------------------------------------------------------------
// Called each time Kademlia::started is emitted (including after reconnect).
// Old connections are automatically cleaned up when the previous listener
// was deleted in Kademlia::stop().

void CoreSession::wireKadListener()
{
    if (!m_kademlia || !m_clientUDP)
        return;

    auto* listener = m_kademlia->getUDPListener();
    auto* udp = m_clientUDP.get();
    if (!listener || !udp)
        return;

    // Receive bridge: ClientUDPSocket → KademliaUDPListener
    //    Reconstruct [opcode][payload] buffer for processPacket().
    connect(udp, &ClientUDPSocket::kadPacketReceived,
        listener, [listener](uint8 opcode, const uint8* data, uint32 size,
                             const Endpoint& sender,
                             bool validReceiverKey, uint32 senderVerifyKey) {
            uint32 senderIP = sender.address().toUint32();
            uint16 senderPort = sender.port();
            QByteArray buf(1 + static_cast<qsizetype>(size), Qt::Uninitialized);
            buf[0] = static_cast<char>(opcode);
            if (size > 0)
                std::memcpy(buf.data() + 1, data, size);
            // The contact's UDP key is the *sender* key it just gave us, bound to
            // OUR public IP — that is the address the key was minted against, so
            // getKeyValue() only releases it while our IP is unchanged. MFC
            // ClientUDPSocket.cpp:122 builds CKadUDPKey(nSenderVerifyKey, GetPublicIP()).
            listener->processPacket(reinterpret_cast<const uint8*>(buf.constData()),
                                    static_cast<uint32>(buf.size()),
                                    senderIP, senderPort,
                                    validReceiverKey,
                                    kad::KadUDPKey(senderVerifyKey, theApp.publicIP()));
        });

    // Send bridge: KademliaUDPListener → ClientUDPSocket
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
            const bool hasTarget = !(cryptTargetID == kad::UInt128());
            const uint8* targetHash = hasTarget ? cryptTargetID.getData() : nullptr;

            // The target's own key rides along on EVERY Kad packet, crypt target or
            // not — MFC KademliaUDPListener.cpp:1885 passes it unconditionally. It is
            // what the peer recognises to mark our contact IP-verified, so gating it
            // on the absence of a KadID stripped it from precisely the packets the
            // verification handshake runs over (HELLO_RES, HELLO_RES_ACK), leaving
            // every contact permanently unverified.
            const uint32 receiverVerifyKey = targetKey.getKeyValue(theApp.publicIP());

            udp->sendPacket(std::move(pkt), destIP, destPort,
                            hasTarget || (receiverVerifyKey != 0),
                            targetHash,
                            true, receiverVerifyKey);
        });
}

// ---------------------------------------------------------------------------
// shutdownKademlia — stop and destroy Kademlia
// ---------------------------------------------------------------------------

void CoreSession::shutdownKademlia()
{
    kad::Kademlia::setKadKeywordResultCallback(nullptr);
    kad::Kademlia::setKadSourceResultCallback(nullptr);
    kad::Kademlia::setClientList(nullptr);
    if (m_kademlia)
        m_kademlia->stop();
    m_kademlia.reset();
    // The shared client UDP socket is released separately in shutdownClientUDP().
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

    if (!theApp.globalSearch) {
        m_globalSearch = std::make_unique<GlobalSearchScheduler>(this);
        theApp.globalSearch = m_globalSearch.get();

        // A search that has already collected more than MAX_RESULTS hits stops
        // asking the rest of the server list — the query was too broad to be worth
        // it. tabHeaderUpdated is emitted on every batch of results.
        if (theApp.searchList) {
            connect(theApp.searchList, &SearchList::tabHeaderUpdated,
                    m_globalSearch.get(), &GlobalSearchScheduler::onResultCountChanged);
        }
    }
}

// ---------------------------------------------------------------------------
// shutdownSearch — release SearchList
// ---------------------------------------------------------------------------

void CoreSession::shutdownSearch()
{
    // The scheduler's timers reach into serverList/serverConnect, so it goes first.
    if (m_globalSearch && theApp.globalSearch == m_globalSearch.get())
        theApp.globalSearch = nullptr;
    m_globalSearch.reset();

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
// initPortMapper — race PCP / NAT-PMP / UPnP and keep the mappings alive
// ---------------------------------------------------------------------------

void CoreSession::initPortMapper()
{
    if (!thePrefs.enableUPnP() || m_portMapper)
        return;

    m_portMapper = std::make_unique<PortMapper>(this);
    theApp.portMapper = m_portMapper.get();

    m_portMapper->setEnabledMethods(thePrefs.portMapProtocols());
    m_portMapper->setLeaseSeconds(thePrefs.portMapLeaseSecs());
    m_portMapper->setPreferredMethod(
        static_cast<PortMapMethod>(thePrefs.portMapMethod()));

    connect(m_portMapper.get(), &PortMapper::statusChanged, this,
            [](PortMapStatus status) {
                logInfo(QStringLiteral("Port mapping: %1").arg(portMapStatusName(status)));
            });

    connect(m_portMapper.get(), &PortMapper::mappingChanged, this,
            [](const PortMapping& mapping, bool ok) {
                if (!ok)
                    return;
                logInfo(QStringLiteral("Port mapping: %1 port %2 -> external %3 (%4, %5s lease)")
                            .arg(portMapPurposeName(mapping.request.purpose))
                            .arg(mapping.request.internalPort)
                            .arg(mapping.externalPort)
                            .arg(portMapMethodName(mapping.method))
                            .arg(mapping.lifetimeSecs));
            });

    connect(m_portMapper.get(), &PortMapper::externalAddressChanged, this,
            [](const Address& address) {
                // Adopt the router's WAN address only when it is genuinely
                // routable and nothing better is known. setPublicIP() expires
                // the server UDP keys and drives the Kad-disagreement path, and
                // under CGNAT the IGD reports 100.64.0.0/10, which is flatly
                // wrong to publish as ours.
                if (!address.isIPv4() || !address.isPublicIP())
                    return;
                if (theApp.publicIP() != 0)
                    return;
                theApp.setPublicIP(address.toNetworkUint32());
            });

    // Remember the winner so the next run does not have to re-derive it.
    connect(m_portMapper.get(), &PortMapper::preferredMethodLearned, this,
            [](PortMapMethod method) {
                thePrefs.setPortMapMethod(static_cast<int>(method));
            });

    m_portMapper->setDesiredMappings(buildPortMapRequests());
    m_portMapper->start();
}

// ---------------------------------------------------------------------------
// shutdownPortMapper — release mappings and drop the subsystem
// ---------------------------------------------------------------------------

void CoreSession::shutdownPortMapper()
{
    if (!m_portMapper)
        return;

    m_portMapper->stop(thePrefs.closeUPnPOnExit());

    if (theApp.portMapper == m_portMapper.get())
        theApp.portMapper = nullptr;
    m_portMapper.reset();
}

// ---------------------------------------------------------------------------
// updatePortMappings — re-declare desired state after a port change
// ---------------------------------------------------------------------------

void CoreSession::updatePortMappings()
{
    if (m_portMapper)
        m_portMapper->setDesiredMappings(buildPortMapRequests());
}

// ---------------------------------------------------------------------------
// buildPortMapRequests — the mappings that should currently exist
// ---------------------------------------------------------------------------

std::vector<PortMapRequest> CoreSession::buildPortMapRequests() const
{
    std::vector<PortMapRequest> requests;

    auto add = [&requests](PortMapPurpose purpose, PortMapProtocol protocol, uint16 port,
                           const QString& description) {
        if (port == 0)
            return;
        PortMapRequest request;
        request.purpose = purpose;
        request.protocol = protocol;
        request.family = PortMapFamily::IPv4;
        request.internalPort = port;
        request.description = description;
        requests.push_back(request);

        // IPv6 has no NAT: the same port is opened as a firewall pinhole. On a
        // CGNAT line this is the only path that yields real inbound reachability,
        // so it is requested whenever we hold a routable address.
        if (thePrefs.portMapIPv6() && theApp.hasConfidentPublicIPv6()) {
            PortMapRequest v6 = request;
            v6.family = PortMapFamily::IPv6;
            v6.internalClient = theApp.publicIPv6();
            requests.push_back(v6);
        }
    };

    // Read the bound ports, not the preferences: with port 0 the OS assigns one,
    // and forwarding the configured value would open a port nothing listens on.
    const uint16 tcpPort = m_listenSocket ? m_listenSocket->connectedPort()
                                          : static_cast<uint16>(thePrefs.port());
    const uint16 udpPort = m_clientUDP ? m_clientUDP->connectedPort()
                                       : static_cast<uint16>(thePrefs.udpPort());

    add(PortMapPurpose::Ed2kTcp, PortMapProtocol::Tcp, tcpPort, QStringLiteral("eMule TCP"));
    add(PortMapPurpose::Ed2kClientUdp, PortMapProtocol::Udp, udpPort,
        QStringLiteral("eMule UDP"));

    // The server UDP port is deliberately absent. UDPSocket only ever receives
    // OP_GLOBSEARCHRES / OP_GLOBFOUNDSOURCES / OP_GLOBSERVSTATRES /
    // OP_SERVER_DESC_RES, every one of them a reply to a request we sent first,
    // so our own outbound datagram already opens the NAT binding. Mapping it
    // would add inbound attack surface for no gain — do not "fix" this.

    if (thePrefs.webServerEnabled() && thePrefs.webServerUPnP()) {
        add(PortMapPurpose::WebServer, PortMapProtocol::Tcp,
            static_cast<uint16>(thePrefs.webServerPort()), QStringLiteral("eMule Web"));
    }

    return requests;
}

// ---------------------------------------------------------------------------
// Local IPv6
// ---------------------------------------------------------------------------

void CoreSession::initLocalIPv6()
{
    // filterLANIPs is the existing "this is a real network" switch. Clearing it already means
    // "I am on a private/test network, stop rejecting non-routable peers" for Kad and the
    // server list; extend the same intent to IPv6 acceptance so an interop rig on ::1 or
    // 2001:db8::/32 works without a second, near-identical preference.
    Address::setLabNetworkMode(!thePrefs.filterLANIPs());
    if (Address::labNetworkMode())
        logWarning(QStringLiteral("IPv6: lab mode active (filterLANIPs is off) — accepting "
                                  "loopback, ULA, link-local and documentation addresses"));

    theApp.onPublicIPv6Changed = &CoreSession::markPeersForIPChange;

    // Publish first, then narrate: the advisory reports the address actually in effect,
    // which may come from publicIPv6Override rather than auto-selection.
    const IPv6PrivacyReport report = scanLocalIPv6();
    const Address effective = updatePublicIPv6(report);
    logIPv6PrivacyAdvisory(report, effective);   // the one and only call site
}

// ---------------------------------------------------------------------------
// markPeersForIPChange — AppContext::onPublicIPv6Changed hook, installed above.
//
// Queue an IPv6 IP-change notification for every connected peer that can use it.
// Only marks them: the packet goes out when the upload queue or a source list
// next walks the client, so an address rotation never fans a write out to every
// socket at once.
// ---------------------------------------------------------------------------

void CoreSession::markPeersForIPChange(const Address& effective)
{
    if (effective.isNull() || !theApp.clientList || !theApp.shouldAdvertisePublicIPv6())
        return;
    int marked = 0;
    theApp.clientList->forEachClient([&](UpDownClient* client) {
        if (!client || !client->supportsIPv6())
            return;
        if (!client->socket() || !client->socket()->isConnected())
            return;
        client->markSendIPPending();
        ++marked;
    });
    if (marked > 0)
        logInfo(QStringLiteral("IPv6: queued IP-change notice for %1 connected peer(s)")
                    .arg(marked));
}

} // namespace eMule
