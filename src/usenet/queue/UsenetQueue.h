#pragma once

/// @file UsenetQueue.h
/// @brief The Usenet download queue: scheduling, failover and completion.
///
/// Lives on the daemon thread and owns every policy decision. The workers own the
/// sockets; this owns *what to ask for and what to do when the answer is no*.
/// That split is the same one ArticleFetcher already documents ("It holds no
/// policy: which server to ask and what to do about a failure belong to the pool
/// and the queue respectively") — extended one level up.
///
/// The failover rule is the whole of Usenet fault tolerance, and it is one line
/// of judgement that must not be re-derived anywhere else:
///
///   - `escalatesToNextLevel(error)` — the server does not have the article (430)
///     — retry at `level + 1`, with that account added to `ignoreServers` so the
///     escalation never asks it twice.
///   - anything else is a *connection* fault. Retry the **same** level. The
///     worker has already backed the server off; a different account on the same
///     rung picks the article up.
///
/// Getting that backwards means either hammering a fill server every time the
/// main provider is briefly busy, or paying for a block account that never gets
/// used.
///
/// **Completion is a cycle, not a line.** Since phase 4 the plan deliberately
/// leaves out the PAR2 recovery volumes, so "every planned segment resolved"
/// means the *download* phase is done, not the item. Post-processing then
/// verifies, and a verify that comes up short sends the item back to
/// downloading for exactly the recovery volumes it turned out to need. Every
/// path out of that loop has to terminate: requestPar2Volumes() returning false
/// — nothing further to ask for — is what ends it, and kMaxPar2Rounds is the
/// backstop.

#include "nntp/NewsServer.h"
#include "post/UsenetDirectUnpack.h"
#include "post/UsenetPostProcessor.h"
#include "queue/UsenetHealth.h"
#include "queue/UsenetQueueItem.h"
#include "queue/UsenetUsage.h"
#include "queue/UsenetWorker.h"
#include "stream/UsenetStreamIndex.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>
#include <vector>

class QThread;
class QTimer;

namespace eMule::usenet {

/// What post-processing should do with a finished item. A struct rather than a
/// row of positional bools, which no call site could be read back from.
struct PostProcessingOptions {
    bool par2 = true;
    bool rename = true;
    bool unpack = true;
    bool cleanup = true;

    /// Unpack an archive set as its volumes land, instead of at the end. Only
    /// meaningful when `unpack` is on — it is the same extraction, moved.
    bool directUnpack = true;
};

class UsenetQueue : public QObject {
    Q_OBJECT

public:
    explicit UsenetQueue(QObject* parent = nullptr);
    ~UsenetQueue() override;

    /// Load persisted items and spin the worker threads up. Idempotent.
    void start();

    /// Stop dispatching, join every worker thread, flush state. Idempotent, and
    /// must complete before anything the queue references is destroyed.
    void stop();

    [[nodiscard]] bool isRunning() const { return m_running; }

    /// Re-slice the server list across the workers. Safe while running.
    void applyServers(const QList<NewsServer>& servers, int retryIntervalSec);

    // -- Queue operations ---------------------------------------------------

    /// Parse @p data and queue it. Returns the new item id, or an empty string
    /// with @p error set.
    ///
    /// @p source decides which existing items refuse a re-add. A release that is
    /// still downloading is refused either way; a *completed* one is refused
    /// only for an automatic add, because re-downloading something deliberately
    /// is a thing people do and a thing feeds should not. Same split, and the
    /// same reasoning, as Ed2kLinkImporter::Source.
    /// @p outcome, when given, says *why* — which is what lets an automatic
    /// caller stop retrying something it already has.
    QString addNzb(const QByteArray& data, const QString& name, QString& error,
                   UsenetAddSource source = UsenetAddSource::Manual,
                   UsenetAddOutcome* outcome = nullptr);

    /// Re-run the availability probe for @p id. A release queued a week ago is a
    /// different question from the one answered when it was added.
    ///
    /// Returns false when the item is unknown, is not in a state that can be
    /// probed, or when nothing could be asked — never an error the user has to
    /// act on, because a probe that cannot run simply produces no verdict.
    bool recheckItem(const QString& id);

    bool removeItem(const QString& id, bool deleteFiles);
    bool pauseItem(const QString& id);
    bool resumeItem(const QString& id);
    bool setItemPriority(const QString& id, int priority);

    [[nodiscard]] QList<const UsenetQueueItem*> items() const;
    [[nodiscard]] const UsenetQueueItem* findItem(const QString& id) const;

    // -- Streaming ----------------------------------------------------------

    /// One run of the logical file, and the file on disk that holds it.
    ///
    /// Paths are resolved on every call and must not be cached: sealFile()
    /// renames the scratch file the moment the last article lands, and
    /// publishStaged() moves it again.
    struct StreamPiece {
        QString path;
        qint64 virtualOffset = 0;   ///< where it starts in the logical file
        qint64 fileOffset = 0;      ///< where it starts inside `path`
        qint64 length = 0;
    };

    /// Everything a Range request needs to answer, for one logical file.
    ///
    /// A raw-posted release is one piece. A stored RAR set is one piece per
    /// volume, and the logical file is the `.mkv` inside — which is why this
    /// stopped being a path in phase 6b.
    struct StreamInfo {
        bool found = false;
        QString fileName;

        /// Unpacked size of the logical file. Zero until enough has been read to
        /// know it — the NZB cannot supply it, its `bytes` being the encoded size.
        qint64 totalSize = 0;

        /// End of the readable run *containing the requested offset*. For a
        /// request at 0 this is the old meaning unchanged.
        qint64 availableEnd = 0;

        bool complete = false;

        QList<StreamPiece> pieces;

        /// Set when this release can never be streamed — a compressed, solid or
        /// encrypted archive. The caller reports it instead of waiting out the
        /// poll, so the user learns why before a player opens.
        QString notSeekableReason;
    };

    /// Whether a Preview action should be offered for @p fileIndex, and if not,
    /// why — a compressed or encrypted archive gets a sentence the GUI can show
    /// instead of a silently greyed-out menu entry, which is §7.3's requirement
    /// that seekability be surfaced rather than fail at play time.
    ///
    /// Container-aware, which is why it lives here and not on UsenetQueueItem:
    /// deciding it needs the streaming index, and the item has no access to one.
    struct PreviewInfo {
        bool previewable = false;
        QString note;
    };
    [[nodiscard]] PreviewInfo previewability(const QString& itemId, int fileIndex);

    /// Look up a logical file for streaming, **and ask for the bytes**.
    ///
    /// The two are one call on purpose: asking about a byte is asking for it.
    /// The item is boosted above the priority field for the next
    /// kStreamingBoostMs, and the articles covering
    /// `[wantOffset, wantOffset + wantLength)` are moved to the front of the
    /// plan — which is what makes a seek land without fetching everything in
    /// between. The boost lapses on its own so nothing has to clear it.
    ///
    /// @p fileIndex may name any volume of an archive set; they all resolve to
    /// the same *set*. @p entryOrdinal then picks a file within it, `-1`
    /// meaning the first playable one — which is what makes a release that
    /// packs an `.nfo` ahead of the feature play the feature.
    ///
    /// Daemon-thread only, like everything else on this class.
    StreamInfo requestStream(const QString& itemId, int fileIndex,
                             qint64 wantOffset = 0, qint64 wantLength = 0,
                             int entryOrdinal = -1);

    /// One row of an archive set's contents, for the GUI's chooser.
    struct ArchiveEntryInfo {
        int entry = -1;         ///< the ordinal `requestStream`'s entryOrdinal takes
        QString name;
        qint64 size = 0;
        bool playable = false;  ///< a Preview of this entry will work
        QString note;           ///< why not, when it will not
    };

    struct ArchiveListing {
        enum class Status {
            Unknown,       ///< nothing on disk and nothing fetchable — paused, or gone
            Scanning,      ///< bytes were promoted; ask again, and `entries` grows
            Complete,      ///< every header parsed; `entries` is final
            NotSeekable,   ///< solid or header-encrypted: there is nothing to list
            NotAnArchive,  ///< a raw post or a `.001` split — one file, never a choice
        };
        Status status = Status::Unknown;
        QString note;
        QList<ArchiveEntryInfo> entries;
    };

    /// Enumerate the files inside @p fileIndex's archive set.
    ///
    /// Unlike previewability(), this **asks for the bytes it is missing** — the
    /// header of the second file inside a set sits past the first file's
    /// payload, so listing a set costs articles. It deliberately does *not*
    /// take requestStream()'s cross-item priority boost: opening a list is not
    /// watching a video, and an item should not outrank every other download
    /// because someone opened a dialog. Reordering within the item's own plan
    /// is all a scan needs.
    ///
    /// Daemon-thread only.
    [[nodiscard]] ArchiveListing listArchiveEntries(const QString& itemId, int fileIndex);

    /// Bytes per second this engine may use in total. 0 is unlimited, as
    /// everywhere else in eMuleQt. Divided across workers, then across their
    /// sockets.
    void setRateLimit(qint64 bytesPerSecond);

    /// Post-processing settings, refreshed from preferences at each job. Kept
    /// here rather than read inside the pipeline so a job carries a consistent
    /// snapshot across the thread boundary.
    void setPostProcessingOptions(const PostProcessingOptions& options);

    /// Decoded bytes per second, measured over the last tick. Feeds the
    /// ED2K/Usenet budget split.
    [[nodiscard]] qint64 currentRate() const { return m_currentRate; }

    /// Whether anything is actually downloading, i.e. whether Usenet needs a
    /// share of the budget at all. An item parked on a spent allowance does not
    /// count: it is not downloading and reserving a share of the line for it
    /// would take bandwidth away from ED2K for as long as the quota lasts.
    [[nodiscard]] bool hasActiveDownloads() const;

    /// Per-account byte meters. Read for the IPC report; written only here.
    [[nodiscard]] const UsenetUsageTracker& usage() const { return m_usage; }

    /// Whether @p s has spent its allowance. Grouped accounts share one meter:
    /// `group` already means "one provider reached two ways", so the plan behind
    /// those host names is one plan and two meters would simply be a wrong
    /// number.
    [[nodiscard]] bool isOverQuota(const NewsServer& s) const;

    /// Correct one account's meter — a plan change, a block top-up, or our
    /// count disagreeing with the provider's. -1 leaves a figure alone. Unparks
    /// anything the old figure had stalled.
    bool setAccountUsage(const QString& accountId, qint64 periodBytes, qint64 totalBytes);

signals:
    /// Progress or state moved. Coalesced by the daemon before it reaches a GUI.
    void itemChanged(const QString& id);

    void itemAdded(const QString& id);
    void itemRemoved(const QString& id);

    /// A terminal outcome. Deliberately separate from itemChanged so the daemon
    /// can broadcast it *uncoalesced*: this is a transition, not a latest value,
    /// and collapsing it inside a coalescing window loses it entirely.
    void itemFinished(const QString& id, bool success, const QString& message);

private:
    struct SegmentKey {
        int fileIndex = -1;
        int segmentIndex = -1;
        [[nodiscard]] quint64 packed() const
        {
            return (quint64(quint32(fileIndex)) << 32) | quint32(segmentIndex);
        }
    };

    /// What has already been tried for a segment that failed at least once.
    /// Segments with no entry have tried nothing, which is the overwhelmingly
    /// common case — so this stays small even for a huge release.
    ///
    /// **The failover rung is deliberately not stored.** It is derived from
    /// `tried` by nextServableLevel() on every dispatch, which is what makes
    /// "escalate only when every account on this level said 430" true by
    /// construction: while an untried sibling remains, the same rung comes back.
    /// A stored rung has to be incremented by somebody, and the somebody
    /// incremented it on the *first* 430.
    struct SegmentAttempt {
        QStringList tried;
        int transportRetries = 0;

        /// Whether any account blamed for a transport fault here was one the
        /// download is allowed to depend on. Optional accounts are not: a block
        /// or fill account being down must not fail the item. Per segment rather
        /// than per attempt, because successive retries reach different accounts
        /// and one required failure anywhere has to keep the failing outcome.
        bool requiredFailure = false;
    };

    /// One archive set being extracted while the item still downloads.
    struct DirectUnpackRun {
        UsenetDirectUnpack* worker = nullptr;   ///< lives on `thread`, deleted with it
        QThread* thread = nullptr;
        bool running = false;
        UsenetDirectUnpackResult result;

        /// How far the extraction has got, pushed over from the worker thread.
        /// This is the second byte source a preview can be served from, and the
        /// only one for a set no map can describe.
        UsenetDirectUnpackProgress progress;

        /// Post-processing owns the output from here: a repair is about to
        /// discard it, or staging is about to rename it into the incoming
        /// directory. Serving from it past this point hands a player a file that
        /// is being moved out from under it.
        bool closing = false;

        /// Last volume ordinal a preview asked the scheduler for. Promoting a
        /// whole volume walks the plan once per segment, so it is done once per
        /// volume rather than once per 250 ms poll.
        int promotedVolume = -1;
    };

    struct ItemRuntime {
        std::unique_ptr<UsenetQueueItem> item;
        QHash<quint64, SegmentAttempt> attempts;
        QSet<quint64> inFlight;
        /// Order segments are handed out in: the index PAR2 last, because it is
        /// only worth having if something else came up short. Recovery volumes
        /// are not in here at all until requestPar2Volumes() adds them.
        QList<quint64> plan;
        int planCursor = 0;
        bool dirty = false;

        /// Guards the download → verify → download cycle. Incremented per round
        /// trip, never reset, and checked against kMaxPar2Rounds.
        int par2Rounds = 0;

        /// A job is with the post-processing thread. Nothing may dispatch for
        /// this item, and no second job may start, until it comes back.
        bool postRunning = false;

        /// Every account that could still serve this item's next article has
        /// spent its allowance. The item leaves the dispatch order until a
        /// rollover, a usage edit or a config change can change the answer —
        /// without that the plan cursor reaches the end with work still pending
        /// and onTick() rebuilds the whole plan four times a second until the
        /// billing day. Runtime only.
        bool quotaParked = false;

        // -- Availability probe -------------------------------------------
        //
        // A whole second key space, deliberately. Sharing `plan` / `planCursor`
        // / `inFlight` / `attempts` looks obvious and is destructive: they are
        // keyed by SegmentKey::packed(), so a probe and a fetch for the same
        // article become mutually exclusive; dispatch() skips any segment whose
        // done bit is set, so a probe could never look at anything already
        // downloaded; and both terminals are wrong for a probe.
        // markSegmentDone() would seal empty files and start post-processing on
        // a STAT, and markSegmentMissing() sets the done bit *and* increments
        // missingSegments — simultaneously stopping the article from ever being
        // fetched and inflating the PAR2 damage estimate. The ladder functions
        // are stateless and are reused; this state is not.
        //
        // All of it is runtime only. No probe survives a restart, and re-probing
        // on every start would spend round trips to re-learn advice.
        QList<quint64> checkPlan;
        int checkCursor = 0;
        QSet<quint64> checkInFlight;
        QHash<quint64, SegmentAttempt> checkAttempts;

        /// What to put the item back to when the probe finishes cleanly.
        ///
        /// Not always Queued: a recheck of a *paused* item must leave it paused.
        /// Un-pausing something the user paused, as a side effect of asking a
        /// question about it, would be the probe deciding something.
        UsenetItemStatus checkResumeStatus = UsenetItemStatus::Queued;

        /// Bytes each answered probe stood for, and how many of those no account
        /// would serve. In Sample mode one article stands for its whole file, so
        /// the weight is the file's; in Full mode it is the article's own.
        qint64 checkProbedBytes = 0;
        qint64 checkMissingBytes = 0;

        /// Files with at least one article no account would serve. A recovery
        /// volume in here cannot repair anything, so it stops counting towards
        /// the recovery total.
        QSet<int> checkMissingFiles;

        /// Somebody is streaming this item until at least this instant, so it
        /// sorts ahead of the priority field in dispatch(). Runtime only, never
        /// persisted: a crash must not leave an item boosted forever.
        qint64 streamingUntilMs = 0;

        /// Maps reads of the logical file onto the volumes holding it. Runtime
        /// only: every input is on disk or one article away, so rebuilding it
        /// after a restart is cheaper than keeping it honest across one.
        UsenetStreamIndex streamIndex;

        /// Archive sets being unpacked as their volumes land, keyed by set base
        /// name. Runtime only — a compressed stream cannot resume mid-way, so
        /// after a restart a set simply starts over from volume one, which costs
        /// only disk since every sealed volume is already there.
        QHash<QString, DirectUnpackRun> directUnpack;

        /// Most bytes ever advertised of an extracted member, by its path.
        ///
        /// An extraction that restarts truncates its output back to zero, and a
        /// player that was at 300 MB must not be told the file is 4 MB long
        /// again. Outlives the run it came from, which is the whole point: while
        /// a fresh run is still below this mark the answer is "wait", not a
        /// shorter file.
        QHash<QString, qint64> streamHighWater;
    };

    void startWorkers();
    void stopWorkers();
    void startPostProcessor();
    void stopPostProcessor();
    void rebuildPlan(ItemRuntime& rt);
    void dispatch();
    void onSegmentFinished(const UsenetFetchResult& result);
    void onCapacityChanged(int workerIndex, int capacity);
    void onTick();

    /// Segment indices covering `[offset, offset + length)` of @p fileIndex's
    /// file, plus one either side as §7.1's ±1 probe. Empty when the part length
    /// is not known yet — no article of that file has landed.
    [[nodiscard]] static QList<int> segmentsCovering(const UsenetQueueItem& item, int fileIndex,
                                                     qint64 offset, qint64 length);

    /// Move those segments to the front of the plan so they are dispatched next,
    /// and drag the cursor back with them so the sequential read-ahead follows
    /// the play head instead of carrying on where it was.
    void promoteRange(ItemRuntime& rt, int fileIndex, qint64 offset, qint64 length);

    void markSegmentDone(ItemRuntime& rt, const UsenetFetchResult& result);
    void handleSegmentFailure(ItemRuntime& rt, const UsenetFetchResult& result);
    void checkFileCompletion(ItemRuntime& rt, int fileIndex);

    /// Close a finished file off in the item's scratch directory: pad it out to
    /// its declared length so PAR2 sees the holes rather than a short file, and
    /// mark it done. It does **not** publish — that is post-processing's last
    /// step now.
    void sealFile(ItemRuntime& rt, int fileIndex);

    void checkItemCompletion(ItemRuntime& rt);

    /// Hand a just-sealed volume to the extraction that is following its set,
    /// starting one if this is the set's first volume. Does nothing unless the
    /// preference is on and the file is an archive volume.
    void pumpDirectUnpack(ItemRuntime& rt, int fileIndex);

    /// Start a run for @p baseName, if volume one of that set is already sealed
    /// and the cap allows it, replaying every volume of the set that has landed.
    /// False when the set is not ready to be extracted yet.
    bool startDirectUnpack(ItemRuntime& rt, const QString& baseName);

    /// Tell every run for this item that no more volumes are coming, so a run
    /// waiting past the last volume ends instead of blocking.
    void endDirectUnpackSets(ItemRuntime& rt);

    /// Abandon every run for this item and join its threads.
    void cancelDirectUnpack(ItemRuntime& rt);

    void onDirectUnpackFinished(const eMule::usenet::UsenetDirectUnpackResult& result);

    /// Record how far an extraction has got. Queued from the worker thread.
    void onDirectUnpackProgress(const eMule::usenet::UsenetDirectUnpackProgress& state);

    /// Start a run for every set of this item whose first volume is already
    /// sealed. Called after a resume, where pauseItem() cancelled the runs and
    /// nothing else would ever start them again: pumpDirectUnpack() only fires
    /// from a *newly* sealed volume.
    void restartDirectUnpack(ItemRuntime& rt);

    /// The member of @p fileIndex's set that @p entryOrdinal names, as far as
    /// the extraction has got — or nullptr when there is no run, or it has not
    /// reached that member.
    ///
    /// Matched by name whenever the index enumerated members, because the two
    /// sides number differently: the index numbers the members it could place,
    /// libarchive numbers everything it walks past.
    [[nodiscard]] const UsenetDirectUnpackEntry* extractionEntryFor(ItemRuntime& rt,
                                                                   int fileIndex,
                                                                   int entryOrdinal);

    /// Serve @p info from the extraction instead of from the volumes.
    ///
    /// The answer for everything the map refuses but libarchive can unpack —
    /// compressed and solid sets. False means "not this way"; true with no
    /// pieces means "wait", which is not the same as a refusal and must not be
    /// turned into one.
    [[nodiscard]] bool streamFromExtraction(ItemRuntime& rt, int fileIndex, int entryOrdinal,
                                            StreamInfo& info);

    /// Ask the scheduler for the volume the extraction is waiting on, so a
    /// preview of a set being unpacked pulls its own next bytes.
    void promoteExtractionVolume(ItemRuntime& rt, int fileIndex);

    /// Let the extraction overrule a member the map refused: playable once
    /// there are bytes, and saying so in the meantime.
    void annotateFromExtraction(ItemRuntime& rt, int fileIndex, int entryOrdinal,
                                ArchiveEntryInfo& row);

    /// Rows for a set the index refused outright, taken from what the extraction
    /// has walked past. Its ordinals are libarchive's, which is the only
    /// numbering that exists when the index enumerated nothing.
    void appendExtractionRows(ItemRuntime& rt, int fileIndex, ArchiveListing& listing);

    /// 0-based position of @p fileIndex within its volume set, or -1. The
    /// naming schemes number differently — `.partNN.rar` from 1, `.rNN` from 0
    /// with a bare `.rar` ahead of it — so only the ranking is meaningful.
    [[nodiscard]] static int volumeOrdinal(const ItemRuntime& rt, int fileIndex,
                                           const QString& baseName);

    /// Best known filename for @p fileIndex: the sealed name, else what the
    /// article header said, else the NZB's.
    [[nodiscard]] static QString volumeNameOf(const ItemRuntime& rt, int fileIndex);

    /// Whether @p fileIndex takes part in the current download round. False for
    /// a recovery volume nobody has asked for.
    [[nodiscard]] static bool isPlanned(const ItemRuntime& rt, int fileIndex);

    void beginPostProcessing(ItemRuntime& rt);
    void onPostStage(const QString& itemId, int stage, int percent, const QString& detail);
    void onPostFinished(const eMule::usenet::UsenetPostResult& result);

    /// Add the smallest set of unrequested recovery volumes covering @p blocks.
    /// False when there is nothing left to add, which is what ends the cycle.
    bool requestPar2Volumes(ItemRuntime& rt, int blocks);

    /// Rename the staged files into place and offer them to the share.
    void publishStaged(ItemRuntime& rt, const eMule::usenet::UsenetPostResult& result);

    void failItem(ItemRuntime& rt, const QString& message);
    void persist(ItemRuntime& rt);

    [[nodiscard]] ItemRuntime* runtimeFor(const QString& id);
    /// The configured account for @p serverKey, or null when it is gone.
    [[nodiscard]] const NewsServer* serverFor(const QString& serverKey) const;

    /// Whether @p s can plausibly still hold an article posted at @p date.
    ///
    /// Fails safe in every direction: an unknown date (0), an unknown retention
    /// (0), a future date and a nonsense date all answer true. Retention is a
    /// figure the user copied off a pricing page, not a protocol fact, so the
    /// only safe error is to ask anyway.
    [[nodiscard]] static bool retentionCovers(const NewsServer& s, qint64 date);

    /// The rung to ask next for an article posted at @p date, given the accounts
    /// in @p tried. -1 means the ladder is exhausted.
    ///
    /// @p relaxed is set when the answer was only reachable by ignoring the
    /// configured retention figures — the caller then sends no retention
    /// exclusions, so a wrong figure can never be the reason an article is
    /// declared missing. The relaxed search restarts at rung 0, deliberately: the
    /// last account tried before giving up is the one retention had skipped.
    [[nodiscard]] int nextServableLevel(const QStringList& tried, qint64 date,
                                        bool& relaxed) const;

    /// Lowest rung at or above @p floorRung holding an untried account that
    /// passes the requested filters, or -1.
    ///
    /// The one scan both ladder questions use. That nextServableLevel() passes
    /// `respectQuota = false` is a structural guarantee rather than a comment:
    /// no quota state can reach the exhaustion verdict, so a spent allowance can
    /// never be the reason an article is declared missing.
    /// Put @p rt into Checking and build its probe plan, or leave it alone and
    /// return false. Returning false is the normal, safe outcome: the engine may
    /// be stopped, no account may be configured, or the mode may be Off, and
    /// none of those may delay a download by a millisecond.
    bool beginHealthCheck(ItemRuntime& rt);

    /// Hand probe requests to the workers, from a reserved slice of their
    /// capacity. Reserved rather than leftover: a busy queue never has leftover
    /// slots, and a Checking item would then wait for the whole queue to drain.
    void dispatchProbes();

    void handleProbeResult(ItemRuntime& rt, const UsenetFetchResult& result);

    /// Turn the answers into a verdict and let the item go — to Queued, or to
    /// Paused with a reason when the shortfall is past the threshold and the
    /// recovery volumes cannot cover it. Never to Failed.
    void finishHealthCheck(ItemRuntime& rt);

    void clearHealthCheck(ItemRuntime& rt) const;

    /// How many bytes one probe of this article stands for.
    [[nodiscard]] qint64 probeWeight(const ItemRuntime& rt, int fileIndex,
                                     int segIndex) const;

    /// The queued item @p nzb would duplicate, or null. @p why is filled with a
    /// sentence for the user when one is returned.
    ///
    /// ⚠️ The sentence must not contain " — ": AddNzbUrlDialog formats a failed
    /// line as "<url> — <reason>" and recovers the URL with section(" — ", 0, 0),
    /// so an em-dash-space inside the reason silently corrupts its retry list.
    [[nodiscard]] const UsenetQueueItem* findDuplicate(const NzbInfo& nzb,
                                                       const QString& name,
                                                       UsenetAddSource source,
                                                       QString& why) const;

    [[nodiscard]] int lowestUntriedRung(const QStringList& tried, qint64 date, int floorRung,
                                        bool respectRetention, bool respectQuota) const;

    /// The rung this queue may actually *spend* on, at or above @p floorRung.
    ///
    /// -1 means every remaining candidate is over its allowance — a **wait**,
    /// never a verdict, which is the whole difference from nextServableLevel().
    /// @p relaxed is promoted to true when retention had to be ignored to find
    /// one, so a retention guess can no more stall the queue than it can strand
    /// an article.
    [[nodiscard]] int nextAffordableLevel(const QStringList& tried, qint64 date,
                                          int floorRung, bool& relaxed) const;

    /// Accounts on @p rung that must not be asked for this article: over
    /// allowance always, retention-uncovered only when @p applyRetention.
    ///
    /// The quota half is never relaxed — an over-quota account is a measured
    /// fact, not a figure off a pricing page.
    [[nodiscard]] QStringList dispatchExclusions(int rung, qint64 date,
                                                 bool applyRetention) const;

    /// Whether worker @p w holds connection budget on an account at @p rung that
    /// is not in @p ignore. Subsumes the old rung-coverage test — with nothing
    /// excluded it answers exactly what m_workerLevels did — and additionally
    /// stops a worker being handed an article it can only bounce.
    [[nodiscard]] bool workerCanServe(int w, int rung, const QStringList& ignore) const;

    /// Whether an untried account exists that is only being skipped because it
    /// has spent its allowance.
    ///
    /// While one does, a failure is not final: the article is reachable and we
    /// are merely choosing not to pay for it. Both of handleSegmentFailure()'s
    /// terminal transitions gate on this, because neither consults
    /// nextServableLevel() and either would otherwise turn a spending limit into
    /// a missing article or a failed item.
    [[nodiscard]] bool quotaBlockedCandidateExists(const QStringList& tried) const;

    /// Recompute the over-allowance set. Cheap (16 accounts at most) and done
    /// once per dispatch round rather than once per segment, so every segment in
    /// a round sees the same answer.
    void refreshQuotaState();

    /// Park @p rt and say why, once per stall rather than four times a second.
    void noteQuotaStall(ItemRuntime& rt, const QStringList& tried, qint64 date);

    /// Let every parked item try again — a rollover, a usage edit or a config
    /// change happened, so the answer can have changed.
    void unparkQuotaStalls();

    /// Record a segment as resolved-but-absent and say why. The `done` bit means
    /// resolved, so this is what stops the scheduler asking again; PAR2 repair is
    /// what recovers the bytes.
    void markSegmentMissing(ItemRuntime& rt, int fileIndex, int segmentIndex,
                            const QString& messageId, const QString& reason);

    // std::vector, not QList: QList requires copyable elements and ItemRuntime
    // holds a unique_ptr.
    std::vector<std::unique_ptr<ItemRuntime>> m_items;

    QList<QThread*> m_threads;
    QList<UsenetWorker*> m_workers;
    QList<int> m_workerCapacity;
    QList<int> m_workerInFlight;

    QList<NewsServer> m_servers;
    int m_retryIntervalSec = 60;

    /// Distinct configured levels, ascending — the failover ladder. A level's
    /// index here is its rung. Built by nntpLevelLadder() from the same list and
    /// the same predicate every worker's pool uses, so the two cannot number the
    /// rungs differently.
    QList<int> m_ladder;

    /// Every usable account is optional, so there is no "elsewhere" to try and
    /// the flag means nothing. Without this a lone optional account being down
    /// would mark every article missing and publish a silently ruined release
    /// instead of failing legibly.
    bool m_allServersOptional = false;

    /// Rungs each worker can actually lease on. A grouped account's divided
    /// share can be zero on most workers, and a worker asked for a rung it does
    /// not hold answers "no server available" — indistinguishable from busy, and
    /// it bounces every tick.
    QList<QSet<int>> m_workerLevels;

    /// The accounts behind those rungs. A rung is only worth giving a worker an
    /// article for when the worker holds budget on an account there that this
    /// article may ask — and a spent allowance changes that without changing any
    /// slice, which is why the rung set alone is not enough.
    QList<QSet<QString>> m_workerServers;

    /// Per-account byte meters, and the sidecar behind them.
    UsenetUsageTracker m_usage;

    /// Keys of accounts that have spent their allowance. Recomputed per dispatch
    /// round and **never** written into SegmentAttempt::tried, so a rollover or
    /// a raised cap takes effect on the very next dispatch with no state to
    /// migrate.
    QSet<QString> m_overQuota;

    /// Ticks since the last usage flush, and whether the "everything is over its
    /// allowance" line has already been logged for the current stall. The stall
    /// re-evaluates four times a second; unguarded it would fill log.log.
    int m_usageTicks = 0;
    bool m_quotaStallLogged = false;

    /// Buckets already warned at 90% of their allowance, so the line is said
    /// once rather than four times a second. Cleared when the figure drops back
    /// under — a rollover or a correction re-arms it for the new period.
    QSet<QString> m_quotaWarned;

    QThread* m_postThread = nullptr;
    UsenetPostProcessor* m_postProcessor = nullptr;

    bool m_par2Enabled    = true;
    bool m_renameEnabled  = true;
    bool m_unpackEnabled  = true;
    bool m_cleanupEnabled = true;
    bool m_directUnpackEnabled = true;

    /// Runs in flight across the whole queue. Each holds a thread that is
    /// blocked for as long as its download takes, so this is capped rather than
    /// left to grow with the queue; a set over the cap just unpacks at the end.
    int m_directUnpackRuns = 0;
    static constexpr int kMaxDirectUnpacks = 2;

    QTimer* m_tickTimer = nullptr;
    qint64 m_rateLimit = 0;
    qint64 m_currentRate = 0;
    qint64 m_bytesThisTick = 0;

    /// Set when a dispatch round found every server blocked or busy. Cleared on
    /// the next tick, which is what turns a spin into a 250 ms retry.
    bool m_starved = false;

    bool m_running = false;
};

} // namespace eMule::usenet
