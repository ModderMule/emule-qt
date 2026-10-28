#pragma once

/// @file Ed3kShared.h
/// @brief eMule's shared files served over ed3k: every complete file of the SharedFileList
/// is hashed (BLAKE3 root and block values), shared under its root, published as a source
/// and named by an `ed2k` ALIAS record (alias.md §2: MD4 ‖ size).
///
/// - Serving (WantBlock/VerifyBlock) and the upload slot (FileRequested) are the base's
///   (Ed3kJoinBase), owner "shared", member = a stable index per file.
/// - Hash cache: Config/ed3k/shared.dat, keyed by MD4 + size + mtime + path, holds the root
///   and the block values; a hit shares at once. Misses are hashed on a worker, one at a
///   time (a big share list must not flood the thread pool).
/// - Removed files are unshared and unpublished; a moved file keeps its root when its size
///   and date did not change.

#include "Ed3kJoinBase.h"

#include <QByteArray>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
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

    /// @p configDir: the cache goes to <configDir>/ed3k/shared.dat. Both must outlive this.
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
    /// The `ed2k` ALIAS record of @p md4 is published.
    [[nodiscard]] bool aliasPublished(const QByteArray& md4) const;
    /// @p md4 is published with its keywords (Publish::Both: found by ed3k search), not only
    /// as a source; not while holding back.
    [[nodiscard]] bool keywordsPublished(const QByteArray& md4) const;
    /// Tests: an unreadable file is hashed again after @p ms (default kRehashMs).
    void setRehashDelay(int ms) { m_rehashMs = ms; }
    /// Files hashed since construction (cache misses).
    [[nodiscard]] int hashCount() const { return m_hashCount; }
    [[nodiscard]] QString cachePath() const;
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

signals:
    /// @p md4 is shared and published under @p root.
    void fileShared(const QByteArray& md4);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    void onFileBlock(const Download& d, const ::ed3k::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ::ed3k::Root& root, const QByteArray& data) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    [[nodiscard]] bool sharedGone(const Shared& s) const override;
    [[nodiscard]] ::ed3k::UploadPriority uploadPriority(const Shared& s) const override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;

private:
    /// What was hashed: the cache entry of one file.
    struct Hashed {
        QString path;
        std::uint64_t size = 0;
        qint64 mtimeMs = 0;
        ::ed3k::Root root;
        ::ed3k::Bytes hashes;
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
    void share(File& f, const Hashed& h);
    void enqueueHash(File& f);
    void hashNext();
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

    SharedFileSource& m_source;
    bool m_enabled = true;
    std::map<QByteArray, File> m_files;
    std::map<int, QByteArray> m_members;
    int m_nextMember = 0;
    /// md4 → what was hashed (loaded from shared.dat, kept while the path exists)
    std::map<QByteArray, Hashed> m_cache;
    std::deque<QByteArray> m_hashQueue;
    bool m_hashing = false;
    int m_hashCount = 0;
    int m_rehashMs = kRehashMs;
    /// reported by sharedGone() (const): handled on the next turn
    mutable std::set<QByteArray> m_suspects;
    mutable QTimer m_recheckTimer;
    QTimer m_priorityTimer;
};

} // namespace eMule::ed3k
