#pragma once

/// @file UsenetQueueItem.h
/// @brief One queued NZB and everything needed to resume it after a restart.
///
/// The unit of identity is the item `id`, a UUID string. It is the IPC key, the
/// push coalescing sub-key and the GUI's selection-restore key — deliberately not
/// a list index, for the same reason NewsServer::key() is not one: the index moves
/// when the user reorders, and a half-finished download would then be attributed
/// to the wrong row.
///
/// Completion is tracked per *segment*, in a bit per article. A 10 000-segment
/// release costs 1.25 KB, which is cheap enough to persist verbatim and is what
/// makes a restart resume rather than refetch.

#include "nzb/NzbInfo.h"

#include <QBitArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>

namespace eMule::usenet {

/// Persisted as a raw int in the .nzbstate sidecar and sent as one over IPC, so
/// existing values never move — new states are appended.
enum class UsenetItemStatus : quint8 {
    Queued = 0,
    Downloading = 1,
    Paused = 2,
    Complete = 3,
    Failed = 4,

    // Post-processing. All three mean the download phase finished and the item
    // is with UsenetPostProcessor; none of them may dispatch segments.
    Verifying = 5,
    Repairing = 6,
    Unpacking = 7,

    /// Asking the servers whether they still hold the release, before spending
    /// anything on it. Appended rather than made an ItemRuntime flag on purpose:
    /// isActive() is false here, so a checking item leaves the dispatch order
    /// *and* stops counting as a live download for the bandwidth split, in one
    /// edit rather than at two call sites somebody has to remember. A raw int in
    /// a sidecar is a commitment, but every exhaustive switch over this enum has
    /// no default and so becomes a -Wswitch warning — a compiler-enforced list of
    /// everywhere that now needs an opinion.
    ///
    /// Never resumed *into*: UsenetQueueStore::load() demotes it to Queued
    /// alongside Downloading, because no probe survives a restart and re-probing
    /// on every start would spend round trips re-learning advice.
    Checking = 8,
};

[[nodiscard]] QString describeUsenetItemStatus(UsenetItemStatus s);

/// The five levels a release can be queued at. Five rather than eD2K's three
/// because a Usenet queue holds a few large releases where "this one before
/// those two, but after the one I am watching" is a thing people mean; NZBGet
/// ships the same five for the same reason. The values are the stored ints, so
/// the original three keep their meaning and an old sidecar needs no migration.
inline constexpr int kUsenetPriorityVeryLow = -2;
inline constexpr int kUsenetPriorityLow = -1;
inline constexpr int kUsenetPriorityNormal = 0;
inline constexpr int kUsenetPriorityHigh = 1;
inline constexpr int kUsenetPriorityVeryHigh = 2;

/// Highest first — menu order, and the order the queue runs them in.
inline constexpr std::array kUsenetPriorityLevels{
    kUsenetPriorityVeryHigh, kUsenetPriorityHigh, kUsenetPriorityNormal,
    kUsenetPriorityLow, kUsenetPriorityVeryLow,
};

/// The names live in the GUI (`usenetPriorityName()`), not here: the GUI does
/// not link eMule::Usenet, and the wire carries the int. The daemon clamps, so
/// the two cannot disagree about how many levels there are.
///
/// Into [kUsenetPriorityVeryLow, kUsenetPriorityVeryHigh]. A GUI sending 7 is a
/// bug worth bounding rather than obeying: an out-of-range value would make a
/// sixth bucket that nothing can name and no menu entry can ever select again.
[[nodiscard]] int clampUsenetPriority(int priority);

/// Strip anything a file system would object to, and anything that would let a
/// crafted name escape the temp directory.
///
/// Every name that reaches this module comes from a stranger: an NZB subject is
/// attacker-controlled, `=ybegin name=` is whatever the poster wrote, and PAR2
/// stores *paths*, not bare filenames. None of them may be handed to the disk
/// unexamined.
[[nodiscard]] QString sanitizeName(const QString& raw);

/// Per-file progress. Parallel to NzbInfo::files — same index, same order.
struct UsenetFileState {
    /// Scratch file under the Usenet temp tree, always carrying
    /// Preferences::kUsenetPartSuffix so no share scan can ever pick it up.
    QString tempPath;

    /// Where it landed once complete. Empty until then.
    QString finalPath;

    /// The name yEnc declared in `=ybegin name=`. For a *partially* obfuscated
    /// post this is the only place the real filename exists, so it overrides
    /// whatever the subject parser guessed. A fully obfuscated one scrambles
    /// this too — see par2FileName.
    QString articleFileName;

    /// The name the PAR2 recovery set gives this file, matched by its length and
    /// the MD5 of its first 16 KiB.
    ///
    /// Outranks every other source because it is the only one that is *checked*:
    /// a subject is a stranger's formatting and `=ybegin name=` is whatever the
    /// poster typed, but this one had to hash-match the bytes on disk. Empty on
    /// any release that is not obfuscated, and empty until both the index .par2
    /// and this file's first 16 KiB have landed.
    QString par2FileName;

    /// Total size from `=ybegin size=`, 0 until the first article arrives. The
    /// NZB cannot supply this — its `bytes` is the *encoded* size.
    qint64 declaredSize = 0;

    /// One bit per segment, in NzbFileInfo::segments order.
    ///
    /// The bit means **resolved**, not "arrived": a segment missing on every
    /// server in the ladder is also set, so the scheduler stops asking for it.
    /// `missing` is which of the two, and a file with a non-zero
    /// `missingSegments` is short by design — PAR2 repair is what recovers it.
    ///
    /// A retry clears a done bit whose `missing` bit is set, and that is the
    /// only thing in the module that ever clears one.
    QBitArray done;

    /// Which segments were resolved by being unavailable rather than by
    /// arriving. Same indexing as `done`, and always a subset of it except
    /// between a retry re-arming a segment and that segment resolving again.
    ///
    /// It exists so "no server has this" can be revisited: the verdict was about
    /// the servers configured at the time it was reached, and the user may since
    /// have added one. `missingSegments` is its population count and stays the
    /// figure everything else reads -- see the invariant on `missingSegments`.
    ///
    /// An optional sidecar key. An older sidecar loads it empty while
    /// `missingSegments` is non-zero, which is exactly the state a retry refuses
    /// rather than guesses at.
    QBitArray missing;

    /// Byte ranges actually on disk, merged and sorted, half-open `[start, end)`.
    ///
    /// `done` cannot answer "is byte X readable yet?". Its bit means *resolved*,
    /// and a segment missing on every server sets it too — so a set bit can mean
    /// no bytes were ever written. Nor can the ranges be derived from the NZB:
    /// `NzbSegment::bytes` is the *encoded* size. Only the article's own
    /// `=ypart begin` says where its payload lands, so this is filled from
    /// UsenetFetchResult::decodedOffset and from nowhere else.
    ///
    /// Downloading is in plan order, so in practice this holds one or two
    /// intervals even for a 10 000-article release.
    QList<QPair<qint64, qint64>> written;

    /// Decoded length of a *full* part, learned from the articles themselves.
    ///
    /// Phase 6b needs to answer "which article holds byte X" for a byte nobody
    /// has fetched, and nothing else can: `NzbSegment::bytes` is the encoded
    /// size, and `done` says nothing about offsets. A poster splits a file into
    /// equal parts with a short remainder, so the largest decoded article seen
    /// *is* the part length — §7.1's recipe, with the arriving `=ypart begin`
    /// as the check. 0 until the first article lands.
    qint64 partLength = 0;

    qint64 decodedBytes = 0;
    bool finalized = false;

    /// How many articles were missing on every server in the ladder. Non-zero
    /// means the file **has zeros in it right now** -- which is how direct
    /// unpack, the sealed-volume replay, the encrypted preview and
    /// post-processing all read it, so it may only fall when bytes actually
    /// arrive, never when a retry merely decides to ask again.
    ///
    /// Invariant, checked before a retry is allowed: `missing.count(true) ==
    /// missingSegments`. A sidecar written before `missing` existed fails it,
    /// and that is the signal to refuse rather than guess which articles they
    /// were.
    int missingSegments = 0;

    [[nodiscard]] bool allSegmentsDone() const;

    /// Merge `[start, end)` into `written`, coalescing with any neighbours.
    void addWritten(qint64 start, qint64 length);

    /// End of the contiguous run that starts at byte 0, i.e. how much of the
    /// file can be streamed from the beginning. 0 when nothing has landed yet.
    ///
    /// Deliberately *not* "how many bytes exist": a hole left by an article that
    /// was missing on every server stops the count dead, because the bytes after
    /// it are at the wrong offsets for a player. PAR2 repair is what fills it.
    [[nodiscard]] qint64 availableEnd() const;

    /// End of the contiguous run *containing* @p offset, or @p offset itself
    /// when nothing covers it. `availableEnd()` is exactly `availableFrom(0)`.
    ///
    /// Phase 6b needs the general form: a seek asks "how far can I read from
    /// here", and a stored-RAR volume's payload never starts at byte 0 of the
    /// volume file — the archive header sits in front of it.
    [[nodiscard]] qint64 availableFrom(qint64 offset) const;
};

class UsenetQueueItem {
public:
    UsenetQueueItem() = default;

    QString id;
    QString name;
    NzbInfo nzb;
    UsenetItemStatus status = UsenetItemStatus::Queued;

    /// Higher runs first; see kUsenetPriorityVeryHigh and friends. Persisted as
    /// the raw int, so the three original values keep the meaning they had.
    int priority = 0;

    /// The accounts that could have served this item, digested, as they were
    /// when it failed. Empty until it fails, and cleared when it succeeds.
    ///
    /// Resume re-asks the missing articles only when this differs from the
    /// current set -- i.e. when the user has actually changed something a retry
    /// could benefit from. Without the gate, Resume would re-ask every dead
    /// article of every failed release each time it is pressed, and
    /// setItemPassword() resumes too, so setting a passphrase would do it
    /// silently. An optional sidecar key; absent means "never failed here", and
    /// an upgraded daemon therefore re-asks nothing on its first run.
    QString failedLadder;

    /// Index into `Preferences::categories()`, 0 being the implicit "All".
    ///
    /// The *index* is stored and the folder is asked for at completion, never
    /// cached here: a release is categorised when it is queued and lands hours
    /// later, and the user can repoint or delete the category in between. See
    /// `Preferences::incomingDirForCategory()`, which is the single resolution
    /// point and already falls back when the folder is gone.
    int category = 0;

    QList<UsenetFileState> files;

    /// Set when the item stopped for a reason worth showing the user.
    QString error;

    /// The release is password-protected and no password we have opens it.
    ///
    /// Persisted rather than derived from `error`, because the GUI turns it into
    /// a "Set Password…" prompt and matching on message text would break the
    /// moment the message is translated. Cleared by a successful post-processing
    /// run, so a retry that works leaves nothing behind.
    bool passwordRequired = false;

    /// Absolute paths of what this release actually published, in staging order.
    ///
    /// Not derivable from `files`: an unpacked release publishes the *extracted*
    /// members, which are not NZB files at all, and even a raw one publishes in
    /// name order with the .par2 files dropped. Persisted, so a completed item is
    /// still openable after a restart.
    QStringList publishedPaths;

    /// Indices into nzb.files of the PAR2 recovery volumes actually asked for.
    ///
    /// Persisted, and it has to be: the download plan is rebuilt from scratch on
    /// every restart, so a queue that forgot which volumes it had requested
    /// would drop them from the plan, verify short again, and ask for them a
    /// second time — once per restart, forever.
    QSet<int> requestedPar2;

    [[nodiscard]] qint64 totalEncodedBytes() const { return nzb.totalEncodedBytes(); }
    [[nodiscard]] qint64 decodedBytes() const;
    [[nodiscard]] int segmentCount() const { return nzb.segmentCount(); }
    [[nodiscard]] int doneSegmentCount() const;

    /// Whether @p fileIndex is worth offering a preview for: a video or audio
    /// payload file, never a PAR2 member.
    ///
    /// Archive volumes are excluded by the same test — an `.r00` is not a media
    /// extension. That is not an oversight: a stored RAR set streams as a
    /// partial `.rar`, which no player opens, and mapping reads through the
    /// volume headers is Tier B (phase 6b). Better a disabled menu entry than a
    /// preview that fails at play time.
    /// The best name known for @p fileIndex: the PAR2 set's, else the one yEnc
    /// declared, else what the subject parser recovered from the NZB.
    ///
    /// Empty when none of the three has an answer yet. Callers add their own
    /// last-resort fallback, because it genuinely differs — sealFile() invents
    /// one, the streaming index would rather ask for more bytes.
    [[nodiscard]] QString bestFileName(int fileIndex) const;

    [[nodiscard]] bool isFilePreviewable(int fileIndex) const;

    /// 0-100. Uses segment counts, not bytes: NzbSegment::bytes is the encoded
    /// size and decoded bytes are not derivable from it, so a byte-based figure
    /// would drift against the real total.
    [[nodiscard]] int percentComplete() const;

    /// Whether the item wants segments dispatched to it. Deliberately false
    /// during post-processing: a verify pass must not race the writer that is
    /// still filling one of the files it is reading.
    [[nodiscard]] bool isActive() const
    {
        return status == UsenetItemStatus::Queued || status == UsenetItemStatus::Downloading;
    }

    /// Whether the item is with the post-processing pipeline.
    [[nodiscard]] bool isPostProcessing() const
    {
        return status == UsenetItemStatus::Verifying || status == UsenetItemStatus::Repairing
            || status == UsenetItemStatus::Unpacking;
    }

    /// Neither finished nor stopped — still on its way somewhere. What the GUI
    /// shows a progress bar for and what the bandwidth split counts.
    [[nodiscard]] bool isRunning() const { return isActive() || isPostProcessing(); }

    /// Live post-processing progress, 0-100. Not persisted: a restart re-runs
    /// the whole pipeline.
    int postPercent = 0;

    /// What the pipeline is doing right now, for the Status column.
    QString postDetail;

    /// Percentage of the release believed obtainable, or **-1 for "not
    /// assessed"** — which is not the same as 100 and must not be shown as it.
    ///
    /// Combines both halves: articles the NZB never listed, and articles no
    /// configured account still holds. Advisory in the strongest sense — nothing
    /// in the download path reads it.
    int healthPercent = -1;

    /// Bytes behind that percentage, and the recovery data the release ships to
    /// cover them. A shortfall smaller than the recovery is very likely
    /// repairable, which is why the percentage alone is never the verdict.
    qint64 healthMissingBytes = 0;
    qint64 healthRecoveryBytes = 0;

    /// Whether any server was actually asked. False means healthPercent, if set
    /// at all, is the NZB's own arithmetic and says nothing about availability.
    bool healthProbed = false;

    /// Exact identity of the release: a digest over its message-ids.
    /// See nzbArticleDigest(). **Not persisted** -- every message-id is already
    /// in the sidecar, so this re-derives exactly on load, and a stored copy
    /// would only be a second thing that could disagree with the first.
    QString articleDigest;

    /// Heuristic identity, for the "looks like a repost of" report. Also
    /// re-derived rather than stored, for the same reason.
    QString releaseKey;

    /// Why the item is sitting still when it is neither downloading nor failed
    /// — today, an allowance that has been spent. Not persisted, for the same
    /// reason postDetail is not: it is a live condition, and a restart
    /// re-derives it. Deliberately not `error`, which means "this download
    /// failed"; waiting for a billing day has failed nothing.
    QString stalledReason;

    /// Size the per-file state to match the parsed NZB. Safe to call twice.
    void initFileStates(const QString& tempRoot);
};

} // namespace eMule::usenet
