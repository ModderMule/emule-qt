#pragma once

/// @file AICHSyncThread.h
/// @brief Background AICH hash synchronization thread.
///
/// Replaces the original MFC CAICHSyncThread. On startup it
/// 1. indexes known2_64.met (cutting off a damaged tail),
/// 2. has the main thread match the index against the shared files,
/// 3. builds and stores the recovery hashset of every shared file that has none.
///
/// The thread only ever touches files on disk. Everything that reads or changes a
/// KnownFile happens in the two slots below, on the thread this object lives in.
/// 4. drops the sets no known, shared or downloading file refers to
///    (MFC AICHSyncThread.cpp:163-247), when told where to look for those files.

#include "AICHData.h"
#include "files/HashFailureStore.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <deque>
#include <unordered_set>

namespace eMule {

class KnownFile;
class KnownFileList;
class SafeFile;
class SharedFileList;

class AICHSyncThread : public QThread {
    Q_OBJECT

public:
    /// @param configDir    Directory containing known2_64.met
    /// @param sharedFiles  Shared file list to sync against (used on the owner thread only)
    explicit AICHSyncThread(const QString& configDir, SharedFileList* sharedFiles,
                            QObject* parent = nullptr);
    ~AICHSyncThread() override;

    /// Request graceful shutdown; follow with wait().
    void requestStop();

    /// Allow the purge: every file that may still need its set is in @p knownFiles, the
    /// shared list or the download queue (looked up when the purge runs — it does not
    /// exist yet when this thread is made). Without this call nothing is ever dropped.
    void setPurgeSource(KnownFileList* knownFiles);

signals:
    /// The index is loaded and the shared files were matched; @p filesToHash lack a set.
    void syncComplete(int filesToHash);

    /// Unreferenced sets were dropped from known2_64.met.
    void purged(uint dropped, quint64 bytes);

    /// A missing hashset was built (or not). Emitted after it was applied to the file.
    void fileHashed(const QByteArray& fileHash, bool success);

    // Worker -> owner thread.
    void indexLoaded();
    /// @p readInFull: the file was there to be read, so a failure cost a whole pass.
    void hashSetBuilt(const QByteArray& fileHash, const QByteArray& masterHash, bool success,
                      bool readInFull);

protected:
    void run() override;

private slots:
    void onIndexLoaded();
    void onHashSetBuilt(const QByteArray& fileHash, const QByteArray& masterHash, bool success,
                        bool readInFull);

private:
    struct Job {
        QByteArray fileHash;   // MD4, how the result finds its file again
        QString path;
        uint64 size = 0;
    };

    [[nodiscard]] bool isClosing() const
    {
        return m_stopping.load(std::memory_order_relaxed);
    }

    bool loadIndex();
    bool convertKnown2ToKnown264(SafeFile& targetFile);
    /// Give @p file the part hashes of a set that is in known2. Owner thread.
    void applyStoredHashSet(KnownFile* file);
    /// The master hashes still referred to, or false when that cannot be told. Owner thread.
    bool collectKeepSet(std::unordered_set<AICHHash>& keep) const;
    /// Did building this file's set fail before, with the file unchanged since? Owner thread.
    [[nodiscard]] bool failedBefore(const QString& path, uint64 size) const;
    void saveFailures() const;

    QString m_configDir;
    SharedFileList* m_sharedFiles;
    std::atomic<bool> m_stopping{false};

    QMutex m_jobMutex;
    QWaitCondition m_jobReady;
    std::deque<Job> m_jobs;
    bool m_jobsQueued = false;   // the owner thread has handed over its list

    KnownFileList* m_knownFiles = nullptr;
    bool m_purgeAllowed = false;
    bool m_hasDuplicates = false;                // seen by loadIndex()
    bool m_purgeQueued = false;                  // guarded by m_jobMutex, as m_keep
    std::unordered_set<AICHHash> m_keep;

    /// Files whose set could not be built, by path: not read again until they change.
    /// Without it such a file was read in full at every start. Owner thread.
    QHash<QString, HashFailureRecord> m_failures;
};

} // namespace eMule
