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

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>

class QFileSystemWatcher;

namespace eMule {

class SharedDirWatcher : public QObject {
    Q_OBJECT

public:
    explicit SharedDirWatcher(QObject* parent = nullptr);

    /// Replace the watched set. Unchanged roots keep their watch.
    void setRoots(const QStringList& dirs);
    [[nodiscard]] QStringList roots() const { return m_roots.keys(); }
    /// Roots the OS refused to watch, or on a network share; these are polled.
    [[nodiscard]] QStringList polledRoots() const;

    /// Test seam: whether a directory counts as being on a network share.
    void setRemoteCheck(std::function<bool(const QString&)> check) { m_isRemote = std::move(check); }
    /// True for a directory on a network filesystem.
    [[nodiscard]] static bool isOnNetworkShare(const QString& dir);

    /// Quiet time before a changed directory is reported, the longest a busy one is
    /// held back, and the poll period. Tests shorten them.
    void setTimings(int settleMs, int maxHoldMs, int pollMs);

    /// Above this many directories waiting at once, report one overflow instead.
    static constexpr int kMaxPending = 64;

signals:
    void directoryChanged(const QString& dir);
    /// Everything that settled in one go; emitted with directoryChanged().
    void directoriesChanged(const QStringList& dirs);
    /// Too much changed at once: rescan everything.
    void overflow();

private:
    void onDirectoryEvent(const QString& dir);
    void flush();
    void poll();

    struct Root {
        bool watched = false;
        bool remote = false;      // network share: polled even when watched
    };
    struct Pending {
        qint64 firstMs = 0;
        qint64 lastMs = 0;
    };

    QFileSystemWatcher* m_watcher = nullptr;
    QHash<QString, Root> m_roots;
    QHash<QString, Pending> m_pending;
    QTimer m_flushTimer;
    QTimer m_pollTimer;
    int m_settleMs = 2000;
    int m_maxHoldMs = 10000;
    std::function<bool(const QString&)> m_isRemote;
};

} // namespace eMule
