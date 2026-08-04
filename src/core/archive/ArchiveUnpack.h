#pragma once

/// @file ArchiveUnpack.h
/// @brief Transparent unwrapping of downloaded config files (ipfilter.dat, nodes.dat,
///        server.met) that were published compressed.
///
/// Public lists are usually served as `.gz` or `.zip`, so every "update from URL" path
/// has to unpack before it can parse. Detection is by content, never by URL extension —
/// the same rule MFC follows (srchybrid/PPgSecurity.cpp:238-340, which sniffs ZIP, then
/// RAR, then GZIP). Backed by libarchive, so gzip, zip, bz2, xz, 7z, rar and tar all
/// work through one call.
///
/// This is a sibling of ArchiveReader rather than a method on it for two reasons: it
/// reads from memory (callers hold the HTTP body already), and it enables libarchive's
/// `raw` format, which ArchiveReader must not have — see the note in the .cpp.

#include "utils/Types.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace eMule {

/// Ceiling on the unpacked size. These are config files inflated from untrusted network
/// data, so a decompression bomb is refused rather than allowed to exhaust memory.
inline constexpr uint64 kMaxUnpackedBytes = 64ull * 1024 * 1024;

struct UnwrapResult {
    QByteArray data;              ///< Member contents, or the input unchanged.
    QString    entryName;         ///< Member taken; empty when passed through.
    bool       wasArchive = false;///< True only when @p data came out of an archive.
    QString    error;             ///< Set when an archive was recognised but not usable.
};

/// Unwrap a downloaded payload.
///
/// Input that is not an archive — including plain text and binary server.met — is
/// returned byte-for-byte, so this is safe to apply unconditionally to any download.
/// A recognised archive yields the first entry matching @p preferredNames (compared
/// case-insensitively against the member's file name), else its largest regular file.
///
/// On failure @p error is set and @p data is left empty; callers should treat that as a
/// failed download rather than silently falling back to the compressed bytes.
[[nodiscard]] UnwrapResult unwrapDownload(const QByteArray& data,
                                          const QStringList& preferredNames = {},
                                          uint64 maxBytes = kMaxUnpackedBytes);

} // namespace eMule
