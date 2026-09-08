#pragma once

/// @file UsenetWatchFolder.h
/// @brief A folder dropped .nzb files are picked up from.
///
/// Lives in the Usenet module rather than the daemon because it calls
/// UsenetQueue::addNzb() directly, and is therefore testable against a real
/// queue with no IPC in the way.
///
/// Three things here are not obvious and are each the subject of a test:
///
///   - **A file that is still being written has not arrived yet.** The watcher
///     fires when a file is *created*, which for anything larger than a buffer
///     is before the writer has finished. NzbFile::parse() does reject a
///     truncated document, so half a file can never become half a release --
///     but the *move* is the damage: without a settle check the file is
///     declared invalid and filed into `_failed/`, which is indistinguishable
///     from a corrupt download and loses the user their .nzb. So a candidate is
///     read only once its size and mtime have held still.
///   - **The watcher alone is not enough, twice over.** FSEvents coalesces, and
///     no event ever fires for the files that were already there when the daemon
///     started. Hence a periodic rescan and a scan at start.
///   - **A duplicate is a success.** The queue refusing a release it already has
///     is the answer, not a failure, so the file goes to `_processed/`. Filing
///     it as failed would be a lie, and leaving it in place would make the
///     scanner re-read it forever.
///
/// Consumed files are moved, never deleted. The queue is not a receipt, and a
/// user who wants to know what became of a file they dropped should be able to
/// look.

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

namespace eMule::usenet {

class UsenetQueue;

class UsenetWatchFolder : public QObject {
    Q_OBJECT

public:
    explicit UsenetWatchFolder(UsenetQueue* queue, QObject* parent = nullptr);
    ~UsenetWatchFolder() override;

    /// Re-read Preferences::usenetWatchDir() and start or stop accordingly.
    /// Called at startup and on every settings save.
    void applyPreferences();

    /// Scan now. Also the whole of what the timer and the watcher do, so a test
    /// never has to wait for either.
    void scan();

    [[nodiscard]] QString directory() const { return m_dir; }

    /// Where a consumed file goes. Subdirectories of the watch folder, and
    /// excluded from scanning -- otherwise the scanner reads its own output.
    static constexpr auto kProcessedDir = "_processed";
    static constexpr auto kFailedDir = "_failed";

    /// How long a file's size and mtime must hold still before it is read.
    static constexpr int kSettleMs = 2000;

    /// Rescan period. The watcher is the fast path; this is what covers a
    /// coalesced event and a file that appeared while the daemon was down.
    static constexpr int kRescanMs = 30 * 1000;

    /// Attempts before a file that will not queue is filed as failed.
    static constexpr int kMaxAttempts = 3;

private:
    /// What a candidate looked like last time we saw it.
    struct Sighting {
        qint64 size = 0;
        QDateTime modified;
        QDateTime firstSeen;
        int attempts = 0;
    };

    void consume(const QString& path);
    void fileTo(const QString& path, const char* subdir);
    void stopWatching();

    UsenetQueue* m_queue = nullptr;
    QString m_dir;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_rescan = nullptr;

    /// Keyed by absolute path.
    QHash<QString, Sighting> m_seen;
};

} // namespace eMule::usenet
