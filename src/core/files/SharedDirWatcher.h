#pragma once

/// @file SharedDirWatcher.h
/// @brief Tells the shared file list which shared directories changed on disk.
///
/// Watches the roots non-recursively — the share model is one directory, no
/// subdirectories. Events are held until a directory has been quiet for a moment,
/// so a burst (a copy of many files) becomes one rescan. Roots the OS would not
/// watch, and roots on a network share (where a watch is accepted but tells
/// nothing reliable), are reported at intervals; the rescan behind the report
/// stats first and opens nothing that did not change.
///
/// The OS calls run on a worker thread: on macOS QFileSystemWatcher::addPath()
/// waits for fseventsd, which took 19-38 s at startup and froze the core. A root
/// counts as neither watched nor polled until the worker answers.

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>

class QFileSystemWatcher;
class QThread;

namespace eMule {

/// Owns the QFileSystemWatcher; lives on SharedDirWatcher's worker thread.
class SharedDirWatchWorker : public QObject {
    Q_OBJECT

public:
    explicit SharedDirWatchWorker(QObject* parent = nullptr);

    struct Request {
        QString dir;
        quint64 id = 0;
    };

    // Worker thread only.
    /// One addPaths() for the lot: Qt restarts the macOS stream once per call.
    void watch(const QList<Request>& requests, bool checkRemote);
    void unwatch(const QString& dir);
    /// Re-adds a directory that lost its watch (deleted and made again).
    void rewatchIfLost(const QString& dir, quint64 request);

    std::function<bool(const QString&)> isRemote;
    std::function<void(const QString&)> beforeAdd;

signals:
    void watched(const QString& dir, bool ok, bool remote, quint64 request);
    void rewatched(const QString& dir, bool ok, quint64 request);
    void directoryChanged(const QString& dir);

private:
    QFileSystemWatcher* m_watcher = nullptr;
};

class SharedDirWatcher : public QObject {
    Q_OBJECT

public:
    explicit SharedDirWatcher(QObject* parent = nullptr);
    ~SharedDirWatcher() override;

    /// Replace the watched set. Unchanged roots keep their watch. Returns at once;
    /// new roots are watched once the worker answers.
    void setRoots(const QStringList& dirs);
    [[nodiscard]] QStringList roots() const { return m_roots.keys(); }
    /// Roots the OS refused to watch, or on a network share; these are polled.
    [[nodiscard]] QStringList polledRoots() const;
    /// True while the worker has not answered for some root yet.
    [[nodiscard]] bool watchesPending() const;

    /// Test seam: whether a directory counts as being on a network share.
    /// Called on the worker thread.
    void setRemoteCheck(std::function<bool(const QString&)> check);
    /// Test seam: runs on the worker thread before each addPath(), e.g. to stall.
    void setBeforeAddHook(std::function<void(const QString&)> hook);
    /// True for a directory on a network filesystem.
    [[nodiscard]] static bool isOnNetworkShare(const QString& dir);

    /// Quiet time before a changed directory is reported, the longest a busy one is
    /// held back, and the poll period. Tests shorten them.
    void setTimings(int settleMs, int maxHoldMs, int pollMs);

    /// Above this many directories waiting at once, report one overflow instead.
    static constexpr int kMaxPending = 64;
    /// A watch that took longer than this may have missed changes: report the root once.
    static constexpr int kSlowWatchMs = 1000;

signals:
    void directoryChanged(const QString& dir);
    /// Everything that settled in one go; emitted with directoryChanged().
    void directoriesChanged(const QStringList& dirs);
    /// Too much changed at once: rescan everything.
    void overflow();

private:
    struct Root {
        bool watched = false;
        bool remote = false;      // network share: polled even when watched
        bool pending = false;     // worker has not answered yet
        bool catchUp = false;     // report once if the answer came late
        quint64 request = 0;
        qint64 askedMs = 0;
    };
    struct Pending {
        qint64 firstMs = 0;
        qint64 lastMs = 0;
    };

    /// False when the event overflowed (overflow() was emitted).
    bool onDirectoryEvent(const QString& dir);
    void flush();
    void poll();
    /// Marks the root pending and queues it; sendWatches() hands the batch over.
    void requestWatch(const QString& dir, Root& root, bool catchUp);
    void sendWatches(bool checkRemote);
    void onWatched(const QString& dir, bool ok, bool remote, quint64 request);
    void onRewatched(const QString& dir, bool ok, quint64 request);
    void updatePollTimer();
    [[nodiscard]] static bool isPolled(const Root& r) { return !r.pending && (!r.watched || r.remote); }

    QThread* m_thread = nullptr;
    SharedDirWatchWorker* m_worker = nullptr;
    QHash<QString, Root> m_roots;
    QHash<QString, Pending> m_pending;
    QTimer m_flushTimer;
    QTimer m_pollTimer;
    int m_settleMs = 2000;
    int m_maxHoldMs = 10000;
    quint64 m_nextRequest = 1;
    QList<SharedDirWatchWorker::Request> m_toWatch;
};

} // namespace eMule
