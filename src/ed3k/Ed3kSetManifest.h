#pragma once

/// @file Ed3kSetManifest.h
/// @brief A set manifest read for showing and choosing members (file-set.md §1, §2.3): title,
/// id, seq and the members with their paths. Pure, no engine: the core checked the manifest
/// already (SetManifest, SetInner); this only reads it, it checks nothing beyond the framing.
///
/// Also the local names of member paths (§2.2: names a file system cannot hold are changed
/// by the reader) and a path kept below the set's directory.

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace eMule::ed3k {

struct Ed3kSetEntry {
    ::ed3k::Root root;
    std::uint64_t size = 0;
    /// "dir/sub/name", "/" between components
    QString path;
    QByteArray ed2k;          ///< 16 B or empty
    bool isSet = false;       ///< an inner set's manifest (§2.5)
    bool mount = false;       ///< a directory another set fills (multi-author-set.md §2)
    std::uint32_t setFiles = 0;
    std::uint64_t setSize = 0;
    /// a sealed member: root and size are the sealed file's (nonce ‖ cipher, 12 bytes more):
    /// with the epoch key of a paid set (subscription.md §8.1 `epoch`), or k_data of a hidden
    /// set's link (hidden-set.md §4.2 `sealed`)
    std::optional<std::uint64_t> epoch;
    bool sealed = false;
    [[nodiscard]] bool isSealed() const { return epoch.has_value() || sealed; }
};

struct Ed3kSetManifest {
    QString title;
    QByteArray id;
    std::uint64_t seq = 0;
    QByteArray key;           ///< the author's key (32 B) or empty
    QStringList dirs;
    std::vector<Ed3kSetEntry> files;
    QByteArray offer;         ///< FileSet.offer (subscription.md §3): a serialised SignedOffer; empty = not paid
    QStringList exts;         ///< FileSet.exts (§5.3): the author's suggested file types, lower case, no dot

    /// "ed3kset" ‖ 0x01 ‖ SetFile; nullopt: not one.
    [[nodiscard]] static std::optional<Ed3kSetManifest> parse(const QByteArray& file);
};

/// @p path below @p dir with names every file system holds (§2.2); empty: it would leave
/// @p dir (never for a path the core accepted).
[[nodiscard]] QString ed3kSetLocalPath(const QString& dir, const QString& path);
/// File types as FileSet.exts holds them (§5.3): lower case, no dot ("*.mkv", ".MKV" → "mkv"),
/// 1-16 letters or digits, no repeats, at most 64. Bad entries dropped.
[[nodiscard]] QStringList ed3kNormalizeExts(const QStringList& exts);
/// Whether @p fileName ends in one of @p exts; "rar" also takes .r00-.r99 volumes, "zip", "rar"
/// and "7z" the .001 splits. Empty @p exts: every file.
[[nodiscard]] bool ed3kExtMatches(const QString& fileName, const QStringList& exts);
/// The local default of a followed set without the author's file types: video, audio,
/// archives, images, subtitles, text; no scripts or programs.
[[nodiscard]] QStringList ed3kDefaultSetExts();
/// One path component made safe (reserved names, `:`, trailing dot or space, control chars).
[[nodiscard]] QString ed3kSafeName(const QString& name);

} // namespace eMule::ed3k
