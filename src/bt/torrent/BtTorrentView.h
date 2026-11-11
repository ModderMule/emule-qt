#pragma once

/// @file BtTorrentView.h
/// @brief libtorrent status → the plain rows of BtTypes.h. Every std::string ↔ QString
/// crossing of a torrent is here (research doc §5.5 trap 5). Internal to src/bt.

#include "BtTypes.h"

#include <libtorrent/announce_entry.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/info_hash.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <vector>

namespace eMule::bt {

class BtTorrentView {
public:
    /// The id of a torrent (BtIds).
    static QString idOf(const libtorrent::info_hash_t& hashes);
    /// "<52 base32 chars>.b32.i2p" of an I2P destination hash.
    static QString i2pAddressOf(const libtorrent::sha256_hash& destination);

    static BtTorrentRow row(const libtorrent::torrent_status& st);
    /// Rows of @p ti's files; progress and priorities by file index, missing = 0 / normal.
    /// @p avail = peers per piece (piece_availability); empty: availability N/A.
    /// Plain data in: the handle's own calls wait for libtorrent's thread (report 27)
    static QList<BtFileRow> files(const libtorrent::torrent_info& ti, const std::vector<std::int64_t>& progress,
                                  const std::vector<libtorrent::download_priority_t>& priorities,
                                  const std::vector<int>& avail = {});
    static QList<BtPeerRow> peers(const std::vector<libtorrent::peer_info>& list);
    /// The Flags column of a peer, as qBittorrent's (d/D, u/U, K, ?, ...).
    static QString peerFlags(const libtorrent::peer_info& p);
    static QList<BtTrackerRow> trackers(const std::vector<libtorrent::announce_entry>& list);
    /// Text of a tracker error; SAM's terse I2P results explained.
    static QString errorText(const libtorrent::error_code& ec);
    /// What a loaded .torrent holds, without a session.
    static BtTorrentPreview preview(const libtorrent::add_torrent_params& atp);

    /// Peers that have each piece: up to `buckets` values, each the rarest piece of
    /// its bucket, capped at 255. Empty without metadata.
    static QByteArray availabilityMap(const std::vector<int>& avail, int buckets = 256);

    /// The part map of a row: up to `buckets` values 0..255.
    static QByteArray pieceMap(const libtorrent::typed_bitfield<libtorrent::piece_index_t>& pieces, int buckets = 256);

private:
    /// share of @p file's pieces with a peer count > 0; -1 when @p avail is empty
    static double fileAvailability(const libtorrent::file_storage& fs, libtorrent::file_index_t file,
                                   const std::vector<int>& avail);
};

} // namespace eMule::bt
