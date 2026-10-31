#pragma once

/// @file DaemonApp.h
/// @brief Orchestrator for the headless core daemon.
///
/// Owns CoreSession + IpcServer + CoreNotifierBridge.
/// Manages startup and shutdown sequence.

#include "LogRelay.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

class QTimer;

namespace eMule {

class BandwidthCoordinator;
class CoreSession;
class ApiEventFeeder;
class ApiEventHub;
class DaemonApiBackend;
class DaemonTorrentWebBackend;
class DaemonUsenetWebBackend;
class TranslationRouter;
class WebServer;

class IpcServer;
class CoreNotifierBridge;
class PowerManager;
class AutoUpdater;

namespace usenet { class UsenetSession; }
namespace bt { class BtSession; }
namespace ed3k { class Ed3kSession; class Ed3kUsenetJoin; class UsenetJoinQueue; class Ed3kTorrentJoin; class Ed3kEd2kJoin; class Ed3kShared; class SharedFileSource; class Ed3kFriends; class Ed3kRooms; class Ed3kBootstrap; class Ed3kNames; class Ed3kSiteServer; class Ed3kCredit; class Ed3kUploads; class Ed3kFilters; class Ed3kChain; class Ed3kHosterJoin; class Ed3kJdResolver; class Ed3kJavaRuntime; class Ed3kDownloads; class Ed3kSearch; class Ed3kSets; class Ed3kLive; class Ed3kLiveBroadcast; class Ed3kPaid; }
namespace indexer { class IndexerFeedList; class IndexerSearchList; }

class DaemonApp : public QObject {
    Q_OBJECT

public:
    explicit DaemonApp(QObject* parent = nullptr);
    ~DaemonApp() override;

    /// Start the core session and IPC server. Returns true on success.
    bool start();
    /// Why start() failed; empty after success.
    [[nodiscard]] QString startError() const { return m_startError; }
    /// Start without auto-connecting (--hold-connect). Before start().
    void setConnectHold(bool hold) { m_connectHold = hold; }
    /// IPC port from --port, not saved to prefs; 0 = prefs. Before start().
    void setIpcPortOverride(quint16 port) { m_ipcPortOverride = port; }
    /// --autoupdate[=hours]; @p hours 0 = version-check interval. Before start().
    void setAutoUpdate(bool on, int hours) { m_autoUpdateOn = on; m_autoUpdateHours = hours; }
    /// --autoupdate given and not in a container (the updater starts once loaded).
    [[nodiscard]] static bool autoUpdateEnabled();

    /// Quit, then have main() start the daemon again (new ports, same arguments).
    static void requestRestart();
    [[nodiscard]] static bool restartRequested() { return s_restartRequested; }

    /// Stop the IPC server and core session.
    void stop();

    /// Returns true if both core session and IPC server are running.
    [[nodiscard]] bool isRunning() const;

    /// Access the web server (may be nullptr if not running).
    [[nodiscard]] WebServer* webServer() const { return m_webServer.get(); }

    /// The installed translator the web UI's languages go through. Not owned;
    /// set before start(). Without it the web UI is English.
    void setTranslationRouter(TranslationRouter* router) { m_translations = router; }

    /// Access the singleton instance (set during start, cleared on stop).
    [[nodiscard]] static DaemonApp* instance() { return s_instance; }

    /// Access the core session (nullptr if not started).
    [[nodiscard]] CoreSession* coreSession() const { return m_coreSession.get(); }

    /// Access the Usenet engine (nullptr before start()).
    ///
    /// Owned here rather than by AppContext or CoreSession: both live in
    /// eMule::Core, and core must never depend on eMule::Usenet. The daemon is
    /// the first place that legitimately knows about both.
    [[nodiscard]] usenet::UsenetSession* usenetSession() const { return m_usenetSession.get(); }

    /// Access the shared indexer search session (nullptr before start()).
    ///
    /// Owned here for the same reason UsenetSession is: eMule::Indexer is a peer
    /// of core, not a part of it, and AppContext may not reach across.
    [[nodiscard]] indexer::IndexerSearchList* indexerSearches() const
    {
        return m_indexerSearches.get();
    }

    /// Return all buffered log entries with id > @p lastLogId.
    [[nodiscard]] static std::vector<Ipc::LogEntry> logsSince(int64_t lastLogId);

    /// Random token generated once per daemon process. GUI uses this to detect
    /// daemon restarts and reset its log checkpoints accordingly.
    [[nodiscard]] static QString sessionToken();

    /// Stage of the start-up load ("part-files", …), empty once it is over.
    [[nodiscard]] static QString loadStage();

    /// (Re)compose the Qt logging filter rules from the current logging prefs
    /// (verbose / kadVerboseLog / serverVerboseLog) and apply them. Each debug
    /// subsystem is enabled independently so a single toggle can surface its
    /// channel without turning on the global verbose firehose. Also refreshes
    /// the per-subsystem emit-site gates (setServerVerboseLogging). Safe to call
    /// at startup and whenever a logging pref changes at runtime.
    static void applyLogFilterRules();

    /// Open or close the daemon's own log files (emulecored.log,
    /// emulecored_Verbose.log, _Kad, _Usenet, _Ed3k, _Ed3kKad, _Leecher in the config directory) to
    /// match the logToDiskCore pref. The GUI runs the same call for its own set —
    /// one switch per process, so a line's origin is never in doubt. Safe to call
    /// at startup and whenever the pref changes at runtime.
    static void applyLogFileSettings();

private:
    /// CoreSession::loaded: start what had to wait for the file lists.
    void onCoreLoaded();
    /// --autoupdate: create and start the AutoUpdater.
    void startAutoUpdater();
    /// Tell every GUI whether the core is still reading its files, and which.
    void broadcastCoreLoading(bool loading);
    void startWebServer();
    void stopWebServer();
    void restartWebServer();
    void onWebServerConfigChanged();

    /// Re-apply the news-server list after an Options save. Reached from any
    /// IPC client through IpcServer::usenetConfigChanged.
    void applyUsenetServers();

    /// Re-apply the BitTorrent settings after an Options save, starting or stopping
    /// the engine. Reached through IpcServer::btConfigChanged.
    void applyBtConfig();

    /// Start, stop or restart the ed3k node after an ed3k* preference moved.
    /// Reached through IpcServer::ed3kConfigChanged.
    void applyEd3kConfig();

    /// Re-read the indexer account list after an Options save. Reached from any
    /// IPC client through IpcServer::indexerConfigChanged.
    void applyIndexerConfig();

    /// Turn the queue's signals into IPC push events.
    ///
    /// It lives here rather than in CoreNotifierBridge because the bridge is
    /// built on core signals and the queue is not a core object — routing it
    /// through the bridge would mean linking eMule::Usenet into a class whose
    /// whole job is core.
    void connectUsenetPushes();

    /// Turn the BitTorrent session's signals into IPC push events. Same reasoning
    /// as connectUsenetPushes.
    void connectBtPushes();

    /// Turn the search list's signals into IPC push events. Same reasoning as
    /// connectUsenetPushes: the bridge is built on core signals and these are
    /// not core objects.
    void connectIndexerPushes();

    /// Hand the feed poller somewhere to put an .nzb, and forward its status
    /// reports. The only place eMule::Indexer and eMule::Usenet meet.
    void connectIndexerFeedSink();

    void installLogForwarder();
    void removeLogForwarder();
    static void logMessageHandler(QtMsgType type, const QMessageLogContext& context,
                                  const QString& msg);

    std::unique_ptr<CoreSession> m_coreSession;
    bool m_connectHold = false;
    quint16 m_ipcPortOverride = 0;
    std::unique_ptr<IpcServer> m_ipcServer;
    std::unique_ptr<CoreNotifierBridge> m_notifierBridge;
    /// Declared before m_webServer so the server that points at it dies first.
    std::unique_ptr<DaemonUsenetWebBackend> m_usenetWebBackend;
#ifdef EMULE_HAVE_BITTORRENT
    std::unique_ptr<DaemonTorrentWebBackend> m_torrentWebBackend;
#endif
    std::unique_ptr<DaemonApiBackend> m_apiBackend;
    /// Events for GET /api/v1/events; outlive a web-server restart.
    ApiEventHub* m_eventHub = nullptr;
    ApiEventFeeder* m_eventFeeder = nullptr;
    std::unique_ptr<WebServer> m_webServer;
    std::unique_ptr<usenet::UsenetSession> m_usenetSession;
#ifdef EMULE_HAVE_BITTORRENT
    std::unique_ptr<bt::BtSession> m_btSession;
#endif
#ifdef EMULE_HAVE_ED3K
    /// The ed3k node (packmule); started after Usenet, stopped before it
    std::unique_ptr<ed3k::Ed3kSession> m_ed3kSession;
    /// the usenet join (usenet-join-host.md): needs both sessions; dies before them
    std::unique_ptr<ed3k::UsenetJoinQueue> m_usenetJoinQueue;
    std::unique_ptr<ed3k::Ed3kUsenetJoin> m_usenetJoin;
    /// eMule's shared files over ed3k (pref ed3kShareFiles); dies before the session
    std::unique_ptr<ed3k::SharedFileSource> m_ed3kSharedSource;
    std::unique_ptr<ed3k::Ed3kShared> m_ed3kShared;
    /// ed3k friends and chat (Ed3kFriendsBridge); dies before the session
    std::unique_ptr<ed3k::Ed3kFriends> m_ed3kFriends;
    /// ed3k rooms (Ed3kRoomsBridge); dies before the session
    std::unique_ptr<ed3k::Ed3kRooms> m_ed3kRooms;
    /// node list sources (pref ed3kNodeLists) and the asks of ed3k:nodes links; dies before the session
    std::unique_ptr<ed3k::Ed3kBootstrap> m_ed3kBootstrap;
    /// readable names and the local website server (Ed3kNamesBridge, pref ed3kSitePort); die before the session
    std::unique_ptr<ed3k::Ed3kNames> m_ed3kNames;
    std::unique_ptr<ed3k::Ed3kSiteServer> m_ed3kSites;
    /// credits (Ed3kCreditBridge, prefs ed3kCredit*); dies before the session
    std::unique_ptr<ed3k::Ed3kCredit> m_ed3kCredit;
    /// the upload queue view (GetUploads), filter lists (Ed3kFiltersBridge), the chaintube
    /// chain (prefs ed3kChain*); die before the session
    std::unique_ptr<ed3k::Ed3kUploads> m_ed3kUploads;
    std::unique_ptr<ed3k::Ed3kFilters> m_ed3kFilters;
    std::unique_ptr<ed3k::Ed3kChain> m_ed3kChain;
    /// plain ed3k downloads (Ed3kDownloadsBridge) and ed3k keyword search (SearchType::Ed3k
    /// through MetaSearchService); die before the session
    std::unique_ptr<ed3k::Ed3kDownloads> m_ed3kDownloads;
    std::unique_ptr<ed3k::Ed3kSearch> m_ed3kSearch;
    /// eD2K downloads fed from ed3k too (§14.4); dies before the session
    std::unique_ptr<ed3k::Ed3kEd2kJoin> m_ed2kJoin;
    /// followed sets (their members through m_ed3kDownloads) and live streams (on the website
    /// server's port, Ed3kSetsBridge); die before the downloads and the website server
    /// paid subscriptions (Ed3kPaidBridge); the sets follow paid sets there: dies after them
    std::unique_ptr<ed3k::Ed3kPaid> m_ed3kPaid;
    std::unique_ptr<ed3k::Ed3kSets> m_ed3kSets;
    std::unique_ptr<ed3k::Ed3kLive> m_ed3kLive;
    /// live broadcasting from an HLS folder (Ed3kSetsBridge)
    std::unique_ptr<ed3k::Ed3kLiveBroadcast> m_ed3kLiveBroadcast;
    /// downloads from hoster links (hoster.md, Ed3kHosterBridge, prefs ed3kHoster*); dies before the session
    std::unique_ptr<ed3k::Ed3kHosterJoin> m_hosterJoin;
    /// once a second: the node's totals into the ed3k statistics (Ed3kBridge::sampleCounters)
    QTimer* m_ed3kStatsTimer = nullptr;
#ifdef EMULE_HAVE_HOSTER_JD
    /// the JDownloader sidecar and the Java runtime it runs on
    std::unique_ptr<ed3k::Ed3kJdResolver> m_jdResolver;
    std::unique_ptr<ed3k::Ed3kJavaRuntime> m_javaRuntime;
    /// the java pref the last lookup was for
    QString m_javaLookedFor{QStringLiteral("\x01")};
#endif
    /// The hoster prefs into the join, the resolver and the Java lookup
    void applyHosterConfig();
    /// Stops the ed3k node and its joins (restart: applyEd3kConfig()).
    void stopEd3kNode();
    /// The ed3k chat options from the eD2K "Messages and Comments" prefs
    void applyEd3kChatOptions();
#ifdef EMULE_HAVE_BITTORRENT
    /// the torrent join (torrent-join.md Part C): needs both sessions; dies before them
    std::unique_ptr<ed3k::Ed3kTorrentJoin> m_torrentJoin;
#endif
#endif
    /// eD2K/Usenet/BitTorrent share of the global limits; after both sessions
    std::unique_ptr<BandwidthCoordinator> m_bandwidth;
    std::unique_ptr<indexer::IndexerSearchList> m_indexerSearches;
    std::unique_ptr<indexer::IndexerFeedList> m_indexerFeeds;
    std::unique_ptr<PowerManager> m_powerManager;
    bool m_autoUpdateOn = false;
    int m_autoUpdateHours = 0;
    std::unique_ptr<AutoUpdater> m_autoUpdater;
    TranslationRouter* m_translations = nullptr;
    bool m_running = false;
    QString m_startError;

    static DaemonApp* s_instance;
    static inline bool s_restartRequested = false;
    static QtMessageHandler s_previousHandler;
    static QString s_sessionToken;  ///< Random UUID for this daemon process lifetime.
    static QString s_loadStage;     ///< See loadStage().
};

} // namespace eMule
