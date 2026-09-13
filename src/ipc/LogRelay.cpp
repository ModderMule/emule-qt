/// @file LogRelay.cpp
/// @brief The daemon's log buffer and its IPC forward — implementation.

#include "LogRelay.h"

#include <QDateTime>

namespace eMule::Ipc {

LogRelay::LogRelay(QObject* parent)
    : QObject(parent)
{
}

LogRelay::~LogRelay() = default;

void LogRelay::append(const QString& category, QtMsgType severity, const QString& message)
{
    const qint64 ts = QDateTime::currentSecsSinceEpoch();
    {
        std::lock_guard lock(m_mutex);
        m_buffer.push_back({m_nextId++, category, severity, message, ts});
        while (static_cast<int>(m_buffer.size()) > kMaxBuffered)
            m_buffer.pop_front();
    }

    // One posted flush per burst: forty workers logging in the same millisecond
    // cost the main thread one wakeup, not forty.
    if (!m_flushQueued.exchange(true))
        QMetaObject::invokeMethod(this, &LogRelay::flush, Qt::QueuedConnection);
}

std::vector<LogEntry> LogRelay::since(int64_t lastId) const
{
    std::lock_guard lock(m_mutex);
    std::vector<LogEntry> result;
    for (const auto& entry : m_buffer) {
        if (entry.id > lastId)
            result.push_back(entry);
    }
    return result;
}

IpcMessage LogRelay::toPush(const LogEntry& entry)
{
    IpcMessage push(IpcMsgType::PushLogMessage, 0);
    push.append(static_cast<qint64>(entry.id));
    push.append(entry.category);
    push.append(static_cast<qint64>(entry.severity));
    push.append(entry.message);
    push.append(entry.timestamp);
    return push;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void LogRelay::flush()
{
    // Cleared before the read: a line appended from here on posts the next flush
    // rather than waiting in the buffer for one that already ran.
    m_flushQueued = false;
    for (const auto& entry : since(m_lastForwarded)) {
        m_lastForwarded = entry.id;
        emit ready(toPush(entry));
    }
}

} // namespace eMule::Ipc
