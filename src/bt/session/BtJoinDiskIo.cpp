#include "session/BtJoinDiskIo.h"

#include <libtorrent/pread_disk_io.hpp>
#include <libtorrent/session_params.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>

namespace lt = libtorrent;

namespace eMule::bt {

namespace {

/// A digest that is not the one computed: every bit flipped.
template <class Hash>
Hash spoiled(Hash h)
{
    for (auto& b : h)
        b = static_cast<std::uint8_t>(~b);
    return h;
}

/// Read size for a piece handed to the hook: libtorrent's block.
constexpr int kReadBlock = 16 * 1024;

} // namespace

struct BtJoinDiskIo::Pending {
    std::function<void(lt::piece_index_t, lt::sha1_hash const&, lt::storage_error const&)> handler;
    lt::span<lt::sha256_hash> v2;
    lt::piece_index_t piece;
    lt::sha1_hash hash;

    void finish(bool spoil)
    {
        if (spoil)
            for (auto& block : v2)
                block = spoiled(block);
        handler(piece, spoil ? spoiled(hash) : hash, lt::storage_error{});
    }
};

/// Hands late verdicts back on libtorrent's tick.
class Pump final : public lt::plugin {
public:
    explicit Pump(std::function<BtJoinDiskIo*()> io) : m_io(std::move(io)) {}
    lt::feature_flags_t implemented_features() override { return tick_feature; }
    void on_tick() override
    {
        if (BtJoinDiskIo* io = m_io())
            io->pump();
    }

private:
    std::function<BtJoinDiskIo*()> m_io;
};

BtJoinDiskIo::BtJoinDiskIo(std::unique_ptr<lt::disk_interface> inner)
    : m_inner(std::move(inner))
{
}

BtJoinDiskIo::~BtJoinDiskIo()
{
    {
        std::scoped_lock lock(m_ready->mutex);
        m_ready->alive = false;
        m_ready->jobs.clear();
    }
    // inner holders first: they call back into m_inner
    std::scoped_lock lock(m_mutex);
    m_hooks.clear();
    m_hashOf.clear();
    m_filesOf.clear();
    m_holders.clear();
}

lt::disk_io_constructor_type BtJoinDiskIo::constructor(std::function<void(BtJoinDiskIo*)> made)
{
    return [made = std::move(made)](lt::io_context& ioc, lt::settings_interface const& sett, lt::counters& cnt) -> std::unique_ptr<lt::disk_interface> {
        auto io = std::make_unique<BtJoinDiskIo>(lt::pread_disk_io_constructor(ioc, sett, cnt));
        if (made)
            made(io.get());
        return io;
    };
}

std::shared_ptr<lt::plugin> BtJoinDiskIo::pumpPlugin(std::function<BtJoinDiskIo*()> io)
{
    return std::make_shared<Pump>(std::move(io));
}

void BtJoinDiskIo::pump()
{
    std::vector<std::function<void()>> jobs;
    {
        std::scoped_lock lock(m_ready->mutex);
        jobs.swap(m_ready->jobs);
    }
    for (auto& job : jobs)
        job();
}

void BtJoinDiskIo::attach(const lt::info_hash_t& torrent, std::shared_ptr<BtJoinHook> hook)
{
    std::scoped_lock lock(m_mutex);
    m_hooks[torrent.get_best()] = std::move(hook);
}

void BtJoinDiskIo::detach(const lt::info_hash_t& torrent)
{
    std::scoped_lock lock(m_mutex);
    m_hooks.erase(torrent.get_best());
}

bool BtJoinDiskIo::attached(const lt::info_hash_t& torrent) const
{
    std::scoped_lock lock(m_mutex);
    return m_hooks.contains(torrent.get_best());
}

lt::storage_holder BtJoinDiskIo::new_torrent(lt::storage_params const& p, std::shared_ptr<void> const& torrent)
{
    lt::storage_holder inner = m_inner->new_torrent(p, torrent);
    if (!inner)
        return {};
    const lt::storage_index_t idx = inner;
    std::scoped_lock lock(m_mutex);
    m_holders.insert_or_assign(idx, std::move(inner));
    m_hashOf.insert_or_assign(idx, p.info_hash);
    m_filesOf.insert_or_assign(idx, &p.files);
    return lt::storage_holder(idx, *this);
}

void BtJoinDiskIo::remove_torrent(lt::storage_index_t storage)
{
    lt::storage_holder inner;
    {
        std::scoped_lock lock(m_mutex);
        m_hashOf.erase(storage);
        m_filesOf.erase(storage);
        if (auto it = m_holders.find(storage); it != m_holders.end()) {
            inner = std::move(it->second);
            m_holders.erase(it);
        }
    }
    // dropped outside the lock: it calls m_inner->remove_torrent
}

void BtJoinDiskIo::async_read(lt::storage_index_t storage, lt::peer_request const& r,
                              std::function<void(lt::disk_buffer_holder, lt::storage_error const&)> handler, lt::disk_job_flags_t flags)
{
    m_inner->async_read(storage, r, std::move(handler), flags);
}

bool BtJoinDiskIo::async_write(lt::storage_index_t storage, lt::peer_request const& r, char const* buf, std::shared_ptr<lt::disk_observer> o,
                               std::function<void(lt::storage_error const&)> handler, lt::disk_job_flags_t flags)
{
    return m_inner->async_write(storage, r, buf, std::move(o), std::move(handler), flags);
}

void BtJoinDiskIo::async_hash(lt::storage_index_t storage, lt::piece_index_t piece, lt::span<lt::sha256_hash> v2, lt::disk_job_flags_t flags,
                              std::function<void(lt::piece_index_t, lt::sha1_hash const&, lt::storage_error const&)> handler)
{
    auto hook = hookOf(storage);
    if (!hook) {
        m_inner->async_hash(storage, piece, v2, flags, std::move(handler));
        return;
    }
    int pieceSize = 0;
    {
        std::scoped_lock lock(m_mutex);
        if (auto it = m_filesOf.find(storage); it != m_filesOf.end())
            pieceSize = it->second->piece_size(piece);
    }
    // v2 is the torrent's buffer and stays valid until the handler ran
    m_inner->async_hash(storage, piece, v2, flags,
                        [this, storage, pieceSize, flags, hook, v2, handler = std::move(handler)](
                            lt::piece_index_t p, lt::sha1_hash const& h, lt::storage_error const& err) mutable {
                            if (err || pieceSize <= 0) {
                                handler(p, h, err);
                                return;
                            }
                            // the hook judges the bytes: read them first (still on this thread)
                            readForHook(storage, pieceSize, hook, std::make_shared<Pending>(Pending{std::move(handler), v2, p, h}), flags);
                        });
}

void BtJoinDiskIo::async_hash2(lt::storage_index_t storage, lt::piece_index_t piece, int offset, lt::disk_job_flags_t flags,
                               std::function<void(lt::piece_index_t, lt::sha256_hash const&, lt::storage_error const&)> handler)
{
    // one v2 block, asked for hash requests of peers: the piece verdict is async_hash's
    m_inner->async_hash2(storage, piece, offset, flags, std::move(handler));
}

void BtJoinDiskIo::async_move_storage(lt::storage_index_t storage, std::string p, lt::move_flags_t flags,
                                      std::function<void(lt::status_t, std::string const&, lt::storage_error const&)> handler)
{
    m_inner->async_move_storage(storage, std::move(p), flags, std::move(handler));
}

void BtJoinDiskIo::async_release_files(lt::storage_index_t storage, std::function<void()> handler)
{
    m_inner->async_release_files(storage, std::move(handler));
}

void BtJoinDiskIo::async_check_files(lt::storage_index_t storage, lt::add_torrent_params const* resume_data,
                                     lt::aux::vector<std::string, lt::file_index_t> links,
                                     std::function<void(lt::status_t, lt::storage_error const&)> handler)
{
    m_inner->async_check_files(storage, resume_data, std::move(links), std::move(handler));
}

void BtJoinDiskIo::async_stop_torrent(lt::storage_index_t storage, std::function<void()> handler)
{
    m_inner->async_stop_torrent(storage, std::move(handler));
}

void BtJoinDiskIo::async_rename_file(lt::storage_index_t storage, lt::file_index_t index, std::string name,
                                     std::function<void(std::string const&, lt::file_index_t, lt::storage_error const&)> handler)
{
    m_inner->async_rename_file(storage, index, std::move(name), std::move(handler));
}

void BtJoinDiskIo::async_delete_files(lt::storage_index_t storage, lt::remove_flags_t options, std::function<void(lt::storage_error const&)> handler)
{
    m_inner->async_delete_files(storage, options, std::move(handler));
}

void BtJoinDiskIo::async_set_file_priority(
    lt::storage_index_t storage, lt::aux::vector<lt::download_priority_t, lt::file_index_t> prio,
    std::function<void(lt::storage_error const&, lt::aux::vector<lt::download_priority_t, lt::file_index_t>)> handler)
{
    m_inner->async_set_file_priority(storage, std::move(prio), std::move(handler));
}

void BtJoinDiskIo::async_clear_piece(lt::storage_index_t storage, lt::piece_index_t index, std::function<void(lt::piece_index_t)> handler)
{
    m_inner->async_clear_piece(storage, index, std::move(handler));
}

void BtJoinDiskIo::update_stats_counters(lt::counters& c) const
{
    m_inner->update_stats_counters(c);
}

std::vector<lt::open_file_state> BtJoinDiskIo::get_status(lt::storage_index_t storage) const
{
    return m_inner->get_status(storage);
}

void BtJoinDiskIo::abort(bool wait)
{
    {
        std::scoped_lock lock(m_ready->mutex);
        m_ready->alive = false;
        m_ready->jobs.clear();
    }
    m_inner->abort(wait);
}

void BtJoinDiskIo::submit_jobs()
{
    m_inner->submit_jobs();
}

void BtJoinDiskIo::settings_updated()
{
    m_inner->settings_updated();
}

void BtJoinDiskIo::readForHook(lt::storage_index_t storage, int pieceSize, std::shared_ptr<BtJoinHook> hook,
                               std::shared_ptr<Pending> state, lt::disk_job_flags_t)
{
    struct Read {
        std::vector<char> data;
        int left = 0;
        bool failed = false;
    };
    auto read = std::make_shared<Read>();
    read->data.resize(std::size_t(pieceSize));
    read->left = (pieceSize + kReadBlock - 1) / kReadBlock;
    const auto ask = [ready = m_ready, hook, state, read] {
        if (read->failed) {
            // no bytes, no judge: libtorrent's own verdict stands
            state->finish(false);
            return;
        }
        auto inside = std::make_shared<std::atomic<bool>>(true);
        const auto caller = std::this_thread::get_id();
        hook->pieceHashed(state->piece, std::move(read->data), [ready, state, inside, caller](bool spoil) {
            if (inside->load() && std::this_thread::get_id() == caller) {
                // still on libtorrent's thread inside its handler
                state->finish(spoil);
                return;
            }
            std::scoped_lock lock(ready->mutex);
            if (ready->alive)
                ready->jobs.emplace_back([state, spoil] { state->finish(spoil); });
        });
        inside->store(false);
    };
    for (int offset = 0; offset < pieceSize; offset += kReadBlock) {
        lt::peer_request r;
        r.piece = state->piece;
        r.start = offset;
        r.length = std::min(kReadBlock, pieceSize - offset);
        const auto len = std::size_t(r.length);
        m_inner->async_read(storage, r,
                            [read, offset, len, ask](lt::disk_buffer_holder buf, lt::storage_error const& err) {
                                // the holder has no size: the request's length is what was read
                                if (err || !buf.data())
                                    read->failed = true;
                                else
                                    std::memcpy(read->data.data() + offset, buf.data(), len);
                                // handlers run on libtorrent's network thread, one at a time
                                if (--read->left == 0)
                                    ask();
                            },
                            {});
    }
    m_inner->submit_jobs();
}

std::shared_ptr<BtJoinHook> BtJoinDiskIo::hookOf(lt::storage_index_t storage) const
{
    std::scoped_lock lock(m_mutex);
    auto hash = m_hashOf.find(storage);
    if (hash == m_hashOf.end())
        return nullptr;
    auto it = m_hooks.find(hash->second);
    return it == m_hooks.end() ? nullptr : it->second;
}

} // namespace eMule::bt
