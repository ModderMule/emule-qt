#pragma once

/// @file BtTorrentView.h
/// @brief libtorrent status → the plain rows of BtTypes.h. Every std::string ↔ QString
/// crossing of a torrent is here (research doc §5.5 trap 5). Internal to src/bt.

#include "BtTypes.h"

#include <libtorrent/info_hash.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>

namespace eMule::bt {

class BtTorrentView {
public:
    /// The id of a torrent (BtIds).
    static QString idOf(const libtorrent::info_hash_t& hashes);

    static BtTorrentRow row(const libtorrent::torrent_status& st);
    static QList<BtFileRow> files(const libtorrent::torrent_handle& h);
    static QList<BtPeerRow> peers(const libtorrent::torrent_handle& h);
    static QList<BtTrackerRow> trackers(const libtorrent::torrent_handle& h);
    /// What a loaded .torrent holds, without a session.
    static BtTorrentPreview preview(const libtorrent::add_torrent_params& atp);

    /// Peers that have each piece: up to `buckets` values, each the rarest piece of
    /// its bucket, capped at 255. Empty without metadata.
    static QByteArray availabilityMap(const libtorrent::torrent_handle& h, int buckets = 256);

    /// The part map of a row: up to `buckets` values 0..255.
    static QByteArray pieceMap(const libtorrent::typed_bitfield<libtorrent::piece_index_t>& pieces, int buckets = 256);
};

} // namespace eMule::bt
