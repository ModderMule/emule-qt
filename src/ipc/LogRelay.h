#pragma once

/// @file LogRelay.h
/// @brief The daemon's log buffer, and the hop that gets its lines onto IPC sockets.
///
/// Qt runs a message handler on whichever thread logged, and a QTcpSocket may only
/// be written from its own thread. The daemon used to broadcast straight from the
/// handler, so every worker-thread log line was a cross-thread socket write — a
/// Usenet add opening ~40 connections at once took the daemon down. The relay
/// buffers on the calling thread and forwards on its own.

#include "IpcMessage.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace eMule::Ipc {

/// One buffered log line. Ids are monotonic per relay, i.e. per daemon process.
struct LogEntry {
    int64_t id = 0;
    QString category;
    QtMsgType severity = QtDebugMsg;
    QString message;
    qint64 timestamp = 0;  ///< Unix seconds when the message was generated.
};

/// Keeps the newest lines for SyncLogs and forwards each as a PushLogMessage,
/// always from the relay's own thread.
///
/// Deferred even for a line logged on that thread, as the GUI's LogWidget does.
/// One path keeps the id order strict, and a line logged inside a broadcast
/// ("IPC client disconnected") cannot re-enter the broadcast.
class LogRelay : public QObject {
    Q_OBJECT

public:
    /// Lines kept for SyncLogs. A burst bigger than this before the relay's thread
    /// runs loses its oldest lines from the push as well as from the replay.
    static constexpr int kMaxBuffered = 500;

    explicit LogRelay(QObject* parent = nullptr);
    ~LogRelay() override;

    /// Buffer one line and schedule its forward. Thread-safe.
    void append(const QString& category, QtMsgType severity, const QString& message);

    /// Buffered lines with id > @p lastId, oldest first. Thread-safe.
    [[nodiscard]] std::vector<LogEntry> since(int64_t lastId) const;

    /// The PushLogMessage frame: [logId, category, severity, message, timestamp].
    [[nodiscard]] static IpcMessage toPush(const LogEntry& entry);

signals:
    /// A line is due for broadcast. Emitted on the relay's thread, in id order.
    void ready(const eMule::Ipc::IpcMessage& msg);

private:
    void flush();

    mutable std::mutex m_mutex;
    std::deque<LogEntry> m_buffer;          ///< guarded by m_mutex
    int64_t m_nextId = 1;                   ///< guarded by m_mutex
    int64_t m_lastForwarded = 0;            ///< relay's thread only
    std::atomic_bool m_flushQueued{false};  ///< a flush() is posted and not yet run
};

} // namespace eMule::Ipc
