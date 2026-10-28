#pragma once

/// @file Ed3kDownloads.h
/// @brief Plain ed3k file downloads (a search row, an `ed3k:file` link without `bt=`), and
/// the files of callers (set members: Ed3kSets) through the same list.
///
/// Public API (for the daemon, the IPC bridge and other ed3k classes):
/// - add(Ed3kDownloadSpec) / addLink(link): starts a download of a root; owner tag "dl"
///   (kOwner) unless the spec names another (e.g. "set:<id>"); targetPath empty: the
///   incoming dir + name. Refused: root known already, size 0, no name.
/// - pause / resume / cancel (the part file deleted; a complete row only leaves the list)
///   / setPriority (eMule's kPr*: 0 low, 1 normal, 2 high, 3 very high, 4 very low) / clearCompleted.
/// - list() / info(): snapshots (Ed3kDownloadInfo); signals added, changed (coalesced, at
///   most once a second), progress (bytes held), completed (final path), failed, removed.
/// - setDirs(temp, incoming): <temp>/<root hex>.ed3k.part while it runs; the complete file
///   moves to the target (a free name next to it when taken).
/// - setCompletedHook: called after the move (daemon: SharedFileList adds the file).
///   setSharedElsewhere / releaseShare: another sharer (Ed3kShared) holds the root now;
///   this class forgets its share without closing it on the engine.
///
/// Mechanics are Ed3kJoinBase's (owner = the spec's owner, member = a stable id per
/// download): Kad sources, streams, block requests. wantedBlocks = the missing 256 KiB
/// blocks; each verified block is written into the part file and marked in a bitmap
/// (saved, debounced: Config/ed3k/downloads.dat). Complete: hashed on a worker (root and
/// size must agree; else the bitmap is dropped and it downloads again), moved, shared
/// (Publish::Both, keywords unless holding back) and kept (keepMember) for a restart.
/// Pieces (4 MiB) held are shared while it runs once the block values are in.
///
/// ToDo: the download priority is kept and shown only; the engine has no priority between
/// downloads (it could rank block requests or streams per file). (It orders the turns at a
/// source shared by several downloads: Ed3kJoinBase::downloadPriority.)

#include "Ed3kJoinBase.h"

#include <QDateTime>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace eMule::ed3k {

/// What a caller asks for.
struct Ed3kDownloadSpec {
    ::ed3k::Root root;
    std::uint64_t size = 0;
    QString name;
    /// final path of the file; empty: <incoming>/<name>
    QString targetPath;
    /// owner tag in the base (Config/ed3k/dl/<owner>/ keeps it); empty: Ed3kDownloads::kOwner
    QString owner;
    int category = 0;
    bool paused = false;
    /// eMule's download priority (kPr*: 0 low, 1 normal, 2 high, 3 very high, 4 very low)
    int priority = 1;
};

/// One download as the list shows it.
struct Ed3kDownloadInfo {
    enum class State : int { Downloading = 0, Paused = 1, Verifying = 2, Complete = 3, Failed = 4 };

    ::ed3k::Root root;
    std::uint64_t size = 0;
    QString name;
    QString owner;
    int category = 0;
    int priority = 1;
    State state = State::Downloading;
    QString partPath;
    QString targetPath;
    /// the file now: the part file, or the final path once complete
    QString path;
    std::uint64_t done = 0;        ///< bytes held (verified blocks)
    std::uint64_t rate = 0;        ///< B/s over the last second
    int sources = 0;               ///< streams open
    int transferring = 0;          ///< streams holding a slot
    qint64 addedOn = 0;            ///< s since the epoch
    qint64 completedOn = 0;
    QString error;                 ///< Failed: why (not localized: a log line)
};

class Ed3kDownloads : public Ed3kJoinBase {
    Q_OBJECT

public:
    /// Owner of the downloads a user started (search rows, links).
    static inline const QString kOwner = QStringLiteral("dl");

    /// @p configDir: the state goes to <configDir>/ed3k/downloads.dat (loaded here).
    Ed3kDownloads(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kDownloads() override;

    /// Opens the downloads that are not paused or complete; shares the complete ones again.
    /// Called when the node runs (statusChanged); idempotent.
    void start() override;
    /// Saves and closes every download; the list stays (shown while the node is off).
    void stop() override;

    /// Part files go to @p tempDir, complete files without a target to @p incomingDir.
    void setDirs(const QString& tempDir, const QString& incomingDir);
    /// The incoming dir of a category (eMule's categories); empty: the incoming dir.
    void setCategoryDir(std::function<QString(int)> dirOf) { m_categoryDir = std::move(dirOf); }
    /// After a file was verified and moved (and shared here).
    void setCompletedHook(std::function<void(const Ed3kDownloadInfo&)> hook) { m_completedHook = std::move(hook); }
    /// Whether another sharer serves @p root already: a complete file is not shared again here.
    void setSharedElsewhere(std::function<bool(const ::ed3k::Root&)> pred) { m_sharedElsewhere = std::move(pred); }

    // -- the list ------------------------------------------------------------

    /// Starts a download; the error is a user-visible text (localized).
    std::expected<void, QString> add(const Ed3kDownloadSpec& spec);
    /// An `ed3k:file` link (UTF-8 text, parseFileLink); its root.
    std::expected<::ed3k::Root, QString> addLink(const QString& link, int category = 0, bool paused = false);
    bool pause(const ::ed3k::Root& root);
    bool resume(const ::ed3k::Root& root);
    /// Unfinished: stopped, its part file deleted. Complete: leaves the list and the own share
    /// (the file stays).
    bool cancel(const ::ed3k::Root& root);
    bool setPriority(const ::ed3k::Root& root, int priority);
    bool setCategory(const ::ed3k::Root& root, int category);
    /// Complete rows leave the list; their files stay (served on by the share list, if it took them).
    void clearCompleted();

    [[nodiscard]] std::vector<Ed3kDownloadInfo> list() const;
    [[nodiscard]] std::optional<Ed3kDownloadInfo> info(const ::ed3k::Root& root) const;
    [[nodiscard]] bool contains(const ::ed3k::Root& root) const { return m_entries.contains(root); }

    /// Another sharer (Ed3kShared) took @p root: forget the own share, do not close it.
    void releaseShare(const ::ed3k::Root& root);

    // -- tests -----------------------------------------------------------------

    /// A source by hand (tests switch Kad searches off).
    void addSourceTo(const ::ed3k::Root& root, const ::ed3k::Source& source) { addSource(root, source); }
    [[nodiscard]] QString statePath() const;
    /// Saves at once (tests, stop).
    void saveNow();

signals:
    void added(const ::ed3k::Root& root);
    /// Something of the list changed (state, progress, rate); at most once a second.
    void changed();
    void progress(const ::ed3k::Root& root, std::uint64_t done);
    void completed(const ::ed3k::Root& root, const QString& path);
    void failed(const ::ed3k::Root& root, const QString& error);
    void removed(const ::ed3k::Root& root);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    void onFileBlock(const Download& d, const ::ed3k::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ::ed3k::Root& root, const QByteArray& data) override;
    void onDownloadHashes(const Download& d, const ::ed3k::Bytes& blob) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    [[nodiscard]] bool keepsOwner(const QString& owner) const override;
    [[nodiscard]] QString keptMemberPath(const QString& owner, int member) const override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;
    /// eMule's kPr* as the turn order at a source shared by several downloads.
    [[nodiscard]] int downloadPriority(const Download& d) const override;

private:
    struct Entry {
        Ed3kDownloadInfo info;
        int id = -1;                     ///< member index in the base
        std::vector<bool> have;          ///< per 256 KiB block
        ::ed3k::Bytes hashes;            ///< block values (event::Hashes), for sharing pieces
        bool sharedByList = false;       ///< complete, served by another sharer
        std::uint64_t received = 0;      ///< bytes this run (rate)
        std::uint64_t lastReceived = 0;
    };

    Entry* entryOf(const ::ed3k::Root& root);
    const Entry* entryOfId(int id) const;
    Entry* entryOfId(int id);
    [[nodiscard]] std::uint64_t blockCount(std::uint64_t size) const;
    void openEntry(Entry& e);
    /// Every block held: hash on a worker, then finish().
    void verify(Entry& e);
    void verified(const ::ed3k::Root& root, const std::optional<FileHash>& hashed);
    /// Moves the part file to its target; the path taken, empty on failure.
    QString moveToTarget(const Entry& e, QString* error);
    void shareComplete(Entry& e, std::span<const std::byte> hashes);
    /// The 4 MiB pieces held while it runs (with block values).
    void sharePieces(Entry& e);
    void fail(Entry& e, const QString& error);
    void markChanged();
    void onTick();
    void load();
    [[nodiscard]] QString partPathOf(const ::ed3k::Root& root) const;
    [[nodiscard]] static QString freePath(const QString& path);

    std::map<::ed3k::Root, Entry> m_entries;
    int m_nextId = 0;
    QString m_tempDir;
    QString m_incomingDir;
    std::function<void(const Ed3kDownloadInfo&)> m_completedHook;
    std::function<bool(const ::ed3k::Root&)> m_sharedElsewhere;
    std::function<QString(int)> m_categoryDir;
    QTimer m_tick;
    bool m_changed = false;
};

} // namespace eMule::ed3k
