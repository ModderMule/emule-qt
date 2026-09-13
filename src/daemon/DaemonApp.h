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

namespace eMule {

class CoreSession;
class WebServer;

class IpcServer;
class CoreNotifierBridge;

namespace usenet { class UsenetSession; }
namespace indexer { class IndexerFeedList; class IndexerSearchList; }

class DaemonApp : public QObject {
    Q_OBJECT

public:
    explicit DaemonApp(QObject* parent = nullptr);
    ~DaemonApp() override;

    /// Start the core session and IPC server. Returns true on success.
    bool start();

    /// Stop the IPC server and core session.
    void stop();

    /// Returns true if both core session and IPC server are running.
    [[nodiscard]] bool isRunning() const;

    /// Access the web server (may be nullptr if not running).
    [[nodiscard]] WebServer* webServer() const { return m_webServer.get(); }

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

    /// (Re)compose the Qt logging filter rules from the current logging prefs
    /// (verbose / kadVerboseLog / serverVerboseLog) and apply them. Each debug
    /// subsystem is enabled independently so a single toggle can surface its
    /// channel without turning on the global verbose firehose. Also refreshes
    /// the per-subsystem emit-site gates (setServerVerboseLogging). Safe to call
    /// at startup and whenever a logging pref changes at runtime.
    static void applyLogFilterRules();

    /// Open or close the daemon's own log files (emulecored.log,
    /// emulecored_Verbose.log and emulecored_Kad.log in the config directory) to
    /// match the logToDiskCore pref. The GUI runs the same call for its own set —
    /// one switch per process, so a line's origin is never in doubt. Safe to call
    /// at startup and whenever the pref changes at runtime.
    static void applyLogFileSettings();

private:
    void startWebServer();
    void stopWebServer();
    void restartWebServer();

    /// Re-apply the news-server list after an Options save. Reached from any
    /// IPC client through IpcServer::usenetConfigChanged.
    void applyUsenetServers();

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
    std::unique_ptr<IpcServer> m_ipcServer;
    std::unique_ptr<CoreNotifierBridge> m_notifierBridge;
    std::unique_ptr<WebServer> m_webServer;
    std::unique_ptr<usenet::UsenetSession> m_usenetSession;
    std::unique_ptr<indexer::IndexerSearchList> m_indexerSearches;
    std::unique_ptr<indexer::IndexerFeedList> m_indexerFeeds;
    bool m_running = false;

    static DaemonApp* s_instance;
    static QtMessageHandler s_previousHandler;
    static QString s_sessionToken;  ///< Random UUID for this daemon process lifetime.
};

} // namespace eMule
