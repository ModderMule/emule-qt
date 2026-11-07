#pragma once

/// @file Ed3kJdResolver.h
/// @brief The hoster resolver as a Java sidecar running JDownloader's plugins unmodified
/// (docs/ed3k-spec/docs/hoster-resolver.local.md, tools/hoster-resolver).
///
/// - A process of its own, started at the first task and stopped after kIdleStopMs without
///   one. JSON lines over a loopback socket the daemon listens on; stdin carries only the
///   auth token and is closed then, so JD code reading the console gets EOF, not our requests.
///   It ends when the socket closes, so it dies with the daemon.
/// - The plugins come as resolver.zip (scripts/build-hoster-resolver.sh), shipped with the
///   application and unpacked into <dataDir>/plugins when it changed. JD never writes there
///   (its state goes to <dataDir>/jd), so instances can share one unpacked copy: sharedDir.
/// - A new zip (Ed3kResolverUpdater) waits while the sidecar runs, so a running download or
///   check is never cut; it goes in once the sidecar ends (idle stop). It is unpacked on a
///   worker thread; tasks asked meanwhile are held and sent after.
/// - It gets its data folder and a scratch folder per task, nothing else: no key, no shared
///   folder, no IPC token (note §8). Its stdout+stderr are JD's log: only the driver's own lines
///   ("resolver: ...") are logged, as debug.
/// - Account data goes in with a request and never comes back out.

#ifdef EMULE_HAVE_HOSTER_JD

#include "Ed3kResolver.h"
#include "utils/SidecarProcess.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <map>
#include <vector>

class QTcpServer;
class QTcpSocket;
class QThread;
class QJsonObject;

namespace eMule::ed3k {

struct JdResolverConfig {
    QString java;          ///< the java program; empty: not available
    QString dataDir;       ///< JD's root (jd/: cfg, tmp, logs, captchas) and plugins/
    /// YAML only: plugins unpacked once for several instances, in <sharedDir>/<zip hash>/;
    /// empty: <dataDir>/plugins per instance. JD's state stays in <dataDir>/jd either way.
    QString sharedDir;
    QString zip;           ///< resolver.zip to install from; empty: use what is installed
    QString proxy;         ///< for every request; empty = direct
    bool askPuzzles = true;
    /// captcha solver accounts; sent after every start (the sidecar forgets them when it ends)
    QList<SolverAccount> solvers;
    /// hoster downloads on: plugins unpacked, sidecar may start. Off: nothing is created and
    /// the unpacked plugins are removed (derived from zip; unpacked again when switched on)
    bool enabled = true;
};

class Ed3kJdResolver : public Ed3kResolver {
    Q_OBJECT

public:
    static constexpr int kProtocol = 2;
    static constexpr int kConnectTimeoutMs = 60 * 1000;
    static constexpr int kIdleStopMs = 5 * 60 * 1000;
    static constexpr int kSharedKeepDays = 30;
    static constexpr int kSharedLockWaitMs = 3 * 60 * 1000;

    explicit Ed3kJdResolver(QObject* parent = nullptr);
    ~Ed3kJdResolver() override;

    /// A new java or zip stops a running sidecar; the next task starts it again.
    void setConfig(const JdResolverConfig& config);
    [[nodiscard]] const JdResolverConfig& config() const { return m_config; }

    /// resolver.zip next to the application (macOS Resources, share/emuleqt, a build tree).
    [[nodiscard]] static QString findBundledZip();
    /// What resolver/revision.txt and resolver/api.txt of a zip say; 0: missing
    struct ZipInfo {
        int revision = 0;     ///< JD SVN revision
        int api = 0;          ///< the sidecar protocol it speaks (kProtocol)
        bool layout = false;  ///< Core.jar and jd/plugins/hoster there
    };
    [[nodiscard]] static ZipInfo readZipInfo(const QString& zip);
    /// Unpacks @p zip into @p dataDir/plugins when it differs from what is there. False:
    /// failed (@p error).
    static bool install(const QString& zip, const QString& dataDir, QString* error);
    /// Unpacks @p zip into @p sharedDir/<hash of zip> unless there; locked against other
    /// instances. Returns the resolver root, empty: failed (@p error). Prunes versions no
    /// instance started for kSharedKeepDays.
    static QString installShared(const QString& zip, const QString& sharedDir, QString* error);
    /// Removes what install() unpacked (@p dataDir/plugins and a left-over plugins.tmp)
    static void removePlugins(const QString& dataDir);
    /// The unpacked resolver folder in use (Core.jar, jd/plugins, libs); empty: none
    [[nodiscard]] QString pluginRoot() const { return m_root; }
    /// The command line the sidecar runs with (tests). Holds -Demuleqt.port while listening.
    [[nodiscard]] QStringList arguments() const;

    [[nodiscard]] bool available() const override;
    [[nodiscard]] QString unavailableReason() const override;
    [[nodiscard]] QString description() const override { return m_description; }
    [[nodiscard]] QString engineVersion() const override
    {
        return m_revision.isEmpty() ? QString() : QStringLiteral("r") + m_revision;
    }
    [[nodiscard]] bool running() const;
    /// A new zip waits for the sidecar to end; empty: none
    [[nodiscard]] QString pendingZip() const { return m_pendingZip; }
    /// "r<rev>" of pendingZip(); empty: none or unknown
    [[nodiscard]] QString pendingVersion() const override { return m_pendingVersion; }
    /// A new zip is being unpacked (worker thread)
    [[nodiscard]] bool installing() const { return m_installThread != nullptr; }
    [[nodiscard]] QList<SolverAccountState> solverStates() const override { return m_solverStates; }
    /// No zip and nothing unpacked: emits pluginsNeeded() (once while fetching)
    void wantPlugins() override;
    /// The first zip is being downloaded (Ed3kResolverUpdater): the reason the GUI shows
    void setFetching(bool on);

    TaskId match(const QString& url) override;
    TaskId check(const QString& url, const std::optional<ResolverAccount>& account) override;
    TaskId expand(const QString& url) override;
    TaskId openContainer(const QString& path) override;
    TaskId open(const QString& url, const std::optional<ResolverAccount>& account, const QString& dir,
                std::uint64_t have) override;
    TaskId accountCheck(const QString& host, const ResolverAccount& account) override;
    TaskId sites(const QString& host) override;
    TaskId setSolverAccounts(const QList<SolverAccount>& accounts) override;
    TaskId solveTest(const QString& solver, const QString& type, const QByteArray& image = {}) override;
    void answerPuzzle(std::uint64_t puzzle, const QString& text) override;
    void cancel(TaskId task) override;
    void setAskPuzzles(bool on) override;
    void setProxy(const QString& url) override;

    /// Parses one event line of the sidecar (tests). nullopt: not an event of a task.
    [[nodiscard]] static std::optional<std::pair<TaskId, ResolverEvent>> parseEvent(const QByteArray& line);

signals:
    /// A link wants plugins and there are none: fetch the first zip
    void pluginsNeeded();

private:
    /// Sends @p req with a new id; starts the sidecar first.
    TaskId request(QJsonObject req, bool task);
    void send(const QJsonObject& req);
    struct InstallResult {
        QString root;
        QString error;
        QString revision;
    };
    /// Unpacks the plugins per @p c (enabled, with dataDir); any thread
    static InstallResult installFor(const JdResolverConfig& c);
    /// Unpacks the plugins per m_config; sets m_root and m_installError
    void installPlugins();
    /// installPlugins() on a worker thread: a new zip swapped in at run time
    void installAsync();
    void onInstalled(const InstallResult& r);
    /// Sends the tasks held while installing, or fails them when the resolver can't start
    void releaseHeld();
    bool ensureStarted();
    /// m_config.solvers to the running sidecar
    void sendSolvers();
    void stop();
    /// Closes socket and server; queued requests are dropped
    void closeChannel();
    void onLog(const QByteArray& line);
    void onFinished(int exitCode, bool crashed, bool expected);
    void onIdle();
    void onNewConnection();
    void onSocketRead();
    void onSocketGone();
    void onConnectTimeout();
    /// Ends every open task with Failed (the sidecar died or was stopped).
    void failPending(const QString& code);
    void handleLine(const QByteArray& line);
    /// Hoster Verbose: what a request asks, without url path, account or credentials
    static QString describeRequest(const QJsonObject& req);
    /// Hoster Verbose: one received event, its safe fields
    void logEvent(TaskId id, const QString& ev, const ResolverEvent& e);

    JdResolverConfig m_config;
    SidecarProcess m_sidecar;
    QPointer<QTcpServer> m_server;
    QPointer<QTcpSocket> m_sock;
    QByteArray m_token;       ///< per start; the sidecar's first line must carry it
    QByteArray m_sendQueue;   ///< requests made before the sidecar authenticated
    bool m_authed = false;
    QTimer m_connectTimer;
    QByteArray m_outBuf;      ///< socket read buffer
    QString m_description;
    QString m_installError;
    QString m_root;
    QString m_revision;   ///< JD revision number: the root's revision.txt or the sidecar's hello
    TaskId m_nextId = 1;
    /// open tasks; pure requests (hello, setProxy) are not counted
    std::map<TaskId, bool> m_pending;
    std::map<TaskId, qint64> m_progressLogged;   ///< task: last progress line (ms of the sidecar's uptime)
    QTimer m_idle;
    bool m_stopping = false;
    TaskId m_solverTask = 0;   ///< the solverAccounts sendSolvers() sent last
    QString m_pendingZip;      ///< waits for the sidecar to end
    QString m_pendingVersion;
    QPointer<QThread> m_installThread;
    bool m_reinstall = false;  ///< config changed while installing: install again after
    bool m_fetching = false;   ///< setFetching()
    std::vector<std::pair<TaskId, QJsonObject>> m_held;   ///< tasks asked while installing
    QList<SolverAccountState> m_solverStates;
};

} // namespace eMule::ed3k

#endif // EMULE_HAVE_HOSTER_JD
