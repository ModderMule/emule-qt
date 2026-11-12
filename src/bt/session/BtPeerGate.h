#pragma once

/// @file BtPeerGate.h
/// @brief Drops a torrent's I2P or direct peers after its I2P mode changed.
///
/// libtorrent's set_flags(i2p_torrent) only gates new peers; the connected ones stay.
/// A session plugin keeps each torrent's connections and disconnects the unwanted kind
/// at its next tick, on libtorrent's network thread.

#include <libtorrent/extensions.hpp>
#include <libtorrent/info_hash.hpp>

#include <memory>

namespace eMule::bt {

class BtPeerGate {
public:
    enum class Drop : int { None = 0, I2p = 1, Direct = 2 };

    BtPeerGate();

    /// The plugin for session_params::extensions; once per session.
    [[nodiscard]] std::shared_ptr<lt::plugin> plugin() const;
    /// Any thread: the torrent drops its @p which peers at the next tick.
    void drop(const lt::info_hash_t& hashes, Drop which) const;

    struct State;

private:
    std::shared_ptr<State> m_state;
};

} // namespace eMule::bt
