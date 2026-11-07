#include "pch.h"
/// @file BackgroundFileWriter.cpp
/// @brief Atomic file writes on one worker thread, off the main thread.

#include "utils/BackgroundFileWriter.h"
#include "utils/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace eMule {

BackgroundFileWriter::BackgroundFileWriter()
    : m_thread([this] { run(); })
{
}

BackgroundFileWriter::~BackgroundFileWriter()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

BackgroundFileWriter& BackgroundFileWriter::instance()
{
    static BackgroundFileWriter writer;
    return writer;
}

void BackgroundFileWriter::write(const QString& path, QByteArray data, WriteFn fn)
{
    {
        std::lock_guard lock(m_mutex);
        const auto [it, added] = m_jobs.insert_or_assign(path, Job{std::move(data), std::move(fn)});
        if (added)
            m_order.push_back(path);
    }
    m_wake.notify_one();
}

std::optional<QByteArray> BackgroundFileWriter::pending(const QString& path) const
{
    std::lock_guard lock(m_mutex);
    if (const auto it = m_jobs.find(path); it != m_jobs.end())
        return it->second.data;
    if (m_writing && *m_writing == path)
        return m_writingData;
    return std::nullopt;
}

void BackgroundFileWriter::discard(const QString& path)
{
    std::unique_lock lock(m_mutex);
    if (m_jobs.erase(path) > 0)
        std::erase(m_order, path);
    m_idle.notify_all();   // a flush may wait for what was dropped
    waitNotWriting(lock, [&path](const QString& p) { return p == path; });
}

void BackgroundFileWriter::discardUnder(const QString& dir)
{
    const QString prefix = QDir::cleanPath(dir) + u'/';
    const auto under = [&prefix](const QString& p) { return p.startsWith(prefix); };
    std::unique_lock lock(m_mutex);
    std::erase_if(m_jobs, [&under](const auto& j) { return under(j.first); });
    std::erase_if(m_order, under);
    m_idle.notify_all();
    waitNotWriting(lock, under);
}

void BackgroundFileWriter::flush()
{
    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] { return m_jobs.empty() && !m_writing; });
}

bool BackgroundFileWriter::saveFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(data) != data.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void BackgroundFileWriter::run()
{
    std::unique_lock lock(m_mutex);
    for (;;) {
        m_wake.wait(lock, [this] { return m_stop || !m_order.empty(); });
        if (m_order.empty())
            return;   // stopped, all written
        const QString path = m_order.front();
        m_order.pop_front();
        auto node = m_jobs.extract(path);
        if (node.empty())
            continue;
        Job job = std::move(node.mapped());
        m_writing = path;
        m_writingData = job.data;
        lock.unlock();
        const WriteFn& fn = job.fn ? job.fn : WriteFn(&BackgroundFileWriter::saveFile);
        if (!fn(path, job.data))
            logWarning(QStringLiteral("Cannot write %1").arg(path));
        lock.lock();
        m_writing.reset();
        m_writingData.clear();
        m_idle.notify_all();
    }
}

void BackgroundFileWriter::waitNotWriting(std::unique_lock<std::mutex>& lock,
                                          const std::function<bool(const QString&)>& match)
{
    m_idle.wait(lock, [&] { return !m_writing || !match(*m_writing); });
}

} // namespace eMule
