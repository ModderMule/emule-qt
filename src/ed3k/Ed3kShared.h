#pragma once

/// @file Ed3kShared.h
/// @brief eMule's shared files served over ed3k: every complete file of the SharedFileList
/// is hashed (BLAKE3 root and block values), shared under its root, published as a source
/// and named by an `ed2k` ALIAS record (alias.md §2: MD4 ‖ size).
///
/// - Serving (WantBlock/VerifyBlock) and the upload slot (FileRequested) are the base's
///   (Ed3kJoinBase), owner "shared", member = a stable index per file.
/// - Hash cache: Config/ed3k/shared-index.dat, keyed by MD4 + size + mtime + path, holds the
///   root and where the block values are in shared-hashes.dat (append-only, compacted at
///   start); a hit shares at once. Misses are hashed on a worker, one file per disk at a time
///   (disks in parallel, as eD2K). A file eD2K hashes new comes with its ed3k hash from the
///   same read (sideHasherFactory).
/// - Block values: written to disk once hashed; the engine holds them in RAM within its
///   budget (hashRamMiB, 0 = auto: 2 % of RAM, 64–512 MiB; 0 while free RAM is below
///   lowRamPercent) and asks for them again (WantHashes) when a peer needs them. A start
///   reads none of them. No copy here once stored.
/// - Removed files are unshared and unpublished; a moved file keeps its root when its size
///   and date did not change.

#include "Ed3kDecline.h"
#include "Ed3kJoinBase.h"

#include "files/SideHasher.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QStringList>
#include <QString>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <ctime>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace eMule {
class KnownFile;
class SharedFileList;
}

namespace eMule::ed3k {

/// A complete shared file as Ed3kShared sees it.
struct SharedEntry {
    /// 16 B eD2K hash; ed3kOnly: its root's head (no eD2K hash); a landed file kept out of
    /// eD2K: Ed3kShared::pathKeyOf its path
    QByteArray md4;
    QString path;     ///< full path of the data
    QString name;
    std::uint64_t size = 0;
    /// the file's upload priority (the engine's queue: eMule's score)
    ::ed3k::UploadPriority priority = ::ed3k::UploadPriority::Normal;
    /// the user's own rating (0 none, 1..5) and comment: its NOTE record (Protocol.md §10.7)
    std::uint32_t rating = 0;
    QString comment;
    /// media length in seconds and kbit/s of its keyword record (FileInfo), 0 = unknown
    std::uint32_t duration = 0;
    std::uint32_t bitrate = 0;
    /// media tags of its keyword record (FileInfo §5.4, cut to 128 B, codec 32 B); empty = unknown
    QString artist;
    QString album;
    QString title;
    QString codec;
    /// hashed while eD2K was disabled (KnownFile::isEd3kOnly): no ed2k name, no ALIAS
    bool ed3kOnly = false;
    /// the share list's date of it (secs, stat'ed off the main thread); 0 = unknown
    time_t fileDate = 0;

    [[nodiscard]] bool hasNote() const { return rating != 0 || !comment.isEmpty(); }
};

/// The share list as Ed3kShared reads it: SharedFileList in the daemon
/// (Ed3kShared::listAdapter), a fake in tests.
class SharedFileSource {
public:
    virtual ~SharedFileSource() = default;
    /// Every complete shared file.
    [[nodiscard]] virtual std::vector<SharedEntry> files() const = 0;
    /// The complete shared file of @p md4; nullopt: not shared, or a part file.
    [[nodiscard]] virtual std::optional<SharedEntry> find(const QByteArray& md4) const = 0;
};

class Ed3kShared : public Ed3kJoinBase {
    Q_OBJECT

public:
    /// Owner of every shared file in the base.
    static inline const QString kOwner = QStringLiteral("shared");

    /// @p configDir: the cache goes to <configDir>/ed3k/shared-index.dat + shared-hashes.dat. Both must outlive this.
    Ed3kShared(Ed3kSession& session, SharedFileSource& source, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kShared() override;

    /// SharedFileList as a SharedFileSource. The list must outlive it.
    [[nodiscard]] static std::unique_ptr<SharedFileSource> listAdapter(SharedFileList& list);
    /// @p file as an entry; nullopt for a part file or one without a hash.
    [[nodiscard]] static std::optional<SharedEntry> entryOf(const KnownFile& file);
    /// Connects the list's signals to the slots below.
    void connectList(SharedFileList& list);

    /// Shares every file of the source (pref ed3k.shareFiles on). Idempotent.
    void start() override;
    /// Saves the cache, unpublishes and unshares every file. Before the ed3k node stops.
    void stop() override;
    /// The pref ed3k.shareFiles: starts (node running) or stops.
    void setEnabled(bool on);
    [[nodiscard]] bool isEnabled() const { return m_enabled; }

    // -- state, for the daemon and tests -----------------------------------

    /// The root @p md4 is shared under; nullopt: not (yet) shared.
    [[nodiscard]] std::optional<::ed3k::Root> rootOf(const QByteArray& md4) const;
    /// The shared file served under @p root; nullopt: none (the upload queue view names files).
    [[nodiscard]] std::optional<SharedEntry> entryOfRoot(const ::ed3k::Root& root) const;
    /// @p fn(md4, root) for every file open on the engine (shared and published).
    void forEachSharedRoot(const std::function<void(const QByteArray& md4, const ::ed3k::Root& root)>& fn) const;
    /// Opens @p root on the engine again (another sharer closed or downgraded its record:
    /// Ed3kDownloads dropped a duplicate download). false: no shared file of that root.
    bool reopen(const ::ed3k::Root& root);
    /// A file of the list was hashed to @p root (shared, or waiting for its key).
    [[nodiscard]] bool holdsRoot(const ::ed3k::Root& root) const;
    /// The `ed2k` ALIAS record of @p md4 is published.
    [[nodiscard]] bool aliasPublished(const QByteArray& md4) const;
    /// @p md4 is published with its keywords (Publish::Both: found by ed3k search), not only
    /// as a source; not while holding back.
    [[nodiscard]] bool keywordsPublished(const QByteArray& md4) const;
    /// Batch 15 #12: @p check true = never shared (an opened plain copy of a sealed set
    /// member, hidden-set.md §4.2: Ed3kPlainCopies); asked as files come.
    void setNeverShared(std::function<bool(const QString& path)> check) { m_neverShared = std::move(check); }
    /// The key @p md4 is shared under (hops.md §1, shown to the user); nullopt: not hashed yet.
    [[nodiscard]] std::optional<::ed3k::KeyKind> keyOfFile(const QByteArray& md4) const;
    /// The key a file of the list hashed to @p root is held under; nullopt: none here.
    [[nodiscard]] std::optional<::ed3k::KeyKind> keyOfRoot(const ::ed3k::Root& root) const;
    /// Roots of the list's files whose MD4 @p list names (declined-records.md §2: for
    /// Ed3kSession::setDeclineRootsOf).
    [[nodiscard]] std::vector<::ed3k::Root> rootsOfListed(const DeclineList& list) const;
    /// hops.md §1 SHOULD: @p md4 starts downloading on @p networks (eD2K) and is shared here
    /// under the anonymous key: the user is told once (anonExposed). true: told now.
    bool tellDownloadElsewhere(const QByteArray& md4, const QString& name, const QString& networks);
    /// The user is told once per run that @p root (anonymous key) is on @p networks too.
    /// true: told now; false: told before.
    bool tellExposed(const ::ed3k::Root& root, const QString& name, const QString& networks);
    /// The root @p md4 was hashed to, shared or not (a file of the anonymous key waits while
    /// hiding is off); nullopt: not hashed yet.
    [[nodiscard]] std::optional<::ed3k::Root> hashedRootOf(const QByteArray& md4) const;
    /// Moves a shared file to @p key (the user was asked, hops.md §1): unpublished and closed
    /// under its key, shared under the other. false: not shared here, or the same key.
    bool moveFileKey(const ::ed3k::Root& root, ::ed3k::KeyKind key);
    /// Tests: an unreadable file is hashed again after @p ms (default kRehashMs).
    void setRehashDelay(int ms) { m_rehashMs = ms; }
    /// Files hashed since construction (cache misses).
    [[nodiscard]] int hashCount() const { return m_hashCount; }
    /// Files listed, waiting for or in a hash, and open on the engine (published).
    struct Counts {
        std::size_t total = 0;
        std::size_t hashing = 0;
        std::size_t published = 0;
    };
    [[nodiscard]] Counts counts() const;
    /// Files being hashed now (one per disk at most).
    [[nodiscard]] int hashingNow() const { return int(m_hashingVolumes.size()); }
    /// Tests: which disk a directory counts as (default: DiskLoadLimiter::volumeKey).
    void setVolumeKeyFn(std::function<QString(const QString& dir)> fn)
    {
        m_volumeKeyFn = std::move(fn);
        m_volumeOfDir.clear();
    }
    /// For SharedFileList::setSideHasher: eD2K's read of a new file hashes it for ed3k too.
    /// Nothing while stopped; the result comes back on this thread before the list adds the file.
    /// A file of @p pipeFrom bytes or more is hashed on its own thread (SideHashPipe).
    [[nodiscard]] SideHasherFactory sideHasherFactory(quint64 pipeFrom = kSidePipeFrom);
    static constexpr quint64 kSidePipeFrom = 16ull << 20;
    /// The index of the hash cache (shared-index.dat).
    [[nodiscard]] QString cachePath() const;
    /// The block values of the hash cache (shared-hashes.dat).
    [[nodiscard]] QString hashesPath() const;
    /// Free RAM below @p percent of the total: the engine keeps only block values in use. 0 = off.
    void setLowRamPercent(int percent);
    /// The engine's RAM for block values, MiB; 0 = auto (2 % of RAM, 64–512 MiB).
    void setHashRamMiB(int mib);
    /// The budget the engine gets now, bytes (0 while RAM is low).
    [[nodiscard]] std::uint64_t hashRamBudget();
    /// Auto budget for @p totalRam bytes of RAM (0 = unknown).
    [[nodiscard]] static std::uint64_t autoHashRam(std::uint64_t totalRam);
    /// Tests: replaces the RAM check (true = low).
    void setLowRamCheck(std::function<bool()> check) { m_lowRamCheck = std::move(check); }
    /// The name own notes show (Note.nick, §10.7; cut to 64 B); unset or empty: none. Never
    /// under the anonymous key.
    void setNoteNick(std::function<QString()> nick) { m_noteNick = std::move(nick); }

    /// @p s cut to at most @p bytes of UTF-8, on a character boundary.
    [[nodiscard]] static QString clipUtf8(const QString& s, qsizetype bytes);
    /// Cache entries whose block values are in RAM now.
    [[nodiscard]] std::size_t hashesInRam() const;
    /// Key of a file without an eD2K hash (SharedEntry::md4): never 16 B.
    [[nodiscard]] static QByteArray pathKeyOf(const QString& path);
    /// New eD2K hashes allowed (eD2K not disabled). Off: hashes known before keep their
    /// `ed2k` ALIAS; nothing new is hashed for eD2K.
    void setEd2kHashing(bool on);
    [[nodiscard]] bool ed2kHashing() const { return m_ed2kHashing; }
    /// Landed files kept out of eD2K's share (hoster/Usenet "share landed files" off): not
    /// served here (their join shares them), only their `ed2k` ALIAS published, hashed for it
    /// while ed2kHashing(). Replaces the list.
    void setAliasOnlyFiles(const QStringList& paths);
    /// The eD2K hash an alias-only file was read to; empty: none (yet).
    [[nodiscard]] QByteArray aliasOnlyEd2k(const QString& path) const;
    /// The `ed2k` Alias message (proto) of a file (alias.md §2): MD4 ‖ size as 8 B big endian.
    [[nodiscard]] static ::ed3k::Bytes ed2kAlias(const QByteArray& md4, std::uint64_t size, const ::ed3k::Root& root,
                                                 const QString& name);

public slots:
    void onFileAdded(const eMule::ed3k::SharedEntry& entry);
    void onFileRemoved(const QByteArray& md4);
    /// Renamed or moved: same root when size and date are unchanged, else hashed again.
    void onFileRelocated(const eMule::ed3k::SharedEntry& entry);
    /// Something of the file changed (a download completed): shared when it is complete now.
    void onFileChanged(const QByteArray& md4);
    /// The share list's scan is done; @p full: the whole share. The first full one after
    /// start() logs the count again (start() runs before the startup scan filled the list).
    void onListScanned(bool full);

signals:
    /// @p md4 is shared and published under @p root.
    void fileShared(const QByteArray& md4);
    /// hops.md §1 SHOULD: a file shared under the anonymous key is also on @p networks
    /// (eD2K/Kad: the share list is eD2K's): sources there show the address. Once per file a run.
    void anonExposed(const QString& name, const QString& networks);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    void onFileBlock(const Download& d, const ::ed3k::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ::ed3k::Root& root, const QByteArray& data) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    [[nodiscard]] bool sharedGone(const Shared& s) const override;
    [[nodiscard]] ::ed3k::UploadPriority uploadPriority(const Shared& s) const override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;
    /// The share list follows the hiding switch (no eD2K ALIAS under the anonymous key).
    [[nodiscard]] bool anonCapable() const override { return true; }
    /// a listed file hashed to @p root (holdsRoot), shared or about to be
    [[nodiscard]] bool claimsRoot(const ::ed3k::Root& root) const override { return holdsRoot(root); }
    void onHideChanged(bool hiding) override;
    [[nodiscard]] bool answerHashes(std::uint64_t id, const ::ed3k::Root& root) override;
    void onHashesLost(const ::ed3k::Root& root) override;
    /// By root or MD4: listed files withdrawn (source, keywords, note, `ed2k` ALIAS), files
    /// off the list published again (publishFile).
    void onDeclinedChanged() override;

private:
    /// What was hashed: the cache entry of one file.
    struct Hashed {
        QString path;
        std::uint64_t size = 0;
        qint64 mtimeMs = 0;
        ::ed3k::Root root;
        ::ed3k::Bytes hashes;   ///< valid while inRam
        bool inRam = false;     ///< false: on disk only (diskOffset)
        qint64 diskOffset = -1; ///< in shared-hashes.dat; -1: not stored
        quint32 diskLen = 0;
        QByteArray diskCheck;   ///< MD5 of the stored values (a torn write reads as a miss)
        QByteArray ed2k;        ///< eD2K hash of a path-keyed entry, when hashed for it
    };
    struct File {
        SharedEntry entry;
        int member = -1;
        qint64 mtimeMs = 0;   ///< of the file when it was taken; kMtimeUnknown: stat'ed when hashed
        bool queued = false;  ///< waits for a hash
        bool shared = false;  ///< open on the engine, published
        ::ed3k::Bytes alias;  ///< the ALIAS record published; empty: none
        bool keywords = false;  ///< published with Publish::Both
        // was: qint64 noteAtMs = 0; (packmule stores a kept note again by itself)
        bool notePublished = false;   ///< a NOTE record kept published
        bool declined = false;   ///< shared, not published: declined (decline.txt)
        int rehashTries = 0;    ///< hashes that failed (unreadable)
        bool aliasOnly = false; ///< only its `ed2k` ALIAS (setAliasOnlyFiles), never served here
        bool aliasWaits = false;   ///< alias-only: its root not shared yet, tried again
        int aliasTries = 0;        ///< alias-only: refused for that; the first retry is soon
    };

    /// File::mtimeMs not read yet: hashNext stats it (no disk on add, #200)
    static constexpr qint64 kMtimeUnknown = std::numeric_limits<qint64>::min();

    /// @p aliasOnly: setAliasOnlyFiles (not served, its `ed2k` ALIAS only)
    void add(const SharedEntry& entry, bool aliasOnly = false);
    /// The cached ms date of @p entry when the share list's date says it is that file
    /// (same size, same second): no stat needed. nullopt: unknown, or a date to stat.
    [[nodiscard]] std::optional<qint64> listedDateMs(const SharedEntry& entry, bool byPath, bool aliasOnly) const;
    /// a cache entry add() would take for @p entry (by MD4, or by path before)
    [[nodiscard]] bool hasCacheFor(const SharedEntry& entry, bool byPath) const;
    void remove(const QByteArray& md4);
    /// Shares @p f; values from disk when not in RAM. An unreadable entry is dropped and
    /// @p f hashed again (@p h is gone then).
    void share(File& f, Hashed& h);
    /// The records of shared @p f: source and keywords, note, `ed2k` ALIAS (none when declined).
    void publishFile(File& f, const ::ed3k::Root& root, std::uint64_t size);
    /// Alias-only @p f: its `ed2k` ALIAS of @p h (no hash for eD2K: nothing). Its root not
    /// shared (yet): tried again by the priority timer.
    void publishAliasOnly(File& f, const Hashed& h);
    /// The `ed2k` ALIAS of @p f taken back (any kind of entry).
    void withdrawAlias(File& f);
    /// @p f needs a read: alias-only wants an eD2K hash (only while ed2kHashing()).
    [[nodiscard]] bool wantsEd2k(const File& f) const { return f.aliasOnly; }
    class SideSink;
    void enqueueHash(File& f);
    /// one file per disk in flight
    void hashNext();
    /// files waiting, all disks
    [[nodiscard]] std::size_t hashQueueSize() const;
    /// the disk of @p path (cached per directory)
    [[nodiscard]] QString volumeOf(const QString& path);
    /// The hash of a file eD2K just read (SideSink): cached, and shared when listed.
    void primeHash(const QByteArray& md4, const QString& path, qint64 mtimeMs, const FileHash& hashed);
    /// primeHash while stopped (an ed3k-only file hashed for its key, ed3k off): cached on
    /// disk for the next start, no second read then
    void primeStopped(const QByteArray& md4, const QString& path, qint64 mtimeMs, const FileHash& hashed);
    /// primeStopped's index save, now (start/stop)
    void flushStoppedSave();
    /// @p hashed into the cache: values stored on disk first, then the index.
    Hashed& remember(const QByteArray& md4, const QString& path, qint64 mtimeMs, const FileHash& hashed);
    /// queue drained: one summary line, run reset
    void endHashRun();
    /// once a minute while a run lasts: done, left, rate
    void logHashProgress();
    void onHashed(const QByteArray& md4, const QString& path, qint64 mtimeMs, const std::optional<FileHash>& hashed);
    /// An unreadable file's hash once more (kRehashMs after it failed).
    void rehash(const QByteArray& md4);
    /// Files whose shares the base closed (gone or changed): dropped, taken again if listed.
    void recheck();
    /// Upload priorities read again (the user's, or auto): changed ones to the engine.
    /// Own notes too: a changed one published, a held one again before it expires.
    void refreshPriorities();
    /// Publishes @p f's NOTE record (rating, comment, nick) when it has one and does not hold
    /// back, under the file's key; packmule stores it again before its 24 h end. A cleared
    /// note is withdrawn (its copies run out).
    void publishNote(File& f, const ::ed3k::Root& root);
    /// The nick of a note of @p root: none under the anonymous key.
    [[nodiscard]] std::string noteNick(const ::ed3k::Root& root) const;
    [[nodiscard]] const File* fileOfMember(int member) const;
    /// Size and date of @p path on disk agree with @p f.
    [[nodiscard]] static bool unchanged(const QString& path, std::uint64_t size, qint64 mtimeMs);
    [[nodiscard]] static qint64 mtimeOf(const QString& path);
    /// @p c holds the root of the file at @p path: same size, same date as eD2K's known.met
    /// sees it (sameFileDate: FAT/NAS rounding, DST shift). Path may differ (moved, case).
    [[nodiscard]] static bool cacheMatches(const Hashed& c, const QString& path, std::uint64_t size, qint64 mtimeMs);
    void loadCache();
    void saveCache();
    /// Unlisted entries whose file is gone: checked on a pool thread (after the list's full
    /// scan, at most every kPruneEveryMs), dropped in onCachePruned().
    void pruneCacheLater();
    void onCachePruned(const std::vector<std::pair<QByteArray, QString>>& gone);
    /// Appends @p data (@p h's values) to shared-hashes.dat and notes where. false: not stored.
    bool storeHashes(Hashed& h, std::span<const std::byte> data) const;
    /// shared-index.dat of the entries stored on disk.
    [[nodiscard]] QByteArray indexBlob() const;
    /// @p h's values from shared-hashes.dat; nullopt: missing or torn.
    [[nodiscard]] std::optional<::ed3k::Bytes> loadHashes(const Hashed& h) const;
    /// shared-hashes.dat rewritten with the live values when mostly dead.
    void compactHashes();
    /// Free RAM below m_lowRamPercent (checked at most every few seconds).
    [[nodiscard]] bool lowRam();
    /// Values stored on disk leave RAM: after a hash run, or any time RAM is low.
    void releaseRamHashes();
    /// hashRamBudget() to the engine when it changed.
    void applyHashRamBudget();
    /// The cache entry of @p root.
    using CacheIt = std::map<QByteArray, Hashed>::iterator;
    [[nodiscard]] CacheIt cacheOfRoot(const ::ed3k::Root& root);

    SharedFileSource& m_source;
    bool m_enabled = true;
    std::map<QByteArray, File> m_files;
    /// anonExposed told for these roots (once per file a run)
    std::set<::ed3k::Root> m_exposedTold;
    std::map<int, QByteArray> m_members;
    int m_nextMember = 0;
    /// md4 → what was hashed (loaded from shared-index.dat, kept while the path exists)
    std::map<QByteArray, Hashed> m_cache;
    /// disk → md4s waiting; one file per disk in m_hashingVolumes
    std::map<QString, std::deque<QByteArray>> m_hashQueues;
    std::set<QString> m_hashingVolumes;
    std::map<QString, QString> m_volumeOfDir;
    std::function<QString(const QString&)> m_volumeKeyFn;
    /// read by SideSink factories on hashing workers
    std::shared_ptr<std::atomic<bool>> m_sideOn = std::make_shared<std::atomic<bool>>(false);
    int m_hashCount = 0;
    /// current hash run (queue busy → empty): files, bytes, time; logged when done
    int m_runFiles = 0;
    qint64 m_runBytes = 0;
    QElapsedTimer m_runTimer;
    /// the count after the share list's first full scan was logged (per start)
    bool m_scanLogged = false;
    /// pruneCacheLater(): a check in flight; when the last one started (0: none yet)
    bool m_pruning = false;
    qint64 m_prunedAtMs = 0;
    int m_rehashMs = kRehashMs;
    /// reported by sharedGone() (const): handled on the next turn
    mutable std::set<QByteArray> m_suspects;
    mutable QTimer m_recheckTimer;
    QTimer m_priorityTimer;
    QTimer m_aliasRetry;   ///< alias-only files whose join shares the root a moment later
    QTimer m_stoppedSave;  ///< primeStopped: the index saved once a burst is over
    bool m_stoppedDirty = false;   ///< primeStopped changed the index, not saved yet
    bool m_cacheLoaded = false;   ///< loadCache() ran: m_cache holds the index (stop() keeps it)
    std::function<bool(const QString&)> m_neverShared;
    bool m_ed2kHashing = true;
    QHash<QByteArray, QString> m_aliasOnly;   ///< pathKeyOf → path, setAliasOnlyFiles
    int m_lowRamPercent = 20;
    std::function<bool()> m_lowRamCheck;
    std::function<QString()> m_noteNick;
    qint64 m_lowRamCheckedMs = 0;
    bool m_lowRam = false;
    int m_hashRamMiB = 0;
    /// last budget given to the engine; reset at start()
    std::optional<std::uint64_t> m_hashRamSent;
};

} // namespace eMule::ed3k
