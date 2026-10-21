#include "torrent/BtTorrentView.h"

#include "BtIds.h"

#include <libtorrent/announce_entry.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/torrent_info.hpp>

#include <algorithm>

namespace lt = libtorrent;

namespace eMule::bt {

namespace {

BtState stateOf(const lt::torrent_status& st)
{
    if (st.errc)
        return BtState::Error;
    if (st.moving_storage)
        return BtState::Moving;
    const bool paused = (st.flags & lt::torrent_flags::paused) && !(st.flags & lt::torrent_flags::auto_managed);
    if (paused)
        return BtState::Paused;
    if (st.flags & lt::torrent_flags::paused)
        return BtState::Queued;  // auto-managed and paused: waits for a slot
    switch (st.state) {
    case lt::torrent_status::checking_files: return BtState::CheckingFiles;
    case lt::torrent_status::downloading_metadata: return BtState::DownloadingMetadata;
    case lt::torrent_status::downloading: return BtState::Downloading;
    case lt::torrent_status::finished: return BtState::Finished;
    case lt::torrent_status::seeding: return BtState::Seeding;
    case lt::torrent_status::checking_resume_data: return BtState::CheckingResume;
    }
    return BtState::Queued;
}

QString hexOf(const lt::sha1_hash& h)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(h.data()), static_cast<qsizetype>(h.size())).toHex());
}

QString hexOf(const lt::sha256_hash& h)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(h.data()), static_cast<qsizetype>(h.size())).toHex());
}

QString sourceOf(lt::peer_source_flags_t s)
{
    if (s & lt::peer_info::incoming)
        return QStringLiteral("incoming");
    if (s & lt::peer_info::tracker)
        return QStringLiteral("tracker");
    if (s & lt::peer_info::dht)
        return QStringLiteral("dht");
    if (s & lt::peer_info::pex)
        return QStringLiteral("pex");
    if (s & lt::peer_info::lsd)
        return QStringLiteral("lsd");
    if (s & lt::peer_info::resume_data)
        return QStringLiteral("resume");
    return {};
}

} // namespace

QString BtTorrentView::idOf(const lt::info_hash_t& hashes)
{
    BtInfoHash h;
    if (hashes.has_v1())
        h.v1 = QByteArray(reinterpret_cast<const char*>(hashes.v1.data()), static_cast<qsizetype>(hashes.v1.size()));
    if (hashes.has_v2())
        h.v2 = QByteArray(reinterpret_cast<const char*>(hashes.v2.data()), static_cast<qsizetype>(hashes.v2.size()));
    return BtIds::idOf(h);
}

BtTorrentRow BtTorrentView::row(const lt::torrent_status& st)
{
    BtTorrentRow r;
    r.id = idOf(st.info_hashes);
    r.name = QString::fromStdString(st.name);
    if (r.name.isEmpty())
        r.name = st.info_hashes.has_v1() ? hexOf(st.info_hashes.v1) : hexOf(st.info_hashes.v2);
    r.savePath = QString::fromStdString(st.save_path);
    r.state = stateOf(st);
    r.size = st.total_wanted;
    r.done = st.total_wanted_done;
    r.progress = st.progress_ppm / 1'000'000.0;
    r.rateDown = st.download_payload_rate;
    r.rateUp = st.upload_payload_rate;
    r.peers = st.num_peers - st.num_seeds;
    r.seeds = st.num_seeds;
    r.peersTotal = st.num_incomplete;
    r.seedsTotal = st.num_complete;
    r.downloaded = st.all_time_download;
    r.uploaded = st.all_time_upload;
    r.failedBytes = st.total_failed_bytes;
    if (st.errc)
        r.error = QString::fromStdString(st.errc.message());
    if (st.info_hashes.has_v1())
        r.hashV1 = hexOf(st.info_hashes.v1);
    if (st.info_hashes.has_v2())
        r.hashV2 = hexOf(st.info_hashes.v2);
    r.paused = (st.flags & lt::torrent_flags::paused) && !(st.flags & lt::torrent_flags::auto_managed);
    r.hasMetadata = st.has_metadata;
    r.addedAt = st.added_time;
    r.completedAt = st.completed_time;
    r.seedingSeconds = st.seeding_duration.count();
    r.queuePosition = static_cast<int>(st.queue_position);
    r.pieceMap = pieceMap(st.pieces);
    r.pieces = st.num_pieces;
    if (auto ti = st.torrent_file.lock()) {
        r.pieceLength = ti->piece_length();
        r.numFiles = ti->num_files();
        r.isPrivate = ti->priv();
    }
    if (r.rateDown > 0 && r.size > r.done)
        r.eta = (r.size - r.done) / r.rateDown;
    return r;
}

QList<BtFileRow> BtTorrentView::files(const lt::torrent_handle& h)
{
    QList<BtFileRow> out;
    auto ti = h.torrent_file();
    if (!ti)
        return out;
    const lt::file_storage& fs = ti->layout();
    std::vector<std::int64_t> progress;
    h.file_progress(progress);
    const auto prio = h.get_file_priorities();
    for (lt::file_index_t i : fs.file_range()) {
        const int idx = static_cast<int>(i);
        BtFileRow f;
        f.index = idx;
        f.path = QString::fromStdString(fs.file_path(i));
        f.path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        f.size = fs.file_size(i);
        f.done = idx < static_cast<int>(progress.size()) ? progress[static_cast<std::size_t>(idx)] : 0;
        f.priority = idx < static_cast<int>(prio.size()) ? static_cast<int>(static_cast<std::uint8_t>(prio[static_cast<std::size_t>(idx)])) : 4;
        f.pad = fs.pad_file_at(i);
        out.append(f);
    }
    return out;
}

QList<BtPeerRow> BtTorrentView::peers(const lt::torrent_handle& h)
{
    QList<BtPeerRow> out;
    std::vector<lt::peer_info> list;
    h.get_peer_info(list);
    for (const auto& p : list) {
        BtPeerRow r;
        const auto ep = p.remote_endpoint();
        const auto addr = ep.address();
        r.address = addr.is_v6() ? QStringLiteral("[%1]:%2").arg(QString::fromStdString(addr.to_string())).arg(ep.port())
                                 : QStringLiteral("%1:%2").arg(QString::fromStdString(addr.to_string())).arg(ep.port());
        r.client = QString::fromStdString(p.client);
        r.progress = static_cast<double>(p.progress);
        r.rateDown = p.payload_down_speed;
        r.rateUp = p.payload_up_speed;
        r.downloaded = p.total_download;
        r.uploaded = p.total_upload;
        r.seed = static_cast<bool>(p.flags & lt::peer_info::seed);
        r.encrypted = static_cast<bool>(p.flags & (lt::peer_info::rc4_encrypted | lt::peer_info::plaintext_encrypted));
        r.utp = static_cast<bool>(p.flags & lt::peer_info::utp_socket);
        r.source = sourceOf(p.source);
        out.append(r);
    }
    return out;
}

QList<BtTrackerRow> BtTorrentView::trackers(const lt::torrent_handle& h)
{
    QList<BtTrackerRow> out;
    for (const auto& a : h.trackers()) {
        BtTrackerRow r;
        r.url = QString::fromStdString(a.url);
        r.tier = a.tier;
        for (const auto& ep : a.endpoints) {
            for (const auto& ih : ep.info_hashes) {
                if (!ih.message.empty() && r.message.isEmpty())
                    r.message = QString::fromStdString(ih.message);
                if (ih.last_error)
                    r.message = QString::fromStdString(ih.last_error.message());
                else if (ih.fails == 0 && !ih.updating && ih.start_sent)
                    r.working = true;
                r.seeds = std::max(r.seeds, ih.scrape_complete);
                r.peers = std::max(r.peers, ih.scrape_incomplete);
            }
        }
        out.append(r);
    }
    return out;
}

QByteArray BtTorrentView::pieceMap(const lt::typed_bitfield<lt::piece_index_t>& pieces, int buckets)
{
    const int n = pieces.size();
    if (n <= 0 || buckets <= 0)
        return {};
    const int count = std::min(n, buckets);
    QByteArray out(count, '\0');
    for (int b = 0; b < count; ++b) {
        const int from = static_cast<int>(static_cast<qint64>(b) * n / count);
        const int to = static_cast<int>(static_cast<qint64>(b + 1) * n / count);
        int have = 0;
        for (int i = from; i < to; ++i)
            if (pieces.get_bit(lt::piece_index_t{i}))
                ++have;
        out[b] = static_cast<char>(to > from ? have * 255 / (to - from) : 0);
    }
    return out;
}

} // namespace eMule::bt
