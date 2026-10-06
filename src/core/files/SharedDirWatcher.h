#pragma once

/// @file SharedDirWatcher.h
/// @brief Tells the shared file list which shared directories changed on disk.
///
/// Watches the roots non-recursively — the share model is one directory, no
/// subdirectories. Events are held until a directory has been quiet for a moment,
/// so a burst (a copy of many files) becomes one rescan. Roots the OS would not
/// watch are compared by directory date now and then.

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

class QFileSystemWatcher;

namespace eMule {

class SharedDirWatcher : public QObject {
    Q_OBJECT

public:
    explicit SharedDirWatcher(QObject* parent = nullptr);

    /// Replace the watched set. Unchanged roots keep their watch.
    void setRoots(const QStringList& dirs);
    [[nodiscard]] QStringList roots() const { return m_roots.keys(); }
    /// Roots the OS refused to watch; these are polled.
    [[nodiscard]] QStringList polledRoots() const;

    /// Quiet time before a changed directory is reported, the longest a busy one is
    /// held back, and the poll period. Tests shorten them.
    void setTimings(int settleMs, int maxHoldMs, int pollMs);

    /// Above this many directories waiting at once, report one overflow instead.
    static constexpr int kMaxPending = 64;

signals:
    void directoryChanged(const QString& dir);
    /// Too much changed at once: rescan everything.
    void overflow();

private:
    void onDirectoryEvent(const QString& dir);
    void flush();
    void poll();

    struct Root {
        bool watched = false;
        QDateTime lastModified;   // polled roots only
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
};

} // namespace eMule
