#pragma once

/// @file SharedFileList.h
/// @brief Shared file management — port of MFC CSharedFileList.
///
/// Manages shared files, directory scanning, and background hashing.
/// Uses HashingThread for async file hashing.

#include "files/KnownFileList.h"
#include "files/PublishKeywordList.h"

#include <QMutex>
#include <QObject>
#include <QThread>
#include <QWaitCondition>

#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace eMule {

class KnownFile;
class KnownFileList;
class ServerConnect;

// ---------------------------------------------------------------------------
// HashingThread — background file hashing
// ---------------------------------------------------------------------------

class HashingThread : public QThread {
    Q_OBJECT
public:
    struct Job {
        QString directory;
        QString filename;
        QString sharedDirectory;
        uint64_t generation = 0;
    };

    explicit HashingThread(QObject* parent = nullptr);

    void enqueue(Job job);
    void clearQueue();
    void requestStop();

signals:
    void hashingFinished(eMule::KnownFile* file, uint64 generation);
    void hashingFailed(const QString& directory, const QString& filename, uint64 generation);
    void hashingProgress(int percent);

protected:
    void run() override;

private:
    QMutex m_mutex;
    QWaitCondition m_condition;
    std::list<Job> m_queue;
    bool m_stopRequested = false;
};

// ---------------------------------------------------------------------------
// UnknownFileEntry — file waiting to be hashed
// ---------------------------------------------------------------------------

struct UnknownFileEntry {
    QString directory;
    QString filename;
    QString sharedDirectory;
};

// ---------------------------------------------------------------------------
// SharedFileList
// ---------------------------------------------------------------------------

class SharedFileList : public QObject {
    Q_OBJECT
public:
    explicit SharedFileList(KnownFileList* knownFiles, QObject* parent = nullptr);
    ~SharedFileList() override;

    void reload();
    bool safeAddKFile(KnownFile* file, bool onlyAdd = false);
    bool removeFile(KnownFile* file);
    void process();

    KnownFile* getFileByID(const uint8* hash) const;
    bool isUnsharedFile(const uint8* hash) const;
    int getCount() const;
    uint64 getDataSize(uint64& largestOut) const;

    void addKeywords(KnownFile* file);
    void removeKeywords(KnownFile* file);

    /// Thread-safe iteration over all shared files. Lock is held during callback.
    void forEachFile(const std::function<void(KnownFile*)>& callback) const;

    /// Number of files currently queued for hashing.
    int getHashingCount() const;

    // Server / Kad publishing
    void sendListToServer();
    void publish();

    // Server connect integration
    void setServerConnect(ServerConnect* sc);

    /// Reset publishedED2K flag on all files (e.g., on server reconnect).
    void clearED2KPublishFlags();

signals:
    void fileAdded(eMule::KnownFile* file);
    void fileRemoved(eMule::KnownFile* file);

private:
    void findSharedFiles();
    void addFilesFromDirectory(const QString& dir, const QString& sharedDir = {});
    void hashNextFile();

    void onHashingFinished(KnownFile* file, uint64 generation);
    void onHashingFailed(const QString& directory, const QString& filename, uint64 generation);

    KnownFile* getFileByIndex(uint32 index) const;

    std::unordered_map<MD4Key, KnownFile*> m_filesMap;
    std::unordered_set<MD4Key> m_unsharedFiles;
    std::list<UnknownFileEntry> m_waitingForHash;
    PublishKeywordList m_keywords;
    KnownFileList* m_knownFiles = nullptr;
    HashingThread* m_hashingThread = nullptr;
    ServerConnect* m_serverConnect = nullptr;
    mutable QMutex m_mutex;
    uint64 m_generation = 0;
    bool m_hashingInProgress = false;

    // Kad publishing round-robin state
    uint32 m_currFileSrc = 0;
    uint32 m_currFileNotes = 0;
    time_t m_lastPublishKadSrc = 0;
    time_t m_lastPublishKadNotes = 0;
};

} // namespace eMule
