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
///
// ToDo: purge hashsets no shared or known file refers to (MFC AICHSyncThread.cpp:163-247).
// It must not drop the sets of part files still downloading.

#include "utils/Types.h"

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <deque>

namespace eMule {

class KnownFile;
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

signals:
    /// The index is loaded and the shared files were matched; @p filesToHash lack a set.
    void syncComplete(int filesToHash);

    /// A missing hashset was built (or not). Emitted after it was applied to the file.
    void fileHashed(const QByteArray& fileHash, bool success);

    // Worker -> owner thread.
    void indexLoaded();
    void hashSetBuilt(const QByteArray& fileHash, const QByteArray& masterHash, bool success);

protected:
    void run() override;

private slots:
    void onIndexLoaded();
    void onHashSetBuilt(const QByteArray& fileHash, const QByteArray& masterHash, bool success);

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

    QString m_configDir;
    SharedFileList* m_sharedFiles;
    std::atomic<bool> m_stopping{false};

    QMutex m_jobMutex;
    QWaitCondition m_jobReady;
    std::deque<Job> m_jobs;
    bool m_jobsQueued = false;   // the owner thread has handed over its list
};

} // namespace eMule
