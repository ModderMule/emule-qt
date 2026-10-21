#pragma once

/// @file TorrentLinks.h
/// @brief What counts as a BitTorrent link or .torrent file, in one place.
///
/// ExternalLinkHandler, Ed2kLinkImporter, the command line and the window drop all
/// ask the same question. A magnet goes to BitTorrent only when it names a BT info
/// hash (urn:btih / urn:btmh) and no eD2K file: an eD2K magnet, hybrid or not,
/// keeps going to Transfers as it always did.
///
/// Pure string work, no IPC and no core, so it is testable on its own.

#include <QString>
#include <QStringList>

class QMimeData;

namespace eMule::gui {

/// True for a magnet that BitTorrent should take: xt=urn:btih:/urn:btmh: and no
/// eD2K hash (xt=urn:ed2k:, urn:ed2khash:, or an ed2k:// xs/as source).
[[nodiscard]] bool isBitTorrentMagnet(const QString& link);

/// The BitTorrent magnets among @p text's lines/tokens, in order.
[[nodiscard]] QStringList bitTorrentMagnetsIn(const QString& text);

/// A name for a magnet in messages: its dn, else its info hash, else the link.
[[nodiscard]] QString magnetDisplayName(const QString& link);

/// Path (or file: URL) ends in .torrent.
[[nodiscard]] bool isTorrentFilePath(const QString& path);

/// Local, existing .torrent files in a drop. Empty = not ours.
[[nodiscard]] QStringList torrentDropFiles(const QMimeData* mime);

/// "magnet:?xt=urn:btih:<hashV1>&dn=<name>", or empty when @p hashV1 is empty.
[[nodiscard]] QString torrentMagnetLink(const QString& hashV1, const QString& name);

} // namespace eMule::gui
