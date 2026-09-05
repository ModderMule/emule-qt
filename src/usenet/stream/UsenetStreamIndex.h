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
///      their NZB filenames, volumes 1, 2 and N are parsed, and the identity
///      `p1 + (N-2)*p2 + pN == unpackedSize` proves the set is uniform. Three
///      articles then place every volume without reading it.
///
/// A volume is only ever *served* from once its own header has been parsed, so
/// a set that fails the uniformity check degrades to walking headers one volume
/// at a time rather than to serving from a guessed offset.

#include "stream/RarReader.h"

#include <QHash>
#include <QList>
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

/// One per queue item, held in UsenetQueue's runtime and never persisted: every
/// input is either on disk or one article away, so rebuilding after a restart is
/// cheaper than keeping a cache honest across one.
class UsenetStreamIndex {
public:
    /// Resolve a read of @p fileIndex's logical file at @p wantOffset.
    ///
    /// @p fileIndex may name *any* volume of a set — clicking Preview on
    /// `part07.rar` and on `part01.rar` both resolve to the same inner file,
    /// which is what makes the GUI need no new row kind.
    [[nodiscard]] StreamResolve resolve(const UsenetQueueItem& item, int fileIndex,
                                        qint64 wantOffset);

    /// Drop every cached header and set. Volume paths move under us when
    /// sealFile() renames and when post-processing repairs, and a stale path is
    /// indistinguishable from a missing file.
    void invalidate();

private:
    struct SetInfo {
        QList<int> volumes;        ///< NZB file indices, in volume order
        bool isRar = false;
        bool uniform = false;      ///< the identity held; virtual offsets are computable
        bool uniformDecided = false;
        qint64 p1 = 0, p2 = 0, pN = 0;
        QString innerName;
        qint64 totalSize = 0;
        bool refused = false;
        QString reason;
    };

    [[nodiscard]] static QString fileNameOf(const UsenetQueueItem& item, int fileIndex);
    [[nodiscard]] static QList<int> splitMembersOf(const UsenetQueueItem& item, int fileIndex);
    [[nodiscard]] static bool readIfAvailable(const UsenetQueueItem& item, int fileIndex,
                                              qint64 offset, qint64 length, QByteArray& out);

    [[nodiscard]] StreamResolve resolveRawFile(const UsenetQueueItem& item, int fileIndex) const;
    [[nodiscard]] StreamResolve resolveSplitSet(const UsenetQueueItem& item,
                                                const QList<int>& members) const;
    [[nodiscard]] StreamResolve resolveRarSet(const UsenetQueueItem& item, SetInfo& set,
                                              qint64 wantOffset);

    /// Parse volume @p slot's header, or report what it needs. Cached.
    [[nodiscard]] bool volumeHeader(const UsenetQueueItem& item, int fileIndex,
                                    StreamResolve& out);

    QHash<QString, SetInfo> m_sets;   ///< keyed by the set's base name
    QHash<int, RarVolume> m_volumes;  ///< parsed headers, keyed by NZB file index
};

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
