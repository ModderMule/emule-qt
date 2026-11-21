/// @file BtTorrentCreator.cpp
/// @brief .torrent creation (#191) — see BtTorrentCreator.h. After qBittorrent's
/// TorrentCreator: natural file order, dot files skipped, tracker tiers by empty lines.

#include "create/BtTorrentCreator.h"

#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/disk_interface.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/session.hpp>

#include <boost/asio/post.hpp>

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace eMule::bt {

namespace lt = libtorrent;

namespace {

struct Cancelled : std::runtime_error {
    Cancelled()
        : std::runtime_error("cancelled")
    {
    }
};

/// The default disk I/O; once cancelled, new hash jobs fail (operation_aborted). Throwing
/// from set_piece_hashes' callback is not safe: its file_storage dies before the disk
/// threads are stopped (libtorrent assert in pad_file_at, or a hang). A failed job takes
/// libtorrent's own error path instead: abort, then set_piece_hashes returns.
class CancellableDiskIo final : public lt::disk_interface {
public:
    CancellableDiskIo(lt::io_context& ioc, std::unique_ptr<lt::disk_interface> inner, const std::atomic_bool& cancel)
        : m_ioc(ioc)
        , m_inner(std::move(inner))
        , m_cancel(cancel)
    {
    }

    lt::storage_holder new_torrent(lt::storage_params const& p, std::shared_ptr<void> const& t) override
    {
        return m_inner->new_torrent(p, t);   // its holder removes from m_inner, which we own
    }
    void remove_torrent(lt::storage_index_t i) override { m_inner->remove_torrent(i); }
    void async_read(lt::storage_index_t s, lt::peer_request const& r,
                    std::function<void(lt::disk_buffer_holder, lt::storage_error const&)> h, lt::disk_job_flags_t f) override
    {
        m_inner->async_read(s, r, std::move(h), f);
    }
    bool async_write(lt::storage_index_t s, lt::peer_request const& r, char const* buf, std::shared_ptr<lt::disk_observer> o,
                     std::function<void(lt::storage_error const&)> h, lt::disk_job_flags_t f) override
    {
        return m_inner->async_write(s, r, buf, std::move(o), std::move(h), f);
    }
    void async_hash(lt::storage_index_t s, lt::piece_index_t piece, lt::span<lt::sha256_hash> v2, lt::disk_job_flags_t f,
                    std::function<void(lt::piece_index_t, lt::sha1_hash const&, lt::storage_error const&)> h) override
    {
        if (!m_cancel.load(std::memory_order_relaxed))
            return m_inner->async_hash(s, piece, v2, f, std::move(h));
        // as a real job: the handler runs on the io_context, never inside this call
        boost::asio::post(m_ioc, [h = std::move(h), piece] {
            h(piece, lt::sha1_hash{}, lt::storage_error(boost::asio::error::operation_aborted));
        });
    }
    void async_hash2(lt::storage_index_t s, lt::piece_index_t piece, int offset, lt::disk_job_flags_t f,
                     std::function<void(lt::piece_index_t, lt::sha256_hash const&, lt::storage_error const&)> h) override
    {
        m_inner->async_hash2(s, piece, offset, f, std::move(h));
    }
    void async_move_storage(lt::storage_index_t s, std::string p, lt::move_flags_t f,
                            std::function<void(lt::status_t, std::string const&, lt::storage_error const&)> h) override
    {
        m_inner->async_move_storage(s, std::move(p), f, std::move(h));
    }
    void async_release_files(lt::storage_index_t s, std::function<void()> h) override
    {
        m_inner->async_release_files(s, std::move(h));
    }
    void async_check_files(lt::storage_index_t s, lt::add_torrent_params const* rd, lt::aux::vector<std::string, lt::file_index_t> links,
                           std::function<void(lt::status_t, lt::storage_error const&)> h) override
    {
        m_inner->async_check_files(s, rd, std::move(links), std::move(h));
    }
    void async_stop_torrent(lt::storage_index_t s, std::function<void()> h) override
    {
        m_inner->async_stop_torrent(s, std::move(h));
    }
    void async_rename_file(lt::storage_index_t s, lt::file_index_t i, std::string n,
                           std::function<void(std::string const&, lt::file_index_t, lt::storage_error const&)> h) override
    {
        m_inner->async_rename_file(s, i, std::move(n), std::move(h));
    }
    void async_delete_files(lt::storage_index_t s, lt::remove_flags_t o, std::function<void(lt::storage_error const&)> h) override
    {
        m_inner->async_delete_files(s, o, std::move(h));
    }
    void async_set_file_priority(lt::storage_index_t s, lt::aux::vector<lt::download_priority_t, lt::file_index_t> prio,
                                 std::function<void(lt::storage_error const&, lt::aux::vector<lt::download_priority_t, lt::file_index_t>)> h) override
    {
        m_inner->async_set_file_priority(s, std::move(prio), std::move(h));
    }
    void async_clear_piece(lt::storage_index_t s, lt::piece_index_t i, std::function<void(lt::piece_index_t)> h) override
    {
        m_inner->async_clear_piece(s, i, std::move(h));
    }
    void update_stats_counters(lt::counters& c) const override { m_inner->update_stats_counters(c); }
    std::vector<lt::open_file_state> get_status(lt::storage_index_t s) const override { return m_inner->get_status(s); }
    void abort(bool wait) override { m_inner->abort(wait); }
    void submit_jobs() override { m_inner->submit_jobs(); }
    void settings_updated() override { m_inner->settings_updated(); }

private:
    lt::io_context& m_ioc;
    std::unique_ptr<lt::disk_interface> m_inner;
    const std::atomic_bool& m_cancel;
};

bool isDotName(const QString& path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return path.mid(slash + 1).startsWith(u'.');
}

lt::create_flags_t formatFlags(BtTorrentFormat f)
{
    switch (f) {
    case BtTorrentFormat::V1: return lt::create_torrent::v1_only;
    case BtTorrentFormat::V2: return lt::create_torrent::v2_only;
    case BtTorrentFormat::Hybrid: break;
    }
    return {};
}

/// relative names ('/' separators) below the root, the root's own name first
std::expected<std::vector<lt::create_file_entry>, QString> listFiles(const BtCreateParams& p)
{
    const QFileInfo root(p.path);
    if (!root.exists())
        return std::unexpected(QStringLiteral("Path not found: %1").arg(p.path));
    std::vector<lt::create_file_entry> files;
    if (root.isFile()) {
        files.emplace_back(root.fileName().toStdString(), root.size());
        return files;
    }
    if (!root.isDir())
        return std::unexpected(QStringLiteral("Not a file or folder: %1").arg(p.path));

    QCollator natural;
    natural.setNumericMode(true);
    natural.setCaseSensitivity(Qt::CaseInsensitive);
    const QDir dir(root.absoluteFilePath());
    const QString rootName = root.fileName();

    QStringList names;
    if (!p.onlyFiles.isEmpty()) {
        names = p.onlyFiles;
    } else {
        QDirIterator it(dir.path(), QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo fi = it.nextFileInfo();
            if (fi.isSymLink())
                continue;
            names << dir.relativeFilePath(fi.filePath());
        }
    }
    // folders first by path, then files by name: qBittorrent's order
    std::ranges::sort(names, [&natural](const QString& a, const QString& b) {
        const qsizetype sa = a.lastIndexOf(u'/');
        const qsizetype sb = b.lastIndexOf(u'/');
        const QString da = sa < 0 ? QString() : a.left(sa);
        const QString db = sb < 0 ? QString() : b.left(sb);
        if (const int c = natural.compare(da, db); c != 0)
            return c < 0;
        return natural.compare(a.mid(sa + 1), b.mid(sb + 1)) < 0;
    });
    for (const QString& name : names) {
        if (name.isEmpty() || name.startsWith(u'/') || name.split(u'/').contains(QStringLiteral("..")))
            return std::unexpected(QStringLiteral("Invalid file name: %1").arg(name));
        if (p.ignoreDotfiles && std::ranges::any_of(name.split(u'/'), [](const QString& s) { return isDotName(s); }))
            continue;
        const QFileInfo fi(dir.filePath(name));
        if (!fi.isFile())
            return std::unexpected(QStringLiteral("Not a file: %1").arg(fi.filePath()));
        files.emplace_back(QString(rootName + u'/' + name).toStdString(), fi.size());
    }
    if (files.empty())
        return std::unexpected(p.ignoreDotfiles ? QStringLiteral("No files (dot files are left out)")
                                                : QStringLiteral("No files"));
    return files;
}

} // namespace

BtTorrentCreator::BtTorrentCreator(BtCreateParams params, QObject* parent)
    : QObject(parent)
    , m_params(std::move(params))
{
    setAutoDelete(false);   // the owner deletes it after finished()
}

void BtTorrentCreator::run()
{
    BtCreateResult r;
    const QFileInfo root(m_params.path);
    r.name = root.fileName();
    r.savePath = root.absolutePath();
    emit progress(0);
    try {
        auto files = listFiles(m_params);
        if (!files)
            throw std::runtime_error(files.error().toStdString());
        lt::create_torrent t(std::move(*files), m_params.pieceSize, formatFlags(m_params.format));
        for (const QString& seed : m_params.webSeeds)
            if (const QString s = seed.trimmed(); !s.isEmpty())
                t.add_url_seed(s.toStdString());
        int tier = 0;
        for (const QString& line : m_params.trackers) {
            if (const QString url = line.trimmed(); url.isEmpty())
                ++tier;
            else
                t.add_tracker(url.toStdString(), tier);
        }
        const int pieces = t.num_pieces();
        lt::error_code ec;
        int shown = 0;
        const auto diskIo = [this](lt::io_context& ioc, lt::settings_interface const& sett, lt::counters& cnt) {
            return std::make_unique<CancellableDiskIo>(ioc, lt::default_disk_io_constructor(ioc, sett, cnt), m_cancel);
        };
        lt::set_piece_hashes(t, r.savePath.toStdString(), lt::settings_pack{}, diskIo, [this, pieces, &shown](lt::piece_index_t n) {
            if (m_cancel.load(std::memory_order_relaxed))
                return;   // CancellableDiskIo fails the next job
            // one signal per percent, not per piece
            if (const int pct = pieces > 0 ? int((static_cast<int>(n) + 1) * 100LL / pieces) : 100; pct != shown) {
                shown = pct;
                emit progress(pct);
            }
        }, ec);
        if (m_cancel.load(std::memory_order_relaxed))
            throw Cancelled();
        if (ec)
            throw std::runtime_error(ec.message());
        if (!m_params.creator.isEmpty())
            t.set_creator(m_params.creator.toStdString().c_str());
        if (!m_params.comment.isEmpty())
            t.set_comment(m_params.comment.toStdString().c_str());
        t.set_priv(m_params.isPrivate);
        lt::entry e = t.generate();
        if (const QString s = m_params.source.trimmed(); !s.isEmpty())
            e["info"]["source"] = s.toStdString();
        std::vector<char> buf;
        lt::bencode(std::back_inserter(buf), e);
        r.data = QByteArray(buf.data(), qsizetype(buf.size()));
        r.pieceSize = t.piece_length();
        r.pieces = pieces;
        r.ok = true;
    } catch (const Cancelled&) {
        r.cancelled = true;
        r.error = QStringLiteral("Cancelled");
    } catch (const std::exception& e) {
        r.error = QString::fromLocal8Bit(e.what());
    }
    emit finished(r);
}

std::expected<int, QString> BtTorrentCreator::countPieces(const BtCreateParams& params)
{
    try {
        auto files = listFiles(params);
        if (!files)
            return std::unexpected(files.error());
        return lt::create_torrent(std::move(*files), params.pieceSize, formatFlags(params.format)).num_pieces();
    } catch (const std::exception& e) {
        return std::unexpected(QString::fromLocal8Bit(e.what()));
    }
}

} // namespace eMule::bt
