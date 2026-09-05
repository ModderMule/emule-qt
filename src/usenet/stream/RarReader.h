#pragma once

/// @file RarReader.h
/// @brief Header-only RAR parsing, enough to map a stored archive's payload.
///
/// This exists because nothing else can do the job. `core/archive/ArchiveReader`
/// is path-based (`archive_read_open_filename`) and exposes neither the
/// compression method, the packed size, nor the offset of an entry's data —
/// those live only in libarchive's private RAR structs. And it could not be
/// pointed at a half-downloaded volume anyway.
///
/// We are **mapping, not extracting**. For a stored (`-m0`) archive the payload
/// inside a volume is a byte-identical copy of a slice of the original file, so
/// a read of the original reduces to a read of one volume at a known offset.
/// That is the whole of Tier B; see docs/UsenetModule-Research.local.md §7.2.
///
/// Three properties the callers depend on:
///
///   - **Pure.** A QByteArray in, a struct out. No file IO, no event loop, no
///     Qt object affinity — so it is unit-testable against crafted fixtures and
///     runs wherever the caller happens to be.
///   - **Truncation-safe.** Every field read is bounds-checked and yields
///     `NeedMoreBytes`, never a read past the end. The caller is routinely
///     holding only the first article of a volume.
///   - **Header CRCs are not verified.** A wrong map fails the `=ypart`
///     cross-check downstream, and refusing an archive over a CRC we do not
///     need would only turn a playable release into an unplayable one.

#include <QByteArray>
#include <QList>
#include <QString>

namespace eMule::usenet {

/// One file inside one volume. A file split across volumes appears once per
/// volume it touches, each time with that volume's own `packedSize`.
struct RarEntry {
    QString name;
    bool stored = false;        ///< RAR4 method 0x30 / RAR5 method bits == 0
    bool encrypted = false;     ///< per-entry data encryption
    bool splitBefore = false;   ///< continues from the previous volume
    bool splitAfter = false;    ///< continues into the next volume
    qint64 packedSize = 0;      ///< payload length in THIS volume
    qint64 unpackedSize = 0;    ///< whole-file size; only the first volume states it
    qint64 headerOffset = 0;    ///< where this entry's own header block starts
    qint64 dataOffset = 0;      ///< where the payload starts in this volume file
};

enum class RarParse {
    Ok,
    NeedMoreBytes,  ///< the buffer ends mid-header; ask for more and retry
    NotRar,         ///< no RAR marker
    Unsupported,    ///< RAR, but not mappable — see RarVolume::reason
};

/// Which dialect a volume speaks. Needed to resume mid-volume, where there is
/// no marker to tell them apart.
enum class RarFormat { Unknown, Rar4, Rar5 };

struct RarVolume {
    RarParse status = RarParse::NeedMoreBytes;

    RarFormat format = RarFormat::Unknown;

    /// Absolute offset of the first block this parse did *not* consume. The
    /// caller resumes there with parseRarBlocks() once those bytes land — which
    /// is how a file whose header sits past the probe window is ever reached.
    qint64 nextOffset = 0;

    /// An end-of-archive block was seen: this volume holds nothing further.
    bool endOfArchive = false;

    bool isFirstVolume = false;

    /// From RAR5's main header. **-1 means the format did not say**, which is
    /// the normal outcome for RAR4 — it is not an error and not volume zero.
    int volumeNumber = -1;

    bool headersEncrypted = false;
    bool solid = false;

    QList<RarEntry> entries;

    /// Why an Unsupported volume was refused, in words a user can act on. Empty
    /// for every other status.
    QString reason;
};

/// Parse the head of one volume file. @p head need only be long enough to cover
/// the marker, the main header and the first file header — see
/// kRarHeaderProbeBytes. Every entry whose header fits is reported, not just the
/// first: a release that packs a small `.nfo` ahead of the feature puts both in
/// the same window.
[[nodiscard]] RarVolume parseRarVolume(const QByteArray& head);

/// Resume parsing at @p windowStart, which must be a block boundary — the
/// `nextOffset` of an earlier parse of the same volume, or an entry's
/// `dataOffset + packedSize`. There is no marker mid-volume, so @p format has to
/// come from the volume's own head parse.
///
/// Unlike parseRarVolume() this **verifies header CRCs**. The head of a volume
/// sits at a known address and a wrong map there fails the `=ypart` cross-check
/// downstream; a resumed block's address was computed from a previously trusted
/// packedSize, where an error would propagate silently into every later entry.
[[nodiscard]] RarVolume parseRarBlocks(RarFormat format, const QByteArray& window,
                                       qint64 windowStart);

/// How much of a volume to hand parseRarVolume(). Ample for a marker, a main
/// header and a file header carrying a long name; still far inside the first
/// article of any real posting.
inline constexpr int kRarHeaderProbeBytes = 8192;

} // namespace eMule::usenet
