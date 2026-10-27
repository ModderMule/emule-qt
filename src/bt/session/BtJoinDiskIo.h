#pragma once

/// @file BtJoinDiskIo.h
/// @brief libtorrent disk_interface around pread_disk_io: the door through which an ed3k
/// join (torrent.md §4) sees each piece before libtorrent counts it.
///
/// Passes every job through. A storage with a hook attached gets its piece hashes routed
/// through the hook first: the hook answers "spoil" and the digest handed back is wrong, so
/// libtorrent fails the piece and blames its peers itself. Installed from the first session
/// on, so attaching a join later needs no session rebuild.
///
/// No Boost.Asio call is made from here: posting to libtorrent's io_context from a unit of
/// ours crashed (its scheduler read garbage, both with the Homebrew dylib and a static
/// build; cause not found). A verdict that comes later is queued, and the pump plugin
/// (pumpPlugin()) hands it back from libtorrent's own tick, on its network thread.
///
/// libtorrent headers only in src/bt (research doc §4): never include this outside.

#include <libtorrent/disk_interface.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/info_hash.hpp>
#include <libtorrent/io_context.hpp>
#include <libtorrent/session_params.hpp>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace libtorrent {
struct counters;
struct settings_interface;
} // namespace libtorrent

namespace eMule::bt {

/// What an ed3k join does with a piece libtorrent just hashed.
class BtJoinHook {
public:
    virtual ~BtJoinHook() = default;

    /// Called on libtorrent's network thread once the piece's own hash is done, with the
    /// piece's bytes as written (padding included). `done(spoil)` is called once, from
    /// any thread; `spoil` makes libtorrent fail the piece. A verdict at once is handed
    /// back in place; a later one at the next tick (settings_pack::tick_interval,
    /// 500 ms), and dropped when the session is gone.
    virtual void pieceHashed(libtorrent::piece_index_t piece, std::vector<char> data, std::function<void(bool spoil)> done) = 0;
};

class BtJoinDiskIo final : public libtorrent::disk_interface {
public:
    explicit BtJoinDiskIo(std::unique_ptr<libtorrent::disk_interface> inner);
    ~BtJoinDiskIo() override;

    /// The constructor for session_params::disk_io_constructor. `made` (may be null) gets
    /// the instance, to attach hooks to.
    static libtorrent::disk_io_constructor_type constructor(std::function<void(BtJoinDiskIo*)> made = {});

    /// The session plugin that hands late verdicts back on libtorrent's tick. `io` gives
    /// the instance, or null; it is called on the network thread.
    static std::shared_ptr<libtorrent::plugin> pumpPlugin(std::function<BtJoinDiskIo*()> io);

    /// Hands queued verdicts back. On libtorrent's network thread only.
    void pump();

    /// Routes the piece hashes of a torrent through `hook`, before or after it is added.
    /// Thread-safe.
    void attach(const libtorrent::info_hash_t& torrent, std::shared_ptr<BtJoinHook> hook);
    void detach(const libtorrent::info_hash_t& torrent);
    [[nodiscard]] bool attached(const libtorrent::info_hash_t& torrent) const;

    // --- disk_interface ---
    libtorrent::storage_holder new_torrent(libtorrent::storage_params const& p, std::shared_ptr<void> const& torrent) override;
    void remove_torrent(libtorrent::storage_index_t storage) override;
    void async_read(libtorrent::storage_index_t storage, libtorrent::peer_request const& r,
                    std::function<void(libtorrent::disk_buffer_holder, libtorrent::storage_error const&)> handler,
                    libtorrent::disk_job_flags_t flags) override;
    bool async_write(libtorrent::storage_index_t storage, libtorrent::peer_request const& r, char const* buf,
                     std::shared_ptr<libtorrent::disk_observer> o, std::function<void(libtorrent::storage_error const&)> handler,
                     libtorrent::disk_job_flags_t flags) override;
    void async_hash(libtorrent::storage_index_t storage, libtorrent::piece_index_t piece, libtorrent::span<libtorrent::sha256_hash> v2,
                    libtorrent::disk_job_flags_t flags,
                    std::function<void(libtorrent::piece_index_t, libtorrent::sha1_hash const&, libtorrent::storage_error const&)> handler) override;
    void async_hash2(libtorrent::storage_index_t storage, libtorrent::piece_index_t piece, int offset, libtorrent::disk_job_flags_t flags,
                     std::function<void(libtorrent::piece_index_t, libtorrent::sha256_hash const&, libtorrent::storage_error const&)> handler) override;
    void async_move_storage(libtorrent::storage_index_t storage, std::string p, libtorrent::move_flags_t flags,
                            std::function<void(libtorrent::status_t, std::string const&, libtorrent::storage_error const&)> handler) override;
    void async_release_files(libtorrent::storage_index_t storage, std::function<void()> handler) override;
    void async_check_files(libtorrent::storage_index_t storage, libtorrent::add_torrent_params const* resume_data,
                           libtorrent::aux::vector<std::string, libtorrent::file_index_t> links,
                           std::function<void(libtorrent::status_t, libtorrent::storage_error const&)> handler) override;
    void async_stop_torrent(libtorrent::storage_index_t storage, std::function<void()> handler) override;
    void async_rename_file(libtorrent::storage_index_t storage, libtorrent::file_index_t index, std::string name,
                           std::function<void(std::string const&, libtorrent::file_index_t, libtorrent::storage_error const&)> handler) override;
    void async_delete_files(libtorrent::storage_index_t storage, libtorrent::remove_flags_t options,
                            std::function<void(libtorrent::storage_error const&)> handler) override;
    void async_set_file_priority(
        libtorrent::storage_index_t storage, libtorrent::aux::vector<libtorrent::download_priority_t, libtorrent::file_index_t> prio,
        std::function<void(libtorrent::storage_error const&, libtorrent::aux::vector<libtorrent::download_priority_t, libtorrent::file_index_t>)>
            handler) override;
    void async_clear_piece(libtorrent::storage_index_t storage, libtorrent::piece_index_t index,
                           std::function<void(libtorrent::piece_index_t)> handler) override;
    void update_stats_counters(libtorrent::counters& c) const override;
    std::vector<libtorrent::open_file_state> get_status(libtorrent::storage_index_t storage) const override;
    void abort(bool wait) override;
    void submit_jobs() override;
    void settings_updated() override;

private:
    /// Late verdicts waiting for pump(); after abort() they are dropped.
    struct Ready {
        std::mutex mutex;
        bool alive = true;
        std::vector<std::function<void()>> jobs;
    };

    [[nodiscard]] std::shared_ptr<BtJoinHook> hookOf(libtorrent::storage_index_t storage) const;
    /// A hash job waiting for the hook's verdict.
    struct Pending;
    /// Reads a hashed piece through the inner disk I/O, then asks the hook.
    void readForHook(libtorrent::storage_index_t storage, int pieceSize, std::shared_ptr<BtJoinHook> hook,
                     std::shared_ptr<Pending> pending, libtorrent::disk_job_flags_t flags);

    std::shared_ptr<Ready> m_ready = std::make_shared<Ready>();
    std::unique_ptr<libtorrent::disk_interface> m_inner;
    mutable std::mutex m_mutex;
    /// the inner holders: dropping one removes the torrent from the inner disk I/O
    std::map<libtorrent::storage_index_t, libtorrent::storage_holder> m_holders;
    /// storage_params::info_hash: info_hash_t::get_best() of the torrent
    std::map<libtorrent::storage_index_t, libtorrent::sha1_hash> m_hashOf;
    /// storage_params::files: the torrent's, alive as long as its storage
    std::map<libtorrent::storage_index_t, const libtorrent::file_storage*> m_filesOf;
    std::map<libtorrent::sha1_hash, std::shared_ptr<BtJoinHook>> m_hooks;
};

} // namespace eMule::bt
