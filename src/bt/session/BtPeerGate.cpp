#include "session/BtPeerGate.h"

#include <libtorrent/error_code.hpp>
#include <libtorrent/operations.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/torrent_handle.hpp>

#include <atomic>
#include <map>
#include <mutex>
#include <vector>

namespace eMule::bt {

namespace {

class GateTorrent final : public lt::torrent_plugin {
public:
    std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const& pc) override
    {
        m_peers.push_back(pc);
        return {};
    }

    void tick() override
    {
        // network thread: drop closed connections, then the unwanted kind
        std::erase_if(m_peers, [](const lt::peer_connection_handle& pc) { return !pc.native_handle(); });
        const auto which = static_cast<BtPeerGate::Drop>(m_drop.exchange(0));
        if (which == BtPeerGate::Drop::None)
            return;
        for (lt::peer_connection_handle& pc : m_peers) {
            if (pc.is_disconnecting())
                continue;
            lt::peer_info info;
            pc.get_peer_info(info);
            const bool i2p = bool(info.flags & lt::peer_info::i2p_socket);
            if (i2p == (which == BtPeerGate::Drop::I2p))
                pc.disconnect(lt::errors::make_error_code(lt::errors::torrent_aborted), lt::operation_t::bittorrent);
        }
    }

    void request(BtPeerGate::Drop which) { m_drop.store(static_cast<int>(which)); }

private:
    std::vector<lt::peer_connection_handle> m_peers;   ///< network thread only
    std::atomic<int> m_drop{0};
};

} // namespace

struct BtPeerGate::State {
    std::mutex mutex;
    std::map<lt::info_hash_t, std::weak_ptr<GateTorrent>> torrents;
};

namespace {

class GatePlugin final : public lt::plugin {
public:
    explicit GatePlugin(std::shared_ptr<BtPeerGate::State> s) : m_state(std::move(s)) {}

    std::shared_ptr<lt::torrent_plugin> new_torrent(lt::torrent_handle const& h, lt::client_data_t) override
    {
        auto t = std::make_shared<GateTorrent>();
        std::scoped_lock lock(m_state->mutex);
        std::erase_if(m_state->torrents, [](const auto& e) { return e.second.expired(); });
        m_state->torrents[h.info_hashes()] = t;
        return t;
    }

private:
    std::shared_ptr<BtPeerGate::State> m_state;
};

} // namespace

BtPeerGate::BtPeerGate()
    : m_state(std::make_shared<State>())
{
}

std::shared_ptr<lt::plugin> BtPeerGate::plugin() const
{
    return std::make_shared<GatePlugin>(m_state);
}

void BtPeerGate::drop(const lt::info_hash_t& hashes, Drop which) const
{
    std::scoped_lock lock(m_state->mutex);
    if (const auto it = m_state->torrents.find(hashes); it != m_state->torrents.end())
        if (const auto t = it->second.lock())
            static_cast<GateTorrent*>(t.get())->request(which);
}

} // namespace eMule::bt
