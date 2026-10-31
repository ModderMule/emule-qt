#pragma once

/// @file Ed3kJoinBase.h
/// @brief What every ed3k join (usenet.md §4, torrent.md §4) does on the ed3k side: ed3k
/// downloads of member roots (Kad sources, streams, block requests), manifests fetched
/// into memory, serving shared roots, sharing blobs, hashing complete files on a worker,
/// debounced saving, §4.2 set searches (ALIAS records naming sets), and what is kept for a
/// restart (blobs and verified members under Config/ed3k/<tag>/<owner>/).
///
/// Everything runs on the daemon thread (the engine's). The derived join says which
/// blocks it wants, where a block goes and what may be served; it keys its own state by
/// an owner id (queue item, torrent) and a member index.

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <vector>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

class Ed3kSession;

class Ed3kJoinBase : public QObject {
    Q_OBJECT

public:
    ~Ed3kJoinBase() override;

    /// Joins for what is there (saved ones loaded). Called when ed3k starts; idempotent.
    virtual void start() = 0;
    /// Saves every join and drops them; closes downloads. Before the ed3k node stops.
    virtual void stop() = 0;
    [[nodiscard]] bool isStarted() const { return m_started; }

    /// Holding back (usenet.md §6): no ALIAS records, no kept sets or lists. Default off.
    /// A change while started restarts the join; switching on withdraws the kept ALIAS records.
    void setHoldBack(bool on);
    [[nodiscard]] bool holdsBack() const { return m_holdBack; }
    /// Kad searches (sources, claims) on/off; tests switch them off.
    void setSearchEnabled(bool on) { m_searchEnabled = on; }
    /// Sources of a download are searched again every @p everyMs while no stream holds a
    /// slot, and when its last stream closed, at most once per @p minGapMs. Tests shorten it.
    void setSourceResearch(int everyMs, int minGapMs);
    /// Kad source searches asked for so far (tests).
    [[nodiscard]] std::size_t sourceSearches() const { return m_sourceSearches; }
    /// File reads queued or running on a worker (tests).
    [[nodiscard]] std::size_t readsInFlight() const;
signals:
    /// The kept set and ALIAS records of @p owner were published again after a start
    /// (@p aliases: records taken; 0 while holding back).
    void keptRecordsPublished(const QString& owner, int aliases);

public:
    /// Whether this node serves @p root (a member or a blob) now.
    [[nodiscard]] bool sharesRoot(const ffi::Root& root) const { return m_shared.contains(root); }
    /// A priority switch at a node may happen now: with a slot always; queued, only 30 min after
    /// the last queued switch there (Protocol.md §12.5: a 2nd within the budget is BUSY).
    [[nodiscard]] static bool switchAllowed(bool queued, std::optional<std::chrono::steady_clock::time_point> last,
                                            std::chrono::steady_clock::time_point now);

protected:
    static constexpr std::uint64_t kBlock = 256 * 1024;
    /// member index of a set's manifest fetched into memory (set searches, links)
    static constexpr int kSetManifest = -1;
    /// a complete member whose hash failed is hashed again after this long (and at the next start)
    static constexpr int kRehashMs = 10 * 60 * 1000;
    static constexpr int kRehashTries = 6;

    /// One ed3k download: a member's root into its file, or a manifest into memory.
    struct Download {
        QString owner;
        int member = -1;
        std::uint64_t size = 0;
        std::map<std::uint64_t, bool> streams;                ///< stream → holds a slot
        std::map<std::uint64_t, std::uint64_t> outstanding;   ///< offset → stream
        std::set<ffi::NodeId> asked;
        bool hashes = false;
        /// fetched into memory (fetchBlob): the bytes and the blocks there
        bool inMemory = false;
        QByteArray memory;
        std::set<std::uint64_t> got;
        /// sources last searched (ms since the epoch); 0: never
        qint64 searchedAt = 0;
        /// stream → the uploader's piece bitmap as FileStatus told it (missing bits held)
        std::map<std::uint64_t, std::vector<std::uint8_t>> sourceHave;
    };

    /// A file read for a worker. size, mtimeMs >= 0: the file must still have them.
    struct FileRead {
        QString path;
        std::uint64_t offset = 0;
        std::uint32_t len = 0;
        qint64 size = -1;
        qint64 mtimeMs = -1;
    };
    using ReadResults = std::vector<std::optional<QByteArray>>;

    /// A root this node serves: a member's file (owner, member) or a blob in memory.
    struct Shared {
        QString owner;
        int member = -1;
        std::uint64_t size = 0;
        QByteArray blob;
    };

    /// @p tag names the join in log lines ("usenet", "torrent").
    Ed3kJoinBase(Ed3kSession& session, const QString& configDir, QString tag, QObject* parent);

    // -- what the derived join decides ------------------------------------

    /// Block offsets of a file download still wanted (outstanding ones are skipped here).
    [[nodiscard]] virtual std::vector<std::uint64_t> wantedBlocks(const Download& d) const = 0;
    /// A verified block of a file download arrived.
    virtual void onFileBlock(const Download& d, const ffi::event::Block& b) = 0;
    /// A blob (fetchBlob) is complete and verified block by block. The download is gone.
    virtual void onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data) = 0;
    /// The block values of a download arrived (event::Hashes).
    virtual void onDownloadHashes(const Download& d, const ffi::Bytes& blob) { Q_UNUSED(d) Q_UNUSED(blob) }
    /// Where the bytes of a shared member to serve are (read on a worker); nullopt: not
    /// servable (not verified, gone).
    [[nodiscard]] virtual std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const = 0;
    /// The upload priority of a shared member, for the engine's queue (eMule's score).
    [[nodiscard]] virtual ffi::UploadPriority uploadPriority(const Shared& s) const
    {
        Q_UNUSED(s) return ffi::UploadPriority::Normal;
    }
    /// A shared member could not be read: whether its file is gone for good (not shared then).
    [[nodiscard]] virtual bool sharedGone(const Shared& s) const { Q_UNUSED(s) return false; }
    /// Engine events the base leaves alone (AliasFound of searches not startSetSearch's ...).
    virtual void onOtherEvent(const ffi::Event& event) { Q_UNUSED(event) }
    /// A file stream of a download was opened to @p source (FileRequest sent).
    virtual void onStreamOpened(const ffi::Root& root, std::uint64_t stream, const ffi::Source& source)
    {
        Q_UNUSED(root) Q_UNUSED(stream) Q_UNUSED(source)
    }
    /// The FileStatus of a stream of this join (after the base took it).
    virtual void onStreamStatus(const ffi::event::FileStatus& status) { Q_UNUSED(status) }
    /// A stream of this join closed (after the base dropped it).
    virtual void onStreamClosed(const ffi::event::StreamClosed& closed) { Q_UNUSED(closed) }
    /// Download priority, -2 (very low) to 2 (very high), eD2K's order (realPriority - 2).
    /// Turn order at a source node shared with other downloads (one file stream per uploader,
    /// Protocol.md §12.5): higher first (a higher waiter takes the node from a lower holder at
    /// once), the same in arrival order. Also the weight of a download in the session's
    /// block-request depth and its stream cap (priorityWeight). Default 0.
    [[nodiscard]] virtual int downloadPriority(const Download& d) const { Q_UNUSED(d) return 0; }
    /// Whether @p owner still wants a set: the candidates of its set searches are tried only then.
    [[nodiscard]] virtual bool wantsSet(const QString& owner) const { Q_UNUSED(owner) return false; }
    /// The manifest of the set tried for @p owner arrived (verified block by block): take it,
    /// or refuseSet(). The download is gone.
    virtual void onSetManifest(const QString& owner, const ffi::Root& root, const QByteArray& data)
    {
        Q_UNUSED(owner) Q_UNUSED(root) Q_UNUSED(data)
    }
    /// Every set search of @p owner ended and no set was taken.
    virtual void onSetSearchDone(const QString& owner) { Q_UNUSED(owner) }
    /// Whether what is kept of @p owner is still wanted (its queue item, its torrent is there).
    [[nodiscard]] virtual bool keepsOwner(const QString& owner) const { Q_UNUSED(owner) return false; }
    /// The file of a kept member now; empty: none.
    [[nodiscard]] virtual QString keptMemberPath(const QString& owner, int member) const
    {
        Q_UNUSED(owner) Q_UNUSED(member) return {};
    }
    /// Kept blobs are shared (again); false while they are held back (usenet §6).
    [[nodiscard]] virtual bool shareKeptBlobs() const { return true; }
    [[nodiscard]] virtual bool hasOwner(const QString& owner) const = 0;
    virtual void saveOwner(const QString& owner) = 0;

    // -- ed3k downloads ----------------------------------------------------

    /// Opens a download of @p root into the member's file and searches its sources.
    /// false: open already, or refused.
    bool openDownload(const ffi::Root& root, std::uint64_t size, const QString& owner, int member);
    /// The same into memory: onBlobFetched() when every block is there.
    bool fetchBlob(const ffi::Root& root, std::uint64_t size, const QString& owner, int member);
    void stopDownload(const ffi::Root& root);
    /// Every download of @p owner.
    void stopDownloads(const QString& owner);
    [[nodiscard]] bool downloading(const ffi::Root& root) const { return m_downloads.contains(root); }
    /// The open download of @p root (streams, outstanding blocks); null: none.
    [[nodiscard]] const Download* downloadOf(const ffi::Root& root) const;
    /// A source of a download's root: asked for the file.
    void addSource(const ffi::Root& root, const ffi::Source& source);
    void requestBlocks(const ffi::Root& root);
    /// downloadPriority() of @p root changed: its waits re-ordered, nodes taken from or given
    /// to other downloads by the new order, its depth asked again.
    void priorityChanged(const ffi::Root& root);
    /// Sources (open streams) holding each 4 MiB piece of @p d, as their FileStatus told it
    /// (255 at most); empty: no stream told a bitmap.
    [[nodiscard]] static std::vector<std::uint8_t> pieceAvailability(const Download& d);
    /// Weight of a download (eD2K's order: very low 1 … very high 5).
    [[nodiscard]] int priorityWeight(const Download& d) const;

    // -- serving -----------------------------------------------------------

    /// Shares @p data from memory under its root; logs when it is not @p expect. Its root;
    /// nullopt: not shared.
    std::optional<ffi::Root> shareBlob(const QByteArray& data, const QString& name, std::optional<ffi::Root> expect);
    /// Opens a member file on the engine (complete or partial). false: refused (logged).
    bool shareMember(const ffi::SharedFile& file, const QString& owner, int member);
    void unshare(const ffi::Root& root);
    /// Forgets the share of @p root without closing it on the engine (another sharer took it).
    void forgetShare(const ffi::Root& root) { m_shared.erase(root); }
    /// uploadPriority() of @p root again (changed by the user).
    void updateUploadPriority(const ffi::Root& root);
    /// Every root of @p owner.
    void unshareOwner(const QString& owner);
    /// Publishes a shared file: its SOURCE record, and its keywords unless holding back
    /// (Publish::Both). A name without a keyword (Protocol.md §10.1) gets its source alone.
    /// The mode taken, or the engine's error.
    std::expected<ffi::Publish, ffi::Error> publishShared(const ffi::FileInfo& info);
    [[nodiscard]] bool isShared(const ffi::Root& root) const { return m_shared.contains(root); }

    // -- hashing -----------------------------------------------------------

    struct FileHash {
        ffi::Root root;
        std::uint64_t size = 0;
        ffi::Bytes hashes;
    };
    /// Hashes @p path on a worker; @p done on this thread (nullopt: unreadable), not at
    /// all once stopped or destroyed.
    void hashInBackground(const QString& path, std::function<void(std::optional<FileHash>)> done);
    /// The same on this thread (files about to be deleted).
    [[nodiscard]] static std::optional<FileHash> hashNow(const QString& path);

    // -- reads ---------------------------------------------------------------

    /// One read; nullopt: unreadable, short, or the file changed (size, mtimeMs).
    [[nodiscard]] static std::optional<QByteArray> readFile(const FileRead& r);
    /// Reads @p reads on the session's read pool; @p done on this thread with one result
    /// each, in order. One batch per @p owner at a time (the order of the calls); dropped at
    /// closeAll(). Inline while m_readsNow.
    void readInBackground(const QString& owner, std::vector<FileRead> reads, std::function<void(ReadResults)> done);

    // -- §4.2 set searches (alias.md §4) ---------------------------------

    /// ALIAS search of @p id under @p scheme with relation @p rel. Once it found roots and
    /// wantsSet(), its candidates are tried in turn: each manifest fetched (fetchBlob,
    /// kSetManifest) → onSetManifest(). false: not started (searches off, refused).
    bool startSetSearch(const QString& owner, std::string_view scheme, std::span<const std::uint8_t> id,
                        std::uint32_t rel = ED3K_REL_SET);
    /// A set found otherwise (known, tests): tried like a candidate of a search. false: not fetched.
    bool trySetCandidate(const QString& owner, const ffi::Root& root, std::uint64_t size,
                         std::span<const ffi::PublisherKey> publishers);
    /// The set tried for @p owner (@p root) was refused: its publishers blamed (§8), the next
    /// candidate tried. false: none left (onSetSearchDone() when no search is left either).
    bool refuseSet(const QString& owner, const ffi::Root& root);
    /// The set being tried for @p owner, and the keys of its publishers.
    [[nodiscard]] std::optional<ffi::Root> setTrying(const QString& owner) const;
    [[nodiscard]] std::vector<ffi::PublisherKey> setPublishers(const QString& owner) const;
    /// Ends every set search of @p owner.
    void endSetSearches(const QString& owner);
    /// A set search of @p owner is running or has candidates left.
    [[nodiscard]] bool setSearching(const QString& owner) const;

    // -- kept for a restart (Config/ed3k/<tag>/<owner>/) -----------------

    /// Shares @p data (shareBlob) and keeps it, to share it again after a restart.
    void keepBlob(const QString& owner, const QByteArray& data, const QString& name, std::optional<ffi::Root> expect);
    /// A verified member to share again after a restart; hashed again first.
    void keepMember(const QString& owner, int member, const ffi::Root& root, std::uint64_t size, const QString& name);
    /// What completion published, to publish again after a restart: the set (its manifest
    /// kept with keepBlob) for the MEMBER rows, and ALIAS records (added to those kept).
    void keepRecords(const QString& owner, std::optional<ffi::Root> set, const QString& name,
                     const std::vector<ffi::Bytes>& aliases);
    /// Shares again what was kept of the owners still there (keepsOwner); drops the rest.
    /// The kept records are published again once each member had its first hash.
    /// Members are hashed again first; one unreadable is tried again after kRehashMs.
    void shareKept();
    /// Forgets what is kept of @p owner (removed).
    void dropKept(const QString& owner);
    [[nodiscard]] QString keptDir(const QString& owner) const;

    // -- state -------------------------------------------------------------

    void scheduleSave(const QString& owner);
    void dropDirty(const QString& owner) { m_dirty.remove(owner); }
    /// Marks started, a fresh cancel flag.
    void beginStart();
    /// Closes every download, stream and share; cancels workers; saves nothing.
    void closeAll();
    void pumpSession();
    [[nodiscard]] QString joinDir(const QString& sub) const;
    void logDebug(const QString& msg) const;
    void logWarning(const QString& msg) const;
    void logInfo(const QString& msg) const;

    static QString hexOf(std::span<const std::uint8_t> bytes);
    static QString errorName(ffi::Error e);
    static std::span<const std::byte> spanOf(const QByteArray& b);
    /// A plain file name for one directory, never a path; @p fallback when nothing is left.
    static QString safeName(QString name, const QString& fallback = {});
    /// Every file ending in @p suffix in @p dirs (each dir once), absolute paths.
    static QStringList metFilesIn(const QStringList& dirs, QStringView suffix);

    Ed3kSession& m_session;
    QString m_configDir;
    bool m_started = false;
    bool m_holdBack = false;
    bool m_searchEnabled = true;
    /// reads inline (files about to be deleted: usenet filesRepaired)
    bool m_readsNow = false;

private:
    struct SetTry;
    struct Kept;
    /// Per session: the download holding each source node, and those waiting for it.
    struct NodeTurns;
    struct ReadBatch {
        std::vector<FileRead> reads;
        std::function<void(ReadResults)> done;
    };
    struct ReadQueue {
        std::deque<ReadBatch> batches;
        bool busy = false;
    };

    void onEventPolled(const ffi::Event& event, bool* handled);
    void onSessionStatus();
    bool answerBlock(std::uint64_t id, const ffi::Root& root, std::uint64_t offset, std::optional<std::uint32_t> len);
    /// The bytes of a WantBlock / VerifyBlock (nullopt: blockFail, and a gone file unshared).
    void finishBlock(std::uint64_t id, const ffi::Root& root, std::optional<QByteArray> data);
    /// The front batch of @p owner on a worker.
    void runReads(const QString& owner);
    void readsDone(const QString& owner, ReadResults results);
    void onBlock(const ffi::event::Block& b);
    void onKadFound(const ffi::event::KadFound& found);
    bool open(const ffi::Root& root, std::uint64_t size, const QString& owner, int member, bool inMemory);
    void saveDirty();
    /// Kad sources of a download again (when allowed by the gap).
    void searchSourcesAgain(const ffi::Root& root, bool force);
    void onResearchTimer();
    void onSetAliasFound(const ffi::event::AliasFound& found);
    /// The next candidate of @p owner's search; another search with roots after the last.
    bool tryNextSet(const QString& owner);
    [[nodiscard]] Kept readKept(const QString& owner) const;
    void writeKept(const QString& owner, const Kept& kept);
    void shareKeptMember(const QString& owner, int member, const ffi::Root& root, std::uint64_t size, const QString& name,
                         int tries);
    void shareKeptHashed(const QString& owner, int member, const ffi::Root& root, std::uint64_t size, const QString& name,
                         int tries, const QString& path, const std::optional<FileHash>& hashed);
    /// A kept member's first hash is done; the last one publishes the kept records.
    void keptMemberDone(const QString& owner);
    void publishKeptRecords(const QString& owner);
    /// unpublishAlias of every kept owner's records (hold back switched on).
    void withdrawKeptRecords();

    // -- turns at a source node (Protocol.md §12.5: one file stream per uploader) --------

    /// The registries by session; turnsOf: the one of @p session (every join of it shares one).
    [[nodiscard]] static std::map<const Ed3kSession*, NodeTurns>& turnsMap();
    [[nodiscard]] static NodeTurns& turnsOf(const Ed3kSession& session);
    [[nodiscard]] NodeTurns& turns() const;
    /// The FileRequest of @p root to @p source; the node is held by it then. false: refused.
    bool openStream(const ffi::Root& root, const ffi::Source& source);
    /// A stream of @p root already holds the node of @p source: the newer record goes to
    /// packmule, which keeps the stream and takes its endpoints and relays (true).
    bool refreshStream(const ffi::Root& root, const ffi::Source& source);
    /// Another download holds the node of @p source: @p root waits for its turn (true).
    bool waitTurn(const ffi::Root& root, const ffi::Source& source);
    /// The node a stream of this join holds.
    [[nodiscard]] std::optional<ffi::NodeId> nodeHeldBy(std::uint64_t stream) const;
    /// The node is free: the next waiting download takes it (its FileRequest replaces an
    /// open stream at the core, §12.5: the slot or position stays). false: none took it.
    bool passTurn(const ffi::NodeId& node);
    /// @p root of this join gets its turn at the node of @p source. false: not opened.
    bool grantTurn(const ffi::Root& root, const ffi::Source& source);
    /// The stream of @p root gives its node to the next waiting download and waits again
    /// itself (round robin). false: none waits, nothing changed.
    bool switchTurn(const ffi::Root& root, std::uint64_t stream);
    /// The waiters of @p node lose it (unreachable): asked again after a source search.
    void dropWaiters(const ffi::NodeId& node);
    /// The wait of @p root at @p node ended without a turn.
    void turnLost(const ffi::Root& root, const ffi::NodeId& node);
    /// Every turn and wait of this join; @p pass: held nodes go to the next waiter.
    void dropTurns(bool pass);
    /// The holder of @p node switches to a waiter of higher priority, if one waits. true: switched.
    bool yieldToHigher(const ffi::NodeId& node);

    // -- priority shares (the session's downloads, every join) ------------------

    /// Streams a download may open (its weight: kMaxStreams at normal).
    [[nodiscard]] std::size_t streamCap(const Download& d) const;
    /// Ranges to keep outstanding on each of @p granted streams of @p d: the engine's hint
    /// for every granted stream of the session, shared by priorityWeight.
    [[nodiscard]] std::size_t rangesPerStream(const Download& d, std::size_t granted) const;

    QString m_tag;
    std::map<ffi::Root, Shared> m_shared;
    std::map<ffi::Root, std::unique_ptr<Download>> m_downloads;
    /// download streams → root; Kad source lookups → root
    std::map<std::uint64_t, ffi::Root> m_streams;
    std::map<std::uint64_t, ffi::Root> m_sourceLookups;
    /// set searches → (owner, roots found); per owner the set tried
    std::map<std::uint32_t, std::pair<QString, bool>> m_setSearches;
    std::map<QString, std::unique_ptr<SetTry>> m_setTries;
    QSet<QString> m_dirty;
    QTimer m_saveTimer;
    QTimer m_researchTimer;
    int m_researchGapMs = 60 * 1000;
    std::size_t m_sourceSearches = 0;
    /// kept members of an owner whose first hash after start is still running
    std::map<QString, int> m_keptPending;
    std::map<QString, ReadQueue> m_reads;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};

} // namespace eMule::ed3k
