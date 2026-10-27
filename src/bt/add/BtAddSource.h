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

    /// A magnet link with `xt=urn:btih:` or `urn:btmh:`, or an `ed3k:file` link with `bt`
    /// (torrent.md §7: as a magnet of that infohash). An ed2k magnet is refused.
    static std::expected<libtorrent::add_torrent_params, QString> fromMagnet(const QString& uri);

    /// Whether text is a BitTorrent magnet link (and not an ed2k one), or an `ed3k:file`
    /// link of a torrent set (with `bt`).
    static bool isBtMagnet(const QString& text);

    /// The `ed3k:file` link a torrent is joined under (torrent.md §4.1): the link itself,
    /// or a magnet's `xs` when it is one. Empty: none. Validated by the ed3k side.
    static QString ed3kLinkOf(const QString& uri);
};

} // namespace eMule::bt
