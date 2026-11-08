#pragma once

/// @file MetaRowFiles.h
/// @brief A meta search row (torrent/Usenet/web/container) and which file of its
/// metafile it stands for. Shared by the IPC handlers and the web page's grabs.

#include "MetaSearchService.h"

#include "enodemeta/MetaHash.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QCborArray>
#include <QString>

#include <optional>

namespace eMule {

class SearchFile;

namespace usenet {
struct NzbInfo;
}

/// What a fetch needs from a meta row, live or restored.
struct MetaRow {
    QByteArray hash16;
    QString catalogId;
    QString name;
    enodemeta::Kind kind = enodemeta::Kind::Unspecified;
    std::optional<MetaSearchService::Target> target;
    uint32 fileIndex = enodemeta::kFileIndexWholeSet32;   ///< file row's file, else whole set
    QString filePath;   ///< FT_META_FILEPATH: an NZB's subject, over the index

    [[nodiscard]] bool isNzb() const { return kind == enodemeta::Kind::Nzb; }
    [[nodiscard]] bool isWholeSet() const { return fileIndex == enodemeta::kFileIndexWholeSet32; }
};

/// A live search row as a MetaRow.
[[nodiscard]] MetaRow metaRowOf(const SearchFile& f, const QByteArray& hash16);

/// File index from meta-hash bytes 4..5 (uint16 LE), for a row with no SearchFile.
[[nodiscard]] uint32 hashFileIndex(const QByteArray& hash16);

/// A sibling row of the same search, by hash: live only, else from its hash.
[[nodiscard]] MetaRow siblingRow(const QString& hashHex, uint32 searchID);

/// The NZB file a file row stands for: subject first (NZB hashes are path
/// authoritative), then the index. nullopt: whole set, a par2 file, or no match.
[[nodiscard]] std::optional<int> resolveNzbFile(const usenet::NzbInfo& nzb, const MetaRow& r);

/// The torrent file a file row stands for, from BtBridge::inspectTorrent's `files`:
/// path first, then the index. nullopt: whole torrent, a pad file, or no match.
[[nodiscard]] std::optional<int> resolveTorrentFile(const QCborArray& files, const MetaRow& r);

/// Queue name for an NZB row: a file row takes its whole-set row's name, while
/// the search is live. Else the row's own name.
[[nodiscard]] QString releaseName(const MetaRow& row, uint32 searchID);

/// "Release.Name" + the kind's extension, safe as a file name. A container is
/// named by the format its content type says (enodemeta amendment 27).
[[nodiscard]] QString suggestedFileName(const MetaRow& r, const QString& contentType);

} // namespace eMule
