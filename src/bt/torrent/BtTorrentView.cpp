#include "torrent/BtTorrentView.h"

#include "BtIds.h"

#include <libtorrent/announce_entry.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/torrent_info.hpp>

#include <algorithm>
#include <chrono>

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

/// A libtorrent clock point → Unix seconds; 0 when never set.
qint64 unixOf(lt::time_point tp)
{
    if (tp == lt::time_point{})
        return 0;
    const auto ago = std::chrono::duration_cast<std::chrono::seconds>(lt::clock_type::now() - tp).count();
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return std::max<qint64>(0, now - ago);
}

/// Transmission's peer status letters (libtransmission/peer-mgr.cc).
QString flagsOf(const lt::peer_info& p)
{
    QString f;
    const auto has = [&p](lt::peer_flags_t bit) { return static_cast<bool>(p.flags & bit); };
    if (has(lt::peer_info::utp_socket))
        f += QLatin1Char('T');
    if (has(lt::peer_info::optimistic_unchoke))
        f += QLatin1Char('O');
    const bool weInterested = has(lt::peer_info::interesting);
    const bool weChoked = has(lt::peer_info::choked);
    const bool theyInterested = has(lt::peer_info::remote_interested);
    const bool theyChoked = has(lt::peer_info::remote_choked);
    if (weInterested)
        f += weChoked ? QLatin1Char('d') : QLatin1Char('D');
    if (theyInterested)
        f += theyChoked ? QLatin1Char('u') : QLatin1Char('U');
    if (!weChoked && !weInterested)
        f += QLatin1Char('K');
    if (!theyChoked && !theyInterested)
        f += QLatin1Char('?');
    if (has(lt::peer_info::rc4_encrypted) || has(lt::peer_info::plaintext_encrypted))
        f += QLatin1Char('E');
    if (has(lt::peer_info::holepunched))
        f += QLatin1Char('h');
    if (p.source & lt::peer_info::dht)
        f += QLatin1Char('H');
    else if (p.source & lt::peer_info::pex)
        f += QLatin1Char('X');
    if (!has(lt::peer_info::outgoing_connection))
        f += QLatin1Char('I');
    return f;
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
        // gone from torrent_info at ABI 100 (2.1, no deprecated functions): BtSession fills
        // them from the add parameters then
#if TORRENT_ABI_VERSION < 4
        r.comment = QString::fromStdString(ti->comment());
        r.creator = QString::fromStdString(ti->creator());
        r.createdAt = static_cast<qint64>(ti->creation_date());
#endif
    }
    r.lastActivity = std::max(unixOf(st.last_download), unixOf(st.last_upload));
    r.activeSeconds = st.active_duration.count();
    r.haveBytes = st.total_done;
    r.totalSize = st.total;
    r.availability = st.has_metadata ? static_cast<double>(st.distributed_copies) : -1.0;
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
        // no half-open attempts: the same peers num_peers counts
        if (p.flags & lt::peer_info::connecting)
            continue;
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
        r.flags = flagsOf(p);
        out.append(r);
    }
    return out;
}

QList<BtTrackerRow> BtTorrentView::trackers(const lt::torrent_handle& h)
{
    QList<BtTrackerRow> out;
    const auto now = lt::clock_type::now();
    for (const auto& a : h.trackers()) {
        BtTrackerRow r;
        r.url = QString::fromStdString(a.url);
        r.tier = a.tier;
        QString error;
        // one entry per local address and hash: working if any of them works
        for (const auto& ep : a.endpoints) {
            for (const auto& ih : ep.info_hashes) {
                if (!ih.message.empty() && r.message.isEmpty())
                    r.message = QString::fromStdString(ih.message);
                if (ih.last_error)
                    error = QString::fromStdString(ih.last_error.message());
                else if (ih.fails == 0 && !ih.updating && ih.start_sent)
                    r.working = true;
                r.seeds = std::max(r.seeds, ih.scrape_complete);
                r.peers = std::max(r.peers, ih.scrape_incomplete);
                r.downloaded = std::max(r.downloaded, ih.scrape_downloaded);
                r.updating = r.updating || ih.updating;
                r.fails = std::max(r.fails, static_cast<int>(ih.fails));
                if (ih.next_announce > now) {
                    const qint64 in = std::chrono::duration_cast<std::chrono::seconds>(ih.next_announce - now).count();
                    r.nextAnnounce = r.nextAnnounce < 0 ? in : std::min(r.nextAnnounce, in);
                }
            }
        }
        if (!r.working && !error.isEmpty()) {
            r.hasError = true;
            r.message = error;
        }
        out.append(r);
    }
    return out;
}

QByteArray BtTorrentView::availabilityMap(const lt::torrent_handle& h, int buckets)
{
    std::vector<int> avail;
    h.piece_availability(avail);
    const int n = static_cast<int>(avail.size());
    if (n <= 0 || buckets <= 0)
        return {};
    const int count = std::min(n, buckets);
    QByteArray out(count, '\0');
    for (int b = 0; b < count; ++b) {
        const int from = static_cast<int>(static_cast<qint64>(b) * n / count);
        const int to = std::max(from + 1, static_cast<int>(static_cast<qint64>(b + 1) * n / count));
        // the rarest piece of the bucket
        int least = avail[static_cast<std::size_t>(from)];
        for (int i = from + 1; i < to; ++i)
            least = std::min(least, avail[static_cast<std::size_t>(i)]);
        out[b] = static_cast<char>(std::clamp(least, 0, 255));
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
