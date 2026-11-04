#pragma once

/// @file BtShareGuard.h
/// @brief Keeps files BitTorrent is still writing out of the eD2K / ed3k share (#124).
///
/// libtorrent writes into the download dir under the final names, so the share scan
/// would hash half-done files. Blocked: an unfinished torrent's whole content; once
/// done, only its files left incomplete (skipped ones). Fed by the session's signals.

#ifdef EMULE_HAVE_BITTORRENT

#include "BtTypes.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace eMule {

class BtShareGuard {
public:
    /// The torrent's files, asked only when it turns done.
    using FilesFn = std::function<QList<bt::BtFileRow>()>;

    struct Change {
        QStringList released;    ///< folders to rescan: their files may be shared now
        bool blockedMore = false;  ///< drop shared files that are blocked now
    };

    /// The torrent as it is now; nullopt = removed (its blocks stay for the session).
    Change update(const QString& id, const std::optional<bt::BtTorrentRow>& row, const FilesFn& files);
    [[nodiscard]] bool blocks(const QString& filePath) const;

private:
    struct Entry {
        bool done = false;
        QStringList paths;   ///< as on disk
        QStringList keys;    ///< SharedFileList::pathKey of each
    };
    QHash<QString, Entry> m_entries;
};

} // namespace eMule

#endif // EMULE_HAVE_BITTORRENT
