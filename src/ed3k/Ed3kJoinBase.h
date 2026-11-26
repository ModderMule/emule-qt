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
#include "utils/Log.h"

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
#include <utility>
#include <vector>

class QLoggingCategory;

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
    /// hops.md §1: this network's files follow the hiding switch (Options › ed3k › Extended ›
    /// Kad publishing): downloads, shares, set and ALIAS records under the anonymous key while
    /// hiding runs. Off: node key only, a file of the anonymous key is left out (skipped).
    /// A change while started restarts the join; kept sets and ALIAS records are taken back
    /// first and go out again under their files' key. Default off.
    void setAnonKey(bool on);
    [[nodiscard]] bool anonKey() const { return m_anonKey; }
    /// Days a deleted file's records stay out (alias.md §3 "Deleted file"; yaml
    /// ed3k.deletedFileDays, default 365, 0: no age limit, < 0: none). Shorter ends the rows
    /// over at once. Their network's check ends them regardless.
    void setDeletedDays(int days);
    [[nodiscard]] int deletedDays() const { return m_deletedDays; }
    /// A deleted file's name is asked at least this often (alias.md §3: 7 days)
    static constexpr qint64 kDeletedCheckMs = qint64(7) * 24 * 3600 * 1000;
    /// Deleted files kept (tests, Statistics).
    [[nodiscard]] std::size_t deletedCount() const { return deletedRows().size(); }
    /// Kad searches (sources, claims) on/off; tests switch them off.
    void setSearchEnabled(bool on) { m_searchEnabled = on; }
    /// Sources of a download are searched again every @p everyMs while no stream holds a
    /// slot, and when its last stream closed, at most once per @p minGapMs. Tests shorten it.
    void setSourceResearch(int everyMs, int minGapMs);
    /// Kad source searches asked for so far (tests).
    [[nodiscard]] std::size_t sourceSearches() const { return m_sourceSearches; }
    /// File reads queued or running on a worker (tests).
    [[nodiscard]] std::size_t readsInFlight() const;
    /// Reads of @p owner queued or running.
    [[nodiscard]] bool readsPending(const QString& owner) const;
signals:
    /// The kept set and ALIAS records of @p owner were published again after a start
    /// (@p aliases: records taken; 0 while holding back).
    void keptRecordsPublished(const QString& owner, int aliases);

public:
    /// Whether this node serves @p root (a member or a blob) now.
    [[nodiscard]] bool sharesRoot(const ffi::Root& root) const { return m_shared.contains(root); }
    /// A set search of @p owner is running or has candidates left.
    [[nodiscard]] bool setSearching(const QString& owner) const;
    /// A priority switch at a node may happen now: with a slot always; queued, only 30 min after
    /// the last queued switch there (Protocol.md §12.5: a 2nd within the budget is BUSY).
    [[nodiscard]] static bool switchAllowed(bool queued, std::optional<std::chrono::steady_clock::time_point> last,
                                            std::chrono::steady_clock::time_point now);

    /// A source of a download as the Transfers list shows it: its stream at the uploader.
    struct SourceInfo {
        ffi::KeyKind key = ffi::KeyKind::Node;   ///< the key of ours it serves
        ffi::NodeId node;
        ffi::Root root;
        QString join;                            ///< the join's tag ("torrent", "usenet", "hoster", "dl", ...)
        QString fileName;                        ///< the download's (empty: unnamed)
        std::uint64_t size = 0;
        std::optional<ffi::Address> endpoint;    ///< none: relays only, or the anonymous key
        bool holding = false;     ///< holds an upload slot there
        /// asked for this file; its one stream there (§12.5) serves @c otherName now
        bool otherFile = false;
        QString otherName;
        /// the other downloads of ours waiting for this node (eD2K's A4AF files of the source)
        QStringList otherWants;
        /// FileStatus.have per 4 MiB piece (missing bits are held, empty = complete file)
        std::vector<std::uint8_t> have;
        bool haveKnown = false;   ///< a FileStatus.have came (was: empty read as not told)
        /// per 4 MiB piece: every block of it is ours (what the join still wants is not)
        std::vector<bool> ownPieces;
        /// block offsets asked on its stream, not here yet (in order)
        std::vector<std::uint64_t> asked;
    };
    /// The sources of @p root: stream holders, then nodes held by another download of ours.
    [[nodiscard]] std::vector<SourceInfo> sourcesOf(const ffi::Root& root) const;
    /// Nodes where @p root waits for another download's turn (eD2K's A4AF source count).
    [[nodiscard]] int waitingFor(const ffi::Root& root) const;
    /// sourcesOf @p root in every join of @p session.
    [[nodiscard]] static std::vector<SourceInfo> sourcesAt(const Ed3kSession& session, const ffi::Root& root);
    /// Download source @p node of @p key in any join of @p session; nullopt: no stream there.
    [[nodiscard]] static std::optional<SourceInfo> sourceAt(const Ed3kSession& session, ffi::KeyKind key,
                                                          const ffi::NodeId& node);
    /// The sources of @p session holding an upload slot for us (the Downloading list).
    [[nodiscard]] static std::vector<SourceInfo> downloadingAt(const Ed3kSession& session);
    /// Tag of the first torrent/usenet/hoster join of @p session sharing or downloading @p root; empty: none.
    [[nodiscard]] static QString networkJoinAt(const Ed3kSession& session, const ffi::Root& root);
    /// A job of a join: its tag and owner (torrent id, usenet item id, "h<id>" hoster item, md4 hex).
    struct JoinOwner {
        QString tag;
        QString owner;
        friend bool operator==(const JoinOwner&, const JoinOwner&) = default;
    };
    /// Jobs of the eD2K/torrent/usenet/hoster joins of @p session sharing or downloading @p root:
    /// the other names of a root known here (alias.md has no root → name lookup).
    [[nodiscard]] static std::vector<JoinOwner> aliasJobsAt(const Ed3kSession& session, const ffi::Root& root);
    /// Torrent/usenet/hoster jobs of @p session with a root of theirs downloaded or shared on
    /// ed3k: the Transfers Downloading list shows their own network's clients too.
    [[nodiscard]] static std::vector<JoinOwner> networkJobsAt(const Ed3kSession& session);
    /// The ed3k sources of the downloads of job @p owner of the join tagged @p tag (one per
    /// node and key, a slot holder first): its peers in the Torrents/Usenet/Hoster tab.
    [[nodiscard]] static std::vector<SourceInfo> jobSourcesAt(const Ed3kSession& session, const QString& tag,
                                                             const QString& owner);
    /// Whether the join tagged @p tag of @p session has job @p owner. nullopt: that join is
    /// not started (ed3k starting or off), nothing can be judged yet.
    [[nodiscard]] static std::optional<bool> joinedAt(const Ed3kSession& session, const QString& tag,
                                                      const QString& owner);
    [[nodiscard]] const QString& tag() const { return m_tag; }
    /// What a network join (eD2K, BitTorrent, usenet, hoster) does with a root (File Details).
    struct JoinState {
        enum class Kind {
            Joined,      ///< a job of it shares or downloads the root on ed3k
            Skipped,     ///< left out, see reason (a job of it holds the file or would)
            Available,   ///< running, no job of it holds the root
            Off,         ///< not started (its network or ed3k off)
        };
        QString tag;
        Kind kind = Kind::Off;
        QStringList owners;   ///< Joined, Skipped: its jobs
        /// Skipped: "anonKey" (hops.md §1: the root is under the anonymous key, the network's
        /// anonymous key switch is off)
        QString reason;
        /// Joined: true while held under the anonymous key (the network's switch on)
        bool anon = false;
    };
    /// One row per network join of @p session (eD2K, BitTorrent, usenet, hoster) for @p root.
    [[nodiscard]] static std::vector<JoinState> joinStatesAt(const Ed3kSession& session, const ffi::Root& root);
    /// hops.md §1: this join (not anonCapable) left @p root out, it is under the anonymous key.
    [[nodiscard]] bool skipped(const ffi::Root& root) const { return m_skipped.contains(root); }

    // -- #224 local copies -------------------------------------------------------

    /// A file on disk found under @p root (folder scan, Ed3kLocalScan): every join of
    /// @p session copies from it like from a shared file, the open downloads of it start now.
    /// Kept for the session; a read checks @p size and @p mtimeMs (changed: not read).
    static void addFoundCopy(const Ed3kSession& session, const ffi::Root& root, const QString& path,
                             std::uint64_t size, qint64 mtimeMs);
    /// Found copies of @p session (tests, Statistics).
    [[nodiscard]] static int foundCopies(const Ed3kSession& session);
    /// Root and size of every open file download of @p session (what a folder scan looks for).
    [[nodiscard]] static std::vector<std::pair<ffi::Root, std::uint64_t>> wantedFiles(const Ed3kSession& session);
    /// A whole file of @p root (@p size bytes) here: a shared one all servable, or a found
    /// copy; empty: none. No disk access (the read checks).
    [[nodiscard]] static QString localCopyPath(const Ed3kSession& session, const ffi::Root& root, std::uint64_t size);
    /// Hashes @p path (blocking: a worker); root and size, nullopt: unreadable or cancelled.
    [[nodiscard]] static std::optional<std::pair<ffi::Root, std::uint64_t>> hashFileRoot(const QString& path,
                                                                                      const std::atomic<bool>& cancel);

protected:
    static constexpr std::uint64_t kBlock = 256 * 1024;
    /// #224: blocks of a local copy per background read
    static constexpr std::size_t kCopyBatch = 16;
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
        /// the key it is downloaded under (hops.md §1; Ed3kHide::keyFor at open)
        ffi::KeyKind key = ffi::KeyKind::Node;
        /// anonymous key: downloadOpen done (after the first anonFileRequest held the root there)
        bool anonOpened = false;
        /// #224: blocks read from a local copy (copyLocal); network requests wait meanwhile
        bool copying = false;
        bool copied = false;   ///< the local copy was read through once
        int copiedBlocks = 0;
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
    /// Name of a file download, shown on its Kad source lookups; empty: none.
    [[nodiscard]] virtual QString downloadName(const ffi::Root& root, const Download& d) const
    {
        Q_UNUSED(root) Q_UNUSED(d) return {};
    }
    /// A verified block of a file download arrived.
    virtual void onFileBlock(const Download& d, const ffi::event::Block& b) = 0;
    /// A blob (fetchBlob) is complete and verified block by block. The download is gone.
    virtual void onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data) = 0;
    /// The block values of a download arrived (event::Hashes).
    virtual void onDownloadHashes(const Download& d, const ffi::Bytes& blob) { Q_UNUSED(d) Q_UNUSED(blob) }
    /// #224: a block of download @p d read from a local copy (copyLocal): taken like a network
    /// block (written, counted); true: taken. Default: not taken.
    virtual bool takeLocalBlock(const Download& d, std::uint64_t offset, std::span<const std::byte> data)
    {
        Q_UNUSED(d) Q_UNUSED(offset) Q_UNUSED(data) return false;
    }
    /// #224: the local copy of @p d was read through, @p blocks taken; the network asks for the
    /// rest after this (the download may be gone already: complete).
    virtual void onLocalCopyDone(const Download& d, int blocks) { Q_UNUSED(d) Q_UNUSED(blocks) }
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
    /// event::WantHashes of a shared root opened with hashesStored: true when taken (answer
    /// later with hashesProvide / hashesFail, by @p id); false: failed for it.
    [[nodiscard]] virtual bool answerHashes(std::uint64_t id, const ffi::Root& root) { Q_UNUSED(id) Q_UNUSED(root) return false; }
    /// event::HashesLost: the stored values of a shared root failed (hash it again).
    virtual void onHashesLost(const ffi::Root& root) { Q_UNUSED(root) }
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
    /// The download's own stream cap (eD2K's per-file source limit); 0: by priority (streamCap).
    [[nodiscard]] virtual int downloadSourceLimit(const Download& d) const { Q_UNUSED(d) return 0; }
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
    /// What was kept of @p owner is shared again; false holds it back (kept on disk).
    [[nodiscard]] virtual bool sharesKeptOwner(const QString& owner) const { Q_UNUSED(owner) return true; }
    /// Whether this join's downloads and member shares follow the hiding switch (Ed3kHide):
    /// plain downloads and the share list always. Joins of other networks as the user set it
    /// per network (setAnonKey; was: never, they stayed on the node key).
    // was: [[nodiscard]] virtual bool anonCapable() const { return false; }
    [[nodiscard]] virtual bool anonCapable() const { return m_anonKey; }
    /// Hiding started (true) or stopped (false; the anonymous node took its downloads and
    /// shares along, the base dropped them). Default: nothing.
    virtual void onHideChanged(bool hiding) { Q_UNUSED(hiding) }
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
    /// #224: what wantedBlocks() lists of the open download @p root read from a local copy
    /// first (hasLocalCopy), batch by batch through takeLocalBlock(); its block requests wait
    /// meanwhile, onLocalCopyDone() after. false: none here, not open, or read once already.
    bool copyLocal(const ffi::Root& root);
    [[nodiscard]] bool copyingLocal(const ffi::Root& root) const;
    /// downloadPriority() of @p root changed: its waits re-ordered, nodes taken from or given
    /// to other downloads by the new order, its depth asked again.
    void priorityChanged(const ffi::Root& root);
    /// downloadSourceLimit() of @p root changed: a higher one takes sources of the next search.
    void sourceLimitChanged(const ffi::Root& root);
    /// eD2K's "Swap All A4AF Sources to This File": @p root goes first at every node it waits
    /// for; a queued holder there switches now (30 min budget, §12.5), a slot holder keeps its
    /// turn. Returns the nodes it waits for.
    int swapTurnsTo(const ffi::Root& root);
    /// Sources (open streams) holding each 4 MiB piece of @p d, as their FileStatus told it
    /// (255 at most); empty: no stream told a bitmap.
    [[nodiscard]] static std::vector<std::uint8_t> pieceAvailability(const Download& d);
    /// Weight of a download (eD2K's order: very low 1 … very high 5).
    [[nodiscard]] int priorityWeight(const Download& d) const;

    // -- serving -----------------------------------------------------------

    /// Shares @p data from memory under its root; logs when it is not @p expect. Its root;
    /// nullopt: not shared. @p key: Anon opens and publishes it on the anonymous node
    /// (anonFileOpen, anonPublish; State while not hiding), recorded when anonCapable().
    // was: std::optional<ffi::Root> shareBlob(const QByteArray& data, const QString& name, std::optional<ffi::Root> expect);
    std::optional<ffi::Root> shareBlob(const QByteArray& data, const QString& name, std::optional<ffi::Root> expect,
                                       ffi::KeyKind key = ffi::KeyKind::Node);
    /// Opens a member file on the engine (complete or partial). false: refused (logged).
    bool shareMember(const ffi::SharedFile& file, const QString& owner, int member);
    void unshare(const ffi::Root& root);
    /// Forgets the share of @p root without closing it on the engine (another sharer took it).
    void forgetShare(const ffi::Root& root) { m_shared.erase(root); }
    /// uploadPriority() of @p root again (changed by the user).
    void updateUploadPriority(const ffi::Root& root);
    /// Every root of @p owner.
    void unshareOwner(const QString& owner);
    /// unshareOwner, its kept blobs (manifest, info dictionary) too, and its kept set and ALIAS
    /// records withdrawn: the owner leaves ed3k (torrent.md §6.3 turned off). Kept on disk.
    void withdrawOwner(const QString& owner);
    /// Publishes a shared file: its SOURCE record, and its keywords unless holding back
    /// (Publish::Both). A name without a keyword (Protocol.md §10.1) gets its source alone.
    /// The mode taken, or the engine's error.
    std::expected<ffi::Publish, ffi::Error> publishShared(const ffi::FileInfo& info);
    [[nodiscard]] bool isShared(const ffi::Root& root) const { return m_shared.contains(root); }
    /// #224: some join of the session serves a file under @p root here (Shared Files, a
    /// finished download, another torrent's member), not a member of @p owner of this join.
    [[nodiscard]] bool hasLocalCopy(const ffi::Root& root, const QString& owner) const;
    /// #224: where to read [@p offset, +@p len) of @p root from such a local copy; nullopt:
    /// none, or those bytes not servable there (unverified, changed).
    [[nodiscard]] std::optional<FileRead> localCopyRead(const ffi::Root& root, std::uint64_t offset, std::uint32_t len,
                                                        const QString& owner) const;
    /// Takes back what publishShared published (unpublish, or anonUnshare + anonUnpublish
    /// under the anonymous key).
    void unpublishShared(const ffi::Root& root, ffi::Publish mode);
    /// declined() changed (Ed3kSession::declinedChanged, declined-records.md §2): shared roots
    /// now listed are withdrawn (unpublishShared Both, their note); ones no longer listed are
    /// published again from the FileInfo publishShared kept. Their ALIAS and set records only
    /// at the next start (shareKept).
    virtual void onDeclinedChanged();
    /// publishShared keeps each FileInfo for onDeclinedChanged; off for a join with its own
    /// republish path (Ed3kShared: many files).
    void setKeepPublishInfo(bool on) { m_keepPublishInfo = on; }
    /// The key a member of this join is opened under: Ed3kHide::keyFor when anonCapable(),
    /// else the node key.
    [[nodiscard]] ffi::KeyKind keyFor(const ffi::Root& root) const;
    /// The key this join holds @p root under: the node key unless anonCapable(); then the
    /// recorded key, the engine's (fileKey) unless only joins of the node key hold it, else
    /// what keyFor would take.
    // was: the engine's key first: a torrent member opened under the node key first took
    // the shared file of the same root there for good (keys.json overwritten)
    [[nodiscard]] ffi::KeyKind keyOf(const ffi::Root& root) const;
    /// hops.md §1: @p root is under the anonymous key or an anonCapable() join takes it there
    /// (hiding). A join that is not anonCapable() leaves it out (skipped), never opens it.
    [[nodiscard]] bool anonHeld(const ffi::Root& root) const;
    /// hops.md §1: the set (MEMBER rows, published under the node key) of @p owner is left out
    /// while a member is skipped or anonHeld: it would tie the anonymous file to this node.
    /// anonCapable(): left out while its members are under both keys (setKeyOf).
    [[nodiscard]] bool withholdSet(const QString& owner, std::span<const ffi::Root> members) const;
    /// hops.md §1: the key the set records of @p owner go under (manifest, set, MEMBER rows):
    /// the one key all @p members are under (none yet: keyForUse). nullopt: withheld (both
    /// keys, a member left out, or the anonymous key while hiding is off).
    [[nodiscard]] std::optional<ffi::KeyKind> setKeyOf(const QString& owner, std::span<const ffi::Root> members) const;
    /// The key blobs of @p owner (manifest, info dictionary, article list) are shared under:
    /// setKeyOf of its members held and kept; withheld: the node key.
    [[nodiscard]] ffi::KeyKind blobKeyOf(const QString& owner) const;
    /// ALIAS records one by one, each under its file's key (hops.md §1: an anonymous file's
    /// only through a path): the node key; State (held under the anonymous key) on the
    /// anonymous node when anonCapable() and hiding. The count published; none: the last error.
    std::expected<std::uint32_t, ffi::Error> publishAliasRecords(std::span<const ffi::Bytes> records);
    /// The set of manifest @p root taken back under either key.
    void unpublishSet(const ffi::Root& root);
    /// An anonCapable() join takes @p root (before it opens it): default, it shares or
    /// downloads it. Ed3kShared: a listed file hashed to it.
    [[nodiscard]] virtual bool claimsRoot(const ffi::Root& root) const
    {
        return m_shared.contains(root) || m_downloads.contains(root);
    }
    /// Moves an open download to @p key (hops.md §1: the user was asked): closed under its key,
    /// opened again under the other. false: no such download, or the key is the same.
    bool moveDownloadKey(const ffi::Root& root, ffi::KeyKind key);

    // -- hashing -----------------------------------------------------------

    /// A worker's hash or copy: progress 0-1000, and a stop from this thread.
    struct FileStep {
        std::atomic<int> permille{0};
        std::atomic<bool> stop{false};
    };
    struct FileHash {
        ffi::Root root;
        std::uint64_t size = 0;
        ffi::Bytes hashes;
        QByteArray ed2k;   ///< eD2K MD4 (16 B) when asked for, in the same read; else empty
    };
    /// Hashes @p path on a worker; @p done on this thread (nullopt: unreadable), not at
    /// all once stopped or destroyed. @p withEd2k: its eD2K hash too. @p step: its progress.
    void hashInBackground(const QString& path, std::function<void(std::optional<FileHash>)> done, bool withEd2k = false,
                          std::shared_ptr<FileStep> step = {});
    /// Runs @p work on the hash pool (disk load lowered); the step it returns runs on this
    /// thread, not at all once stopped or destroyed.
    void runHashJob(std::function<std::function<void()>(const std::atomic<bool>& cancel)> work);
    /// The same on this thread (files about to be deleted).
    [[nodiscard]] static std::optional<FileHash> hashNow(const QString& path);
    /// Hash state of the whole 256 KiB blocks of @p path below @p upTo (Hasher::checkpoint),
    /// for a whole read going on after a restart (httpWholeResume).
    struct HashCheckpoint {
        QByteArray blob;
        std::uint64_t offset = 0;
    };
    /// On a worker; @p done on this thread (nullopt: unreadable or no whole block), not at
    /// all once stopped or destroyed.
    void checkpointInBackground(const QString& path, std::uint64_t upTo,
                                std::function<void(std::optional<HashCheckpoint>)> done);

    /// "Move to" (#187): the part file @p src to @p dst on a worker (rename, else a copy
    /// that fits; the source then goes). @p done on this thread, empty = moved; not at all
    /// once stopped or destroyed. The caller writes its .met beside the new place.
    void relocateInBackground(const QString& src, const QString& dst, std::uint64_t dataSize,
                              std::function<void(QString error)> done);
    /// Delivers @p src to @p dst across volumes on a worker (FileMoveThread::copyThenRename).
    /// @p done on this thread, true = there and @p src gone; not at all once stopped or destroyed.
    void copyInBackground(const QString& src, const QString& dst, std::shared_ptr<FileStep> step,
                          std::function<void(bool ok)> done);

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
    /// Members are hashed again first, unless the block hashes saved at the last hash still
    /// fit (size, mtime, root); one unreadable is tried again after kRehashMs.
    void shareKept();
    /// What was kept of @p owner shared and published again (sharesKeptOwner permitting).
    void shareKeptOwner(const QString& owner);
    /// Forgets what is kept of @p owner (removed).
    void dropKept(const QString& owner);
    [[nodiscard]] QString keptDir(const QString& owner) const;
    /// Root of the set kept for @p owner at its completion; none: not complete here.
    [[nodiscard]] std::optional<ffi::Root> keptSet(const QString& owner) const;
    /// Where the kept members of @p owner are on disk (member → path), for a member file that
    /// outlives its job (a cleared NZB: files left in incoming). Kept with keepMember.
    void keepMemberPaths(const QString& owner, const QHash<int, QString>& paths);
    [[nodiscard]] QHash<int, QString> keptMemberPaths(const QString& owner) const;

    // -- deleted files (alias.md §3 "Deleted file"; Config/ed3k/<tag>/deleted.tsv) -----

    /// A verified file deleted after its own use (volumes unpacked): its ALIAS records stay
    /// out without the file until its name is found gone or deletedDays() after deletion.
    struct DeletedFile {
        ffi::Root root;
        QString owner;
        int member = -1;
        std::uint64_t size = 0;
        QString name;
        qint64 deletedAt = 0;   ///< ms since the epoch
        qint64 checkedAt = 0;   ///< ms: its name last found there; 0: not yet
        QStringList check;      ///< what its network asks (usenet: message ids; hoster: the link)
    };
    /// Keeps @p f (saved, told to the engine). false: off (0 days), over, or refused.
    bool keepDeleted(const DeletedFile& f);
    /// Its name gone, deleted by the user, or over: the engine's records of it end, the row
    /// goes; the owner's kept records are withdrawn once nothing keeps it.
    void endDeleted(const ffi::Root& root, const QString& why);
    [[nodiscard]] bool isDeleted(const ffi::Root& root) const;
    [[nodiscard]] bool hasDeleted(const QString& owner) const;
    [[nodiscard]] std::vector<DeletedFile> deletedOf(const QString& owner) const;
    [[nodiscard]] const std::vector<DeletedFile>& deletedFiles() const { return deletedRows(); }
    /// Its name found there now (the network's check).
    void deletedChecked(const ffi::Root& root, qint64 at);
    /// The saved rows told to the engine, the over ones ended; the hourly check started. At
    /// start, before shareKept publishes the kept records.
    void loadDeleted();
    /// The kept ALIAS records of @p owner published again (a file of it just deleted).
    void republishKeptAliases(const QString& owner);
    /// The network's checks of deleted files due (hourly while started); default none.
    virtual void checkDeleted(qint64 now) { Q_UNUSED(now) }
    /// A row ended: the network forgets what it holds for it; default nothing.
    virtual void onDeletedEnded(const DeletedFile& f) { Q_UNUSED(f) }

    // -- state -------------------------------------------------------------

    void scheduleSave(const QString& owner);
    /// The same, saved after @p delayMs at the latest (a hash run: fewer index rewrites).
    void scheduleSave(const QString& owner, int delayMs);
    void dropDirty(const QString& owner) { m_dirty.remove(owner); }
    /// Marks started, a fresh cancel flag.
    void beginStart();
    /// Closes every download, stream and share; cancels workers; saves nothing.
    void closeAll();
    void pumpSession();
    [[nodiscard]] QString joinDir(const QString& sub) const;
    void logDebug(const QString& msg) const;
    /// Lazy: @p make runs only while the category's debug lines are on
    template <LogMessageMaker F> void logDebug(F&& make) const
    {
        if (logCategory()().isDebugEnabled())
            logDebug(QString(make()));
    }
    void logWarning(const QString& msg) const;
    void logInfo(const QString& msg) const;
    /// Category of the log*() lines: emule.ed3k unless a join has its own tab (hoster)
    using LogCategory = const QLoggingCategory& (*)();
    [[nodiscard]] virtual LogCategory logCategory() const;

    static QString hexOf(std::span<const std::uint8_t> bytes);
    /// 64 hex digits; nullopt: not a root
    static std::optional<ffi::Root> rootFromHex(const QString& hex);
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
    bool m_anonKey = false;   ///< setAnonKey
    bool m_searchEnabled = true;
    /// reads inline (files about to be deleted: usenet filesRepaired)
    bool m_readsNow = false;

private:
    /// "ed3k <tag>: ", or "<tag>: " in a join's own category
    [[nodiscard]] QString logPrefix() const;
    /// Options label of this network's anonymous-key switch ("Torrents"); empty: none.
    [[nodiscard]] QString anonSwitchName() const;
    struct SetTry;
    struct Kept;
    /// Per session: the download holding each source node, and those waiting for it.
    struct NodeTurns;
    /// A source node as one key of this machine sees it: the node and the anonymous node are
    /// two downloaders to an uploader (§12.5 per key).
    using TurnNode = std::pair<ffi::KeyKind, ffi::NodeId>;
    struct ReadBatch {
        std::vector<FileRead> reads;
        std::function<void(ReadResults)> done;
    };
    struct ReadQueue {
        std::deque<ReadBatch> batches;
        bool busy = false;
    };

    // was: void onEventPolled(const ffi::Event& event, bool* handled);
    void onEventPolled(const ffi::Event& event, ffi::KeyKind key, bool* handled);
    void onSessionStatus();
    /// Ed3kHide started / stopped: anonymous streams and lookups dropped or searched again.
    void hideChanged(bool hiding);
    /// m_sourceLookups key: a lookup id of the anonymous node gets bit 63 (ids of both nodes
    /// may be the same).
    [[nodiscard]] static std::uint64_t lookupKey(std::uint64_t lookup, ffi::KeyKind key);
    bool answerBlock(std::uint64_t id, const ffi::Root& root, std::uint64_t offset, std::optional<std::uint32_t> len);
    /// The bytes of a WantBlock / VerifyBlock (nullopt: blockFail, and a gone file unshared).
    void finishBlock(std::uint64_t id, const ffi::Root& root, std::optional<QByteArray> data);
    /// The front batch of @p owner on a worker.
    void runReads(const QString& owner);
    void readsDone(const QString& owner, ReadResults results);
    void onBlock(const ffi::event::Block& b);
    // was: void onKadFound(const ffi::event::KadFound& found);
    void onKadFound(const ffi::event::KadFound& found, ffi::KeyKind key);
    bool open(const ffi::Root& root, std::uint64_t size, const QString& owner, int member, bool inMemory);
    /// #224: the next batch of copyLocal from block @p from on; endLocalCopy when none is left.
    void copyLocalBatch(const ffi::Root& root, std::uint64_t from);
    void endLocalCopy(const ffi::Root& root);
    void saveDirty();
    /// Kad sources of a download again (when allowed by the gap).
    void searchSourcesAgain(const ffi::Root& root, bool force);
    void onResearchTimer();
    void onSetAliasFound(const ffi::event::AliasFound& found);
    /// The next candidate of @p owner's search; another search with roots after the last.
    bool tryNextSet(const QString& owner);
    [[nodiscard]] Kept readKept(const QString& owner) const;
    /// readKept of the kept dir @p dir; any thread
    [[nodiscard]] static Kept readKeptIn(const QString& dir);
    /// shareKeptOwner after the read pool read @p kept and its blobs (@p blobs, one per kept.blobs)
    void shareKeptRead(const QString& owner, const Kept& kept, const std::vector<QByteArray>& blobs);
    /// publishKeptRecords after the read pool read the set manifest and the ALIAS records
    void publishKeptRead(const QString& owner, const Kept& kept, const QByteArray& manifest,
                         const std::vector<ffi::Bytes>& records);
    void writeKept(const QString& owner, const Kept& kept);
    void shareKeptMember(const QString& owner, int member, const ffi::Root& root, std::uint64_t size, const QString& name,
                         int tries);
    /// shareKeptMember after the read pool's look: @p hashes the saved block hashes that
    /// still fit (size, mtime, root); nullopt: hashed again.
    void shareKeptChecked(const QString& owner, int member, const ffi::Root& root, std::uint64_t size, const QString& name,
                          int tries, const QString& path, const QString& cache, qint64 mtimeMs,
                          std::optional<ffi::Bytes> hashes);
    void shareKeptHashed(const QString& owner, int member, const ffi::Root& root, std::uint64_t size, const QString& name,
                         int tries, const QString& path, const std::optional<FileHash>& hashed);
    /// A kept member's first hash is done; the last one publishes the kept records.
    void keptMemberDone(const QString& owner);
    void publishKeptRecords(const QString& owner);
    [[nodiscard]] std::vector<DeletedFile>& deletedRows() const;
    void saveDeleted() const;
    [[nodiscard]] QString deletedPath() const;
    /// Rows over deletedDays() ended; the network's checks asked.
    void onDeletedTimer();
    /// @p f is past the age limit at @p now (never with 0 days: no limit).
    [[nodiscard]] bool deletedOver(const DeletedFile& f, qint64 now) const;
    /// unpublishAlias of every kept owner's records (hold back switched on).
    void withdrawKeptRecords();
    /// unpublishSet of every kept owner's set (setAnonKey: published again under the new key).
    void withdrawKeptSets();

    // -- turns at a source node (Protocol.md §12.5: one file stream per uploader) --------

    // -- hops.md §1: joins of the node key and the anonymous key ---------------

    /// A root this join (not anonCapable) left out for the anonymous key: its job.
    struct Skipped {
        QString owner;
        int member = -1;
    };
    /// Leaves @p root out (anonHeld): logged once per root, kept for File Details and a rejoin.
    void skipRoot(const ffi::Root& root, Skipped s);
    /// An anonCapable() join takes @p root under the anonymous key: joins of the node key
    /// holding it give it up (yieldToAnon).
    void takeFromNodeJoins(const ffi::Root& root);
    /// This join (not anonCapable) gives @p root up: records taken back, closed, skipped.
    void yieldToAnon(const ffi::Root& root);
    /// @p root moved to the node key (Ed3kHide::keyMoved): a skipped member shared again.
    void rejoinSkipped(const ffi::Root& root);
    /// Only joins of the node key hold @p root (its engine key says nothing of the user's choice).
    [[nodiscard]] bool heldByNodeJoinsOnly(const ffi::Root& root) const;
    /// Drops the skipped roots of @p owner (job gone, withdrawn).
    void dropSkipped(const QString& owner);
    /// A member of @p owner yielded to the anonymous key: its kept set taken back (withholdSet).
    void withdrawSet(const QString& owner);

    /// The registries by session; turnsOf: the one of @p session (every join of it shares one).
    [[nodiscard]] static std::map<const Ed3kSession*, NodeTurns>& turnsMap();
    [[nodiscard]] static NodeTurns& turnsOf(const Ed3kSession& session);
    [[nodiscard]] NodeTurns& turns() const;
    /// The FileRequest of @p root to @p source; the node is held by it then. false: refused.
    bool openStream(const ffi::Root& root, const ffi::Source& source);
    /// SourceInfo of @p stream of @p root at @p node; nullopt: its download is gone.
    [[nodiscard]] std::optional<SourceInfo> holderInfo(const TurnNode& node, const ffi::Root& root, std::uint64_t stream,
                                                      const ffi::Source& source) const;
    /// A stream of @p root already holds the node of @p source: the newer record goes to
    /// packmule, which keeps the stream and takes its endpoints and relays (true).
    bool refreshStream(const ffi::Root& root, const ffi::Source& source);
    /// Another download holds the node of @p source: @p root waits for its turn (true).
    bool waitTurn(const ffi::Root& root, const ffi::Source& source);
    /// The node a stream of this join holds.
    [[nodiscard]] std::optional<TurnNode> nodeHeldBy(std::uint64_t stream) const;
    /// @p node under the key the download of @p root is under.
    [[nodiscard]] TurnNode turnNode(const ffi::Root& root, const ffi::NodeId& node) const;
    /// The node is free: the next waiting download takes it (its FileRequest replaces an
    /// open stream at the core, §12.5: the slot or position stays). false: none took it.
    bool passTurn(const TurnNode& node);
    /// @p root of this join gets its turn at the node of @p source. false: not opened.
    bool grantTurn(const ffi::Root& root, const ffi::Source& source);
    /// The stream of @p root gives its node to the next waiting download and waits again
    /// itself (round robin). false: none waits, nothing changed.
    bool switchTurn(const ffi::Root& root, std::uint64_t stream);
    /// The waiters of @p node lose it (unreachable): asked again after a source search.
    void dropWaiters(const TurnNode& node);
    /// The wait of @p root at @p node ended without a turn.
    void turnLost(const ffi::Root& root, const ffi::NodeId& node);
    /// Every turn and wait of this join; @p pass: held nodes go to the next waiter.
    void dropTurns(bool pass);
    /// The holder of @p node switches to a waiter of higher priority, if one waits. true: switched.
    bool yieldToHigher(const TurnNode& node);

    // -- priority shares (the session's downloads, every join) ------------------

    /// Streams a download may open (its weight: kMaxStreams at normal).
    [[nodiscard]] std::size_t streamCap(const Download& d) const;
    /// Ranges to keep outstanding on each of @p granted streams of @p d: the engine's hint
    /// for every granted stream of the session, shared by priorityWeight.
    [[nodiscard]] std::size_t rangesPerStream(const Download& d, std::size_t granted) const;

    QString m_tag;
    std::map<ffi::Root, Shared> m_shared;
    /// roots left out for the anonymous key (hops.md §1), not anonCapable() only
    std::map<ffi::Root, Skipped> m_skipped;
    std::map<ffi::Root, std::unique_ptr<Download>> m_downloads;
    /// download streams → root; Kad source lookups → root
    std::map<std::uint64_t, ffi::Root> m_streams;
    std::map<std::uint64_t, ffi::Root> m_sourceLookups;   ///< by lookupKey()
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
    /// deleted.tsv, read on first use
    mutable std::optional<std::vector<DeletedFile>> m_deleted;
    QTimer m_deletedTimer;
    int m_deletedDays = 365;
    /// publishShared's FileInfo per shared root (onDeclinedChanged), and the roots withdrawn
    /// or refused as declined
    std::map<ffi::Root, ffi::FileInfo> m_publishInfo;
    std::set<ffi::Root> m_declinedOut;
    bool m_keepPublishInfo = true;
};

} // namespace eMule::ed3k
