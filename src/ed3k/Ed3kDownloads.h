#pragma once

/// @file Ed3kDownloads.h
/// @brief Plain ed3k file downloads (a search row, an `ed3k:file` link without `bt=`), and
/// the files of callers (set members: Ed3kSets) through the same list.
///
/// Public API (for the daemon, the IPC bridge and other ed3k classes):
/// - add(Ed3kDownloadSpec) / addLink(link): starts a download of a root; owner tag "dl"
///   (kOwner) unless the spec names another (e.g. "set:<id>"); targetPath empty: the
///   incoming dir + name. Refused: root known already, size 0, no name. A root or eD2K hash
///   of decline.txt (Ed3kDecline.h) is added paused and declinedAdded emitted.
/// - pause / resume / cancel (the part file deleted; a complete row only leaves the list)
///   / setPriority (eMule's kPr*: 0 low, 1 normal, 2 high, 3 very high, 4 very low) / clearCompleted.
/// - list() / info(): snapshots (Ed3kDownloadInfo); signals added, changed (coalesced, at
///   most once a second), progress (bytes held), completed (final path), failed, removed.
/// - setDirs(temps, incoming): <first temp>/<root hex>.ed3k.part while it runs, its progress
///   beside it in <root hex>.ed3k.part.met; the complete file moves to the target (a free name
///   next to it when taken). Every temp dir is scanned for *.ed3k.part.met (setDirs, start):
///   the two files copied from another machine are a download here (targets under the incoming
///   dir are kept relative).
/// - setOwner: an unfinished row to another owner (Ed3kSets: members of a set not here → kOwner).
/// - setCompletedHook: called after the move (daemon: SharedFileList adds the file).
///   setSharedElsewhere / releaseShare: another sharer (Ed3kShared) holds the root now;
///   this class forgets its share without closing it on the engine. A plain download of a
///   root shared there is refused (add) and an unfinished one dropped (start, releaseShare).
///
/// Listed in the Transfers window only, also when the root is an alias of a torrent, usenet
/// or hoster job (alias.md): never shown in those networks' tabs. Its bytes come over ed3k
/// alone, and a 2nd row there would double every action. The jobs known here sharing the
/// root are named in its tooltip and Details (Ed3kJoinBase::aliasJobsAt) instead.
///
/// Mechanics are Ed3kJoinBase's (owner = the spec's owner, member = a stable id per
/// download): Kad sources, streams, block requests. wantedBlocks = the missing 256 KiB
/// blocks; each verified block is written into the part file and marked in a bitmap
/// (saved, debounced, into the .met; Config/ed3k/downloads.dat keeps the complete rows). Complete: hashed on a worker (root and
/// size must agree; else the bitmap is dropped and it downloads again), moved, shared
/// (Publish::Both, keywords unless holding back) and kept (keepMember) for a restart.
/// Pieces (4 MiB) held are shared while it runs once the block values are in.
/// HTTP sources (`ws=` of a link, kept in the .met): Ed3kDownloadsWeb.cpp. One fetch per
/// download at a time, missing blocks in order; without block values packmule reads the
/// file whole from 0 (HttpUnverified, checked by HttpWhole). Never under the anonymous key.
///
/// was: ToDo: the download priority is kept and shown only; the engine has no priority between
/// downloads (it could rank block requests or streams per file). (It orders the turns at a
/// source shared by several downloads: Ed3kJoinBase::downloadPriority.)
/// (done: Ed3kJoinBase shares by priority weight, eD2K's order: the turn at a shared source,
/// a higher waiter taking it from a lower holder at once; the session's block-request depth;
/// the streams opened per download.)

#include "Ed3kJoinBase.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <deque>
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
    /// the key it goes under (a followed set's, hops.md §1); nullopt: Ed3kHide::keyFor
    std::optional<::ed3k::KeyKind> key;
    /// the eD2K hash (16 B) the link names, if any (the anonymous-key exposure check)
    QByteArray md4;
    /// `ws=` of the link: HTTP sources of the whole file (http-source.md §2)
    QStringList webUrls;
    /// `wh=` of the link: URLs of its hash body (http-source.md §3.1)
    QStringList webHashUrls;
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
    std::uint64_t transferred = 0; ///< bytes received, all runs (MFC Transferred)
    int sources = 0;               ///< streams open
    int transferring = 0;          ///< streams holding a slot
    int a4af = 0;                  ///< nodes held by another download of ours, this one waits (eD2K A4AF)
    qint64 addedOn = 0;            ///< s since the epoch
    qint64 completedOn = 0;
    QString error;                 ///< Failed: why (not localized: a log line)
    std::vector<bool> blocks;      ///< per 256 KiB block: held (verified)
    std::vector<bool> pending;     ///< per block: asked for, not here yet; empty: not running
    /// HTTP source read whole: bytes [0, unchecked) on disk, root checked at the end (not in done/blocks)
    std::uint64_t unchecked = 0;
    /// per 4 MiB piece: open sources holding it (FileStatus.have, 255 at most); empty: none told
    std::vector<std::uint8_t> availability;
    /// eMule's "Increase priority for preview parts": first and last part first
    bool previewPrio = false;
    /// eD2K's per-file source limit: streams it may open; 0: by priority
    int maxSources = 0;
    /// media file, unfinished, first eD2K part held (PartFile::isPreviewPossible)
    bool previewPossible = false;
    /// HTTP sources (ws=) left out: the file is under the anonymous key (hops.md §1); 0: none
    int webDropped = 0;
    /// Verifying: the final hash or copy, 0-1000; -1 none
    int completionPermille = -1;
    bool copying = false;          ///< Verifying: hashed, being copied to another volume
};

class Ed3kDownloads : public Ed3kJoinBase {
    Q_OBJECT

public:
    /// Owner of the downloads a user started (search rows, links).
    static inline const QString kOwner = QStringLiteral("dl");

    /// @p configDir: complete rows go to <configDir>/ed3k/downloads.dat (loaded here); the
    /// unfinished ones come from the temp dirs (setDirs).
    Ed3kDownloads(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kDownloads() override;

    /// Opens the downloads that are not paused or complete; shares the complete ones again.
    /// Called when the node runs (statusChanged); idempotent.
    void start() override;
    /// Saves and closes every download; the list stays (shown while the node is off).
    void stop() override;

    /// Part files go to the first of @p tempDirs (all are scanned for .met files), complete
    /// files without a target to @p incomingDir.
    void setDirs(const QStringList& tempDirs, const QString& incomingDir);
    void setDirs(const QString& tempDir, const QString& incomingDir) { setDirs(QStringList{tempDir}, incomingDir); }
    /// The incoming dir of a category (eMule's categories); empty: the incoming dir.
    void setCategoryDir(std::function<QString(int)> dirOf) { m_categoryDir = std::move(dirOf); }
    /// After a file was verified and moved (and shared here).
    void setCompletedHook(std::function<void(const Ed3kDownloadInfo&)> hook) { m_completedHook = std::move(hook); }
    /// Whether another sharer serves @p root already: a complete file is not shared again here.
    void setSharedElsewhere(std::function<bool(const ::ed3k::Root&)> pred) { m_sharedElsewhere = std::move(pred); }
    /// hops.md §1 SHOULD: a download added under the anonymous key; the hook tells the user
    /// when the file is on another network too (eD2K queue, hoster). @p md4 may be empty.
    using AnonAddedHook = std::function<void(const ::ed3k::Root& root, const QByteArray& md4, const QString& name)>;
    void setAnonAddedHook(AnonAddedHook hook) { m_anonAdded = std::move(hook); }

    // -- the list ------------------------------------------------------------

    /// Starts a download; the error is a user-visible text (localized).
    std::expected<void, QString> add(const Ed3kDownloadSpec& spec);
    /// An `ed3k:file` link (UTF-8 text, parseFileLink); its root.
    std::expected<::ed3k::Root, QString> addLink(const QString& link, int category = 0, bool paused = false);
    bool pause(const ::ed3k::Root& root);
    bool resume(const ::ed3k::Root& root);
    /// "Move to" (#187): the part file to @p dir (one of the temp dirs from then on), on a
    /// worker; a running or failed download goes on there. All data in: it completes into
    /// @p dir instead. Empty: started; else why not (English).
    [[nodiscard]] QString moveTo(const ::ed3k::Root& root, const QString& dir);
    /// Unfinished: stopped, its part file deleted. Complete: leaves the list and the own share
    /// (the file stays).
    bool cancel(const ::ed3k::Root& root);
    bool setPriority(const ::ed3k::Root& root, int priority);
    bool setCategory(const ::ed3k::Root& root, int category);
    /// Rename (Mass Rename): an unfinished download's file name to be, a complete one's file
    /// on disk. The error is a user-visible text.
    std::expected<void, QString> rename(const ::ed3k::Root& root, const QString& name);
    /// Add Sources: an http(s) URL of the whole file (as ws=); false: no such running download.
    bool addWebSource(const ::ed3k::Root& root, const QString& url);
    /// The file's own preview flag (eMule's per-file switch; the option covers every file).
    bool setPreviewPrio(const ::ed3k::Root& root, bool on);
    /// Source Limit... of an unfinished download (0 to 65535, 0: by priority).
    bool setSourceLimit(const ::ed3k::Root& root, int limit);
    /// Swap All A4AF Sources to This File (Ed3kJoinBase::swapTurnsTo); nullopt: none or complete.
    std::optional<int> swapA4af(const ::ed3k::Root& root);
    /// A player reads from @p offset (preview stream, "Enable seeking in downloads"): end
    /// (exclusive) of the blocks held from there; nullopt: no such download. Less than
    /// PartFile::kStreamLead ahead: the blocks from the first missing one go first, their
    /// hash values too (downloadFocus), for PartFile::kStreamFocusMs.
    std::optional<std::uint64_t> playFrom(const ::ed3k::Root& root, std::uint64_t offset);
    /// An unfinished download to @p owner (Ed3kSets: an imported member of an unknown set).
    bool setOwner(const ::ed3k::Root& root, const QString& owner);
    /// Complete rows leave the list; their files stay (served on by the share list, if it took them).
    void clearCompleted();

    [[nodiscard]] std::vector<Ed3kDownloadInfo> list() const;
    [[nodiscard]] std::optional<Ed3kDownloadInfo> info(const ::ed3k::Root& root) const;
    [[nodiscard]] bool contains(const ::ed3k::Root& root) const { return m_entries.contains(root); }

    /// The HTTP (ws=) source of a running download, for its source row.
    struct WebSourceInfo {
        QString url;
        bool fetching = false;           ///< a request runs
        std::uint64_t at = 0;            ///< offset of the running request
        std::vector<bool> have;          ///< per 256 KiB block, checked
        std::uint64_t unchecked = 0;     ///< whole read: bytes [0, unchecked) on disk
    };
    /// nullopt: no HTTP source attached (or not downloading).
    [[nodiscard]] std::optional<WebSourceInfo> webSourceOf(const ::ed3k::Root& root) const;

    /// Another sharer (Ed3kShared) took @p root: forget the own share, do not close it. An
    /// unfinished plain download of it is dropped (part file + .met in Temp; the shared file
    /// is not touched). true: dropped, its share closed: the sharer opens @p root again.
    bool releaseShare(const ::ed3k::Root& root);

    /// The key @p root is downloaded or shared under (hops.md §1, shown to the user).
    [[nodiscard]] ::ed3k::KeyKind keyOfRoot(const ::ed3k::Root& root) const { return keyOf(root); }
    /// Moves a running download to @p key (the user was asked). false: none running, same key.
    bool moveKey(const ::ed3k::Root& root, ::ed3k::KeyKind key) { return moveDownloadKey(root, key); }

    // -- tests -----------------------------------------------------------------

    /// A source by hand (tests switch Kad searches off).
    void addSourceTo(const ::ed3k::Root& root, const ::ed3k::Source& source) { addSource(root, source); }
    [[nodiscard]] QString statePath() const;
    /// Saves at once (tests, stop).
    void saveNow();

signals:
    void added(const ::ed3k::Root& root);
    /// declined-records.md §2: @p root (or its eD2K hash) is in decline.txt; added paused,
    /// the user is told (CoreNotifierBridge). Not for a download asked paused.
    void declinedAdded(const ::ed3k::Root& root, const QString& name);
    /// Something of the list changed (state, progress, rate); at most once a second.
    void changed();
    void progress(const ::ed3k::Root& root, std::uint64_t done);
    void completed(const ::ed3k::Root& root, const QString& path);
    void failed(const ::ed3k::Root& root, const QString& error);
    void removed(const ::ed3k::Root& root);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    [[nodiscard]] QString downloadName(const ffi::Root& root, const Download& d) const override;
    void onFileBlock(const Download& d, const ::ed3k::event::Block& b) override;
    bool takeLocalBlock(const Download& d, std::uint64_t offset, std::span<const std::byte> data) override;
    void onBlobFetched(const QString& owner, int member, const ::ed3k::Root& root, const QByteArray& data) override;
    void onDownloadHashes(const Download& d, const ::ed3k::Bytes& blob) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    [[nodiscard]] bool keepsOwner(const QString& owner) const override;
    [[nodiscard]] QString keptMemberPath(const QString& owner, int member) const override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;
    /// eMule's kPr* as the turn order at a source shared by several downloads.
    [[nodiscard]] int downloadPriority(const Download& d) const override;
    [[nodiscard]] int downloadSourceLimit(const Download& d) const override;
    /// Plain ed3k downloads follow the hiding switch (hops.md §1).
    [[nodiscard]] bool anonCapable() const override { return true; }
    /// HTTP source events (Ed3kDownloadsWeb.cpp)
    void onOtherEvent(const ::ed3k::Event& event) override;

private:
    /// HTTP source state of one download (this run only)
    struct Web {
        std::uint64_t source = 0;        ///< engine source id; 0: none
        int urlIndex = 0;                ///< webUrls entry in use
        std::optional<std::uint64_t> fetch;
        std::uint64_t fetchAt = 0;       ///< offset of @c fetch
        bool whole = false;              ///< read whole from 0, checked at the end
        std::uint64_t wholeNext = 0;     ///< next offset of the whole read
        int failures = 0;                ///< fetches failed in a row
        QDeadlineTimer retryAt;          ///< no fetch before
        bool loggedAnon = false;
        bool resuming = false;           ///< bytes of an earlier whole read hashed again, no fetch yet
        bool resumed = false;            ///< whole read went on from a checkpoint
    };
    struct Entry {
        Ed3kDownloadInfo info;
        int id = -1;                     ///< member index in the base
        std::vector<bool> have;          ///< per 256 KiB block
        ::ed3k::Bytes hashes;            ///< block values (event::Hashes), for sharing pieces
        bool sharedByList = false;       ///< complete, served by another sharer
        bool metDirty = false;           ///< the .met is behind
        std::uint64_t received = 0;      ///< bytes this run (rate)
        std::uint64_t lastReceived = 0;
        std::deque<std::uint64_t> rateSamples;   ///< bytes per tick, newest last (rate average)
        bool moving = false;             ///< moveTo() runs
        bool resumeAfterMove = false;
        std::optional<std::uint64_t> focus;   ///< playFrom(): first byte fetched first
        QDeadlineTimer focusUntil;       ///< focus ends (onTick)
        QStringList webUrls;             ///< ws= HTTP sources (kept in the .met)
        QStringList webHashUrls;         ///< wh= hash body URLs (kept in the .met)
        /// whole read: bytes [0, wholeAt) on disk, root not checked yet (kept in the .met)
        std::uint64_t wholeAt = 0;
        bool prefixChecking = false;     ///< webCheckPrefix() runs on a worker
        std::shared_ptr<FileStep> completion;   ///< final hash/copy running
        Web web;
    };

    Entry* entryOf(const ::ed3k::Root& root);
    /// An unfinished plain download of a root shared by another sharer (setSharedElsewhere).
    [[nodiscard]] bool isSharedDuplicate(const Entry& e) const;
    /// Drops @p root as such a duplicate: cancel() with a log line saying why.
    void dropSharedDuplicate(const ::ed3k::Root& root);
    const Entry* entryOfId(int id) const;
    Entry* entryOfId(int id);
    [[nodiscard]] std::uint64_t blockCount(std::uint64_t size) const;
    void openEntry(Entry& e);
    /// Every block held: hash on a worker, then finish().
    void verify(Entry& e);
    void verified(const ::ed3k::Root& root, const std::optional<FileHash>& hashed);
    /// Moves the part file to its target; the path taken, empty on failure.
    /// Free final path of @p e, its folder made.
    QString targetOf(const Entry& e);
    /// The file is at @p path: complete and shared.
    void delivered(Entry& e, const QString& path, std::span<const std::byte> hashes);
    void endCompletion(Entry& e);
    void shareComplete(Entry& e, std::span<const std::byte> hashes);
    /// The 4 MiB pieces held while it runs (with block values).
    void sharePieces(Entry& e);
    void fail(Entry& e, const QString& error);
    /// A verified block into the part file; false: not taken.
    bool storeBlock(Entry& e, std::uint64_t offset, std::span<const std::byte> data);

    // -- HTTP sources (Ed3kDownloadsWeb.cpp) ----------------------------------
    /// URLs new to @p list: kept; true when one was added.
    static bool mergeWebUrls(QStringList& list, const QStringList& urls);
    /// Running download with URLs: its source at the engine.
    void webAttach(Entry& e);
    /// http-source.md §3.2: HTTP sources of the anonymous key only through the anonymous
    /// proxy (Ed3kHideSettings anonHttp + reportProxy); false: not used.
    [[nodiscard]] bool anonWebAllowed() const;
    /// Stops the fetch and forgets the source.
    void webDetach(Entry& e);
    /// A fetch for every download whose source idles (tick, after a fetch).
    void webDrive();
    void webFetch(Entry& e);
    void webFetched(Entry& e, std::uint32_t code);
    void webUnverified(Entry& e, std::uint64_t offset, const ::ed3k::Bytes& data);
    void webWhole(Entry& e, bool ok);
    void webDropped(Entry& e, std::uint32_t code);
    /// A whole read of an earlier run goes on: its bytes on disk hashed again (Hasher::checkpoint).
    void webResume(Entry& e);
    void webResumed(const ffi::Root& root, std::uint64_t source, const std::optional<HashCheckpoint>& cp);
    /// Block values known (onDownloadHashes): the unchecked bytes [0, wholeAt) hashed again
    /// on a worker, matching blocks held; the rest is fetched block by block.
    void webCheckPrefix(Entry& e);
    void webPrefixChecked(const ffi::Root& root, std::uint64_t upTo, const std::optional<HashCheckpoint>& cp);
    Entry* entryOfWebSource(std::uint64_t source);
    void markChanged();
    /// The entry changed: its .met is written at the next save.
    void touch(Entry& e);
    void onTick();
    void load();
    /// Takes up the *.ed3k.part.met of the temp dirs not in the list yet.
    void scanTempDirs();
    [[nodiscard]] std::optional<Entry> readMet(const QString& metPath) const;
    void writeMet(Entry& e);
    /// moveTo() done: the entry's part file is at @p to now (its .met written there).
    void relocated(Entry& e, const QString& from, const QString& to, const QString& dir);
    [[nodiscard]] static QString metPathOf(const Entry& e) { return e.info.partPath + QStringLiteral(".met"); }
    [[nodiscard]] QString partPathOf(const ::ed3k::Root& root) const;
    [[nodiscard]] static QString freePath(const QString& path);

    std::map<::ed3k::Root, Entry> m_entries;
    int m_nextId = 0;
    QStringList m_tempDirs;
    QString m_incomingDir;
    std::function<void(const Ed3kDownloadInfo&)> m_completedHook;
    std::function<bool(const ::ed3k::Root&)> m_sharedElsewhere;
    AnonAddedHook m_anonAdded;
    std::function<QString(int)> m_categoryDir;
    QTimer m_tick;
    bool m_changed = false;
    bool m_anonWebAllowed = false;   ///< last webDrive: anonymous HTTP sources allowed
};

} // namespace eMule::ed3k
