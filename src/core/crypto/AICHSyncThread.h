#pragma once

/// @file AICHSyncThread.h
/// @brief Background AICH hash synchronization thread.
///
/// Replaces the original MFC CAICHSyncThread. Runs on startup to:
/// 1. Load the known2_64.met hash index
/// 2. Match stored hashes to shared files
/// 3. Upgrade files missing AICH part hashes
/// 4. Purge unused hashsets
/// 5. Queue unmatched files for AICH hashing

#include "utils/Types.h"

#include <QThread>

#include <atomic>
#include <functional>

namespace eMule {

class KnownFile;
class KnownFileList;
class SafeFile;
class SharedFileList;

class AICHSyncThread : public QThread {
    Q_OBJECT

public:
    /// @param configDir  Directory containing known2_64.met
    /// @param sharedFiles  Shared file list to sync against
    /// @param knownFiles   Known file list for duplicate checking
    explicit AICHSyncThread(const QString& configDir,
                            SharedFileList* sharedFiles,
                            KnownFileList* knownFiles,
                            QObject* parent = nullptr);

    /// Request graceful shutdown.
    void requestStop() { m_stopping.store(true, std::memory_order_relaxed); }

signals:
    /// Emitted when sync is complete with the count of files needing AICH hashing.
    void syncComplete(int filesToHash);

    /// Emitted for each file that has been AICH-hashed during sync.
    void fileHashed(eMule::KnownFile* file, bool success);

    /// Progress: remaining files to hash.
    void hashingProgress(int remaining);

protected:
    void run() override;

private:
    [[nodiscard]] bool isClosing() const
    {
        return m_stopping.load(std::memory_order_relaxed);
    }

    bool convertKnown2ToKnown264(SafeFile& targetFile);

    QString m_configDir;
    SharedFileList* m_sharedFiles;
    KnownFileList* m_knownFiles;
    std::atomic<bool> m_stopping{false};
};

} // namespace eMule
