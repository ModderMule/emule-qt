#pragma once

/// @file BtLocateFiles.h
/// @brief #224: files of a torrent found in folders, by size, proved by the torrent's own
/// hashes (v2: file root; v1: every piece touching the file, neighbours included). No blake3
/// needed. Blocking: run it on a worker. Reads only.

#include <libtorrent/fwd.hpp>

#include <QString>
#include <QStringList>

#include <atomic>
#include <map>
#include <vector>

namespace eMule::bt {

/// A file not proved whole: its proved whole pieces, copied into the download (add_piece).
struct BtLocateCopy {
    QString path;
    std::vector<int> pieces;
};

struct BtLocateScan {
    int files = 0;        ///< files looked at
    int candidates = 0;   ///< of a wanted size
    int hashed = 0;       ///< candidates hashed
    int partial = 0;      ///< a candidate exists, nothing of it proved
    std::map<int, QString> found;        ///< file index → proved file
    std::map<int, BtLocateCopy> copies;  ///< file index → whole pieces proved (edge unverifiable)
};

/// @p targets: file indexes to look for. @p current: where every file is now (index → path);
/// a candidate at its own path is skipped, neighbours are read from there.
[[nodiscard]] BtLocateScan locateTorrentFiles(const libtorrent::torrent_info& ti, const QStringList& folders,
                                              const std::vector<int>& targets, const std::map<int, QString>& current,
                                              const std::atomic<bool>* cancel = nullptr);

/// Piece @p piece read from @p path (file @p file; pad files as zeros); empty on a read error.
[[nodiscard]] std::vector<char> readLocatedPiece(const libtorrent::torrent_info& ti, int piece, int file, const QString& path);

} // namespace eMule::bt
