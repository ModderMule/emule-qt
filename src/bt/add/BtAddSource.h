#pragma once

/// @file BtAddSource.h
/// @brief One entry point from what the user gave to libtorrent's add_torrent_params:
/// .torrent bytes or a magnet link (research doc §7.1 rule 4). Internal to src/bt.

#include "BtTypes.h"

#include <libtorrent/add_torrent_params.hpp>

#include <QByteArray>
#include <QString>

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace eMule::bt {

class BtAddSource {
public:
    /// A .torrent larger than this is refused (libtorrent's own limit is 100 MiB).
    static constexpr qsizetype kMaxTorrentFile = 64 * 1024 * 1024;

    /// The bytes of a .torrent file. Error: an English reason.
    static std::expected<libtorrent::add_torrent_params, QString> fromTorrentFile(const QByteArray& data);

    /// A magnet link with `xt=urn:btih:` or `urn:btmh:`, or an `ed3k:file` link with `bt`
    /// (torrent.md §7: as a magnet of that infohash). An ed2k magnet is refused.
    static std::expected<libtorrent::add_torrent_params, QString> fromMagnet(const QString& uri);

    /// Whether text is a BitTorrent magnet link (and not an ed2k one), or an `ed3k:file`
    /// link of a torrent set (with `bt`).
    static bool isBtMagnet(const QString& text);

    /// The `ed3k:file` link a torrent is joined under (torrent.md §4.1): the link itself,
    /// or a magnet's `xs` when it is one. Empty: none. Validated by the ed3k side.
    static QString ed3kLinkOf(const QString& uri);

    /// The I2P policy of a new torrent. I2P only when asked (@p i2pOnly; nullopt = the
    /// settings' i2pOnlyNew), or when it has an .i2p tracker: then i2p_torrent is set.
    /// While I2P is on, an I2P torrent gets the I2P trackers, and in mixed mode any
    /// non-private one does. Returns whether the torrent is I2P only.
    static bool applyI2p(libtorrent::add_torrent_params& atp, const BtSettings& s, std::optional<bool> i2pOnly);

    /// Stop after metadata, so files can be picked before anything downloads. When asked
    /// (@p stop; nullopt = the settings' stopAfterMetadata): a magnet without metadata
    /// skips every file until it arrives (default_dont_download) and true is returned,
    /// the session stops it then. A .torrent has its file list already: added paused.
    static bool applyStopAfterMetadata(libtorrent::add_torrent_params& atp, const BtSettings& s,
                                       std::optional<bool> stop);

    /// An announce URL on an .i2p host.
    static bool isI2pTracker(const std::string& url);

    /// Each .i2p tracker in a tier of its own, after the others. libtorrent announces
    /// only the first working tracker of a tier unless announce_to_all_trackers, so a
    /// shared I2P tier stopped at the first answer. Returns whether a tier moved.
    static bool spreadI2pTiers(const std::vector<std::string>& urls, std::vector<int>& tiers);
};

} // namespace eMule::bt
