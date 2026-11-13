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

#include "Ed3kJoinBase.h"

#include "files/SideHasher.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QString>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
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
    QByteArray md4;   ///< 16 B eD2K hash
    QString path;     ///< full path of the data
    QString name;
    std::uint64_t size = 0;
    /// the file's upload priority (the engine's queue: eMule's score)
    ::ed3k::UploadPriority priority = ::ed3k::UploadPriority::Normal;
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
    /// Cache entries whose block values are in RAM now.
    [[nodiscard]] std::size_t hashesInRam() const;
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
    void onHideChanged(bool hiding) override;
    [[nodiscard]] bool answerHashes(std::uint64_t id, const ::ed3k::Root& root) override;
    void onHashesLost(const ::ed3k::Root& root) override;

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
    };
    struct File {
        SharedEntry entry;
        int member = -1;
        qint64 mtimeMs = 0;   ///< of the file when it was taken
        bool queued = false;  ///< waits for a hash
        bool shared = false;  ///< open on the engine, published
        ::ed3k::Bytes alias;  ///< the ALIAS record published; empty: none
        bool keywords = false;  ///< published with Publish::Both
        int rehashTries = 0;    ///< hashes that failed (unreadable)
    };

    void add(const SharedEntry& entry);
    void remove(const QByteArray& md4);
    /// Shares @p f; values from disk when not in RAM. An unreadable entry is dropped and
    /// @p f hashed again (@p h is gone then).
    void share(File& f, Hashed& h);
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
    void refreshPriorities();
    [[nodiscard]] const File* fileOfMember(int member) const;
    /// Size and date of @p path on disk agree with @p f.
    [[nodiscard]] static bool unchanged(const QString& path, std::uint64_t size, qint64 mtimeMs);
    [[nodiscard]] static qint64 mtimeOf(const QString& path);
    void loadCache();
    void saveCache();
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
    int m_rehashMs = kRehashMs;
    /// reported by sharedGone() (const): handled on the next turn
    mutable std::set<QByteArray> m_suspects;
    mutable QTimer m_recheckTimer;
    QTimer m_priorityTimer;
    std::function<bool(const QString&)> m_neverShared;
    int m_lowRamPercent = 20;
    std::function<bool()> m_lowRamCheck;
    qint64 m_lowRamCheckedMs = 0;
    bool m_lowRam = false;
    int m_hashRamMiB = 0;
    /// last budget given to the engine; reset at start()
    std::optional<std::uint64_t> m_hashRamSent;
};

} // namespace eMule::ed3k
