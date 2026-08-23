#pragma once

/// @file SharedFileList.h
/// @brief Shared file management — port of MFC CSharedFileList.
///
/// Manages shared files, directory scanning, and background hashing.
/// Uses HashingThread for async file hashing.

#include "files/KnownFileList.h"
#include "files/PublishKeywordList.h"
#include "utils/EntityMap.h"

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QThread>
#include <QWaitCondition>

#include <array>
#include <functional>
#include <vector>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>

class tst_SharedFileList;

namespace eMule {

class KnownFile;
class KnownFileList;
class PartFile;
class Server;
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

        // -- Part-file rehash (MFC's CAddFileThread carrying an m_partfile) --------
        // Set to re-verify an existing .part against a known hashset instead of
        // hashing a new shared file. Everything the worker needs is copied in here by
        // the main thread: the worker never touches the PartFile, which stays owned by
        // the download queue and may be deleted while the job is queued. The file hash
        // is how the completion finds its way back to the right object.
        QByteArray rehashFileHash;
        QString rehashPartPath;
        uint64 rehashFileSize = 0;
        std::vector<std::array<uint8, 16>> rehashPartHashes;

        [[nodiscard]] bool isRehash() const { return !rehashFileHash.isEmpty(); }
    };

    explicit HashingThread(QObject* parent = nullptr);

    void enqueue(Job job);
    /// Drop queued work. Rehash jobs are kept: a share reload has nothing to do with
    /// a part file mid-verification, and dropping one would strand it in
    /// WaitingForHash with nothing left to move it on.
    void clearQueue();
    void requestStop();

signals:
    void hashingFinished(eMule::KnownFile* file, uint64 generation);
    void hashingFailed(const QString& directory, const QString& filename, uint64 generation);
    void hashingProgress(int percent);
    /// One byte per part: 1 if it verified against the hashset, 0 if it did not.
    void partFileRehashed(const QByteArray& fileHash, const QByteArray& partOk);

protected:
    void run() override;

private:
    /// Re-verify one part file against the hashset carried in the job.
    void runRehash(const Job& job);

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

class SharedFileList : public EntityMap<MD4Key, KnownFile> {
    Q_OBJECT

    // White-box access for the unit test: it exercises the offer filter and the cap
    // against a plain Server, which is otherwise reachable only through a live
    // ServerConnect socket.
    friend class ::tst_SharedFileList;

public:
    explicit SharedFileList(KnownFileList* knownFiles, QObject* parent = nullptr);
    ~SharedFileList() override;

    void reload();
    /// Add a file to the shared list.
    ///
    /// @param onlyAdd  suppress the "schedule an ED2K republish" flag, for bulk adds
    ///        (a startup scan, addPartFilesToShare) that would otherwise set it once
    ///        per file. MFC srchybrid/SharedFileList.cpp:658-669.
    bool safeAddKFile(KnownFile* file, bool onlyAdd = false);
    /// Drop a file from the shared list, and remember the hash as unshared so
    /// isUnsharedFile() can answer a peer that asks for it. The mark is *not* a
    /// re-add gate — safeAddKFile() clears it (MFC AddFile, srchybrid/SharedFileList.cpp:695).
    bool removeFile(KnownFile* file);
    void process();

    KnownFile* getFileByID(const uint8* hash) const;
    bool isUnsharedFile(const uint8* hash) const;

    // -- Share membership (MFC CSharedFileList::ShouldBeShared and friends) ------

    /// Should this path be shared, per the user's preferences?
    ///
    /// @param dirPath        the directory the file lives in.
    /// @param filePath       the file itself, or empty to ask only about the directory.
    /// @param mustBeShared   ask only about directories that *cannot* be unshared —
    ///        the incoming directory. Used to grey out "Unshare".
    /// Port of srchybrid/SharedFileList.cpp:1388-1418.
    [[nodiscard]] bool shouldBeShared(const QString& dirPath, const QString& filePath,
                                      bool mustBeShared) const;

    /// Stop sharing one file, durably: drops it from the list and records the path so
    /// no later scan picks it up again. Returns false if the file is not actually
    /// shared, or sits somewhere that cannot be unshared.
    /// Port of srchybrid/SharedFileList.cpp:1430-1465.
    bool excludeFile(const QString& filePath);

    /// Share one file that no shared directory covers. Returns false if its directory
    /// is not shareable at all. Port of srchybrid/SharedFileList.cpp:610-638.
    bool addSingleSharedFile(const QString& filePath);

    /// Does this directory hold any individually-shared file?
    /// Port of srchybrid/SharedFileList.cpp:1420-1427.
    [[nodiscard]] bool containsSingleSharedFiles(const QString& dirPath) const;

    /// Persisted single-shared / excluded path lists (Config/sharedfiles.dat).
    void loadSharedFilesConfig();
    void saveSharedFilesConfig() const;
    int getCount() const;
    uint64 getDataSize(uint64& largestOut) const;

    void addKeywords(KnownFile* file);
    void removeKeywords(KnownFile* file);

    /// Thread-safe iteration over all shared files. Lock is held during callback.
    void forEachFile(const std::function<void(KnownFile*)>& callback) const;

    /// Number of files currently queued for hashing.
    int getHashingCount() const;

    /// Queue a part file for re-verification against its own MD4 hashset, because its
    /// .part no longer matches the date recorded in the .part.met. MFC spawns a
    /// CAddFileThread for this (srchybrid/PartFile.cpp:1136).
    void enqueuePartFileRehash(PartFile* file);

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
    /// Pick the files to put in the next OP_OFFERFILES, honouring the server's large
    /// file support and its GetSoftFiles() limit, and mark them published. Split out of
    /// sendListToServer() so both rules can be tested without a live server socket.
    std::vector<KnownFile*> takeFilesToOffer(const Server* srv);

    void findSharedFiles();
    void addFilesFromDirectory(const QString& dir, const QString& sharedDir = {});
    /// Queue one explicitly-shared file for hashing (or re-add it if already known).
    /// Port of srchybrid/SharedFileList.cpp:1468.
    void checkAndAddSingleFile(const QString& filePath);
    void hashNextFile();

    [[nodiscard]] QString sharedFilesConfigPath() const;

    void onHashingFinished(KnownFile* file, uint64 generation);
    void onPartFileRehashed(const QByteArray& fileHash, const QByteArray& partOk);
    void onHashingFailed(const QString& directory, const QString& filename, uint64 generation);

    /// Index-based access for the Kad round-robin. Lock-free by design:
    /// **the caller must already hold m_mutex.**
    KnownFile* fileAtIndexLocked(uint32 index) const;

    /// Parse an .emulecollection into the file, if it is one. Does disk I/O, so it
    /// runs outside the map lock — see the hook contract in EntityMap.h.
    void detectCollection(KnownFile* file);

    // EntityMap<MD4Key, KnownFile> hooks. Storage (m_map) and the mutex guarding it
    // live in the base. These carry only work that must happen under that lock;
    // everything with a side effect lives in safeAddKFile()/removeFile().
    [[nodiscard]] MD4Key keyFor(KnownFile* file) const override;
    [[nodiscard]] bool isDuplicate(const MD4Key& key, KnownFile* file) const override;
    void onEntityAdded(KnownFile* file) override;
    void onEntityRemoved(KnownFile* file) override;

    /// Hashes we used to share. Guarded by the base's m_mutex, alongside m_map:
    /// written from the add/remove hooks, read by isUnsharedFile().
    std::unordered_set<MD4Key> m_unsharedFiles;

    /// The durable share membership, persisted to Config/sharedfiles.dat. MFC's
    /// m_liSingleSharedFiles / m_liSingleExcludedFiles. Paths are stored as given and
    /// compared case-insensitively, as MFC does with CompareNoCase. Main thread only.
    QSet<QString> m_singleSharedFiles;
    QSet<QString> m_singleExcludedFiles;

    PublishKeywordList m_keywords;
    KnownFileList* m_knownFiles = nullptr;
    HashingThread* m_hashingThread = nullptr;
    ServerConnect* m_serverConnect = nullptr;

    /// Guards the hashing pipeline below — and nothing else. Deliberately separate
    /// from the base's m_mutex, which guards m_map/m_unsharedFiles: a directory scan
    /// holds this one while feeding files in through safeAddKFile(), which takes the
    /// other. The two must never nest in either direction.
    mutable QMutex m_hashMutex;
    std::list<UnknownFileEntry> m_waitingForHash;
    uint64 m_generation = 0;
    bool m_hashingInProgress = false;

    /// ED2K republish throttle — MFC m_lastPublishED2KFlag / m_lastPublishED2K
    /// (srchybrid/SharedFileList.cpp:1229-1236). Main thread only.
    bool m_republishED2K = false;
    time_t m_lastPublishED2K = 0;

    // Kad publishing round-robin state
    uint32 m_currFileSrc = 0;
    uint32 m_currFileNotes = 0;
    time_t m_lastPublishKadSrc = 0;
    time_t m_lastPublishKadNotes = 0;
};

} // namespace eMule
