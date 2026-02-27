#pragma once

/// @file DaemonApp.h
/// @brief Orchestrator for the headless core daemon.
///
/// Owns CoreSession + IpcServer + CoreNotifierBridge.
/// Manages startup and shutdown sequence.

#include <QObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace eMule {

class CoreSession;

class IpcServer;
class CoreNotifierBridge;

/// A single buffered log entry with a monotonic ID.
struct LogEntry {
    int64_t id = 0;
    QString category;
    QtMsgType severity = QtDebugMsg;
    QString message;
    qint64 timestamp = 0;  ///< Unix seconds when the message was generated.
};

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

    /// Return all buffered log entries with id > @p lastLogId.
    [[nodiscard]] static std::vector<LogEntry> logsSince(int64_t lastLogId);

    /// Random token generated once per daemon process. GUI uses this to detect
    /// daemon restarts and reset its log checkpoints accordingly.
    [[nodiscard]] static QString sessionToken();


    /// Maximum number of log entries to buffer (per daemon instance).
    static constexpr int MaxLogBuffer = 500;

private:
    void installLogForwarder();
    void removeLogForwarder();
    static void logMessageHandler(QtMsgType type, const QMessageLogContext& context,
                                  const QString& msg);

    std::unique_ptr<CoreSession> m_coreSession;
    std::unique_ptr<IpcServer> m_ipcServer;
    std::unique_ptr<CoreNotifierBridge> m_notifierBridge;
    bool m_running = false;

    static DaemonApp* s_instance;
    static QtMessageHandler s_previousHandler;
    static int64_t s_nextLogId;
    static std::deque<LogEntry> s_logBuffer;
    static std::mutex s_logMutex;
    static QString s_sessionToken;  ///< Random UUID for this daemon process lifetime.
};

} // namespace eMule
