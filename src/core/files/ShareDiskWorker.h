#pragma once

/// @file ShareDiskWorker.h
/// @brief Runs the shared file list's slow disk reads on a thread of its own.
///
/// A share on a NAS made every rescan wait on the network (#84). Folder listings,
/// the tree walk, the move probe and the stats of files shared one by one run here; each job
/// returns what to do with its result, which runs back on the owner's thread.
/// Jobs run one at a time, in order. A job of a lane replaces one of that lane
/// still waiting, so a busy share does not pile up work.

#include <QObject>
#include <QString>

#include <deque>
#include <functional>

class QThread;

namespace eMule {

/// What a job hands back: runs on the owner's thread.
using ShareDiskApply = std::function<void()>;
/// Runs on the disk thread; must not touch the owner.
using ShareDiskJob = std::function<ShareDiskApply()>;

/// Runs the jobs; lives on ShareDiskWorker's thread.
class ShareDiskJobRunner : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    void run(const ShareDiskJob& job);

signals:
    void done(const eMule::ShareDiskApply& apply);
};

class ShareDiskWorker : public QObject {
    Q_OBJECT

public:
    explicit ShareDiskWorker(QObject* parent = nullptr);
    ~ShareDiskWorker() override;

    /// Queues @p job; one of @p lane still waiting is replaced. An empty lane never replaces.
    void run(const QString& lane, ShareDiskJob job);
    /// A job of @p lane runs or waits.
    [[nodiscard]] bool busy(const QString& lane) const;
    /// A job of @p lane waits (not running yet): run() on that lane replaces it.
    [[nodiscard]] bool waiting(const QString& lane) const;
    /// Any job runs or waits.
    [[nodiscard]] bool busy() const { return m_running || !m_queue.empty(); }

private:
    struct Queued {
        QString lane;
        ShareDiskJob job;
    };

    void startNext();
    void onDone(const ShareDiskApply& apply);

    QThread* m_thread = nullptr;
    ShareDiskJobRunner* m_runner = nullptr;
    bool m_running = false;
    QString m_runningLane;
    std::deque<Queued> m_queue;
};

} // namespace eMule
