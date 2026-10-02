#pragma once

/// @file UsenetStateWriter.h
/// @brief Writes `.nzbstate` sidecars on a thread of their own.
///
/// UsenetQueue marks an item dirty on every finished article and used to
/// re-serialise the whole sidecar (every segment's message-id) on its dispatch
/// thread four times a second. On a large NZB that is megabytes of YAML per
/// tick, and freed connections waited for new work meanwhile.
///
/// Now the queue posts a snapshot (cheap: every member is implicitly shared Qt
/// data) and this thread writes it. Per item, only the newest snapshot is kept,
/// and saves are debounced to one per kMinIntervalMs. A remove is never delayed
/// and runs in posting order, so a pending save cannot resurrect a removed item.
/// The crash window is at most ~1 s of progress bits; the bytes are on disk and
/// those articles are simply fetched again.

#include "queue/UsenetQueueItem.h"

#include <QHash>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace eMule::usenet {

class UsenetStateWriter {
public:
    static constexpr int kMinIntervalMs = 1000;

    UsenetStateWriter();
    ~UsenetStateWriter();

    UsenetStateWriter(const UsenetStateWriter&) = delete;
    UsenetStateWriter& operator=(const UsenetStateWriter&) = delete;

    /// Queue @p snapshot for writing; replaces any not yet written for its id.
    void post(UsenetQueueItem snapshot);

    /// Drop any pending save for @p id and remove its sidecar.
    void postRemove(const QString& id);

    /// Block until everything posted so far is on disk, debounce ignored.
    void flush();

    /// Sidecar writes performed so far. For tests.
    [[nodiscard]] int writesPerformed() const { return m_writes.load(); }

private:
    struct Pending {
        bool removeFirst = false;
        std::optional<UsenetQueueItem> save;
    };

    void run(std::stop_token stop);
    [[nodiscard]] qint64 dueAtMs(const QString& id, const Pending& p) const;
    [[nodiscard]] static qint64 nowMs();

    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::condition_variable m_idle;

    QHash<QString, Pending> m_pending;
    std::deque<QString> m_order;            ///< ids in first-posted order
    QHash<QString, qint64> m_lastWriteMs;
    int m_flushRequests = 0;
    quint64 m_generation = 0;               ///< bumped by every post/remove/flush
    bool m_busy = false;

    std::atomic<int> m_writes{0};
    std::jthread m_thread;                  ///< last: started after the state above
};

} // namespace eMule::usenet
