#include "queue/UsenetStateWriter.h"

#include "queue/UsenetQueueStore.h"

#include <QElapsedTimer>

#include <algorithm>
#include <chrono>
#include <limits>

namespace eMule::usenet {

UsenetStateWriter::UsenetStateWriter()
    : m_thread([this](std::stop_token stop) { run(stop); })
{
}

UsenetStateWriter::~UsenetStateWriter()
{
    flush();
    m_thread.request_stop();
    m_wake.notify_all();
}

void UsenetStateWriter::post(UsenetQueueItem snapshot)
{
    if (snapshot.id.isEmpty())
        return;
    {
        std::lock_guard lock(m_mutex);
        const QString id = snapshot.id;
        auto it = m_pending.find(id);
        if (it == m_pending.end()) {
            it = m_pending.insert(id, Pending{});
            m_order.push_back(id);
        }
        it->save = std::move(snapshot);
        ++m_generation;
    }
    m_wake.notify_all();
}

void UsenetStateWriter::postRemove(const QString& id)
{
    if (id.isEmpty())
        return;
    {
        std::lock_guard lock(m_mutex);
        auto it = m_pending.find(id);
        if (it == m_pending.end()) {
            it = m_pending.insert(id, Pending{});
            m_order.push_back(id);
        }
        it->save.reset();
        it->removeFirst = true;
        ++m_generation;
    }
    m_wake.notify_all();
}

void UsenetStateWriter::flush()
{
    std::unique_lock lock(m_mutex);
    ++m_flushRequests;
    ++m_generation;
    m_wake.notify_all();
    m_idle.wait(lock, [this] { return m_pending.isEmpty() && !m_busy; });
    --m_flushRequests;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

qint64 UsenetStateWriter::nowMs()
{
    static const QElapsedTimer clock = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return clock.elapsed();
}

qint64 UsenetStateWriter::dueAtMs(const QString& id, const Pending& p) const
{
    if (p.removeFirst || m_flushRequests > 0)
        return 0;
    const auto last = m_lastWriteMs.constFind(id);
    return last == m_lastWriteMs.constEnd() ? 0 : *last + kMinIntervalMs;
}

void UsenetStateWriter::run(std::stop_token stop)
{
    std::unique_lock lock(m_mutex);
    while (true) {
        if (stop.stop_requested() && m_pending.isEmpty())
            return;

        // The first due entry in posting order, else when the next one falls due.
        const qint64 now = nowMs();
        qint64 nextDue = std::numeric_limits<qint64>::max();
        auto due = m_order.end();
        for (auto it = m_order.begin(); it != m_order.end(); ++it) {
            const qint64 at = dueAtMs(*it, *m_pending.constFind(*it));
            if (at <= now || stop.stop_requested()) {
                due = it;
                break;
            }
            nextDue = std::min(nextDue, at);
        }

        if (due == m_order.end()) {
            if (m_pending.isEmpty())
                m_idle.notify_all();
            // Any post, remove or flush bumps m_generation and ends the wait early.
            const quint64 seen = m_generation;
            const auto changed = [this, seen] { return m_generation != seen; };
            if (nextDue == std::numeric_limits<qint64>::max())
                m_wake.wait(lock, stop, changed);
            else
                m_wake.wait_for(lock, stop, std::chrono::milliseconds(nextDue - now), changed);
            continue;
        }

        const QString id = *due;
        m_order.erase(due);
        Pending work = m_pending.take(id);
        m_busy = true;
        lock.unlock();

        if (work.removeFirst)
            UsenetQueueStore::remove(id);
        if (work.save) {
            UsenetQueueStore::save(*work.save);
            m_writes.fetch_add(1);
        }

        lock.lock();
        m_busy = false;
        if (work.save)
            m_lastWriteMs.insert(id, nowMs());
        else
            m_lastWriteMs.remove(id);
        if (m_pending.isEmpty())
            m_idle.notify_all();
    }
}

} // namespace eMule::usenet
