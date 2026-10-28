#pragma once

/// @file Ed3kJdResolver.h
/// @brief The hoster resolver as a Java sidecar running JDownloader's plugins unmodified
/// (docs/ed3k-spec/docs/hoster-resolver.local.md, tools/hoster-resolver).
///
/// - A process of its own, started at the first task and stopped after kIdleStopMs without
///   one. JSON lines over stdin/stdout; it ends when stdin closes, so it dies with the daemon.
/// - The plugins come as resolver.zip (scripts/build-hoster-resolver.sh), shipped with the
///   application and unpacked into <dataDir>/plugins when it changed. JD writes its caches
///   next to the plugin classes, so they must live in a writable folder.
/// - It gets its data folder and a scratch folder per task, nothing else: no key, no shared
///   folder, no IPC token (note §8). Its stderr is JD's log: only the driver's own lines
///   ("resolver: ...") are logged, as debug.
/// - Account data goes in with a request and never comes back out.

#ifdef EMULE_HAVE_HOSTER_JD

#include "Ed3kResolver.h"

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <map>

class QProcess;
class QJsonObject;

namespace eMule::ed3k {

struct JdResolverConfig {
    QString java;          ///< the java program; empty: not available
    QString dataDir;       ///< JD's root (cfg/, tmp/, logs/) and plugins/
    QString zip;           ///< resolver.zip to install from; empty: use what is installed
    QString proxy;         ///< for every request; empty = direct
    bool askPuzzles = true;
};

class Ed3kJdResolver : public Ed3kResolver {
    Q_OBJECT

public:
    static constexpr int kProtocol = 1;
    static constexpr int kIdleStopMs = 5 * 60 * 1000;

    explicit Ed3kJdResolver(QObject* parent = nullptr);
    ~Ed3kJdResolver() override;

    /// A new java or zip stops a running sidecar; the next task starts it again.
    void setConfig(const JdResolverConfig& config);
    [[nodiscard]] const JdResolverConfig& config() const { return m_config; }

    /// resolver.zip next to the application (macOS Resources, share/emuleqt, a build tree).
    [[nodiscard]] static QString findBundledZip();
    /// Unpacks @p zip into @p dataDir/plugins when it differs from what is there. False:
    /// failed (@p error).
    static bool install(const QString& zip, const QString& dataDir, QString* error);
    /// The command line the sidecar runs with (tests).
    [[nodiscard]] QStringList arguments() const;

    [[nodiscard]] bool available() const override;
    [[nodiscard]] QString unavailableReason() const override;
    [[nodiscard]] QString description() const override { return m_description; }
    [[nodiscard]] bool running() const;

    TaskId match(const QString& url) override;
    TaskId check(const QString& url, const std::optional<ResolverAccount>& account) override;
    TaskId expand(const QString& url) override;
    TaskId open(const QString& url, const std::optional<ResolverAccount>& account, const QString& dir,
                std::uint64_t have) override;
    TaskId accountCheck(const QString& host, const ResolverAccount& account) override;
    void answerPuzzle(std::uint64_t puzzle, const QString& text) override;
    void cancel(TaskId task) override;
    void setAskPuzzles(bool on) override;
    void setProxy(const QString& url) override;

    /// Parses one event line of the sidecar (tests). nullopt: not an event of a task.
    [[nodiscard]] static std::optional<std::pair<TaskId, ResolverEvent>> parseEvent(const QByteArray& line);

private:
    /// Sends @p req with a new id; starts the sidecar first.
    TaskId request(QJsonObject req, bool task);
    void send(const QJsonObject& req);
    bool ensureStarted();
    void stop();
    void onStdout();
    void onStderr();
    void onFinished();
    void onIdle();
    /// Ends every open task with Failed (the sidecar died or was stopped).
    void failPending(const QString& code);
    void handleLine(const QByteArray& line);

    JdResolverConfig m_config;
    QPointer<QProcess> m_proc;
    QByteArray m_outBuf;
    QByteArray m_errBuf;
    QString m_description;
    QString m_installError;
    TaskId m_nextId = 1;
    /// open tasks; pure requests (hello, setProxy) are not counted
    std::map<TaskId, bool> m_pending;
    QTimer m_idle;
    bool m_stopping = false;
};

} // namespace eMule::ed3k

#endif // EMULE_HAVE_HOSTER_JD
