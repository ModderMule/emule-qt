#pragma once

/// @file SharedFileList.h
/// @brief Shared file management — port of MFC CSharedFileList.
///
/// Manages shared files, directory scanning, and background hashing.
/// Uses HashingThread for async file hashing.

#include "files/HashFailureStore.h"
#include "files/KadPublishStore.h"
#include "files/KnownFileList.h"
#include "files/PublishKeywordList.h"
#include "protocol/Tag.h"
#include "utils/EntityMap.h"

#include <QMutex>
#include <QHash>
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
class SharedDirWatcher;
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
        /// Size and date the scan saw; mtime 0 = not known, hash whatever is there.
        uint64 scannedSize = 0;
        time_t scannedMtime = 0;

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
        uint64 rehashToken = 0;   // echoed back so a stale result can be told apart

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
    /// The file is not what the scan saw any more; nothing was read.
    void hashingDeferred(const QString& directory, const QString& filename, uint64 generation);
    void hashingProgress(int percent);
    /// One byte per part: 1 if it verified against the hashset, 0 if it did not.
    /// partOk holds one PartFile::PartVerdict value per part.
    void partFileRehashed(const QByteArray& fileHash, const QByteArray& partOk, uint64 token);

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
    QString key;   // SharedFileList::pathKey of the file
    uint64 size = 0;     // as scanned
    time_t mtime = 0;    // as scanned; 0 = not known
    QString volume;      // which disk it is on; filled when queued
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

    /// Bring the share in line with the disk: files that are still there with the
    /// same size and date are left alone, the rest leave, join, or go to hashing.
    void reload();
    /// The same for one shared directory.
    void rescanDirectory(const QString& dir);
    /// Several changed directories as one diff, so a move between them is a move.
    void rescanDirectories(const QStringList& dirs);
    /// Follow the shared directories on disk from now on (the daemon; a unit test
    /// reloads by hand). Also holds back files that were written a moment ago.
    void setWatchingEnabled(bool enabled);
    [[nodiscard]] SharedDirWatcher* watcher() const { return m_watcher; }

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

    /// Re-read the media tags of every shared file that is not a part file, a few per
    /// tick; known.met is saved when the pass ends. MFC RebuildMetaData
    /// (srchybrid/SharedFileList.cpp:1381-1386), there in one blocking loop.
    /// @return number of files queued.
    int rebuildMetaData();
    [[nodiscard]] bool isRebuildingMetaData() const { return !m_metaRebuildQueue.empty(); }

    KnownFile* getFileByID(const uint8* hash) const;
    /// True when exactly this object is shared, not merely one with its hash.
    [[nodiscard]] bool isFilePtrInList(const KnownFile* file) const;
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

    /// The directory rules as path keys, resolved once for many questions — a scan or
    /// a list reply asks per file. Single-file entries are read live.
    struct ShareRules {
        QSet<QString> incomingDirs;
        QSet<QString> sharedDirs;
        QString usenetTempRoot;
    };
    [[nodiscard]] ShareRules shareRules() const;
    [[nodiscard]] bool shouldBeShared(const ShareRules& rules, const QString& dirPath,
                                      const QString& filePath, bool mustBeShared) const;

    /// Cleaned and case-folded: two spellings of one path give one key (MFC compares
    /// with CompareNoCase).
    [[nodiscard]] static QString pathKey(const QString& path);

    /// Stop sharing one file, durably: drops it from the list and records the path so
    /// no later scan picks it up again. Returns false if the file is not actually
    /// shared, or sits somewhere that cannot be unshared.
    /// Port of srchybrid/SharedFileList.cpp:1430-1465.
    bool excludeFile(const QString& filePath);

    /// Share one file that no shared directory covers. Returns false if its directory
    /// is not shareable at all. Port of srchybrid/SharedFileList.cpp:610-638.
    bool addSingleSharedFile(const QString& filePath);

    /// Pick up a file that has just appeared in a location the share already
    /// covers — a finished Usenet download landing in the incoming directory.
    ///
    /// Distinct from addSingleSharedFile(), which is for a file *no* shared
    /// directory covers and therefore needs its own sharedfiles.dat entry. That
    /// one refuses the incoming directory outright, because isShareableDirectory()
    /// excludes it: incoming is shared implicitly and can never be listed. Using
    /// it here would log a warning and share nothing until the next full rescan.
    ///
    /// Returns false when the location is not in fact shared, so a caller cannot
    /// use this to smuggle a file onto the network.
    bool addFileInSharedLocation(const QString& filePath);

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

    /// The directories shared files sit in, as peers see them (sharedDirectory()),
    /// and the files of one — from an index, not a walk of the share.
    [[nodiscard]] std::vector<QString> sharedDirectories() const;
    [[nodiscard]] std::vector<KnownFile*> filesInDirectory(const QString& dir) const;
    /// Re-file a shared file whose directory changed (a download that completed).
    void refreshDirectoryOf(KnownFile* file);

    /// See fileChanged(). Any thread.
    void noteFileChanged(const uint8* fileHash);

    /// Thread-safe iteration over all shared files. Lock is held during callback.
    void forEachFile(const std::function<void(KnownFile*)>& callback) const;

    /// Number of files currently queued for hashing.
    int getHashingCount() const;

    /// Queue a part file for re-verification against its own MD4 hashset, because its
    /// .part no longer matches the date recorded in the .part.met. MFC spawns a
    /// CAddFileThread for this (srchybrid/PartFile.cpp:1136).
    /// False when nothing was queued; the caller must then not wait for a result.
    bool enqueuePartFileRehash(PartFile* file);

    // Server / Kad publishing
    void sendListToServer();
    void publish();

    /// Keep keyword publish times in @p path: loads now, saves while running and on
    /// destruction. Without it every start publishes every keyword again.
    void setKadPublishStorePath(const QString& path);
    void saveKadPublishStore();

    /// Remember given-up hash failures in @p path (loads now, saves as they change).
    void setHashFailureStorePath(const QString& path);

    // Server connect integration
    void setServerConnect(ServerConnect* sc);

    /// Reset publishedED2K flag on all files (e.g., on server reconnect).
    void clearED2KPublishFlags();

signals:
    void fileAdded(eMule::KnownFile* file);
    void fileRemoved(eMule::KnownFile* file);
    /// A shared file was renamed or moved on disk; same object, new name and path.
    void fileRelocated(eMule::KnownFile* file);
    /// Something a list row shows changed for the file with this hash. By hash, not
    /// by pointer: it may come from the hashing thread, for a file that is gone by
    /// the time it is delivered — and for one that is not shared at all.
    void fileChanged(const QByteArray& fileHash);
    /// A part-file rehash result was applied (not emitted for a stale or orphaned one).
    void partFileRehashApplied(const QByteArray& fileHash, const QByteArray& partOk);

private:
    /// Pick the files to put in the next OP_OFFERFILES, honouring the server's large
    /// file support and its GetSoftFiles() limit, and mark them published. Split out of
    /// sendListToServer() so both rules can be tested without a live server socket.
    std::vector<KnownFile*> takeFilesToOffer(const Server* srv);

    /// The tag list of one OP_OFFERFILES record for @p srv
    /// (MFC CSharedFileList::CreateOfferedFilePacket).
    static std::vector<Tag> offeredTags(KnownFile& file, const Server* srv);

    /// Whether @p kw should be published now; takes over a stored due time first.
    [[nodiscard]] bool keywordIsDue(PublishKeyword& kw, time_t now);
    /// Schedule the next round and, when something was @p sent, remember it on disk.
    void noteKeywordPublished(PublishKeyword& kw, time_t now, bool sent);
    /// What a keyword's publish covers: the shared, complete files behind it.
    [[nodiscard]] KadPublishStore::Fingerprint keywordFingerprint(const PublishKeyword& kw);

    /// One file found on disk by a scan.
    struct DiskEntry {
        QString directory;
        QString filename;
        QString sharedDirectory;
        uint64 size = 0;
        time_t mtime = 0;
    };

    /// The diff behind reload() (@p onlyDirs empty) and rescanDirectories().
    void rescan(const QStringList& onlyDirs);
    /// Add to @p scope every root that now holds a file gone from a directory in it.
    void widenScopeForMoves(QSet<QString>& scope) const;
    /// What the watcher looks at: the scan roots plus the directories of files
    /// shared one by one.
    [[nodiscard]] QStringList watchRoots() const;
    /// The shareable files of one directory, by path key.
    void listDirectory(const QString& dir, QHash<QString, DiskEntry>& out) const;
    /// Every directory a scan walks: incoming directories first, then shared ones.
    [[nodiscard]] QStringList shareRoots() const;
    /// Point a shared file at the name and place it has on disk now.
    void relocateFile(KnownFile* file, const DiskEntry& entry);
    /// Hash retries and held-back directories that have come due.
    void stepDeferredScans();
    /// Queue one explicitly-shared file for hashing (or re-add it if already known).
    /// Port of srchybrid/SharedFileList.cpp:1468.
    void checkAndAddSingleFile(const QString& filePath);
    /// Give every idle worker the next waiting file of its volume. m_hashMutex held.
    void hashNextFile();
    /// Append to the hash queue. m_hashMutex held.
    void queueForHash(UnknownFileEntry entry);
    /// The file a worker reported on is off it. m_hashMutex held.
    [[nodiscard]] UnknownFileEntry takeHashing(const QString& key);
    /// The disk a directory is on (QStorageInfo::rootPath), cached. Main thread.
    [[nodiscard]] QString volumeKeyFor(const QString& directory);
    /// The worker for a volume: one per disk up to kMaxHashWorkers, shared beyond.
    [[nodiscard]] HashingThread* workerFor(const QString& volume);
    /// Smallest files first for a share nobody has hashed yet, the rest in path order.
    static void orderForHashing(std::vector<UnknownFileEntry>& entries, bool coldStart);
    void loadHashFailures();
    void saveHashFailures();

    [[nodiscard]] QString sharedFilesConfigPath() const;

    void onHashingFinished(KnownFile* file, uint64 generation);
    void onPartFileRehashed(const QByteArray& fileHash, const QByteArray& partOk, uint64 token);
    void onHashingFailed(const QString& directory, const QString& filename, uint64 generation);
    void onHashingDeferred(const QString& directory, const QString& filename, uint64 generation);

    /// First file at or after @p cursor (wrapping) for which @p due says yes; the
    /// cursor moves past it. One walk of the map, not one per probe.
    KnownFile* nextDueFile(uint32& cursor, const std::function<bool(KnownFile*)>& due);

    /// Parse an .emulecollection into the file, if it is one. Does disk I/O, so it
    /// runs outside the map lock — see the hook contract in EntityMap.h.
    void detectCollection(KnownFile* file);

    /// Settle a few files' container verdicts per tick, off the IPC poll path.
    ///
    /// handleGetSharedFiles() walks the whole share on every poll and must never
    /// open a file for it, so the reading happens here instead: 12 bytes per file
    /// with a media extension, memoised on the file, under a millisecond budget.
    /// The list payloads then report whatever this has resolved so far.
    void warmContainerChecks();

    /// One slice of a running rebuildMetaData() pass.
    void stepMetaDataRebuild();

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

    /// sharedDirectory() -> hashes of the files in it, and the directory each hash is
    /// filed under. Same lock, same hooks. By hash, so a replaced object leaves no
    /// pointer behind.
    std::unordered_map<QString, std::unordered_set<MD4Key>> m_byDirectory;
    std::unordered_map<MD4Key, QString> m_directoryOf;
    void indexDirectoryLocked(KnownFile* file);
    void unindexDirectoryLocked(const MD4Key& key);

    /// The durable share membership, persisted to Config/sharedfiles.dat. MFC's
    /// m_liSingleSharedFiles / m_liSingleExcludedFiles. pathKey() -> the path as given
    /// (that is what sharedfiles.dat keeps). Main thread only.
    QHash<QString, QString> m_singleSharedFiles;
    QHash<QString, QString> m_singleExcludedFiles;

    PublishKeywordList m_keywords;
    KadPublishStore m_publishStore;        // keyword due times across restarts
    QString m_publishStorePath;            // empty = not persisted
    QString m_hashFailureStorePath;        // empty = not persisted
    time_t m_publishStoreSavedAt = 0;
    KnownFileList* m_knownFiles = nullptr;

    /// One sequential worker per physical disk, disks in parallel: two files on the
    /// same spindle only make each other slower. Created on first use.
    static constexpr int kMaxHashWorkers = 4;
    /// A share nobody has hashed yet offers its smallest files first.
    static constexpr int kColdStartFiles = 200;
    static constexpr uint64 kColdStartBytes = 256ull * 1024 * 1024;
    std::vector<HashingThread*> m_hashWorkers;
    QHash<QString, HashingThread*> m_workerOfVolume;
    QHash<QString, QString> m_volumeOfDir;
    /// Test seam: which volume a directory counts as (default: its mount point).
    std::function<QString(const QString&)> m_volumeKeyFn;
    ServerConnect* m_serverConnect = nullptr;

    /// Guards the hashing pipeline below — and nothing else. Deliberately separate
    /// from the base's m_mutex, which guards m_map/m_unsharedFiles: a directory scan
    /// holds this one while feeding files in through safeAddKFile(), which takes the
    /// other. The two must never nest in either direction.
    mutable QMutex m_hashMutex;
    std::list<UnknownFileEntry> m_waitingForHash;
    uint64 m_generation = 0;
    /// The files on the workers, by path key.
    struct Hashing {
        HashingThread* worker = nullptr;
        UnknownFileEntry entry;
    };
    QHash<QString, Hashing> m_hashing;
    /// Workers found with nothing waiting; cleared when the queue grows.
    QSet<HashingThread*> m_drainedWorkers;

    /// A file that could not be hashed: tried again after a growing wait, then left
    /// alone until its size or date changes. By path key. Main thread only.
    struct HashFailure {
        UnknownFileEntry entry;
        uint64 size = 0;
        time_t mtime = 0;
        int attempts = 0;
        time_t retryAt = 0;    // 0: no retry scheduled
        bool queued = false;   // back in the hash queue
        bool givenUp = false;
    };
    QHash<QString, HashFailure> m_hashFailures;
    size_t m_hashFailuresSaved = 0;     // signature of the given-up set on disk
    time_t m_hashFailuresCheckedAt = 0;
    std::array<int, 3> m_hashRetrySecs{5, 30, 120};

    /// Directories with a file too fresh to hash, and when to look again.
    QHash<QString, time_t> m_settleDirs;
    int m_settleSecs = 0;   // 0: hash at once (set by setWatchingEnabled)
    static constexpr int kSettleRecheckSecs = 10;

    SharedDirWatcher* m_watcher = nullptr;

    /// Set once a whole pass found every file resolved; cleared when a file joins the
    /// share. A part file whose first part has not landed never resolves and so keeps
    /// the sweep awake — which is the point, it has to be caught when the bytes come.
    /// Main thread only.
    bool m_containerSweepIdle = false;

    /// How many still-unresolved files to step over before filling the next slice, so
    /// files that cannot answer (an unreadable one, a part file with no first part)
    /// cannot fill every slice forever and starve the rest. Wraps to 0 at the end.
    size_t m_containerSweepSkip = 0;

    /// Files still to visit in a rebuildMetaData() pass, by hash — a file may leave the
    /// share between ticks. Main thread only.
    std::vector<MD4Key> m_metaRebuildQueue;
    int m_metaRebuildTotal = 0;

    /// ED2K republish throttle — MFC m_lastPublishED2KFlag / m_lastPublishED2K
    /// (srchybrid/SharedFileList.cpp:1229-1236). Main thread only.
    bool m_republishED2K = false;
    time_t m_lastPublishED2K = 0;

    // Kad publishing round-robin state
    uint32 m_currFileSrc = 0;
    uint32 m_currFileNotes = 0;
    /// A probe that found nothing due is not repeated every tick (MFC probes one
    /// file per call and rests). Cleared when a file joins the share.
    static constexpr time_t kPublishProbeRestSecs = 10;
    time_t m_srcProbeRestUntil = 0;
    time_t m_notesProbeRestUntil = 0;
    time_t m_lastPublishKadSrc = 0;
    time_t m_lastPublishKadNotes = 0;
};

} // namespace eMule
