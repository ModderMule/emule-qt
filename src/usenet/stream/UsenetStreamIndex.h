#pragma once

/// @file UsenetStreamIndex.h
/// @brief Maps reads of a logical media file onto the volume files that hold it.
///
/// Phase 6b, Tier B of docs/UsenetModule-Research.local.md §7.3. A stored
/// (`-m0`) RAR volume's payload is a byte-identical slice of the original file,
/// so playing a release reduces to reading the right volume at the right
/// offset — no decompression, and no need to have downloaded the volumes in
/// between.
///
/// **It never fetches and never blocks.** When it is missing bytes it says so
/// (`NeedBytes`) and returns; UsenetQueue promotes the segments covering that
/// range and the preview request's existing 250 ms poll asks again. The state
/// machine advances one step per tick, which is why phase 6b needed no new
/// waiting machinery on top of 6a's.
///
/// Three release shapes resolve here, in order of how they are recognised:
///
///   1. **Not an archive** — one extent covering the file. This is phase 6a,
///      subsumed rather than special-cased.
///   2. **A `.001`/`.002` split** — the members concatenated in order. §7.2
///      calls it "trivial arithmetic" and it is.
///   3. **A stored RAR set** — the interesting one. Volumes are ordered from
///      their NZB filenames, then the set's *members* (the files inside it) are
///      enumerated one at a time, each placed across the volumes it spans.
///
/// A set may hold several members — a feature plus its `.nfo`, or a season pack
/// of ten episodes. `memberIndex` picks one; `-1` means the first playable one,
/// which is what makes an `.nfo`-first release play the movie rather than
/// reporting that the archive holds nothing playable.
///
/// **Only volume slots are ever predicted, never byte addresses.** A volume
/// starts with a RAR marker, so a slot probe checks itself for free; a guessed
/// mid-volume address has no such anchor, and RAR5 would defeat one anyway —
/// `DataSize` is a vint, so a short tail volume's header is physically smaller
/// than a full one's and every address derived from a sibling is off by the
/// vint-width delta. Every byte offset probed here is either 0 or the exact
/// `dataOffset + packedSize` of a header already parsed in that same volume.

#include "stream/RarReader.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>

namespace eMule::usenet {

class UsenetQueueItem;

/// One contiguous run of the logical file, living inside one NZB file.
struct StreamExtent {
    int fileIndex = -1;        ///< the NZB file (the volume) holding it
    qint64 virtualOffset = 0;  ///< where it starts in the logical file
    qint64 fileOffset = 0;     ///< where it starts inside that volume file
    qint64 length = 0;
};

enum class StreamPlan {
    Unknown,       ///< nothing decided yet; ask again once something has landed
    NeedBytes,     ///< fetch the named range, then ask again
    Ready,
    NotSeekable,   ///< and it never will be — see StreamResolve::reason
};

struct StreamResolve {
    StreamPlan plan = StreamPlan::Unknown;

    // NeedBytes
    int needFileIndex = -1;
    qint64 needOffset = 0;
    qint64 needLength = 0;

    // Ready
    QString fileName;    ///< the inner name from the RAR header — real even when the NZB's is obfuscated
    qint64 totalSize = 0;
    QList<StreamExtent> extents;

    // NotSeekable
    QString reason;      ///< shown to the user in place of a disabled Preview
};

/// One file inside an archive set, as offered to the GUI's chooser.
struct StreamMember {
    int index = -1;         ///< ordinal in archive order — the handle `&entry=` carries
    QString name;
    qint64 size = 0;        ///< unpacked size
    bool playable = false;  ///< video or audio by extension
    bool mappable = false;  ///< stored and unencrypted, so its bytes can be served
    QString note;           ///< why not, when !mappable
};

/// The set's contents. `members` is always a *prefix* of the true listing, so an
/// ordinal handed out early never re-points at a different file later.
struct StreamListing {
    StreamPlan plan = StreamPlan::Unknown;

    int needFileIndex = -1;
    qint64 needOffset = 0;
    qint64 needLength = 0;

    QList<StreamMember> members;
    bool complete = false;   ///< every header in the set has been parsed
    bool isArchive = false;  ///< false for a raw post or a `.001` split
    QString reason;
};

/// One per queue item, held in UsenetQueue's runtime and never persisted: every
/// input is either on disk or one article away, so rebuilding after a restart is
/// cheaper than keeping a cache honest across one.
class UsenetStreamIndex {
public:
    /// Resolve a read of @p fileIndex's set at @p wantOffset, within the inner
    /// file @p memberIndex (`-1` = the first playable one).
    ///
    /// @p fileIndex may name *any* volume of a set — clicking Preview on
    /// `part07.rar` and on `part01.rar` both resolve to the same *set*, and the
    /// member ordinal is a property of the set rather than of the volume
    /// clicked, so both return byte-identical extents. That is what keeps the
    /// queue tree from needing a new kind of row.
    [[nodiscard]] StreamResolve resolve(const UsenetQueueItem& item, int fileIndex,
                                        int memberIndex, qint64 wantOffset);

    /// Enumerate the set's inner files. Same never-fetch contract as resolve():
    /// a `NeedBytes` plan means "promote that range and ask again", and
    /// `members` meanwhile holds what is already known.
    [[nodiscard]] StreamListing list(const UsenetQueueItem& item, int fileIndex);

    /// Drop every cached header and set. Volume paths move under us when
    /// sealFile() renames and when post-processing repairs, and a stale path is
    /// indistinguishable from a missing file.
    ///
    /// Cheap on purpose: nothing cached here is anything but a function of the
    /// bytes on disk, so everything dropped is re-derived by re-reading files
    /// that are still there. Rebuilding costs CPU and never costs an article.
    void invalidate();

private:
    /// What a volume's headers told us. Keyed by NZB file index.
    struct VolumeCache {
        RarParse status = RarParse::NeedMoreBytes;
        RarFormat format = RarFormat::Unknown;
        int volumeNumber = -1;
        bool isFirstVolume = false;
        bool endOfArchive = false;
        QString reason;                  ///< set-level refusal: solid, encrypted headers, not RAR
        QMap<qint64, RarEntry> blocks;   ///< header offset -> entry, every block parsed so far
        qint64 scanned = 0;              ///< first offset in this volume not yet parsed
    };

    /// One inner file, and the run of volume parts holding it.
    struct MemberInfo {
        QString name;
        qint64 unpackedSize = 0;
        bool stored = false;
        bool encrypted = false;

        int startSlot = -1;
        qint64 startDataOffset = 0;
        qint64 firstPartSize = 0;
        bool firstSplitAfter = false;

        int endSlot = -1;                ///< -1 until the run is placed
        qint64 endDataOffset = 0;
        qint64 endPartSize = 0;
        qint64 midPartSize = 0;          ///< the constant interior part; 0 if the run is short

        bool placed = false;
        bool uniform = false;            ///< interiors follow the model; extents are derived
        QList<StreamExtent> extents;     ///< only when !uniform — walked from real headers
        QString note;                    ///< per-member refusal, e.g. compressed beside stored
    };

    struct SetInfo {
        QList<int> volumes;              ///< NZB file indices, in volume order
        RarFormat format = RarFormat::Unknown;
        QList<MemberInfo> members;

        int scanSlot = 0;                ///< enumeration cursor: which volume,
        qint64 scanOffset = 0;           ///< ... and which block boundary inside it
        bool complete = false;

        bool refused = false;            ///< set-level only; a bad member never lands here
        QString reason;
    };

    [[nodiscard]] static QString fileNameOf(const UsenetQueueItem& item, int fileIndex);

    /// Whether @p fileIndex's name is as good as it is going to get.
    ///
    /// Not "is it empty": on a fully obfuscated post `=ybegin name=` returns a
    /// name that is real text and means nothing, and treating that as an answer
    /// orders a volume set from whichever files happened to look like volumes.
    /// A name is settled when PAR2 named it, when the release has no PAR2 index
    /// to name it with, or when the bytes PAR2 would need were there and it
    /// still did not match.
    [[nodiscard]] static bool nameIsSettled(const UsenetQueueItem& item, int fileIndex);

    /// How many opening bytes to ask for when a name is not settled.
    ///
    /// ⚠️ kRarHeaderProbeBytes is 8192 and PAR2 identifies a file by its first
    /// 16384, so a release that needs PAR2 to name it must ask for the larger of
    /// the two. With ~700 KB articles both arrive in the same article and the
    /// difference never shows; with a small-article post, asking for 8 KB leaves
    /// the name unsettled forever and the prefetch loops.
    [[nodiscard]] static qint64 nameProbeBytes(const UsenetQueueItem& item);
    [[nodiscard]] static QList<int> splitMembersOf(const UsenetQueueItem& item, int fileIndex);
    [[nodiscard]] static bool readIfAvailable(const UsenetQueueItem& item, int fileIndex,
                                              qint64 offset, qint64 length, QByteArray& out);

    [[nodiscard]] StreamResolve resolveRawFile(const UsenetQueueItem& item, int fileIndex) const;
    [[nodiscard]] StreamResolve resolveSplitSet(const UsenetQueueItem& item,
                                                const QList<int>& members) const;

    /// Order a set's volumes from their names, or say what is still missing.
    [[nodiscard]] SetInfo* setFor(const UsenetQueueItem& item, int fileIndex,
                                  StreamResolve& out);

    /// Parse volume @p slot's head, or report what it needs. Cached.
    [[nodiscard]] bool volumeHead(const UsenetQueueItem& item, const SetInfo& set, int slot,
                                  StreamResolve& out);

    /// The block at @p offset in volume @p slot, resuming mid-volume. @p offset
    /// is always a block boundary: 0, or an entry's `dataOffset + packedSize`.
    [[nodiscard]] bool blocksAt(const UsenetQueueItem& item, const SetInfo& set, int slot,
                                qint64 offset, RarEntry& entry, qint64& nextOffset,
                                bool& exhausted, StreamResolve& out);

    /// Advance the enumeration until @p wantMember is placed (`-1` = until the
    /// first playable member is), or until the set is fully enumerated when
    /// @p all is set.
    [[nodiscard]] bool scanUntil(const UsenetQueueItem& item, SetInfo& set, int wantMember,
                                 bool all, StreamResolve& out);

    /// Work out which volumes member @p m spans.
    [[nodiscard]] bool placeMember(const UsenetQueueItem& item, SetInfo& set, int m,
                                   StreamResolve& out);

    /// The continuation header a volume opens with, or nullptr when unparsed.
    [[nodiscard]] const RarEntry* firstEntryOf(const SetInfo& set, int slot) const;

    /// Does @p slot close @p mi's run? Records the end on success.
    [[nodiscard]] bool acceptEnd(const SetInfo& set, MemberInfo& mi, int slot) const;

    /// The member ordinal a caller means: @p requested, or the first playable one.
    [[nodiscard]] int pickMember(const SetInfo& set, int requested) const;

    /// Extents for a placed member, parsing whatever volume @p wantOffset lands
    /// in so a read is never served from a modelled payload start.
    [[nodiscard]] StreamResolve memberExtents(const UsenetQueueItem& item, SetInfo& set, int m,
                                              qint64 wantOffset);

    QHash<QString, SetInfo> m_sets;        ///< keyed by the set's base name
    QHash<int, VolumeCache> m_volumes;     ///< keyed by NZB file index
};

/// Whether a name is something a player can open — the same test ED2K's
/// PartFile::isPreviewPossible() uses, so the two networks cannot disagree.
/// Shared rather than copied: the queue asks it of names that never reach the
/// index, and two copies of this test are exactly how the answers drift apart.
[[nodiscard]] bool isPlayableName(const QString& name);

/// End of the readable run of the logical file starting at @p virtualOffset,
/// walking across extent boundaries for as long as each successive volume is
/// readable from its own `fileOffset`.
[[nodiscard]] qint64 availableFrom(const UsenetQueueItem& item,
                                   const QList<StreamExtent>& extents,
                                   qint64 virtualOffset);

/// The extent holding @p virtualOffset, or nullptr when nothing does.
[[nodiscard]] const StreamExtent* extentAt(const QList<StreamExtent>& extents,
                                           qint64 virtualOffset);

} // namespace eMule::usenet
