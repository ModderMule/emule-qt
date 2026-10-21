#pragma once

/// @file BtAddSource.h
/// @brief One entry point from what the user gave to libtorrent's add_torrent_params:
/// .torrent bytes or a magnet link (research doc §7.1 rule 4). Internal to src/bt.

#include <libtorrent/add_torrent_params.hpp>

#include <QByteArray>
#include <QString>

#include <expected>

namespace eMule::bt {

class BtAddSource {
public:
    /// A .torrent larger than this is refused (libtorrent's own limit is 100 MiB).
    static constexpr qsizetype kMaxTorrentFile = 64 * 1024 * 1024;

    /// The bytes of a .torrent file. Error: an English reason.
    static std::expected<libtorrent::add_torrent_params, QString> fromTorrentFile(const QByteArray& data);

    /// A magnet link with `xt=urn:btih:` or `urn:btmh:`. An ed2k magnet is refused.
    static std::expected<libtorrent::add_torrent_params, QString> fromMagnet(const QString& uri);

    /// Whether text is a BitTorrent magnet link (and not an ed2k one).
    static bool isBtMagnet(const QString& text);
};

} // namespace eMule::bt
