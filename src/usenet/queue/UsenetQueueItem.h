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
};

[[nodiscard]] QString describeUsenetItemStatus(UsenetItemStatus s);

/// Per-file progress. Parallel to NzbInfo::files — same index, same order.
struct UsenetFileState {
    /// Scratch file under the Usenet temp tree, always carrying
    /// Preferences::kUsenetPartSuffix so no share scan can ever pick it up.
    QString tempPath;

    /// Where it landed once complete. Empty until then.
    QString finalPath;

    /// The name yEnc declared in `=ybegin name=`. For an obfuscated post this is
    /// the only place the real filename exists, so it overrides whatever the
    /// subject parser guessed.
    QString articleFileName;

    /// Total size from `=ybegin size=`, 0 until the first article arrives. The
    /// NZB cannot supply this — its `bytes` is the *encoded* size.
    qint64 declaredSize = 0;

    /// One bit per segment, in NzbFileInfo::segments order.
    ///
    /// The bit means **resolved**, not "arrived": a segment missing on every
    /// server in the ladder is also set, so the scheduler stops asking for it.
    /// `missingSegments` is what distinguishes the two, and a file with a
    /// non-zero count is short by design — PAR2 repair is what recovers it.
    QBitArray done;

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
    /// means the finished file has holes in it: PAR2 repair (phase 4) is what
    /// fills them, and until then this is the only record that they exist.
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

    /// Higher runs first. Matches the ED2K convention rather than NZBGet's.
    int priority = 0;

    QList<UsenetFileState> files;

    /// Set when the item stopped for a reason worth showing the user.
    QString error;

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

    /// Size the per-file state to match the parsed NZB. Safe to call twice.
    void initFileStates(const QString& tempRoot);
};

} // namespace eMule::usenet
