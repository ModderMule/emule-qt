#include "session/BtPeerGate.h"

#include <libtorrent/error_code.hpp>
#include <libtorrent/operations.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/torrent_handle.hpp>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>

namespace eMule::bt {

namespace {

[[nodiscard]] bool isI2p(const lt::peer_connection_handle& pc)
{
    lt::peer_info info;
    pc.get_peer_info(info);
    return bool(info.flags & lt::peer_info::i2p_socket);
}

void close(lt::peer_connection_handle& pc)
{
    if (!pc.is_disconnecting())
        pc.disconnect(lt::errors::make_error_code(lt::errors::torrent_aborted), lt::operation_t::bittorrent);
}

/// a direct peer of a strict torrent: closed as soon as libtorrent lets us
class Refuse final : public lt::peer_plugin {
public:
    explicit Refuse(lt::peer_connection_handle pc) : m_pc(std::move(pc)) {}
    void on_connected() override { close(m_pc); }
    bool on_handshake(lt::span<char const>) override
    {
        close(m_pc);
        return true;
    }

private:
    lt::peer_connection_handle m_pc;
};

class GateTorrent final : public lt::torrent_plugin {
public:
    explicit GateTorrent(bool strict) : m_strict(strict) {}

    std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const& pc) override
    {
        m_peers.push_back(pc);
        if (m_strict.load() && !isI2p(pc))
            return std::make_shared<Refuse>(pc);
        return {};
    }

    void tick() override
    {
        // network thread: drop closed connections, then the unwanted kind
        std::erase_if(m_peers, [](const lt::peer_connection_handle& pc) { return !pc.native_handle(); });
        auto which = static_cast<BtPeerGate::Drop>(m_drop.exchange(0));
        if (m_strict.load())
            which = BtPeerGate::Drop::Direct;
        if (which == BtPeerGate::Drop::None)
            return;
        for (lt::peer_connection_handle& pc : m_peers)
            if (!pc.is_disconnecting() && isI2p(pc) == (which == BtPeerGate::Drop::I2p))
                close(pc);
    }

    void request(BtPeerGate::Drop which) { m_drop.store(static_cast<int>(which)); }
    void setStrict(bool on) { m_strict.store(on); }

private:
    std::vector<lt::peer_connection_handle> m_peers;   ///< network thread only
    std::atomic<int> m_drop{0};
    std::atomic<bool> m_strict;
};

/// one torrent: a v1 or v2 hash in common (a magnet's v1 before a hybrid's metadata)
[[nodiscard]] bool same(const lt::info_hash_t& a, const lt::info_hash_t& b)
{
    return (a.has_v1() && b.has_v1() && a.v1 == b.v1) || (a.has_v2() && b.has_v2() && a.v2 == b.v2);
}

} // namespace

struct BtPeerGate::State {
    std::mutex mutex;
    std::map<lt::info_hash_t, std::weak_ptr<GateTorrent>> torrents;
    std::vector<lt::info_hash_t> strict;

    [[nodiscard]] bool isStrict(const lt::info_hash_t& h) const
    {
        return std::ranges::any_of(strict, [&h](const lt::info_hash_t& s) { return same(s, h); });
    }
};

namespace {

class GatePlugin final : public lt::plugin {
public:
    explicit GatePlugin(std::shared_ptr<BtPeerGate::State> s) : m_state(std::move(s)) {}

    std::shared_ptr<lt::torrent_plugin> new_torrent(lt::torrent_handle const& h, lt::client_data_t) override
    {
        std::scoped_lock lock(m_state->mutex);
        auto t = std::make_shared<GateTorrent>(m_state->isStrict(h.info_hashes()));
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

void BtPeerGate::setStrict(const lt::info_hash_t& hashes, bool on) const
{
    std::scoped_lock lock(m_state->mutex);
    std::erase_if(m_state->strict, [&hashes](const lt::info_hash_t& s) { return same(s, hashes); });
    if (on)
        m_state->strict.push_back(hashes);
    for (const auto& [h, weak] : m_state->torrents)
        if (same(h, hashes))
            if (const auto t = weak.lock())
                static_cast<GateTorrent*>(t.get())->setStrict(on);
}

} // namespace eMule::bt
