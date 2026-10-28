/// @file DaemonApp.cpp
/// @brief Orchestrator for the headless core daemon — implementation.

#include "DaemonApp.h"
#include "net/GuardedNetworkAccessManager.h"
#include "CoreNotifierBridge.h"
#include "ApiEventFeeder.h"
#include "DaemonApiBackend.h"
#include "DaemonTorrentWebBackend.h"
#include "DaemonUsenetWebBackend.h"
#include "IpcServer.h"
#include "MetaSearchService.h"
#include "PowerManager.h"
#include "BandwidthCoordinator.h"
#include "BtBridge.h"
#include "Ed3kBridge.h"
#include "Ed3kCreditBridge.h"
#include "Ed3kFiltersBridge.h"
#include "Ed3kUploadsBridge.h"
#include "Ed3kDownloadsBridge.h"
#include "Ed3kSetsBridge.h"
#include "Ed3kHosterBridge.h"
#include "Ed3kFriendsBridge.h"
#include "Ed3kRoomsBridge.h"
#include "Ed3kNamesBridge.h"
#include "UsenetBridge.h"

#include "IpcMessage.h"
#include "LogRelay.h"

#include "app/AppConfig.h"
#include "app/AppContext.h"
#include "app/CoreSession.h"
#include "ipfilter/IPFilter.h"
#include "kademlia/KadLog.h"
#include "net/BindAddress.h"
#include "net/HttpDefaults.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "stats/StatsHistory.h"
#include "stats/StatsSnapshot.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"
#include "UsenetSession.h"
#ifdef EMULE_HAVE_BITTORRENT
#include "BtSession.h"
#include "BtTypes.h"
#endif
#ifdef EMULE_HAVE_ED3K
#include "Ed3kBootstrap.h"
#include "Ed3kChain.h"
#include "Ed3kCredit.h"
#include "Ed3kFilters.h"
#include "Ed3kUploads.h"
#include "Ed3kDownloads.h"
#include "Ed3kFriends.h"
#include "Ed3kRooms.h"
#include "Ed3kSearch.h"
#include "Ed3kSets.h"
#include "Ed3kLive.h"
#include "Ed3kNames.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kShared.h"
#include "Ed3kSiteServer.h"
#include "Ed3kUsenetJoin.h"
#include "Ed3kTorrentJoin.h"
#include "Ed3kHosterJoin.h"
#ifdef EMULE_HAVE_HOSTER_JD
#include "Ed3kJavaRuntime.h"
#include "Ed3kJdResolver.h"
#endif
#endif
#include "net/ProxySettings.h"
#include "IndexerFeedList.h"
#include "IndexerSearch.h"
#include "IndexerSearchList.h"
#include "IndexerResult.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"
#include "ipc/PushCoalescer.h"
#include "webserver/ApiEventHub.h"
#include "webserver/WebServer.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "utils/OtherFunctions.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>
#include <QUuid>

#include <openssl/rand.h>


namespace eMule {

using namespace Ipc;

DaemonApp* DaemonApp::s_instance = nullptr;
QtMessageHandler DaemonApp::s_previousHandler = nullptr;
QString DaemonApp::s_sessionToken;
QString DaemonApp::s_loadStage;

namespace {

/// Process lifetime, not DaemonApp's: a worker thread can log until it is joined.
Q_GLOBAL_STATIC(Ipc::LogRelay, s_logRelay)

} // namespace

DaemonApp::DaemonApp(QObject* parent)
    : QObject(parent)
{
}

DaemonApp::~DaemonApp()
{
    stop();
}

void DaemonApp::requestRestart()
{
    s_restartRequested = true;
    // Every GUI waits for the new core rather than launching its own
    if (s_instance && s_instance->m_ipcServer)
        s_instance->m_ipcServer->broadcast(IpcMessage(IpcMsgType::PushCoreRestarting, 0));
    // Queued: the caller's reply frame is flushed before teardown
    QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit,
                              Qt::QueuedConnection);
}

bool DaemonApp::start()
{
    if (m_running)
        return true;

    // Install log forwarder first so all startup messages (including Kad start)
    // are captured in the buffer and visible to the GUI when it connects.
    installLogForwarder();

    // Start core session
    m_coreSession = std::make_unique<CoreSession>(this);
    if (m_connectHold) {
        logInfo(QStringLiteral("Auto-connect held until the first start wizard is done"));
        m_coreSession->setConnectHold(true);
        // No GUI ever showed up: don't sit offline for good. A GUI that leaves
        // releases it in IpcServer::onClientDisconnected().
        QTimer::singleShot(std::chrono::minutes(2), this, [this] {
            if (m_coreSession && m_coreSession->isConnectHeld()
                && (!m_ipcServer || m_ipcServer->clientCount() == 0))
                m_coreSession->releaseConnectHold();
        });
    }
    m_coreSession->start();
    MetaSearchService::instance();   // registers itself as theApp.metaSearch

    // Generate IPC auth token on first run
    auto tokens = thePrefs.ipcTokens();
    if (tokens.isEmpty()) {
        QByteArray raw(16, Qt::Uninitialized);
        RAND_bytes(reinterpret_cast<unsigned char*>(raw.data()), 16);
        QString token = QString::fromLatin1(raw.toHex());  // 32 hex chars
        tokens.append(token);
        thePrefs.setIpcTokens(tokens);
        thePrefs.save();
    }
    logInfo(QStringLiteral("IPC auth token: %1").arg(tokens.first()));

    // Resolve and log public IP (non-blocking, opt-in)
    if (thePrefs.logPublicIP()) {
        auto* nam = new GuardedNetworkAccessManager(this);
        // curl -6 ifconfig.me <- for v6 detection later
        QNetworkRequest req = Http::makeRequest(QUrl(QStringLiteral("https://api.ipify.org")));
        req.setTransferTimeout(5000);
        auto* reply = nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [reply, nam]() {
            if (reply->error() == QNetworkReply::NoError) {
                const QString ip = QString::fromUtf8(reply->readAll()).trimmed();
                if (!ip.isEmpty())
                    logInfo(QStringLiteral("Public IP address: %1").arg(ip));
            }
            reply->deleteLater();
            nam->deleteLater();
        });
    }

    // Start IPC server
    m_ipcServer = std::make_unique<IpcServer>(this);

    const QHostAddress addr = thePrefs.ipcListenAddress().isEmpty()
        ? QHostAddress::LocalHost
        : QHostAddress(thePrefs.ipcListenAddress());
    const uint16 port = m_ipcPortOverride != 0 ? m_ipcPortOverride : thePrefs.ipcPort();

    if (!m_ipcServer->start(addr, port)) {
        logError(QStringLiteral("Failed to start IPC server on %1:%2")
                     .arg(addr.toString()).arg(port));
        m_startError = m_ipcServer->errorText();
        m_coreSession->stop();
        m_coreSession.reset();
        removeLogForwarder();
        return false;
    }

    // Connect core signals to IPC push events
    m_notifierBridge = std::make_unique<CoreNotifierBridge>(m_ipcServer.get(), this);
    m_notifierBridge->connectAll();
    // Via the session, not the mapper: enableUPnP creates and drops mappers at runtime
    connect(m_coreSession.get(), &CoreSession::portMapStatusChanged,
            m_notifierBridge.get(), &CoreNotifierBridge::onPortMapStatusChanged);
    connect(m_coreSession.get(), &CoreSession::portBindFailed,
            m_notifierBridge.get(), &CoreNotifierBridge::onPortBindFailed);
    // Any that failed before this connection existed
    for (const int port : m_coreSession->failedBindPorts())
        m_notifierBridge->onPortBindFailed(port);

    // The bound interface went away, came back or was changed: the session has closed
    // the P2P sockets; the parts it does not own follow here.
    theApp.onNetworkSuspended = [this](bool) {
        if (m_usenetSession)
            m_usenetSession->onNetworkRouteChanged();
        if (m_notifierBridge)
            m_notifierBridge->pushNetworkState();
    };

    // Events for the REST stream: the pushes the GUIs get, plus two the GUIs have
    // no use for as events (they refetch rows) but an API client waits on.
    m_eventHub = new ApiEventHub(this);
    m_eventFeeder = new ApiEventFeeder(m_eventHub, this);
    m_ipcServer->setPushTap([this](const Ipc::IpcMessage& msg) { m_eventFeeder->onPush(msg); });
    if (theApp.downloadQueue) {
        connect(theApp.downloadQueue, &DownloadQueue::fileCompleted, this, [this](PartFile* file) {
            if (!file)
                return;
            m_eventFeeder->event(QStringLiteral("downloads.completed"), QJsonObject{
                {QStringLiteral("hash"), md4str(file->fileHash())},
                {QStringLiteral("fileName"), file->fileName()},
            });
        });
    }
    if (theApp.sharedFileList) {
        const auto sharedChanged = [this] { m_eventFeeder->hint(QStringLiteral("shared.changed")); };
        connect(theApp.sharedFileList, &SharedFileList::fileAdded, this, sharedChanged);
        connect(theApp.sharedFileList, &SharedFileList::fileRemoved, this, sharedChanged);
    }

    // Connect web server config changes from any IPC client
    connect(m_ipcServer.get(), &IpcServer::webServerConfigChanged,
            this, &DaemonApp::onWebServerConfigChanged);
    connect(m_ipcServer.get(), &IpcServer::webTemplateReloadRequested, this, [this] {
        if (m_webServer)
            m_webServer->reloadTemplate();
    });
    connect(m_ipcServer.get(), &IpcServer::usenetConfigChanged,
            this, &DaemonApp::applyUsenetServers);
    connect(m_ipcServer.get(), &IpcServer::indexerConfigChanged,
            this, &DaemonApp::applyIndexerConfig);
    connect(m_ipcServer.get(), &IpcServer::btConfigChanged,
            this, &DaemonApp::applyBtConfig);
    connect(m_ipcServer.get(), &IpcServer::ed3kConfigChanged,
            this, &DaemonApp::applyEd3kConfig);
#ifdef EMULE_HAVE_ED3K
    // after every SetPreferences: the "Messages and Comments" options of ed3k chat
    connect(m_ipcServer.get(), &IpcServer::webServerConfigChanged,
            this, &DaemonApp::applyEd3kChatOptions);
#endif

    // The web server starts in onCoreLoaded(): a REST client must not read
    // half-loaded lists or add a download whose part file is still unread.
    connect(m_coreSession.get(), &CoreSession::loaded, this, &DaemonApp::onCoreLoaded);
    connect(m_coreSession.get(), &CoreSession::loadStageChanged, this, [this](const QString& stage) {
        s_loadStage = stage;
        broadcastCoreLoading(true);
    });

    // Usenet. Always constructed; usenetEnabled() gates only the auto-start, so
    // the switch takes effect without a daemon restart.
    m_usenetSession = std::make_unique<usenet::UsenetSession>();
    usenet::theUsenetSession = m_usenetSession.get();
    connectUsenetPushes();

    // The Download graph's Usenet line. Core cannot see the engine, so it is
    // handed a reader — through the global, which stop() nulls first.
    if (theApp.statsHistory) {
        theApp.statsHistory->setUsenetDownRateSource([] {
            const auto* session = usenet::theUsenetSession;
            return (session && session->queue())
                       ? static_cast<float>(session->queue()->currentRate()) / 1024.0f
                       : 0.0f;
        });
    }
    if (thePrefs.usenetEnabled())
        m_usenetSession->start();

    // ed3k (packmule): after Usenet (E2's usenet join uses both). Its own UDP port,
    // mapped by CoreSession::buildPortMapRequests while ed3kEnabled()
#ifdef EMULE_HAVE_ED3K
    m_ed3kSession = std::make_unique<ed3k::Ed3kSession>();
    Ed3kBridge::setSession(m_ed3kSession.get());
    connect(m_ed3kSession.get(), &ed3k::Ed3kSession::statusChanged, this, [this] {
        if (!m_ipcServer)
            return;
        IpcMessage msg(IpcMsgType::PushEd3kStatus, 0);
        msg.append(Ed3kBridge::statusToCbor());
        m_ipcServer->broadcast(msg);
    });
    // before the node starts: it attaches on statusChanged
    if (auto* queue = m_usenetSession->queue()) {
        m_usenetJoinQueue = ed3k::Ed3kUsenetJoin::queueAdapter(*queue);
        m_usenetJoin = std::make_unique<ed3k::Ed3kUsenetJoin>(*m_ed3kSession, *m_usenetJoinQueue, thePrefs.configDir());
        m_usenetJoin->connectQueue(*queue);
        m_usenetJoin->setHoldBack(thePrefs.ed3kHoldBackPosts());
    }
    // the share list over ed3k; also attaches on statusChanged
    if (theApp.sharedFileList) {
        m_ed3kSharedSource = ed3k::Ed3kShared::listAdapter(*theApp.sharedFileList);
        m_ed3kShared = std::make_unique<ed3k::Ed3kShared>(*m_ed3kSession, *m_ed3kSharedSource, thePrefs.configDir());
        m_ed3kShared->connectList(*theApp.sharedFileList);
        // a completed download stays the same object: no fileAdded
        if (theApp.downloadQueue)
            connect(theApp.downloadQueue, &DownloadQueue::fileCompleted, m_ed3kShared.get(), [s = m_ed3kShared.get()](PartFile* f) {
                if (f)
                    s->onFileChanged(QByteArray(reinterpret_cast<const char*>(f->fileHash()), 16));
            });
        m_ed3kShared->setEnabled(thePrefs.ed3kShareFiles());
    }
    // friends and chat; attaches on statusChanged
    m_ed3kFriends = std::make_unique<ed3k::Ed3kFriends>(*m_ed3kSession);
    applyEd3kChatOptions();
    Ed3kFriendsBridge::setFriends(m_ed3kFriends.get());
    Ed3kFriendsBridge::connectPushes(*m_ed3kFriends, [this](const IpcMessage& msg) {
        if (m_ipcServer)
            m_ipcServer->broadcast(msg);
    });
    // rooms (room.md); attaches on statusChanged and joins the saved rooms again
    m_ed3kRooms = std::make_unique<ed3k::Ed3kRooms>(*m_ed3kSession);
    Ed3kRoomsBridge::setRooms(m_ed3kRooms.get());
    Ed3kRoomsBridge::connectPushes(*m_ed3kRooms, [this](const IpcMessage& msg) {
        if (m_ipcServer)
            m_ipcServer->broadcast(msg);
    });
    // node lists (bootstrap.md §3); an ed3k:nodes link is asked through the GUI, none refuses it
    m_ed3kBootstrap = std::make_unique<ed3k::Ed3kBootstrap>(*m_ed3kSession);
    m_ed3kBootstrap->setSources(thePrefs.ed3kNodeLists());
    m_ed3kBootstrap->setAsker([this](std::uint64_t ask, const QString& host, const QString& url) {
        if (!m_ipcServer || !m_ipcServer->hasReadyClient())
            return false;
        IpcMessage msg(IpcMsgType::PushEd3kBootstrapAsk, 0);
        msg.append(qint64(ask));
        msg.append(host);
        msg.append(url);
        m_ipcServer->broadcast(msg);
        return true;
    });
    connect(m_ed3kBootstrap.get(), &ed3k::Ed3kBootstrap::sourceAdded, this, [](const QString& link) {
        QStringList links = thePrefs.ed3kNodeLists();
        if (links.contains(link))
            return;
        links.append(link);
        thePrefs.setEd3kNodeLists(links);
        thePrefs.save();
    });
    Ed3kBridge::setBootstrap(m_ed3kBootstrap.get());
    // readable names (DNS duty, name lists) and the local website server; attach on statusChanged
    m_ed3kNames = std::make_unique<ed3k::Ed3kNames>(*m_ed3kSession, thePrefs.configDir());
    m_ed3kSites = std::make_unique<ed3k::Ed3kSiteServer>(*m_ed3kSession, *m_ed3kNames);
    m_ed3kSites->setPort(thePrefs.ed3kSitePort());
    Ed3kNamesBridge::setNames(m_ed3kNames.get(), m_ed3kSites.get());
    Ed3kNamesBridge::connectPushes(*m_ed3kNames, m_ed3kSites.get(), [this](const IpcMessage& msg) {
        if (m_ipcServer)
            m_ipcServer->broadcast(msg);
    });
    // credits (credit.md): key file, ledger from the prefs; configured on statusChanged
    m_ed3kCredit = std::make_unique<ed3k::Ed3kCredit>(*m_ed3kSession, thePrefs.configDir());
    Ed3kCreditBridge::setCredit(m_ed3kCredit.get());
    connect(m_ed3kCredit.get(), &ed3k::Ed3kCredit::statusChanged, this, [this] {
        if (!m_ipcServer)
            return;
        IpcMessage msg(IpcMsgType::PushEd3kStatus, 0);
        msg.append(Ed3kBridge::statusToCbor());
        m_ipcServer->broadcast(msg);
    });
    // stake claims and transactions (credit.md §6.7)
    Ed3kCreditBridge::connectPushes(*m_ed3kCredit, [this](const IpcMessage& msg) {
        if (m_ipcServer)
            m_ipcServer->broadcast(msg);
    });
    // batch 12 (G): the upload queue view, filter lists, the chaintube chain from the prefs
    m_ed3kUploads = std::make_unique<ed3k::Ed3kUploads>(*m_ed3kSession);
    m_ed3kUploads->setNameLookup([this](const ::ed3k::Root& root) -> std::optional<std::pair<QString, std::uint64_t>> {
        if (!m_ed3kShared)
            return std::nullopt;
        const auto e = m_ed3kShared->entryOfRoot(root);
        return e ? std::optional(std::pair{e->name, e->size}) : std::nullopt;
    });
    Ed3kUploadsBridge::setUploads(m_ed3kUploads.get());
    m_ed3kFilters = std::make_unique<ed3k::Ed3kFilters>(*m_ed3kSession, thePrefs.configDir());
    Ed3kFiltersBridge::setFilters(m_ed3kFilters.get());
    Ed3kFiltersBridge::connectPushes(*m_ed3kFilters, [this](const IpcMessage& msg) {
        if (m_ipcServer)
            m_ipcServer->broadcast(msg);
    });
    m_ed3kChain = std::make_unique<ed3k::Ed3kChain>(*m_ed3kSession);
    // plain ed3k downloads; after Ed3kShared (it starts first on statusChanged and takes the
    // complete files of the share list: those are not shared twice)
    m_ed3kDownloads = std::make_unique<ed3k::Ed3kDownloads>(*m_ed3kSession, thePrefs.configDir());
    {
        const QStringList temps = thePrefs.tempDirs();
        m_ed3kDownloads->setDirs(temps.isEmpty() ? thePrefs.incomingDir() : temps.first(), thePrefs.incomingDir());
    }
    m_ed3kDownloads->setCategoryDir([](int category) { return thePrefs.incomingDirForCategory(category); });
    m_ed3kDownloads->setCompletedHook([](const ed3k::Ed3kDownloadInfo& done) {
        // the incoming dir is shared: eD2K hashes it, Ed3kShared then serves it (releaseShare)
        if (theApp.sharedFileList && !theApp.sharedFileList->addFileInSharedLocation(done.path))
            logDebug(QStringLiteral("ed3k: %1 is not in a shared location").arg(done.path));
    });
    if (m_ed3kShared) {
        m_ed3kDownloads->setSharedElsewhere([s = m_ed3kShared.get()](const ::ed3k::Root& root) { return s->sharesRoot(root); });
        connect(m_ed3kShared.get(), &ed3k::Ed3kShared::fileShared, m_ed3kDownloads.get(),
                [s = m_ed3kShared.get(), d = m_ed3kDownloads.get()](const QByteArray& md4) {
                    if (const auto root = s->rootOf(md4))
                        d->releaseShare(*root);
                });
    }
    Ed3kDownloadsBridge::setDownloads(m_ed3kDownloads.get());
    Ed3kDownloadsBridge::connectPushes(*m_ed3kDownloads, [this](const IpcMessage& msg) {
        if (m_ipcServer)
            m_ipcServer->broadcast(msg);
    });
    // followed sets: members into <incoming>/<set>/ through the downloads; live streams on
    // the website server's port (attach on statusChanged)
    m_ed3kSets = std::make_unique<ed3k::Ed3kSets>(*m_ed3kSession, *m_ed3kDownloads, thePrefs.configDir());
    m_ed3kSets->setIncomingDir(thePrefs.incomingDir());
    m_ed3kLive = std::make_unique<ed3k::Ed3kLive>(*m_ed3kSession, *m_ed3kSites);
    Ed3kSetsBridge::setSets(m_ed3kSets.get(), m_ed3kLive.get(), m_ed3kSites.get());
    Ed3kSetsBridge::connectPushes(*m_ed3kSets, *m_ed3kLive, m_ed3kSites.get(), [this](const IpcMessage& msg) {
        if (m_ipcServer)
            m_ipcServer->broadcast(msg);
    });
    // ed3k keyword search: SearchType::Ed3k through the Meta search runner
    m_ed3kSearch = std::make_unique<ed3k::Ed3kSearch>(*m_ed3kSession);
    MetaSearchService::instance().setEd3kRunner(m_ed3kSearch.get());
    // filter lists (filter-list.md §3): mark judged rows, drop them only when a list hides
    m_ed3kSearch->setJudge([this](const ::ed3k::FoundFile& f) {
        using J = ed3k::Ed3kSearch::Judged;
        if (!m_ed3kFilters)
            return J::None;
        const auto j = m_ed3kFilters->judge(f.root, f.ed2k, f.publisher);   // AUTHOR: the publishing node (spec question)
        if (!j || j->verdict == ::ed3k::FilterVerdict::Good)
            return J::None;
        return j->hide ? J::Hide : (j->disagree ? J::None : J::Mark);
    });
    // hoster links (hoster.md); the resolver is the JDownloader sidecar when built in
    m_hosterJoin = std::make_unique<ed3k::Ed3kHosterJoin>(*m_ed3kSession, thePrefs.configDir());
    {
        const auto pushStatus = [this] {
            if (!m_ipcServer)
                return;
            IpcMessage msg(IpcMsgType::PushEd3kStatus, 0);
            msg.append(Ed3kBridge::statusToCbor());
            m_ipcServer->broadcast(msg);
        };
        ed3k::Ed3kResolver* resolver = nullptr;
        ed3k::Ed3kJavaRuntime* java = nullptr;
#ifdef EMULE_HAVE_HOSTER_JD
        m_jdResolver = std::make_unique<ed3k::Ed3kJdResolver>();
        m_javaRuntime = std::make_unique<ed3k::Ed3kJavaRuntime>(QFileInfo(thePrefs.configDir()).absolutePath());
        resolver = m_jdResolver.get();
        java = m_javaRuntime.get();
        connect(m_jdResolver.get(), &ed3k::Ed3kResolver::stateChanged, this, pushStatus);
        connect(m_javaRuntime.get(), &ed3k::Ed3kJavaRuntime::stateChanged, this, [this, pushStatus] {
            const auto h = thePrefs.ed3kHoster();
            // none found: fetch one when the user allows that
            if (m_javaRuntime->state() == ed3k::Ed3kJavaRuntime::State::Missing && h.enabled && h.autoJava)
                m_javaRuntime->download();
            applyHosterConfig();
            pushStatus();
        });
#endif
        m_hosterJoin->setResolver(resolver);
        Ed3kHosterBridge::setHoster(m_hosterJoin.get(), resolver, java);
        Ed3kHosterBridge::connectPushes(*m_hosterJoin, [this](const IpcMessage& msg) {
            if (m_ipcServer)
                m_ipcServer->broadcast(msg);
        });
    }
    applyEd3kConfig();
    if (m_ed3kSession->isRunning())
        m_hosterJoin->start();
#endif

    // BitTorrent. Like Usenet: always constructed, btEnabled() gates the start.
    // The listen port is mapped by the app's PortMapper (CoreSession::buildPortMapRequests);
    // libtorrent's UPnP/NAT-PMP are off (BtSettingsMap)
    bt::BtSession* btSession = nullptr;
#ifdef EMULE_HAVE_BITTORRENT
    m_btSession = std::make_unique<bt::BtSession>();
    btSession = m_btSession.get();
    BtBridge::setSession(m_btSession.get());
    connectBtPushes();
    // the app's IP filter (§7.6): kept by the session across stop/start
    BtBridge::applyIpFilter();
    if (theApp.ipFilter) {
        connect(theApp.ipFilter, &IPFilter::filterLoaded, this, [] { BtBridge::applyIpFilter(); });
        connect(theApp.ipFilter, &IPFilter::filterCleared, this, [] { BtBridge::applyIpFilter(); });
    }
    if (thePrefs.btEnabled())
        m_btSession->start(BtBridge::settingsFromPrefs());
#endif
#if defined(EMULE_HAVE_ED3K) && defined(EMULE_HAVE_BITTORRENT)
    // after both sessions; the node may run already (statusChanged was before this)
    m_torrentJoin = std::make_unique<ed3k::Ed3kTorrentJoin>(*m_ed3kSession, *m_btSession, thePrefs.configDir());
    if (m_ed3kSession->isRunning())
        m_torrentJoin->start();
#endif
    // The graphs' and toolbar's BitTorrent rates; reads BtBridge's session, nulled in stop()
    if (theApp.statsHistory)
        theApp.statsHistory->setBtRateSource(&BtBridge::ratesKb);

    // One split of the global limits for all three engines
    m_bandwidth = std::make_unique<BandwidthCoordinator>(m_usenetSession.get(), btSession);
#ifdef EMULE_HAVE_ED3K
    m_bandwidth->setEd3k(m_ed3kSession.get());
#endif
    m_bandwidth->start();
#ifdef EMULE_HAVE_ED3K
    // the graphs' ed3k lines: the coordinator's wire rates (B/s → KB/s), cleared before it goes
    if (theApp.statsHistory)
        theApp.statsHistory->setEd3kRateSource([bw = m_bandwidth.get()] {
            const auto& s = bw->snapshot();
            return EngineRates{static_cast<float>(s.ed3kDownRate) / 1024.0f, static_cast<float>(s.ed3kUpRate) / 1024.0f};
        });
#endif

    // Indexer search. No enable switch: an account list that is empty is the off
    // state, and there is nothing running to gate — a search only ever happens
    // because the user asked for one.
    m_indexerSearches = std::make_unique<indexer::IndexerSearchList>();
    indexer::theIndexerSearchList = m_indexerSearches.get();
    connectIndexerPushes();

    // Feeds. Constructed after the Usenet session, because the sink it is given
    // reaches into that session's queue.
    m_indexerFeeds = std::make_unique<indexer::IndexerFeedList>();
    indexer::theIndexerFeeds = m_indexerFeeds.get();
    m_indexerFeeds->setPasswordFlagStore(m_indexerSearches->passwordFlags());
    connectIndexerFeedSink();
    m_indexerFeeds->applyPreferences();

    // Prevent standby. MFC ResetStandByIdleTimer's condition, plus Usenet.
    m_powerManager = std::make_unique<PowerManager>([] {
        const auto* usenetSession = usenet::theUsenetSession;
        return theApp.isConnected()
            || (theApp.uploadQueue && theApp.uploadQueue->uploadQueueLength() > 0)
            || (theApp.downloadQueue && theApp.downloadQueue->datarate() > 0)
            || (usenetSession && usenetSession->queue()
                && usenetSession->queue()->currentRate() > 0);
    });
    connect(m_ipcServer.get(), &IpcServer::standbyConfigChanged,
            m_powerManager.get(), &PowerManager::evaluate);
    m_powerManager->start();

    m_running = true;
    logInfo(QStringLiteral("Daemon started — IPC server on %1:%2")
                .arg(addr.toString()).arg(port));
    return true;
}

void DaemonApp::stop()
{
    if (!m_running)
        return;

    // Bank this session's statistics before tearing down. The periodic flush in
    // CoreSession has probably already done it; the flush is idempotent, so this
    // only picks up whatever happened since.
    if (theApp.statistics) {
        flushCumulativeStats(thePrefs);
        thePrefs.save();
    }

    // First: its predicate reads the Usenet session and core queues.
    m_powerManager.reset();

    stopWebServer();

    // Before the IPC server goes away: a Test-button reply in flight holds a
    // QPointer to its handler, and the engine's own sockets must be torn down
    // while the event loop is still turning. UsenetSession::stop() is also where
    // the worker threads are joined, so by the time it returns nothing is left
    // running that could reach a half-destroyed daemon.
    // Feeds first: a poll in flight holds a QPointer to this list and a callback
    // that reaches the Usenet queue through the sink, and both have to stop
    // before either is torn down.
    indexer::theIndexerFeeds = nullptr;
    m_indexerFeeds.reset();

    indexer::theIndexerSearchList = nullptr;
    m_indexerSearches.reset();

    // before the sessions it throttles
    if (theApp.statsHistory)
        theApp.statsHistory->setEd3kRateSource({});
    m_bandwidth.reset();

#ifdef EMULE_HAVE_ED3K
    // the joins first: save themselves and close their downloads while the engines run
#ifdef EMULE_HAVE_BITTORRENT
    if (m_torrentJoin)
        m_torrentJoin->stop();
    m_torrentJoin.reset();
#endif
    if (m_usenetJoin)
        m_usenetJoin->stop();
    m_usenetJoin.reset();
    m_usenetJoinQueue.reset();
    MetaSearchService::instance().setEd3kRunner(nullptr);
    m_ed3kSearch.reset();
    Ed3kSetsBridge::setSets(nullptr, nullptr, nullptr);
    m_ed3kLive.reset();
    m_ed3kSets.reset();
    Ed3kDownloadsBridge::setDownloads(nullptr);
    if (m_ed3kDownloads)
        m_ed3kDownloads->stop();
    m_ed3kDownloads.reset();
    if (m_ed3kShared)
        m_ed3kShared->stop();
    m_ed3kShared.reset();
    m_ed3kSharedSource.reset();
    Ed3kFriendsBridge::setFriends(nullptr);
    m_ed3kFriends.reset();
    Ed3kRoomsBridge::setRooms(nullptr);
    m_ed3kRooms.reset();
    Ed3kBridge::setBootstrap(nullptr);
    m_ed3kBootstrap.reset();
    Ed3kNamesBridge::setNames(nullptr, nullptr);
    m_ed3kSites.reset();
    Ed3kCreditBridge::setCredit(nullptr);
    m_ed3kCredit.reset();
    Ed3kUploadsBridge::setUploads(nullptr);
    m_ed3kUploads.reset();
    Ed3kFiltersBridge::setFilters(nullptr);
    if (m_ed3kFilters)
        m_ed3kFilters->stop();
    m_ed3kFilters.reset();
    m_ed3kChain.reset();
    Ed3kHosterBridge::setHoster(nullptr, nullptr, nullptr);
    if (m_hosterJoin)
        m_hosterJoin->stop();
    m_hosterJoin.reset();
#ifdef EMULE_HAVE_HOSTER_JD
    m_jdResolver.reset();
    m_javaRuntime.reset();
#endif
    if (m_ed3kNames)
        m_ed3kNames->stop();
    m_ed3kNames.reset();
    // saves the node's state; before Usenet
    Ed3kBridge::setSession(nullptr);
    if (m_ed3kSession)
        m_ed3kSession->stop();
    m_ed3kSession.reset();
#endif

    if (theApp.statsHistory)
        theApp.statsHistory->setUsenetDownRateSource({});
    usenet::theUsenetSession = nullptr;
    if (m_usenetSession)
        m_usenetSession->stop();
    m_usenetSession.reset();

    // saves resume data and the DHT state; before IPC and core go away
    if (theApp.statsHistory)
        theApp.statsHistory->setBtRateSource({});
    BtBridge::setSession(nullptr);
#ifdef EMULE_HAVE_BITTORRENT
    if (m_btSession)
        m_btSession->stop();
    m_btSession.reset();
#endif

    // stopWorkers() delivers its last results after the flush at the top, and
    // they are counted in core; bank them too. main() saves once more after this.
    if (theApp.statistics)
        flushCumulativeStats(thePrefs);

    theApp.onNetworkSuspended = nullptr;
    m_notifierBridge.reset();

    if (m_ipcServer)
        m_ipcServer->stop();
    m_ipcServer.reset();

    if (m_coreSession)
        m_coreSession->stop();
    m_coreSession.reset();

    m_running = false;
    logInfo(QStringLiteral("Daemon stopped."));

    // Last, so the whole teardown is on record. This used to run before the core
    // session went away, which closed the log file sink mid-shutdown and threw
    // away every line after it -- Kad's stop, the nodes.dat write and "Daemon
    // stopped." all vanished, making a shrunken routing table unforensic.
    // Forwarding to IPC clients is already a no-op by now (the relay's
    // connection null-checks m_ipcServer), so staying installed this long is free.
    removeLogForwarder();
}

bool DaemonApp::isRunning() const
{
    return m_running;
}

// ---------------------------------------------------------------------------
// Web server management
// ---------------------------------------------------------------------------

void DaemonApp::startWebServer()
{
    m_webServer = std::make_unique<WebServer>(this);

    // Inject dependencies from core
    m_webServer->setDownloadQueue(theApp.downloadQueue);
    m_webServer->setUploadQueue(theApp.uploadQueue);
    m_webServer->setServerList(theApp.serverList);
    m_webServer->setServerConnect(theApp.serverConnect);
    m_webServer->setSearchList(theApp.searchList);
    m_webServer->setSharedFileList(theApp.sharedFileList);
    m_webServer->setFriendList(theApp.friendList);
    m_webServer->setStatistics(theApp.statistics);
    m_webServer->setStatsHistory(theApp.statsHistory);
    m_webServer->setPreferences(&thePrefs);
    m_webServer->setTranslationRouter(m_translations);

    // Usenet preview. Injected as a callback rather than a pointer: WebServer
    // lives in eMule::Core and `core -> usenet` must never happen. DaemonApp is
    // the one object that links both, which is exactly the seam setLogProvider
    // below already uses.
    m_webServer->setUsenetStreamResolver(
        [](const UsenetStreamRequest& ask) -> UsenetStreamSource {
            UsenetStreamSource out;
            if (!usenet::theUsenetSession || !usenet::theUsenetSession->queue())
                return out;

            const auto info = usenet::theUsenetSession->queue()->requestStream(
                ask.itemId, ask.fileIndex, ask.wantOffset, ask.wantLength, ask.entryOrdinal);
            out.found             = info.found;
            out.fileName          = info.fileName;
            out.totalSize         = info.totalSize;
            out.availableEnd      = info.availableEnd;
            out.complete          = info.complete;
            out.notSeekableReason = info.notSeekableReason;

            // A stored RAR set resolves to one piece per volume; a raw post to
            // one. Either way the web server only ever sees paths and offsets.
            for (const auto& piece : info.pieces) {
                out.pieces.append({piece.path, piece.virtualOffset,
                                   piece.fileOffset, piece.length});
            }
            return out;
        });

    // The Usenet page and the /api/v1/usenet routes. Created once and kept across
    // web-server restarts: a URL fetch in flight holds on to it.
    if (!m_usenetWebBackend)
        m_usenetWebBackend = std::make_unique<DaemonUsenetWebBackend>();
    m_webServer->setUsenetBackend(m_usenetWebBackend.get());

#ifdef EMULE_HAVE_BITTORRENT
    // The /api/v1/torrents routes; stateless, it asks BtBridge each time.
    if (!m_torrentWebBackend)
        m_torrentWebBackend = std::make_unique<DaemonTorrentWebBackend>();
    m_webServer->setTorrentBackend(m_torrentWebBackend.get());
#endif

    // The REST API's daemon half and its event source, kept the same way.
    if (!m_apiBackend)
        m_apiBackend = std::make_unique<DaemonApiBackend>(m_ipcServer.get());
    m_webServer->setApiBackend(m_apiBackend.get());
    m_webServer->setEventHub(m_eventHub);

    m_webServer->setLogProvider([] {
        auto entries = DaemonApp::logsSince(0);
        QString text;
        for (const auto& e : entries) {
            text += QDateTime::fromSecsSinceEpoch(e.timestamp).toString(QStringLiteral("HH:mm:ss"));
            text += QLatin1Char(' ');
            text += e.message;
            text += QLatin1Char('\n');
        }
        return text;
    });

    const WebServerConfig config = WebServerConfig::fromPreferences(thePrefs);

    m_webServer->start(config);

    // The web port only needs forwarding while the server is actually up, and
    // only when the user asked for it. This is what finally gives the
    // webServerUPnP preference an effect — the old UPnPManager exposed
    // enableWebServerPort() but nothing ever called it.
    if (m_coreSession)
        m_coreSession->updatePortMappings();
}

namespace {

/// Matches CoreNotifierBridge's kPushWindowMs: below the GUI's 500 ms poll, so a
/// push still beats the poll to the data, while per-segment progress on a large
/// release collapses into a handful of sends.
constexpr int kUsenetPushWindowMs = 250;

/// Feed reports are not a live meter — a poll is minutes apart — so the only
/// burst worth merging is the start/finish pair of one poll.
constexpr int kFeedPushWindowMs = 250;

} // namespace

/// Likewise: the push and the StartIndexerSearch reply must carry the same row.
QCborMap indexerResultToCbor(const indexer::IndexerResult& result, const QString& expression);

void DaemonApp::connectUsenetPushes()
{
    if (!m_usenetSession || !m_ipcServer)
        return;

    auto* coalescer = new Ipc::PushCoalescer(this);
    connect(coalescer, &Ipc::PushCoalescer::ready, this, [this](const IpcMessage& msg) {
        m_ipcServer->broadcast(msg);
    });

    auto pushItem = [this, coalescer](const QString& id) {
        // Keyed on the item, so a 10 000-article release cannot suppress pushes
        // for a second NZB queued beside it. PushCoalescer's subKey is 32 bits and
        // the id is a UUID string, so hash it down.
        const quint32 subKey = qHash(id);
        coalescer->post(IpcMsgType::PushUsenetQueueItem, [this, id] {
            IpcMessage msg(IpcMsgType::PushUsenetQueueItem, 0);
            if (auto* session = m_usenetSession.get(); session && session->queue()) {
                if (const auto* item = session->queue()->findItem(id))
                    msg.append(UsenetBridge::itemToCbor(*item));
            }
            return msg;
        }, kUsenetPushWindowMs, subKey);
    };

    connect(m_usenetSession.get(), &usenet::UsenetSession::itemChanged, this, pushItem);
    connect(m_usenetSession.get(), &usenet::UsenetSession::itemAdded, this, pushItem);

    connect(m_usenetSession.get(), &usenet::UsenetSession::itemRemoved, this,
            [this](const QString& id) {
        // Not coalesced: a removal that arrives after a later change for the same
        // key would be dropped, and the GUI would keep showing a row for an item
        // that no longer exists.
        IpcMessage msg(IpcMsgType::PushUsenetItemRemoved, 0);
        msg.append(id);
        m_ipcServer->broadcast(msg);
    });

    connect(m_usenetSession.get(), &usenet::UsenetSession::itemFinished, this,
            [this](const QString& id, bool success, const QString& message) {
        // Also uncoalesced, and for the reason onServerStateChanged is: this is a
        // transition rather than a latest value. A completion collapsed inside a
        // window is a notification the user asked for and never got.
        IpcMessage msg(IpcMsgType::PushUsenetItemFinished, 0);
        msg.append(id);
        msg.append(success);
        msg.append(message);
        m_ipcServer->broadcast(msg);
    });

    // Uncoalesced for the same reason: a pause and a resume inside one window
    // would otherwise leave a client showing whichever came first.
    connect(m_usenetSession.get(), &usenet::UsenetSession::enginePausedChanged, this,
            [this](bool paused) {
        IpcMessage msg(IpcMsgType::PushUsenetEngineState, 0);
        msg.append(QCborMap{{QStringLiteral("paused"), paused}});
        m_ipcServer->broadcast(msg);
    });
}

void DaemonApp::connectBtPushes()
{
#ifdef EMULE_HAVE_BITTORRENT
    if (!m_btSession || !m_ipcServer)
        return;

    auto* coalescer = new Ipc::PushCoalescer(this);
    connect(coalescer, &Ipc::PushCoalescer::ready, this, [this](const IpcMessage& msg) {
        m_ipcServer->broadcast(msg);
    });

    // A row per second per torrent: coalesced on the id
    connect(m_btSession.get(), &bt::BtSession::torrentChanged, this, [coalescer](const QString& id) {
        coalescer->post(IpcMsgType::PushTorrentUpdate, [id] {
            IpcMessage msg(IpcMsgType::PushTorrentUpdate, 0);
            msg.append(BtBridge::rowToCbor(id));
            return msg;
        }, kUsenetPushWindowMs, static_cast<quint32>(qHash(id)));
    });

    // Transitions: uncoalesced (see connectUsenetPushes)
    connect(m_btSession.get(), &bt::BtSession::torrentAdded, this, [this](const QString& id) {
        IpcMessage msg(IpcMsgType::PushTorrentAdded, 0);
        msg.append(BtBridge::rowToCbor(id));
        m_ipcServer->broadcast(msg);
    });
    connect(m_btSession.get(), &bt::BtSession::torrentRemoved, this, [this](const QString& id) {
        IpcMessage msg(IpcMsgType::PushTorrentRemoved, 0);
        msg.append(id);
        m_ipcServer->broadcast(msg);
    });
    connect(m_btSession.get(), &bt::BtSession::torrentFinished, this, [this](const QString& id) {
        IpcMessage msg(IpcMsgType::PushTorrentFinished, 0);
        msg.append(id);
        m_ipcServer->broadcast(msg);
    });
    connect(m_btSession.get(), &bt::BtSession::statusChanged, this, [this] {
        IpcMessage msg(IpcMsgType::PushBtStatus, 0);
        msg.append(BtBridge::statusToCbor());
        m_ipcServer->broadcast(msg);
    });
#endif
}

void DaemonApp::applyBtConfig()
{
#ifdef EMULE_HAVE_BITTORRENT
    if (!m_btSession)
        return;
    const bt::BtSettings settings = BtBridge::settingsFromPrefs();
    const bool enabled = thePrefs.btEnabled();
    if (enabled && !m_btSession->isRunning())
        m_btSession->start(settings);
    else if (!enabled && m_btSession->isRunning())
        m_btSession->stop();
    else if (enabled)
        m_btSession->applySettings(settings);
    BtBridge::applyIpFilter(true);
    // port, enable switch or btPortMapping may have changed; diff-based, cheap
    if (m_coreSession)
        m_coreSession->updatePortMappings();
#endif
}

void DaemonApp::applyEd3kConfig()
{
#ifdef EMULE_HAVE_ED3K
    if (!m_ed3kSession)
        return;
    const bool enabled = thePrefs.ed3kEnabled();
    const uint16 port = thePrefs.ed3kUdpPort();
    // was: ToDo: the bindAddress pref (interface selection) for the ed3k socket (done: its
    // listen address; a selected interface pins the socket in Ed3kSession::start)
    const std::optional<QHostAddress> bind = BindAddress::listenAddress();
    QStringList unknownExt;
    const std::uint64_t extOff = ed3k::extOffBits(thePrefs.ed3kExtOff(), &unknownExt);
    if (!unknownExt.isEmpty())
        logWarning(QStringLiteral("ed3k: unknown extension(s) in ed3k.extOff ignored: %1").arg(unknownExt.join(QLatin1Char(' '))));
    // a port change takes a restart of the node; the state is saved in between. So do the
    // extensions, LAN discovery and the bind address (read once at start)
    const auto& running = m_ed3kSession->settings();
    const bool startChanged = m_ed3kSession->isRunning()
                              && (running.extOff != extOff || running.lan != thePrefs.ed3kLan()
                                  || (bind && running.bindAddress != *bind));
    if (m_ed3kSession->isRunning() && (!enabled || !bind || startChanged || (port != 0 && m_ed3kSession->udpPort() != port))) {
        if (m_usenetJoin)
            m_usenetJoin->stop();   // restarted on statusChanged
        if (m_ed3kDownloads)
            m_ed3kDownloads->stop();
        if (m_ed3kShared)
            m_ed3kShared->stop();
        if (m_hosterJoin)
            m_hosterJoin->stop();
#ifdef EMULE_HAVE_BITTORRENT
        if (m_torrentJoin)
            m_torrentJoin->stop();
#endif
        m_ed3kSession->stop();
    }
    if (enabled && !m_ed3kSession->isRunning()) {
        ed3k::Ed3kSettings settings;
        settings.udpPort = port;
        settings.configDir = thePrefs.configDir();
        settings.creditRpc = thePrefs.ed3kCreditRpc();
        settings.chaintubeRpc = thePrefs.ed3kChaintubeRpc();
        settings.nick = thePrefs.nick();
        settings.extOff = extOff;
        settings.lan = thePrefs.ed3kLan();
        if (bind) {
            settings.bindAddress = *bind;
            m_ed3kSession->start(settings);
        } else {
            logWarning(QStringLiteral("ed3k: the bind address %1 does not resolve; the node is not started")
                           .arg(thePrefs.bindAddress()));
        }
    } else if (enabled) {
        // the RPC endpoints take no restart
        m_ed3kSession->setRpcEndpoints(::ed3k::RpcClass::Credit, thePrefs.ed3kCreditRpc());
        m_ed3kSession->setRpcEndpoints(::ed3k::RpcClass::Chaintube, thePrefs.ed3kChaintubeRpc());
    }
    // ed3kSitePort: the website server rebinds (it listens only while the node runs)
    if (m_ed3kSites)
        m_ed3kSites->setPort(thePrefs.ed3kSitePort());
    // ed3kCredit*, ed3kCreditRpc: no restart; reconfigured when the ledger moved
    if (m_ed3kCredit)
        m_ed3kCredit->setConfig(thePrefs.ed3kCredit(), int(thePrefs.ed3kCreditRpc().size()));
    // ed3kChain*, ed3kChaintubeRpc: no restart; chainConfigure again when they moved
    if (m_ed3kChain)
        m_ed3kChain->setConfig(thePrefs.ed3kChain(), thePrefs.ed3kChaintubeRpc());
    // ed3kNodeLists: no restart
    if (m_ed3kBootstrap)
        m_ed3kBootstrap->setSources(thePrefs.ed3kNodeLists());
    // ed3kShareFiles: on/off while the node runs (starts with it otherwise)
    if (m_ed3kShared)
        m_ed3kShared->setEnabled(thePrefs.ed3kShareFiles());
    // ed3kHoldBackPosts: a change restarts the usenet join (kept lists and records follow)
    if (m_usenetJoin)
        m_usenetJoin->setHoldBack(thePrefs.ed3kHoldBackPosts());
    // ed3kHoster*: no restart
    applyHosterConfig();
    if (m_coreSession)
        m_coreSession->updatePortMappings();
#endif
}

void DaemonApp::applyHosterConfig()
{
#ifdef EMULE_HAVE_ED3K
    if (!m_hosterJoin)
        return;
    const Ed3kHosterPrefs h = thePrefs.ed3kHoster();
    const QStringList temps = thePrefs.tempDirs();
    m_hosterJoin->setConfig(h, temps.isEmpty() ? QString() : temps.first(), thePrefs.incomingDir());
#ifdef EMULE_HAVE_HOSTER_JD
    if (!m_javaRuntime || !m_jdResolver)
        return;
    // nothing is fetched unless the user switched hoster downloads on; off stops a fetch
    m_javaRuntime->setAllowed(h.enabled);
    if (!h.enabled)
        m_javaLookedFor = QStringLiteral("\x01");
    // the runtime is looked for once the user switched hosters on, and again for a new path
    if (h.enabled && m_javaLookedFor != h.java) {
        m_javaLookedFor = h.java;
        m_javaRuntime->locate(h.java);
    }
    QString proxy;
    if (const ProxySettings p = thePrefs.proxySettings(); p.useProxy && !p.host.isEmpty()) {
        const QString scheme = p.type == PROXYTYPE_HTTP10 || p.type == PROXYTYPE_HTTP11 ? QStringLiteral("http")
                                                                                        : QStringLiteral("socks5");
        const QString auth = p.enablePassword && !p.user.isEmpty()
                                 ? QString::fromLatin1(QUrl::toPercentEncoding(p.user)) + QLatin1Char(':')
                                       + QString::fromLatin1(QUrl::toPercentEncoding(p.password)) + QLatin1Char('@')
                                 : QString();
        proxy = QStringLiteral("%1://%2%3:%4").arg(scheme, auth, p.host).arg(p.port);
        m_javaRuntime->setProxy(toNetworkProxy(p));
    } else {
        m_javaRuntime->setProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
    }
    ed3k::JdResolverConfig cfg;
    if (const auto rt = m_javaRuntime->runtime())
        cfg.java = rt->java;
    cfg.dataDir = QDir(thePrefs.configDir()).filePath(QStringLiteral("ed3k/resolver"));
    cfg.zip = ed3k::Ed3kJdResolver::findBundledZip();
    cfg.proxy = proxy;
    cfg.askPuzzles = h.askPuzzles;
    m_jdResolver->setConfig(cfg);
#endif
#endif
}

void DaemonApp::applyUsenetServers()
{
    if (!m_usenetSession)
        return;

    m_usenetSession->applyPreferences();

    // The enable switch can flip in the same save that changed the servers.
    if (thePrefs.usenetEnabled() && !m_usenetSession->isRunning())
        m_usenetSession->start();
    else if (!thePrefs.usenetEnabled() && m_usenetSession->isRunning())
        m_usenetSession->stop();
}

void DaemonApp::applyIndexerConfig()
{
    if (m_indexerSearches)
        m_indexerSearches->applyPreferences();
    if (m_indexerFeeds)
        m_indexerFeeds->applyPreferences();
}

void DaemonApp::connectIndexerFeedSink()
{
    if (!m_indexerFeeds)
        return;

    // The one place the two modules meet. eMule::Indexer must not depend on
    // eMule::Usenet — a future BitTorrent module reuses the same client — so the
    // feed poller hands out an .nzb and this maps the queue's answer back onto
    // its own vocabulary. handleGrabIndexerResult performs the same join, and
    // for the same reason.
    m_indexerFeeds->setNzbSink(
        [](const indexer::FeedAddRequest& request, QString& error) {
            if (!usenet::theUsenetSession || !usenet::theUsenetSession->queue()) {
                error = QObject::tr("The Usenet engine is not running.");
                // Retry, not Rejected: the release is fine, the daemon is not.
                return indexer::FeedAddOutcome::Retry;
            }

            usenet::UsenetAddOutcome outcome = usenet::UsenetAddOutcome::Failed;
            usenet::theUsenetSession->queue()->addNzb(
                request.payload, request.title, error,
                {.source = usenet::UsenetAddSource::Automatic,
                 .password = request.password,
                 .category = request.downloadCategory},
                &outcome);
            usenet::theUsenetSession->queue()->stats().noteAdd(usenet::UsenetAddOrigin::Feed,
                                                               outcome);

            switch (outcome) {
            case usenet::UsenetAddOutcome::Added:
                return indexer::FeedAddOutcome::Added;
            case usenet::UsenetAddOutcome::Duplicate:
            case usenet::UsenetAddOutcome::AlreadyDownloaded:
                // Terminal. The feed must stop asking — this is not a failure,
                // it is the answer. AlreadyDownloaded joins it deliberately: a
                // person re-downloading something on purpose is a legitimate act
                // and a feed doing it is a mistake, so the feed never gets the
                // question, only the verdict.
                return indexer::FeedAddOutcome::AlreadyHave;
            case usenet::UsenetAddOutcome::Invalid:
                return indexer::FeedAddOutcome::Rejected;
            case usenet::UsenetAddOutcome::Failed:
                break;
            }
            return indexer::FeedAddOutcome::Retry;
        });

    if (!m_ipcServer)
        return;

    auto* coalescer = new Ipc::PushCoalescer(this);
    connect(coalescer, &Ipc::PushCoalescer::ready, this, [this](const IpcMessage& msg) {
        m_ipcServer->broadcast(msg);
    });

    connect(m_indexerFeeds.get(), &indexer::IndexerFeedList::feedStatusChanged, this,
            [coalescer](const indexer::IndexerFeedStatus& status) {
        // Keyed on the feed, so a busy feed cannot suppress another's report.
        // A status is a latest value rather than a transition, which is exactly
        // what coalescing is for.
        coalescer->post(IpcMsgType::PushIndexerFeedStatus, [status] {
            IpcMessage msg(IpcMsgType::PushIndexerFeedStatus, 0);
            msg.append(QCborMap{
                {QStringLiteral("name"),        status.name},
                {QStringLiteral("lastPolled"),
                 status.lastPolled.isValid() ? status.lastPolled.toSecsSinceEpoch() : qint64(0)},
                {QStringLiteral("lastError"),   status.lastError},
                {QStringLiteral("lastMatched"), status.lastMatched},
                {QStringLiteral("seenCount"),   status.seenCount},
                {QStringLiteral("polling"),     status.polling},
            });
            return msg;
        }, kFeedPushWindowMs, qHash(status.name));
    });
}

void DaemonApp::connectIndexerPushes()
{
    if (!m_indexerSearches || !m_ipcServer)
        return;

    connect(m_indexerSearches.get(), &indexer::IndexerSearchList::resultsReady, this,
            [this](quint32 searchId, const QList<indexer::IndexerResult>& rows) {
        // Not coalesced. Every push carries a *different* batch of rows — this is
        // an append, not a latest value — so a coalescing window would not merge
        // them, it would throw all but the last away.
        IpcMessage msg(IpcMsgType::PushIndexerResults, 0);
        msg.append(static_cast<qint64>(searchId));
        const indexer::IndexerSearch* search = m_indexerSearches->search(searchId);
        const QString expression = search ? search->query().text : QString();
        QCborArray out;
        for (const auto& row : rows)
            out.append(indexerResultToCbor(row, expression));
        msg.append(out);
        m_ipcServer->broadcast(msg);
    });

    connect(m_indexerSearches.get(), &indexer::IndexerSearchList::searchProgress, this,
            [this](quint32 searchId, int done, int total) {
        IpcMessage msg(IpcMsgType::PushIndexerProgress, 0);
        msg.append(static_cast<qint64>(searchId));
        msg.append(static_cast<qint64>(done));
        msg.append(static_cast<qint64>(total));
        m_ipcServer->broadcast(msg);
    });

    connect(m_indexerSearches.get(), &indexer::IndexerSearchList::searchFinished, this,
            [this](quint32 searchId, const QString& error) {
        IpcMessage msg(IpcMsgType::PushIndexerSearchDone, 0);
        msg.append(static_cast<qint64>(searchId));
        msg.append(error);
        m_ipcServer->broadcast(msg);
    });
}

void DaemonApp::onCoreLoaded()
{
    logInfo(QStringLiteral("Core loaded — networks starting"));
    startWebServer();
    s_loadStage.clear();
    // Ahead of the replay: the GUI should drop its "loading" notice before the
    // lists it was waiting for arrive
    broadcastCoreLoading(false);
    if (m_ipcServer)
        m_ipcServer->replayDeferred();
}

void DaemonApp::broadcastCoreLoading(bool loading)
{
    if (!m_ipcServer)
        return;
    IpcMessage msg(IpcMsgType::PushCoreLoading, 0);
    msg.append(loading);
    msg.append(s_loadStage);
    m_ipcServer->broadcast(msg);
}

void DaemonApp::stopWebServer()
{
    if (m_webServer) {
        m_webServer->stop();
        m_webServer.reset();
    }
    if (m_coreSession)
        m_coreSession->updatePortMappings();
}

void DaemonApp::onWebServerConfigChanged()
{
    // Fired after every preference save. A restart ends all web sessions and changes
    // the stream token, so only do it when a setting the server uses has changed.
    if (m_webServer && m_webServer->isRunning()
        && m_webServer->config() == WebServerConfig::fromPreferences(thePrefs)) {
        if (m_coreSession)
            m_coreSession->applyPortMapPreferences();   // enableUPnP / webServerUPnP may have flipped
        return;
    }
    if (m_coreSession)
        m_coreSession->applyPortMapPreferences();
    restartWebServer();
}

void DaemonApp::restartWebServer()
{
    stopWebServer();
    startWebServer();
}

// ---------------------------------------------------------------------------
// Log forwarding to IPC clients
// ---------------------------------------------------------------------------

void DaemonApp::installLogForwarder()
{
    s_instance = this;
    if (s_sessionToken.isEmpty())
        s_sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // First touch, so the relay is created here on the main thread -- it forwards
    // from the thread it lives on. Lines logged before the IPC server exists reach
    // nobody and stay buffered for SyncLogs.
    connect(s_logRelay(), &Ipc::LogRelay::ready, this, [this](const IpcMessage& msg) {
        if (m_ipcServer)
            m_ipcServer->broadcast(msg);
    });
    s_previousHandler = qInstallMessageHandler(logMessageHandler);
}

void DaemonApp::removeLogForwarder()
{
    if (s_instance == this) {
        qInstallMessageHandler(s_previousHandler);
        s_previousHandler = nullptr;
        s_instance = nullptr;
        if (auto* relay = s_logRelay())
            disconnect(relay, nullptr, this, nullptr);
        closeLogFileSink();
    }
}

std::vector<Ipc::LogEntry> DaemonApp::logsSince(int64_t lastLogId)
{
    auto* relay = s_logRelay();
    return relay ? relay->since(lastLogId) : std::vector<Ipc::LogEntry>{};
}

QString DaemonApp::sessionToken()
{
    return s_sessionToken;
}

QString DaemonApp::loadStage()
{
    return s_loadStage;
}

void DaemonApp::logMessageHandler(QtMsgType type, const QMessageLogContext& context,
                                  const QString& msg)
{
    // Always chain to previous handler (console output)
    if (s_previousHandler)
        s_previousHandler(type, context, msg);

    const char* cat = context.category ? context.category : "";
    if (std::strncmp(cat, "emule.", 6) != 0)
        return;

    // Runs on whichever thread logged, so no socket is touched here: the relay
    // buffers the line and broadcasts it from the main thread, which owns them.
    if (auto* relay = s_logRelay())
        relay->append(QString::fromUtf8(cat), type, msg);
}

void DaemonApp::applyLogFilterRules()
{
    // Refresh the emit-site gate for the dedicated server-verbose channel.
    setServerVerboseLogging(thePrefs.serverVerboseLog());
    kad::setKadLogging(thePrefs.kadVerboseLog());

    // Compose category filter rules. Rules are evaluated top-to-bottom, so the
    // per-subsystem overrides below win over the "all off" baseline. Enabling a
    // subsystem's *.debug just lets its qCDebug lines reach the handler; the
    // actual show/hide is still gated by the pref (verbose) or the emit-site
    // bool (kad / server-verbose), so this never leaks one channel into another.
    QStringList rules;
    rules << QStringLiteral("emule.*.debug=false");
    if (thePrefs.verbose())
        rules << QStringLiteral("emule.*.debug=true");
    if (thePrefs.kadVerboseLog())
        rules << QStringLiteral("emule.kad.debug=true");
    if (thePrefs.serverVerboseLog())
        rules << QStringLiteral("emule.serverv.debug=true");
    // Usenet debug lines are rate-bounded (no per-article lines) and have their
    // own tab and file, so they are always on.
    rules << QStringLiteral("emule.usenet.debug=true");
    // ed3k: emule.ed3k.debug (ed3k Verbose tab) follows verbose above; its Kad's debug
    // lines follow the Kad switch, like eD2K's Kad
    if (thePrefs.kadVerboseLog())
        rules << QStringLiteral("emule.ed3k.kad.debug=true");

    QLoggingCategory::setFilterRules(rules.join(QLatin1Char('\n')));
}

void DaemonApp::applyLogFileSettings()
{
    applyLogFileSink(AppConfig::configDir(), QStringLiteral("emulecored"),
                     thePrefs.logToDiskCore(), thePrefs.maxLogFileSize());
}

#ifdef EMULE_HAVE_ED3K
void DaemonApp::applyEd3kChatOptions()
{
    if (!m_ed3kFriends)
        return;
    ed3k::Ed3kFriends::ChatOptions o;
    o.friendsOnly = thePrefs.msgOnlyFriends();
    o.rateLimit = thePrefs.enableSpamFilter();
    o.filter = thePrefs.messageFilter().split(QLatin1Char('|'), Qt::SkipEmptyParts);
    m_ed3kFriends->setChatOptions(o);
}
#endif

} // namespace eMule
