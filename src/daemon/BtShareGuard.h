#pragma once

/// @file BtShareGuard.h
/// @brief Keeps files BitTorrent is still writing out of the eD2K / ed3k share (#124).
///
/// libtorrent writes into the download dir under the final names, so the share scan
/// would hash half-done files. Blocked: an unfinished torrent's whole content; once
/// done, only its files left incomplete (skipped ones). Fed by the session's signals.
/// btShareMode (#177): Off keeps every torrent's content out, finished or not; Public
/// keeps private tracker (ALT) and I2P torrents out.

#ifdef EMULE_HAVE_BITTORRENT

#include "BtTypes.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace eMule {

/// btShareMode pref: which finished torrents are shared on eD2K/Kad (and ed3k)
enum class BtShareMode : int {
    Off = 0,      ///< none
    Public = 1,   ///< not private (ALT), not I2P
    All = 2,      ///< default
};

class BtShareGuard {
public:
    /// The torrent's files, asked only when it turns done; nullopt: not known yet, the
    /// torrent comes again (BtSession::currentFiles)
    using FilesFn = std::function<std::optional<QList<bt::BtFileRow>>()>;

    struct Change {
        QStringList released;    ///< paths to rescan with their folders below (SharedFileList::rescanTrees)
        bool blockedMore = false;  ///< drop shared files that are blocked now
        /// its complete files, once, when a torrent seen downloading turns done (#131):
        /// shared one by one outside a shared folder
        QStringList finished;
    };

    /// The torrent as it is now; nullopt = removed (its blocks stay for the session).
    Change update(const QString& id, const std::optional<bt::BtTorrentRow>& row, const FilesFn& files);
    [[nodiscard]] bool blocks(const QString& filePath) const;
    /// Finished torrents the mode leaves out are blocked too (btShareMode). False when unchanged.
    bool setShareMode(BtShareMode mode);
    [[nodiscard]] BtShareMode shareMode() const { return m_mode; }

private:
    struct Entry {
        bool done = false;
        bool downloading = false;   ///< settled and not done: done next = just finished
        QStringList paths;   ///< as on disk
        QStringList keys;    ///< SharedFileList::pathKey of each
    };
    QHash<QString, Entry> m_entries;
    struct Root {
        QString key;              ///< pathKey of the torrent's file or folder
        bool restricted = false;  ///< private tracker (ALT) or I2P: out unless All
        bool operator==(const Root&) const = default;
    };
    [[nodiscard]] bool rootBlocked(const Root& r) const;
    /// each torrent's root, kept after removal for the session
    QHash<QString, Root> m_roots;
    BtShareMode m_mode = BtShareMode::All;
};

} // namespace eMule

#endif // EMULE_HAVE_BITTORRENT
