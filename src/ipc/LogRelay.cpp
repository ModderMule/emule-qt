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

IpcMessage LogRelay::toBatch(const std::vector<LogEntry>& entries)
{
    QCborArray rows;
    for (const auto& entry : entries) {
        rows.append(QCborArray{static_cast<qint64>(entry.id), entry.category,
                               static_cast<qint64>(entry.severity), entry.message,
                               entry.timestamp});
    }
    IpcMessage push(IpcMsgType::PushLogBatch, 0);
    push.append(rows);
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
    const auto entries = since(m_lastForwarded);
    if (entries.empty())
        return;
    m_lastForwarded = entries.back().id;
    // One frame for the burst: a line each cost the GUI a dispatch and a repaint
    emit ready(toBatch(entries));
}

} // namespace eMule::Ipc
