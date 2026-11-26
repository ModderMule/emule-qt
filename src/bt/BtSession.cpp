#include "BtSession.h"

#include "add/BtAddSource.h"
#include "session/BtJoinDiskIo.h"
#include "session/BtLocateFiles.h"
#include "session/BtPeerGate.h"
#include "session/BtSettingsMap.h"
#include "torrent/BtTorrentView.h"
#include "app/AppContext.h"
#include "files/PartFile.h"
#include "stats/NetworkCounters.h"
#include "stats/Statistics.h"
#include "utils/BackgroundFileWriter.h"
#include "utils/Log.h"
#include "utils/ShareRatio.h"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/i2p_stream.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/kademlia/dht_state.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/session_stats.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/write_resume_data.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QTcpServer>
#include <QThreadPool>
#include <QUdpSocket>

#include <algorithm>
#include <QTimer>

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace lt = libtorrent;

namespace eMule::bt {

namespace {

/// A port free for TCP and UDP alike; 0: none found. libtorrent's port 0 takes an
/// ephemeral TCP port and binds uTP to the same number, which fails inside a UDP-only
/// excluded range (Windows Hyper-V/WinNAT reserve those): no listen socket at all.
quint16 freeTcpUdpPort()
{
    for (int tries = 0; tries < 32; ++tries) {
        QUdpSocket udp;
        if (!udp.bind(QHostAddress::Any, 0))
            continue;
        QTcpServer tcp;
        if (tcp.listen(QHostAddress::Any, udp.localPort()))
            return udp.localPort();
    }
    return 0;
}

constexpr int kTickMs = 1000;
/// resume data of changed torrents every 5 min
constexpr int kResumeEveryTicks = 300;
/// a torrent that only moved data: its totals every 30 min (and at stop). libtorrent
/// marks the totals changed each second a torrent is active, so every seed counted as
/// modified and re-saved with its info dict every 5 min (report 28)
constexpr int kCountersEveryTicks = 1800;
/// .i2p trackers SAM could not resolve yet: asked again every 2 min, for 1 h (#168)
constexpr int kI2pNameRetryTicks = 120;
constexpr int kI2pNameRetries = 30;
/// stop() waits this long for resume data
constexpr qint64 kStopWaitMs = 5000;
/// Counter samples kept for demand(): the longest rate window plus a sample
constexpr qint64 kRateHistoryMs = 3000;
constexpr qint64 kRateWindowMs = 2000;
constexpr qint64 kRecentWindowMs = 500;
/// a detail query asks libtorrent again at most this often (the GUI polls each second)
constexpr qint64 kDetailAskMs = 500;
/// libtorrent reports "no limit" on max_connections as (1 << 24) - 1
constexpr int kLtNoLimit = (1 << 24) - 1;

const QString kSessionFile = QStringLiteral("session.dat");
const QString kResumeDir = QStringLiteral("resume");
const QString kResumeSuffix = QStringLiteral(".fastresume");
/// per-torrent seed limits; speeds and peers are in the resume data
const QString kOptionsFile = QStringLiteral("torrent-options.json");
/// the ed3k:file links torrents were added with (torrent.md §4.1)
const QString kLinksFile = QStringLiteral("ed3k-links.json");
/// #224: torrent files used where they were found
const QString kLocatedFile = QStringLiteral("located-files.json");
/// a partial moved off the torrent's path while its file is linked
const QString kLocatedAside = QStringLiteral(".located-old");
/// magnets to stop once their metadata is in (BtAddSource::applyStopAfterMetadata)
const QString kStopsFile = QStringLiteral("stop-after-metadata.json");
/// strict I2P only torrents: the clearnet trackers and web seeds set aside (BtSession::Impl::I2pOnly)
const QString kI2pOnlyFile = QStringLiteral("i2p-only.json");
const QString kI2pListWaitFile = QStringLiteral("i2p-list-wait.json");
/// what a strict I2P only torrent switches off: peers it would find in the clear
constexpr lt::torrent_flags_t kI2pOnlyOff = lt::torrent_flags::disable_dht | lt::torrent_flags::disable_pex
                                          | lt::torrent_flags::disable_lsd;

/// A torrent plugin of libtorrent as a session plugin: the explicit list of extensions.
struct TorrentPlugin final : lt::plugin {
    using Make = std::shared_ptr<lt::torrent_plugin> (*)(lt::torrent_handle const&, lt::client_data_t);
    explicit TorrentPlugin(Make m) : make(m) {}
    std::shared_ptr<lt::torrent_plugin> new_torrent(lt::torrent_handle const& h, lt::client_data_t data) override { return make(h, data); }
    Make make;
};

std::vector<std::shared_ptr<lt::plugin>> extensions(const BtSettings& s)
{
    std::vector<std::shared_ptr<lt::plugin>> out;
    if (s.pex)
        out.push_back(std::make_shared<TorrentPlugin>(&lt::create_ut_pex_plugin));
    out.push_back(std::make_shared<TorrentPlugin>(&lt::create_ut_metadata_plugin));
    out.push_back(std::make_shared<TorrentPlugin>(&lt::create_smart_ban_plugin));
    return out;
}

/// A write still queued (writeLater, writeQueued) is read back.
QByteArray readAll(const QString& path)
{
    return BackgroundFileWriter::readFile(path).value_or(QByteArray());
}

/// lt::read_resume_data, but the saved "i2p" key wins: libtorrent sets the flag again for
/// any .i2p tracker, which turned "I2P and direct" into "I2P only" on every restart (#241)
lt::add_torrent_params readResume(const QByteArray& data, lt::error_code& ec)
{
    const lt::span<char const> buf(data.constData(), static_cast<std::ptrdiff_t>(data.size()));
    lt::add_torrent_params atp = lt::read_resume_data(buf, ec);
    if (ec)
        return atp;
    lt::error_code dec;
    const lt::bdecode_node rd = lt::bdecode(buf, dec);
    if (!dec && rd.type() == lt::bdecode_node::dict_t && rd.dict_find_int(lt::string_view("i2p"))) {
        if (rd.dict_find_int_value(lt::string_view("i2p"), 0) != 0)
            atp.flags |= lt::torrent_flags::i2p_torrent;
        else
            atp.flags &= ~lt::torrent_flags::i2p_torrent;
    }
    return atp;
}

/// Synced, now: for the stop only, every other save is writeQueued.
bool writeAtomic(const QString& path, const std::vector<char>& data)
{
    BackgroundFileWriter::instance().discard(path);   // an older queued copy must not land after this
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(data.data(), static_cast<qint64>(data.size())) != static_cast<qint64>(data.size())) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

/// A small state file on the background writer: a commit on the main thread waits for the
/// disk (report 43). The writer logs a failed write.
void writeQueued(const QString& path, const QByteArray& json)
{
    BackgroundFileWriter::instance().write(path, json, {});
}

/// Off the main thread: a commit syncs, seconds on a busy disk (report 23); the encode
/// too, seconds of CPU for big seeds with many peers (report 28)
void writeLater(const QString& path, lt::add_torrent_params params)
{
    auto p = std::make_shared<const lt::add_torrent_params>(std::move(params));
    BackgroundFileWriter::instance().writeProduced(
        path,
        [p] {
            const std::vector<char> buf = lt::write_resume_data_buf(*p);
            return QByteArray(buf.data(), qsizetype(buf.size()));
        },
        &BackgroundFileWriter::saveFile);
}

/// The file name of a torrent's resume data: its hash, no ':'.
QString resumeName(const QString& id)
{
    QString name = id;
    name.replace(QLatin1Char(':'), QLatin1Char('_'));
    return name + kResumeSuffix;
}

BtLimitMode limitMode(int v)
{
    return v == 1 ? BtLimitMode::Unlimited : v == 2 ? BtLimitMode::Custom : BtLimitMode::Global;
}

QString message(const lt::alert* a)
{
    return QString::fromStdString(a->message());
}

/// A performance warning repeats each second while it holds: once a minute per torrent and kind.
bool performanceDue(const lt::performance_alert* pa)
{
    using Clock = std::chrono::steady_clock;
    constexpr auto kEvery = std::chrono::minutes(1);
    static std::map<std::pair<QString, int>, Clock::time_point> last;   // alert loop: one thread
    const auto now = Clock::now();
    auto [it, fresh] = last.try_emplace({BtTorrentView::idOf(pa->handle.info_hashes()), int(pa->warning_code)}, now);
    if (!fresh && now - it->second < kEvery)
        return false;
    it->second = now;
    return true;
}

/// A connect/peer alert to the Torrent Verbose log. Connect attempts and
/// snub/request chatter are skipped: failures and disconnects only.
void logPeerAlert(const lt::alert* a)
{
    if (!lcEmuleTorrent().isDebugEnabled())
        return;
    if (const auto* d = lt::alert_cast<lt::peer_disconnected_alert>(a)) {
        // op connect = the dial failed (timeout, refused, no route); else a dropped session
        const bool dial = d->op == lt::operation_t::connect || d->op == lt::operation_t::sock_open
                          || d->op == lt::operation_t::sock_bind;
        logTorrentDebug((dial ? QStringLiteral("peer connect failed: ") : QStringLiteral("peer disconnected: "))
                        + message(a));
        return;
    }
    if (lt::alert_cast<lt::peer_error_alert>(a) || lt::alert_cast<lt::incoming_connection_alert>(a))
        logTorrentDebug(QString::fromLatin1(a->what()) + QStringLiteral(": ") + message(a));
}

/// libtorrent's own line of @p a to the Torrent tab. Skips the per-piece/block,
/// stats and incoming-DHT alerts: a firehose, and polled anyway.
void forwardAlert(const lt::alert* a)
{
    if (lt::alert_cast<lt::session_stats_alert>(a) || lt::alert_cast<lt::state_update_alert>(a)
        || lt::alert_cast<lt::dht_stats_alert>(a) || lt::alert_cast<lt::dht_get_peers_alert>(a)
        || lt::alert_cast<lt::dht_announce_alert>(a))
        return;
    // the answer of only_if_modified when nothing changed: every few minutes per torrent
    if (const auto* sf = lt::alert_cast<lt::save_resume_data_failed_alert>(a); sf && sf->error == lt::errors::resume_data_not_modified)
        return;
    const lt::alert_category_t cat = a->category();
    if (cat & lt::alert_category::piece_progress)
        return;
    // peer connect log only (alert mask): connect failures, disconnects, peer errors
    if (cat & (lt::alert_category::connect | lt::alert_category::peer)) {
        logPeerAlert(a);
        return;
    }
    QString line = QString::fromLatin1(a->what()) + QStringLiteral(": ") + message(a);
    // The same line again within a minute is dropped: a failing i2p or listen
    // socket repeats each second (#139). Performance alerts have their own gate.
    static LogRepeatGate repeats;
    if (!lt::alert_cast<lt::performance_alert>(a)) {
        line = repeats.filter(line, line);
        if (line.isEmpty())
            return;
    }
    if (const auto* pa = lt::alert_cast<lt::performance_alert>(a)) {
        if (!performanceDue(pa))
            return;
        // limits too low: a setting to fix (#87); the rest only say a peer is fast
        using W = lt::performance_alert::performance_warning_t;
        if (pa->warning_code == W::upload_limit_too_low || pa->warning_code == W::download_limit_too_low)
            logTorrentWarning(line);
        else
            logTorrentDebug(line);
    } else if (cat & (lt::alert_category::error | lt::alert_category::performance_warning))
        logTorrentWarning(line);
    else if (cat & (lt::alert_category::dht | lt::alert_category::ip_block))
        logTorrentDebug(line);
    else
        logTorrent(line);
}

/// A join's sink as the disk I/O's hook of one torrent.
class SinkHook final : public BtJoinHook {
public:
    SinkHook(QString id, std::weak_ptr<BtJoinSink> sink) : m_id(std::move(id)), m_sink(std::move(sink)) {}

    void pieceHashed(lt::piece_index_t piece, std::vector<char> data, std::function<void(bool)> done) override
    {
        const auto sink = m_sink.lock();
        if (!sink) {
            done(false);
            return;
        }
        sink->pieceHashed(m_id, static_cast<int>(piece), QByteArray(data.data(), qsizetype(data.size())), std::move(done));
    }

    bool judges() const override
    {
        const auto sink = m_sink.lock();
        return sink && sink->judges(m_id);
    }

private:
    QString m_id;
    std::weak_ptr<BtJoinSink> m_sink;
};

/// A first status of a torrent just added, from its add parameters: no wait for
/// libtorrent's thread. The post_status() answer replaces it.
lt::torrent_status addedStatus(const lt::add_torrent_params& p, const lt::torrent_handle& h, const lt::info_hash_t& hashes)
{
    lt::torrent_status st;
    st.handle = h;
    st.info_hashes = hashes;
    st.flags = p.flags;
    st.state = lt::torrent_status::checking_resume_data;
    st.name = p.name;
    st.save_path = p.save_path;
    st.added_time = p.added_time;
    st.completed_time = p.completed_time;
    st.all_time_download = p.total_downloaded;
    st.all_time_upload = p.total_uploaded;
    if (p.ti && p.ti->is_valid()) {
        st.has_metadata = true;
        st.torrent_file = p.ti;
        st.name = p.ti->name();
        st.total = p.ti->total_size();
        st.total_wanted = st.total;
    }
    return st;
}

} // namespace

struct BtSession::Impl {
    BtSettings settings;
    std::unique_ptr<lt::session> ses;
    /// written on this thread at start/stop, read by the pump on libtorrent's thread
    std::atomic<BtJoinDiskIo*> io{nullptr};
    /// drops I2P / direct peers after a torrent's I2P mode changed
    BtPeerGate peerGate;
    QTimer* timer = nullptr;
    std::map<QString, lt::torrent_handle> handles;
    std::map<QString, BtTorrentRow> rows;
    /// all_time_upload when first seen this core run: session upload = now - base.
    /// Kept over a BT stop/start, dropped on remove.
    std::map<QString, qint64> sessionUpBase;
    /// stopped by a seed limit, and resumed by the user since: the limit stays off
    QSet<QString> limitOverride;
    /// seed limits that differ from the global ones
    std::map<QString, BtTorrentOptions> seedLimits;
    /// I2P only, also while the session mixes: no clearnet tracker, web seed, DHT, PEX or
    /// LSD; the peer gate refuses direct peers. Back as they were when it leaves (kI2pOnlyFile)
    struct I2pOnly {
        std::vector<std::pair<std::string, int>> trackers;   ///< url, tier
        std::vector<std::string> webSeeds;
        lt::torrent_flags_t flagsSet;   ///< of kI2pOnlyOff, the ones it switched on
    };
    std::map<QString, I2pOnly> i2pOnly;
    /// added I2P only: web seeds of the metadata swept out when libtorrent has it
    QSet<QString> i2pOnlySweep;
    int ticks = 0;
    int outstandingResume = 0;
    /// setRateLimits() caps in bytes/s; nullopt = the ones in settings
    std::optional<std::pair<int, int>> rateLimits;
    /// session counters for demand() and the statistics, oldest first
    struct RateSample {
        qint64 ms = 0;
        qint64 recv = 0;
        qint64 sent = 0;
        qint64 recvPayload = 0;
        qint64 sentPayload = 0;
        qint64 failed = 0;
        qint64 redundant = 0;
    };
    std::deque<RateSample> rateSamples;
    QElapsedTimer rateClock;
    int recvIdx = lt::find_metric_idx("net.recv_bytes");
    int sentIdx = lt::find_metric_idx("net.sent_bytes");
    int recvPayloadIdx = lt::find_metric_idx("net.recv_payload_bytes");
    int sentPayloadIdx = lt::find_metric_idx("net.sent_payload_bytes");
    int failedIdx = lt::find_metric_idx("net.recv_failed_bytes");
    int redundantIdx = lt::find_metric_idx("net.recv_redundant_bytes");
    int peersIdx = lt::find_metric_idx("peer.num_peers_connected");
    bool listening = false;
    /// listenPort 0 (random): the port picked at start, kept until stop
    mutable quint16 randomPort = 0;
    int dhtNodes = 0;
    /// one DHT node per listen socket (v4 + v6): routing table size per local endpoint
    std::map<lt::udp::endpoint, int> dhtNodesBySocket;
    /// last SAM error; cleared when the SAM bridge changes
    QString i2pError;
    /// .i2p tracker urls by torrent SAM could not resolve (INVALID_KEY: unknown name or no
    /// lease set yet) → re-asks so far. libtorrent's backoff grows towards an hour
    std::map<QString, std::map<std::string, int>> i2pNameMisses;
    /// external_ip_alert: our address as peers, trackers and DHT see it
    QString externalIPv4;
    QString externalIPv6;
    /// ed3k joins by id; attached to each disk I/O the session makes
    std::map<QString, std::shared_ptr<BtJoinSink>> joins;
    /// ed3k:file links by id (kLinksFile)
    std::map<QString, QString> links;
    /// strict = I2P only; flag on = I2P only, unless the session mixes; else any .i2p tracker = mixed
    BtI2pMode i2pModeOf(const QString& id)
    {
        if (i2pOnly.contains(id))
            return BtI2pMode::I2pOnly;
        const auto r = rows.find(id);
        if (r != rows.end() && r->second.i2p)
            return settings.i2pMixed ? BtI2pMode::Mixed : BtI2pMode::I2pOnly;
        // list held until metadata: still mixed
        if (settings.i2pEnabled && settings.i2pMixed && i2pListWait.contains(id))
            return BtI2pMode::Mixed;
        const auto& trackers = ask(id, Ask::Trackers).trackers;
        const bool any = std::ranges::any_of(trackers, [](const lt::announce_entry& a) { return BtAddSource::isI2pTracker(a.url); });
        return any ? BtI2pMode::Mixed : BtI2pMode::Direct;
    }
    void attachSink(const QString& id) const
    {
        BtJoinDiskIo* disk = io.load();
        const auto j = joins.find(id);
        const lt::info_hash_t hashes = hashesOf(id);   // by hash: also before libtorrent has the torrent
        if (disk && j != joins.end() && (hashes.has_v1() || hashes.has_v2()))
            disk->attach(hashes, std::make_shared<SinkHook>(id, j->second));
    }
    [[nodiscard]] QString linksPath() const { return QDir(settings.stateDir).filePath(kLinksFile); }
    void loadLinks()
    {
        links.clear();
        const QJsonObject root = QJsonDocument::fromJson(readAll(linksPath())).object();
        for (auto it = root.begin(); it != root.end(); ++it)
            links[it.key()] = it.value().toString();
    }
    void saveLinks() const
    {
        QJsonObject root;
        for (const auto& [id, link] : links)
            root.insert(id, link);
        const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
        writeQueued(linksPath(), json);
    }
    /// #224: file index → path used in place (kLocatedFile)
    std::map<QString, std::map<int, QString>> located;
    /// a Locate Files run: scan on a worker, then pause, link, recheck
    struct Locating {
        quint64 run = 0;
        std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
        std::function<void(std::optional<BtLocateReport>)> done;
        BtLocateReport report;
        std::map<int, QString> links;   ///< renames asked, or still to ask
        std::map<int, QString> aside;   ///< file → its partial, moved off the path
        std::map<int, BtLocateCopy> copies;   ///< not proved whole: pieces still to copy
        std::map<int, int> copyWait;          ///< piece given to add_piece → its file
        std::set<int> copiedFiles;
        quint64 copyBatch = 0;
        bool waitingPause = false;
        bool wasPaused = false;
        bool wasAuto = false;
    };
    std::map<QString, Locating> locating;
    quint64 locateRuns = 0;
    /// removed with files, some linked: libtorrent deleted only the part file, these go after
    struct DeleteAfter {
        QString savePath;
        QStringList paths;
    };
    std::map<QString, DeleteAfter> deleteAfter;
    [[nodiscard]] QString locatedPath() const { return QDir(settings.stateDir).filePath(kLocatedFile); }
    void loadLocated()
    {
        located.clear();
        const QJsonObject root = QJsonDocument::fromJson(readAll(locatedPath())).object();
        for (auto it = root.begin(); it != root.end(); ++it) {
            const QJsonObject files = it.value().toObject();
            for (auto f = files.begin(); f != files.end(); ++f)
                located[it.key()][f.key().toInt()] = f.value().toString();
        }
    }
    void saveLocated() const
    {
        QJsonObject root;
        for (const auto& [id, files] : located) {
            QJsonObject o;
            for (const auto& [f, path] : files)
                o.insert(QString::number(f), path);
            root.insert(id, o);
        }
        writeQueued(locatedPath(), QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
    /// Where file @p f is: its found place, else under the save path.
    [[nodiscard]] QString filePath(const QString& id, const lt::file_storage& fs, lt::file_index_t f, const QString& save) const
    {
        if (const auto it = located.find(id); it != located.end())
            if (const auto l = it->second.find(int(f)); l != it->second.end())
                return l->second;
        return QString::fromStdString(fs.file_path(f, save.toStdString()));
    }
    /// Torrent stopped: partials off the path, libtorrent pointed at the found files.
    void linkLocated(const QString& id)
    {
        const auto it = locating.find(id);
        const auto h = handle(id);
        const auto ti = torrentFile(id);
        const auto row = rows.find(id);
        if (it == locating.end())
            return;
        Locating& l = it->second;
        l.waitingPause = false;
        if (!h.is_valid() || !ti || row == rows.end()) {
            l.report.failed += int(l.links.size());
            l.links.clear();
            finishLocate(id);
            return;
        }
        const lt::file_storage& fs = ti->layout();
        // was: rename_file straight on: refused (file_exists) while a partial is at the old path
        for (auto link = l.links.begin(); link != l.links.end();) {
            const QString old = filePath(id, fs, lt::file_index_t(link->first), row->second.savePath);
            if (QFileInfo::exists(old)) {
                const QString aside = old + kLocatedAside;
                QFile::remove(aside);
                if (!QFile::rename(old, aside)) {
                    logWarning(QStringLiteral("BitTorrent: could not move \"%1\" aside").arg(old));
                    ++l.report.failed;
                    link = l.links.erase(link);
                    continue;
                }
                l.aside[link->first] = aside;
            }
            ++link;
        }
        if (l.links.empty()) {
            finishLocate(id);
            return;
        }
        for (const auto& [f, path] : l.links)
            h.rename_file(lt::file_index_t(f), path.toStdString());
    }
    void fileLinked(const QString& id, int file, bool ok)
    {
        const auto it = locating.find(id);
        if (it == locating.end())
            return;
        Locating& l = it->second;
        const auto link = l.links.find(file);
        if (link == l.links.end())
            return;
        const auto aside = l.aside.find(file);
        if (ok) {
            located[id][file] = link->second;
            ++l.report.linked;
            if (aside != l.aside.end())
                QFile::remove(aside->second);
        } else {
            ++l.report.failed;
            if (aside != l.aside.end()) {
                QString back = aside->second;
                back.chop(kLocatedAside.size());
                QFile::rename(aside->second, back);
            }
        }
        if (aside != l.aside.end())
            l.aside.erase(aside);
        l.links.erase(link);
        if (l.links.empty())
            finishLocate(id);
    }
    void finishLocate(const QString& id)
    {
        auto node = locating.extract(id);
        if (node.empty())
            return;
        Locating& l = node.mapped();
        if (const auto h = handle(id); h.is_valid()) {
            if (l.report.linked > 0) {
                saveLocated();
                h.clear_error();
                h.force_recheck();
                saveResume(h);
            } else if (l.report.copied > 0) {
                saveResume(h);
            }
            if (l.wasAuto)
                h.set_flags(lt::torrent_flags::auto_managed);
            if (!l.wasPaused)
                h.resume();
        }
        if (l.done)
            l.done(l.report);
    }
    /// A piece given to add_piece came back: the batch done, the next one.
    void pieceCopied(BtSession* q, const QString& id, int piece, bool ok)
    {
        const auto it = locating.find(id);
        if (it == locating.end())
            return;
        Locating& l = it->second;
        const auto w = l.copyWait.find(piece);
        if (w == l.copyWait.end())
            return;
        if (ok)
            l.copiedFiles.insert(w->second);
        l.copyWait.erase(w);
        if (l.copyWait.empty())
            QMetaObject::invokeMethod(q, [q, id] { q->locateCopyNext(id); }, Qt::QueuedConnection);
    }
    void cancelLocate(const QString& id)
    {
        auto node = locating.extract(id);
        if (node.empty())
            return;
        node.mapped().cancel->store(true);
        if (node.mapped().done)
            node.mapped().done(std::nullopt);
    }
    /// ids stopped at metadata (kStopsFile); default_dont_download is not in the resume data
    QSet<QString> stopAtMeta;
    /// stopped at metadata, not resumed since: nothing wanted, so a finish is not a download
    QSet<QString> metaStopped;
    /// moveStorage() runs (#187): an error left at the old place is cleared once moved
    QSet<QString> moving;
    /// finished, cache flush asked: pread_disk_io holds written blocks in memory, so
    /// torrentFinished waits for cache_flushed_alert (files complete on disk)
    QSet<QString> finishFlush;
    /// flush_cache asks without their alert yet: "on disk" waits for the last one
    std::map<QString, int> flushing;
    void flush(const lt::torrent_handle& h, const QString& id)
    {
        ++flushing[id];
        if (auto un = unflushed.find(id); un != unflushed.end()) {
            flushingPieces[id].unite(un->second);
            unflushed.erase(un);
        }
        h.flush_cache();
    }
    /// pieces passed since the last flush ask: pread_disk_io may hold them in memory
    std::map<QString, QSet<int>> unflushed;
    /// pieces of the flush asks not answered yet
    std::map<QString, QSet<int>> flushingPieces;
    /// streamFrom(): the first missing piece given deadlines; another one clears them
    std::map<QString, int> streamFocus;
    /// "Download preview parts first": first + last pieces of each wanted previewable
    /// file at top priority. Again after each file priority change (it resets them)
    void applyPreviewPieces(const lt::torrent_handle& h, const QString& id);
    /// pieces libtorrent has, from its status updates: havePieces() never waits
    std::map<QString, QBitArray> have;
    /// checked, a join attached: torrentChecked waits for the status with the new pieces
    QSet<QString> checkWaited;
    /// File priorities sent, not yet applied: libtorrent 2.x updates them only when the
    /// disk job returns (file_prio_alert), a poll in between read the old ones (report 21)
    struct PendingPriorities {
        std::vector<lt::download_priority_t> priorities;
        int outstanding = 0;
        QElapsedTimer since;
    };
    std::map<QString, PendingPriorities> pendingPriorities;
    /// torrents a file was deleted from (skipDeletedFile): rechecked together after a pause
    QSet<QString> recheckDue;
    void prioritizeFiles(const lt::torrent_handle& h, const QString& id, std::vector<lt::download_priority_t> prio)
    {
        h.prioritize_files(prio);
        details[id].priorities = prio;
        PendingPriorities& p = pendingPriorities[id];
        p.priorities = std::move(prio);
        ++p.outstanding;
        p.since.start();
    }
    void filePrioritiesApplied(const QString& id)
    {
        auto it = pendingPriorities.find(id);
        if (it != pendingPriorities.end() && --it->second.outstanding <= 0) {
            // an answer asked before the change may have put the old ones in
            details[id].priorities = std::move(it->second.priorities);
            pendingPriorities.erase(it);
        }
    }
    [[nodiscard]] QString stopsPath() const { return QDir(settings.stateDir).filePath(kStopsFile); }
    void loadStops()
    {
        stopAtMeta.clear();
        for (const QJsonValue& v : QJsonDocument::fromJson(readAll(stopsPath())).array())
            stopAtMeta.insert(v.toString());
    }
    void saveStops() const
    {
        if (stopAtMeta.isEmpty()) {
            BackgroundFileWriter::instance().discard(stopsPath());
            QFile::remove(stopsPath());
            return;
        }
        QJsonArray ids;
        for (const QString& id : stopAtMeta)
            ids.append(id);
        const QByteArray json = QJsonDocument(ids).toJson(QJsonDocument::Compact);
        writeQueued(stopsPath(), json);
    }
    /// at metadata: paused, every file back to normal so the user unticks what to skip
    void stopAtMetadata(const lt::torrent_handle& h, const QString& id);
    /// GUI detail data by id: libtorrent's answers to post_*(), never waited for (report 27)
    struct Detail {
        std::vector<std::int64_t> progress;
        bool progressCurrent = false;   ///< answered after the last change of done or state
        bool filesWaited = false;       ///< currentFiles() said nullopt: torrentChanged on the answer
        std::vector<lt::download_priority_t> priorities;
        std::vector<lt::peer_info> peers;
        std::vector<lt::announce_entry> trackers;
        int trackerAsks = 0;            ///< post_trackers without their alert yet
        int staleTrackerAsks = 0;       ///< of those, asked before our last edit: ignored
        std::vector<int> availability;
        std::array<QElapsedTimer, 4> asked;
    };
    enum class Ask { Files, Peers, Trackers, Availability };
    std::map<QString, Detail> details;
    /// added by add(), its add_torrent_alert not in yet: a row, no handle
    struct Adding {
        lt::info_hash_t hashes;
        std::shared_ptr<const lt::torrent_info> ti;
        bool prioritiesChanged = false;   ///< details' priorities go to libtorrent when it comes
        std::optional<lt::remove_flags_t> removed;   ///< removed meanwhile: removed when it comes
    };
    std::map<QString, Adding> adding;
    /// the torrents of the last run added at start, their add_torrent_alert not in yet
    QSet<QString> resuming;
    /// the metadata of @p id, also while it is being added; null: none yet. Never waits
    [[nodiscard]] std::shared_ptr<const lt::torrent_info> torrentFile(const QString& id) const
    {
        if (const lt::torrent_handle h = handle(id); h.is_valid())
            return h.torrent_file();   // a shared_ptr copy
        const auto a = adding.find(id);
        return a == adding.end() ? nullptr : a->second.ti;
    }
    /// info hashes of @p id, also while it is being added; empty: unknown
    [[nodiscard]] lt::info_hash_t hashesOf(const QString& id) const
    {
        if (const lt::torrent_handle h = handle(id); h.is_valid())
            return h.info_hashes();
        const auto a = adding.find(id);
        return a == adding.end() ? lt::info_hash_t{} : a->second.hashes;
    }
    /// the id @p hashes are listed under (added or adding); empty: not listed
    [[nodiscard]] QString listedId(const lt::info_hash_t& hashes) const
    {
        const QString id = BtTorrentView::idOf(hashes);
        if (handles.contains(id) || (adding.contains(id) && !adding.at(id).removed))
            return id;
        // a v2 hash of a hybrid listed under its v1 id; info_hashes() does not wait
        if (hashes.has_v2()) {
            for (const auto& [other, h] : handles)
                if (h.is_valid() && h.info_hashes().v2 == hashes.v2)
                    return other;
            for (const auto& [other, a] : adding)
                if (!a.removed && a.hashes.has_v2() && a.hashes.v2 == hashes.v2)
                    return other;
        }
        return {};
    }
    /// posts the query for @p what unless asked within kDetailAskMs (or @p force)
    Detail& ask(const QString& id, Ask what, bool force = false)
    {
        Detail& dt = details[id];
        const lt::torrent_handle h = handle(id);
        QElapsedTimer& t = dt.asked[static_cast<std::size_t>(what)];
        if (!h.is_valid() || (!force && t.isValid() && t.elapsed() < kDetailAskMs))
            return dt;
        t.start();
        switch (what) {
        case Ask::Files:
            h.post_file_progress({});
            h.post_file_priorities();
            break;
        case Ask::Peers: h.post_peer_info(); break;
        case Ask::Trackers:
            ++dt.trackerAsks;
            h.post_trackers();
            break;
        case Ask::Availability:
            // piece_availability asserts without metadata
            if (const auto ti = h.torrent_file(); ti && ti->is_valid())
                h.post_piece_availability();
            break;
        }
        return dt;
    }
    /// our list after replace_trackers: answers still on their way show the old one
    void setCachedTrackers(const QString& id, std::vector<lt::announce_entry> trackers)
    {
        Detail& dt = details[id];
        dt.trackers = std::move(trackers);
        dt.staleTrackerAsks = dt.trackerAsks;
    }
    /// what an add knows already: trackers and priorities before libtorrent's first answer
    void seedDetail(const QString& id, const lt::add_torrent_params& p)
    {
        Detail& dt = details[id];
        if (dt.trackers.empty())
            for (std::size_t i = 0; i < p.trackers.size(); ++i) {
                lt::announce_entry a(p.trackers[i]);
                a.tier = static_cast<std::uint8_t>(i < p.tracker_tiers.size() ? p.tracker_tiers[i] : 0);
                dt.trackers.push_back(std::move(a));
            }
        if (dt.priorities.empty())
            dt.priorities = p.file_priorities;
    }
    /// BtTorrentView::files() of the cache, with priorities still on their way
    [[nodiscard]] QList<BtFileRow> fileRows(const QString& id, const Detail& dt)
    {
        const auto ti = torrentFile(id);
        if (!ti)
            return {};
        QList<BtFileRow> out = BtTorrentView::files(*ti, dt.progress, dt.priorities, dt.availability);
        // report what was asked for until libtorrent has applied it
        if (auto it = pendingPriorities.find(id); it != pendingPriorities.end()) {
            constexpr qint64 kPendingCapMs = 10'000;   // no alert (unchanged / failed): trust libtorrent again
            if (it->second.since.elapsed() > kPendingCapMs) {
                pendingPriorities.erase(it);
            } else {
                const auto& prio = it->second.priorities;
                for (BtFileRow& f : out)
                    if (f.index >= 0 && f.index < static_cast<int>(prio.size()))
                        f.priority = static_cast<int>(static_cast<std::uint8_t>(prio[static_cast<std::size_t>(f.index)]));
            }
        }
        return out;
    }
    /// the app's filter, set again at each start (session state does not keep it)
    lt::ip_filter ipFilter;
    quint64 blockedPeers = 0;

    [[nodiscard]] QString resumePath(const QString& id) const { return QDir(settings.stateDir).filePath(kResumeDir + QLatin1Char('/') + resumeName(id)); }
    [[nodiscard]] lt::torrent_handle handle(const QString& id) const
    {
        auto it = handles.find(id);
        return it == handles.end() || !it->second.is_valid() ? lt::torrent_handle{} : it->second;
    }
    void saveResume(const lt::torrent_handle& h, lt::resume_data_flags_t flags = lt::torrent_handle::save_info_dict)
    {
        if (!h.is_valid())
            return;
        h.save_resume_data(flags);
        ++outstandingResume;
    }
    [[nodiscard]] QString optionsPath() const { return QDir(settings.stateDir).filePath(kOptionsFile); }
    void loadSeedLimits();
    void saveSeedLimits() const;
    [[nodiscard]] QString i2pOnlyPath() const { return QDir(settings.stateDir).filePath(kI2pOnlyFile); }
    void loadI2pOnly();
    void saveI2pOnly() const;
    /// magnets without metadata: URLs of the I2P list added so far; empty = list held back
    std::map<QString, QStringList> i2pListWait;
    [[nodiscard]] QString i2pListWaitPath() const { return QDir(settings.stateDir).filePath(kI2pListWaitFile); }
    void loadI2pListWait()
    {
        i2pListWait.clear();
        const QJsonObject root = QJsonDocument::fromJson(readAll(i2pListWaitPath())).object();
        for (auto it = root.begin(); it != root.end(); ++it) {
            QStringList urls;
            for (const QJsonValue& v : it.value().toArray())
                urls << v.toString();
            i2pListWait[it.key()] = urls;
        }
    }
    void saveI2pListWait() const
    {
        if (i2pListWait.empty()) {
            BackgroundFileWriter::instance().discard(i2pListWaitPath());
            QFile::remove(i2pListWaitPath());
            return;
        }
        QJsonObject root;
        for (const auto& [id, urls] : i2pListWait)
            root.insert(id, QJsonArray::fromStringList(urls));
        writeQueued(i2pListWaitPath(), QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
    void forgetI2pListWait(const QString& id)
    {
        if (i2pListWait.erase(id) > 0)
            saveI2pListWait();
    }
    /// resume data with metadata, the wait not settled before the core stopped
    void settleI2pList(lt::add_torrent_params& atp, const QString& id)
    {
        const auto it = i2pListWait.find(id);
        if (!atp.ti || it == i2pListWait.end())
            return;
        atp.tracker_tiers.resize(atp.trackers.size(), 0);
        if (atp.ti->priv()) {
            for (std::size_t i = atp.trackers.size(); i-- > 0;) {
                if (!it->second.contains(QString::fromStdString(atp.trackers[i])))
                    continue;
                atp.trackers.erase(atp.trackers.begin() + static_cast<std::ptrdiff_t>(i));
                atp.tracker_tiers.erase(atp.tracker_tiers.begin() + static_cast<std::ptrdiff_t>(i));
            }
        } else if (it->second.isEmpty() && settings.i2pEnabled
                   && (settings.i2pMixed || (atp.flags & lt::torrent_flags::i2p_torrent))) {
            for (const QString& url : settings.i2pTrackers) {
                const std::string u = url.trimmed().toStdString();
                if (u.empty() || std::ranges::find(atp.trackers, u) != atp.trackers.end())
                    continue;
                atp.trackers.push_back(u);
                atp.tracker_tiers.push_back(0);
            }
        }
        i2pListWait.erase(it);
        saveI2pListWait();
    }
    /// a new torrent I2P only: clearnet trackers and web seeds out of @p atp into @p s
    static void stripForI2pOnly(lt::add_torrent_params& atp, I2pOnly& s);
    /// @p id (listed) becomes strict I2P only, or sets aside what came since. Sync calls
    void enterI2pOnly(const QString& id, const lt::torrent_handle& h);
    /// @p id back to what it had before it was strict; no-op when it is not
    void leaveI2pOnly(const QString& id, const lt::torrent_handle& h);
    BtAddResult add(BtSession& q, lt::add_torrent_params atp, const QString& savePath, bool paused,
                    std::optional<bool> i2pOnly, std::optional<bool> stopAfterMetadata,
                    const QString& link = {}, const QList<int>& wanted = {});
    /// switch @p wanted files of a listed torrent on, the others as they are
    void wantFiles(const lt::torrent_handle& h, const QString& id, const QList<int>& wanted);
    void enforceSeedLimits(const lt::torrent_status& st, const BtTorrentRow& row);
    struct SeedLimitHit {
        bool ratio = false;
        bool time = false;
    };
    [[nodiscard]] SeedLimitHit seedLimitHit(const BtTorrentRow& row) const;
    /// tracker alerts of .i2p trackers: remember an INVALID_KEY, forget it on a reply
    void noteI2pName(const lt::alert* a);
    /// announce the remembered misses again, past libtorrent's backoff
    void retryI2pNames();
    /// settings as a pack, with the setRateLimits() caps over the configured ones
    [[nodiscard]] lt::settings_pack pack(const BtSettings& s) const;
    /// bytes/s of one counter over at least @p windowMs, from rateSamples
    [[nodiscard]] qint64 rate(qint64 RateSample::*field, qint64 windowMs) const;
    /// Statistics' session BtCounters; null without a core (tests)
    [[nodiscard]] static BtCounters* counters();
    /// fold @p now, already in rateSamples, into the statistics; @p prev null = first since start
    void countSample(const RateSample* prev, const RateSample& now, qint64 peers) const;
    /// comment, creator and creation date by id: in add_torrent_params since 2.1
    struct Meta {
        QString comment;
        QString creator;
        qint64 createdAt = 0;
    };
    std::map<QString, Meta> meta;
    void noteMeta(const QString& id, const lt::add_torrent_params& p)
    {
        if (!p.comment.empty() || !p.created_by.empty() || p.creation_date != 0)
            meta[id] = {QString::fromStdString(p.comment), QString::fromStdString(p.created_by),
                        static_cast<qint64>(p.creation_date)};
    }
    /// BtTorrentView::row() plus what torrent_info no longer has, and the session upload
    [[nodiscard]] BtTorrentRow row(const lt::torrent_status& st)
    {
        BtTorrentRow r = BtTorrentView::row(st);
        if (auto it = meta.find(r.id); it != meta.end()) {
            r.comment = it->second.comment;
            r.creator = it->second.creator;
            r.createdAt = it->second.createdAt;
        }
        static const std::vector<lt::download_priority_t> kAllWanted;
        const auto pending = pendingPriorities.find(r.id);
        const auto dt = details.find(r.id);
        BtTorrentView::exactBytes(r, st, pending != pendingPriorities.end() ? pending->second.priorities
                                         : dt != details.end()            ? dt->second.priorities
                                                                          : kAllWanted);
        const qint64 base = sessionUpBase.try_emplace(r.id, r.uploaded).first->second;
        r.sessionUploaded = std::max<qint64>(0, r.uploaded - base);
        return r;
    }
    /// unset or unlimited per-torrent connections → the configured default; IP filter on
    void defaultConnections(lt::add_torrent_params& atp) const
    {
        if (atp.max_connections <= 0 || atp.max_connections >= kLtNoLimit)
            atp.max_connections = settings.maxConnectionsPerTorrent;
        // the app's IP filter for every torrent, also one whose resume data had it off (#177)
        atp.flags |= lt::torrent_flags::apply_ip_filter;
    }
};

BtSession::BtSession(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    d->timer = new QTimer(this);
    d->timer->setInterval(kTickMs);
    connect(d->timer, &QTimer::timeout, this, &BtSession::tick);
}

BtSession::~BtSession()
{
    stop();
}

void BtSession::start(const BtSettings& settings)
{
    if (d->ses)
        return;
    d->settings = settings;
    QDir().mkpath(QDir(settings.stateDir).filePath(kResumeDir));

    lt::session_params params;
    const QByteArray state = readAll(QDir(settings.stateDir).filePath(kSessionFile));
    if (!state.isEmpty()) {
        try {
            params = lt::read_session_params(lt::span<char const>(state.constData(), static_cast<std::ptrdiff_t>(state.size())),
                                             lt::session_handle::save_dht_state);
        } catch (const std::exception& e) {
            logWarning(QStringLiteral("BitTorrent: session state unreadable, starting fresh: %1").arg(QString::fromUtf8(e.what())));
            params = lt::session_params{};
        }
    }
    params.settings = d->pack(settings);
    params.ip_filter = d->ipFilter;
    // bound interface gone: start paused, applySettings() resumes
    if (settings.networkBlocked)
        params.flags |= lt::session::paused;
    d->blockedPeers = 0;
    params.extensions = extensions(settings);
    params.extensions.push_back(BtJoinDiskIo::pumpPlugin([impl = d.get()] { return impl->io.load(); }));
    params.extensions.push_back(d->peerGate.plugin());
    params.disk_io_constructor = BtJoinDiskIo::constructor([this](BtJoinDiskIo* io) { d->io = io; });
    try {
        d->ses = std::make_unique<lt::session>(std::move(params));
    } catch (const std::exception& e) {
        logError(QStringLiteral("BitTorrent: libtorrent did not start: %1").arg(QString::fromUtf8(e.what())));
        d->ses.reset();
        d->io = nullptr;
        return;
    }
    // late join verdicts back at once, not at the next tick (#152). A weak handle: no-op
    // (throws) once the session is gone
    if (BtJoinDiskIo* io = d->io.load())
        io->setWake([h = lt::session_handle(*d->ses)]() mutable {
            try {
                h.post_dht_stats();
            } catch (const std::exception&) {
            }
        });
    // libtorrent's thread: only wake ours (research doc §5.2)
    d->ses->set_alert_notify([this] { QMetaObject::invokeMethod(this, &BtSession::drainAlerts, Qt::QueuedConnection); });

    d->loadSeedLimits();
    d->loadI2pOnly();
    d->loadI2pListWait();
    d->loadLinks();
    d->loadLocated();
    d->loadStops();
    const qsizetype stopsAtLoad = d->stopAtMeta.size();

    // the torrents of the last run
    const QDir resumeDir(QDir(settings.stateDir).filePath(kResumeDir));
    int loaded = 0;
    for (const QString& file : resumeDir.entryList({QStringLiteral("*") + kResumeSuffix}, QDir::Files)) {
        const QByteArray data = readAll(resumeDir.filePath(file));
        lt::error_code ec;
        lt::add_torrent_params atp = readResume(data, ec);
        if (ec) {
            logWarning(QStringLiteral("BitTorrent: resume data %1 unreadable: %2").arg(file, QString::fromStdString(ec.message())));
            continue;
        }
        if (atp.save_path.empty())
            atp.save_path = settings.downloadDir.toStdString();
        if (const QString id = BtTorrentView::idOf(atp.ti ? atp.ti->info_hashes() : atp.info_hashes);
            d->stopAtMeta.contains(id)) {
            if (!atp.ti) {
                atp.flags |= lt::torrent_flags::default_dont_download;
            } else {
                // metadata came in, the stop did not make it to disk
                atp.flags |= lt::torrent_flags::paused;
                atp.flags &= ~lt::torrent_flags::auto_managed;
                atp.file_priorities.assign(static_cast<std::size_t>(atp.ti->num_files()), lt::default_priority);
                d->stopAtMeta.remove(id);
            }
        }
        d->settleI2pList(atp, BtTorrentView::idOf(atp.ti ? atp.ti->info_hashes() : atp.info_hashes));
        // older runs put all I2P trackers in one tier: only the first one was asked
        BtAddSource::spreadI2pTiers(atp.trackers, atp.tracker_tiers);
        d->defaultConnections(atp);
        const lt::info_hash_t resumed = atp.ti ? atp.ti->info_hashes() : atp.info_hashes;
        if (d->i2pOnly.contains(BtTorrentView::idOf(resumed)))
            d->peerGate.setStrict(resumed, true);
        d->resuming.insert(BtTorrentView::idOf(resumed));
        d->ses->async_add_torrent(std::move(atp));
        ++loaded;
    }
    if (d->stopAtMeta.size() != stopsAtLoad)
        d->saveStops();
    d->ticks = 0;
    d->timer->start();
    logInfo(QStringLiteral("BitTorrent: libtorrent %1 started, port %2, %3 torrent(s) resumed")
                .arg(QLatin1StringView(LIBTORRENT_VERSION))
                .arg(settings.listenPort)
                .arg(loaded));
    emit statusChanged();
}

void BtSession::stop()
{
    if (!d->ses)
        return;
    while (!d->locating.empty())
        d->cancelLocate(d->locating.begin()->first);
    d->timer->stop();
    d->ses->pause();
    // what alerts queued lands first: the writes below must not be overtaken
    BackgroundFileWriter::instance().flush();
    for (const auto& [id, h] : d->handles)
        d->saveResume(h, lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified);

    // drain until every resume data came, or the time is up
    QElapsedTimer waited;
    waited.start();
    while ((d->outstandingResume > 0 || !d->adding.empty()) && waited.elapsed() < kStopWaitMs) {
        if (!d->ses->wait_for_alert(lt::milliseconds(200)))
            continue;
        std::vector<lt::alert*> alerts;
        d->ses->pop_alerts(&alerts);
        for (lt::alert* a : alerts) {
            if (auto* at = lt::alert_cast<lt::add_torrent_alert>(a)) {
                // added just before the stop: its resume data, or it is gone at the next start
                const auto& p = at->params;
                const QString id = BtTorrentView::idOf(p.ti ? p.ti->info_hashes() : p.info_hashes);
                const auto pending = d->adding.extract(id);
                if (at->error || pending.empty())
                    continue;
                if (pending.mapped().removed) {
                    d->ses->remove_torrent(at->handle, *pending.mapped().removed);
                    continue;
                }
                if (pending.mapped().prioritiesChanged)
                    at->handle.prioritize_files(d->details[id].priorities);
                d->saveResume(at->handle);
            } else if (auto* rd = lt::alert_cast<lt::save_resume_data_alert>(a)) {
                --d->outstandingResume;
                writeAtomic(d->resumePath(BtTorrentView::idOf(rd->params.info_hashes)), lt::write_resume_data_buf(rd->params));
            } else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                --d->outstandingResume;
            }
        }
    }
    if (d->outstandingResume > 0)
        logWarning(QStringLiteral("BitTorrent: %1 torrent(s) without fresh resume data at stop").arg(d->outstandingResume));

    writeAtomic(QDir(d->settings.stateDir).filePath(kSessionFile), lt::write_session_params_buf(d->ses->session_state(lt::session_handle::save_dht_state)));

    d->ses->set_alert_notify([] {});
    // the pump stops before the disk I/O goes
    d->io = nullptr;
    // the proxy's destructor waits for libtorrent's threads, away from the session's
    lt::session_proxy proxy = d->ses->abort();
    d->ses.reset();
    d->handles.clear();
    d->adding.clear();
    d->resuming.clear();
    d->rows.clear();
    d->finishFlush.clear();
    d->flushing.clear();
    d->unflushed.clear();
    d->flushingPieces.clear();
    d->streamFocus.clear();
    d->have.clear();
    d->checkWaited.clear();
    d->i2pNameMisses.clear();
    d->outstandingResume = 0;
    d->listening = false;
    d->randomPort = 0;
    d->dhtNodes = 0;
    d->dhtNodesBySocket.clear();
    d->externalIPv4.clear();
    d->externalIPv6.clear();
    d->rateSamples.clear();
    d->meta.clear();
    logInfo(QStringLiteral("BitTorrent: stopped"));
    emit statusChanged();
}

bool BtSession::isRunning() const
{
    return d->ses != nullptr;
}

bool BtSession::isResuming(const QString& id) const
{
    return d->resuming.contains(id);
}

void BtSession::applySettings(const BtSettings& settings)
{
    const int oldPerTorrent = d->settings.maxConnectionsPerTorrent;
    const bool wasBlocked = d->settings.networkBlocked;
    const bool wasPreviewFirst = d->settings.previewPiecesFirst;
    if (d->settings.i2pSamHost != settings.i2pSamHost || d->settings.i2pSamPort != settings.i2pSamPort
        || d->settings.i2pEnabled != settings.i2pEnabled)
        d->i2pError.clear();
    // a new global limit is the user's word: it applies again
    if (d->settings.seedRatioPercent != settings.seedRatioPercent
        || d->settings.seedTimeLimitMinutes != settings.seedTimeLimitMinutes)
        d->limitOverride.clear();
    d->settings = settings;
    if (!d->ses)
        return;
    if (settings.networkBlocked && !wasBlocked) {
        d->ses->pause();
        d->listening = false;
    }
    d->ses->apply_settings(d->pack(settings));
    // preview pieces off: the file priorities set the pieces anew
    if (wasPreviewFirst != settings.previewPiecesFirst)
        for (const auto& [id, h] : d->handles) {
            if (!h.is_valid())
                continue;
            if (settings.previewPiecesFirst)
                d->applyPreviewPieces(h, id);
            else if (const auto dt = d->details.find(id); dt != d->details.end() && !dt->second.priorities.empty())
                d->prioritizeFiles(h, id, dt->second.priorities);
        }
    d->dhtNodesBySocket.clear();   // listen sockets may have changed; next dht_stats refills
    if (!settings.networkBlocked && wasBlocked)
        d->ses->resume();
    // torrents on the old default follow the new one; a custom limit stays
    if (oldPerTorrent != settings.maxConnectionsPerTorrent)
        for (const auto& [id, h] : d->handles)
            if (h.is_valid() && h.max_connections() == oldPerTorrent)
                h.set_max_connections(settings.maxConnectionsPerTorrent);
}

void BtSession::setRateLimits(quint32 downKb, quint32 upKb)
{
    const std::pair limits{BtSettingsMap::rateBytes(downKb), BtSettingsMap::rateBytes(upKb)};
    if (d->rateLimits == limits)
        return;
    d->rateLimits = limits;
    if (!d->ses)
        return;
    lt::settings_pack p;
    p.set_int(lt::settings_pack::download_rate_limit, limits.first);
    p.set_int(lt::settings_pack::upload_rate_limit, limits.second);
    d->ses->apply_settings(std::move(p));
}

void BtSession::setIpFilter(const std::vector<BtIpRange4>& v4, const std::vector<BtIpRange6>& v6)
{
    lt::ip_filter f;
    for (const BtIpRange4& r : v4)
        f.add_rule(lt::address_v4(r.first), lt::address_v4(r.last), lt::ip_filter::blocked);
    for (const BtIpRange6& r : v6)
        f.add_rule(lt::address_v6(r.first), lt::address_v6(r.last), lt::ip_filter::blocked);
    d->ipFilter = std::move(f);
    if (d->ses)
        d->ses->set_ip_filter(d->ipFilter);
    logTorrentDebug([&]() -> QString { return QStringLiteral("BitTorrent: IP filter of %1 range(s)").arg(v4.size() + v6.size()); });
}

quint64 BtSession::blockedPeers() const
{
    return d->blockedPeers;
}

BtDemand BtSession::demand()
{
    BtDemand out;
    if (!d->ses)
        return out;
    for (const auto& [id, row] : d->rows) {
        if (row.paused)
            continue;
        if (row.state == BtState::Downloading || row.state == BtState::DownloadingMetadata)
            out.downActive = true;
        if ((row.state == BtState::Downloading || row.state == BtState::Seeding
             || row.state == BtState::Finished)
            && row.peers > 0)
            out.upActive = true;
    }
    out.downRate = d->rate(&Impl::RateSample::recv, kRateWindowMs);
    out.downRecent = d->rate(&Impl::RateSample::recv, kRecentWindowMs);
    out.upRate = d->rate(&Impl::RateSample::sent, kRateWindowMs);
    out.upRecent = d->rate(&Impl::RateSample::sent, kRecentWindowMs);
    // answered by a session_stats_alert, sampled in drainAlerts()
    d->ses->post_session_stats();
    return out;
}

BtAddResult BtSession::addTorrentFile(const QByteArray& data, const QString& savePath, bool paused, std::optional<bool> i2pOnly,
                                      std::optional<bool> stopAfterMetadata, const QList<int>& wantedFiles)
{
    if (!d->ses)
        return {BtAddOutcome::NotRunning, {}, QStringLiteral("BitTorrent is off")};
    auto atp = BtAddSource::fromTorrentFile(data);
    if (!atp)
        return {BtAddOutcome::Invalid, {}, atp.error()};
    return d->add(*this, std::move(*atp), savePath, paused, i2pOnly, stopAfterMetadata, {}, wantedFiles);
}

BtAddResult BtSession::addCreatedTorrent(const QByteArray& data, const QString& savePath)
{
    if (!d->ses)
        return {BtAddOutcome::NotRunning, {}, QStringLiteral("BitTorrent is off")};
    auto atp = BtAddSource::fromTorrentFile(data);
    if (!atp)
        return {BtAddOutcome::Invalid, {}, atp.error()};
    // the pieces were hashed from these files a moment ago (qBittorrent: seedMode)
    atp->flags |= lt::torrent_flags::seed_mode;
    return d->add(*this, std::move(*atp), savePath, false, std::nullopt, false);
}

std::expected<QByteArray, QString> BtSession::exportTorrent(const QString& id) const
{
    const auto ti = d->torrentFile(id);
    if (!ti || !ti->is_valid())
        return std::unexpected(d->rows.contains(id) ? QStringLiteral("the torrent has no metadata yet")
                                                    : QStringLiteral("unknown torrent"));
    lt::entry e;
    const auto info = ti->info_section();
    e["info"].preformatted().assign(info.data(), info.data() + info.size());
    // comment, creator, date: kept beside torrent_info since 2.1 (Impl::meta)
    if (const auto m = d->meta.find(id); m != d->meta.end()) {
        if (!m->second.comment.isEmpty())
            e["comment"] = m->second.comment.toStdString();
        if (!m->second.creator.isEmpty())
            e["created by"] = m->second.creator.toStdString();
        if (m->second.createdAt != 0)
            e["creation date"] = m->second.createdAt;
    }

    const lt::torrent_handle h = d->handle(id);
    if (!h.is_valid())
        return std::unexpected(QStringLiteral("the torrent is still being added"));
    // trackers and web seeds as now (edited ones too); sync calls, on a user's request only
    const std::vector<lt::announce_entry> trackers = h.trackers();
    const std::set<std::string> webSeeds = h.url_seeds();
    if (!trackers.empty()) {
        e["announce"] = trackers.front().url;
        auto& tiers = e["announce-list"].list();
        int tier = -1;
        for (const auto& t : trackers) {
            if (tiers.empty() || t.tier != tier) {
                tiers.emplace_back(lt::entry::list_type{});
                tier = t.tier;
            }
            tiers.back().list().emplace_back(t.url);
        }
    }
    if (!webSeeds.empty()) {
        auto& urls = e["url-list"].list();
        for (const auto& w : webSeeds)
            urls.emplace_back(w);
    }

    // BEP 52: a layer per file longer than a piece, keyed by its root
    if (ti->v2()) {
        const auto layers = h.piece_layers();
        const lt::file_storage& fs = ti->layout();
        if (int(layers.size()) != fs.num_files())
            return std::unexpected(QStringLiteral("the v2 piece hashes are not known yet"));
        auto& out = e["piece layers"].dict();
        for (const lt::file_index_t f : fs.file_range()) {
            if (fs.pad_file_at(f) || fs.file_size(f) <= fs.piece_length())
                continue;
            const auto& layer = layers[std::size_t(static_cast<int>(f))];
            if (int(layer.size()) != fs.file_num_pieces(f)
                || std::ranges::any_of(layer, [](const lt::sha256_hash& x) { return x.is_all_zeros(); }))
                return std::unexpected(QStringLiteral("the v2 piece hashes are not known yet"));
            std::string& bytes = out[fs.root(f).to_string()].string();
            for (const auto& x : layer)
                bytes += x.to_string();
        }
    }
    std::vector<char> buf;
    lt::bencode(std::back_inserter(buf), e);
    return QByteArray(buf.data(), qsizetype(buf.size()));
}

std::expected<BtTorrentPreview, QString> BtSession::inspectTorrentFile(const QByteArray& data)
{
    auto atp = BtAddSource::fromTorrentFile(data);
    if (!atp)
        return std::unexpected(atp.error());
    return BtTorrentView::preview(*atp);
}

BtAddResult BtSession::addMagnet(const QString& uri, const QString& savePath, bool paused, std::optional<bool> i2pOnly,
                                 std::optional<bool> stopAfterMetadata)
{
    if (!d->ses)
        return {BtAddOutcome::NotRunning, {}, QStringLiteral("BitTorrent is off")};
    auto atp = BtAddSource::fromMagnet(uri);
    if (!atp)
        return {BtAddOutcome::Invalid, {}, atp.error()};
    return d->add(*this, std::move(*atp), savePath, paused, i2pOnly, stopAfterMetadata, BtAddSource::ed3kLinkOf(uri));
}

bool BtSession::remove(const QString& id, bool deleteFiles)
{
    const auto h = d->handle(id);
    const auto pending = d->adding.find(id);
    if (!d->ses || (!h.is_valid() && pending == d->adding.end()))
        return false;
    const lt::info_hash_t hashes = d->hashesOf(id);
    d->cancelLocate(id);
    lt::remove_flags_t flags = deleteFiles ? lt::session_handle::delete_files : lt::remove_flags_t{};
    // libtorrent's delete_files follows renames to absolute paths: it would delete the user's
    // originals. Only what lies under the save path goes (torrent_deleted_alert).
    if (const auto linked = d->located.find(id); linked != d->located.end()) {
        const auto ti = d->torrentFile(id);
        const auto row = d->rows.find(id);
        if (deleteFiles && ti && ti->is_valid() && row != d->rows.end()) {
            flags = lt::session_handle::delete_partfile;
            Impl::DeleteAfter after{row->second.savePath, {}};
            const lt::file_storage& fs = ti->layout();
            for (const lt::file_index_t f : fs.file_range())
                if (!fs.pad_file_at(f) && !linked->second.contains(int(f)))
                    after.paths << QString::fromStdString(fs.file_path(f, after.savePath.toStdString()));
            d->deleteAfter[id] = std::move(after);
        }
        d->located.erase(linked);
        d->saveLocated();
    }
    if (h.is_valid())
        d->ses->remove_torrent(h, flags);
    else
        pending->second.removed = flags;   // removed when libtorrent has added it
    d->handles.erase(id);
    d->rows.erase(id);
    d->sessionUpBase.erase(id);
    d->limitOverride.remove(id);
    d->metaStopped.remove(id);
    d->finishFlush.remove(id);
    d->flushing.erase(id);
    d->unflushed.erase(id);
    d->flushingPieces.erase(id);
    d->streamFocus.erase(id);
    d->have.erase(id);
    d->checkWaited.remove(id);
    d->pendingPriorities.erase(id);
    d->details.erase(id);
    d->i2pNameMisses.erase(id);
    if (d->links.erase(id) > 0)
        d->saveLinks();
    if (d->stopAtMeta.remove(id))
        d->saveStops();
    d->forgetI2pListWait(id);
    if (auto* disk = d->io.load(); disk && d->joins.contains(id))
        disk->detach(hashes);
    d->joins.erase(id);
    if (d->seedLimits.erase(id) > 0)
        d->saveSeedLimits();
    d->i2pOnlySweep.remove(id);
    if (d->i2pOnly.erase(id) > 0) {
        d->peerGate.setStrict(hashes, false);
        d->saveI2pOnly();
    }
    BackgroundFileWriter::instance().discard(d->resumePath(id));
    QFile::remove(d->resumePath(id));
    emit torrentRemoved(id);
    return true;
}

bool BtSession::pause(const QString& id)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    h.unset_flags(lt::torrent_flags::auto_managed);
    h.pause();
    return true;
}

bool BtSession::resume(const QString& id, bool byUser)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    // only a seed the user resumes at its limit seeds past it
    if (auto it = d->rows.find(id); byUser && it != d->rows.end() && it->second.paused
        && it->second.wantedDone) {
        const auto hit = d->seedLimitHit(it->second);
        if (hit.ratio || hit.time)
            d->limitOverride.insert(id);
    }
    d->metaStopped.remove(id);
    // a write error (disk full) is tried again
    h.clear_error();
    h.unset_flags(lt::torrent_flags::upload_mode);
    h.set_flags(lt::torrent_flags::auto_managed);
    h.resume();
    return true;
}

void BtSession::locateFiles(const QString& id, const QStringList& folders, std::function<void(std::optional<BtLocateReport>)> done)
{
    const auto h = d->handle(id);
    const auto ti = d->torrentFile(id);
    const auto row = d->rows.find(id);
    if (!h.is_valid() || !ti || !ti->is_valid() || row == d->rows.end() || d->locating.contains(id) || folders.isEmpty()) {
        done(std::nullopt);
        return;
    }
    const lt::file_storage& fs = ti->layout();
    const QBitArray have = havePieces(id);
    const auto linked = d->located.find(id);
    std::vector<int> targets;
    std::map<int, QString> current;
    for (const lt::file_index_t f : fs.file_range()) {
        if (fs.pad_file_at(f))
            continue;
        current[int(f)] = d->filePath(id, fs, f, row->second.savePath);
        if (fs.file_size(f) <= 0 || (linked != d->located.end() && linked->second.contains(int(f))))
            continue;
        const int first = int(fs.map_file(f, 0, 0).piece);
        const int last = int(fs.map_file(f, fs.file_size(f) - 1, 0).piece);
        bool complete = have.size() > last;
        for (int p = first; complete && p <= last; ++p)
            complete = have.testBit(p);
        if (!complete)
            targets.push_back(int(f));
    }
    if (targets.empty()) {
        done(BtLocateReport{});
        return;
    }
    auto& l = d->locating[id];
    l.run = ++d->locateRuns;
    l.done = std::move(done);
    const quint64 run = l.run;
    const auto cancel = l.cancel;
    QPointer<BtSession> self(this);
    QThreadPool::globalInstance()->start([self, id, run, cancel, ti, folders, targets, current] {
        BtLocateScan scan = locateTorrentFiles(*ti, folders, targets, current, cancel.get());
        QMetaObject::invokeMethod(self.data(), [self, id, run, scan = std::move(scan)] {
            if (!self)
                return;
            const auto it = self->d->locating.find(id);
            if (it == self->d->locating.end() || it->second.run != run)
                return;
            auto& st = it->second;
            st.report.files = scan.files;
            st.report.candidates = scan.candidates;
            st.report.hashed = scan.hashed;
            st.report.partial = scan.partial;
            st.links = scan.found;
            st.copies = scan.copies;
            self->locateCopyNext(id);
        }, Qt::QueuedConnection);
    });
}

std::map<int, QString> BtSession::locatedFiles(const QString& id) const
{
    const auto it = d->located.find(id);
    return it == d->located.end() ? std::map<int, QString>{} : it->second;
}

bool BtSession::recheck(const QString& id)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    h.force_recheck();
    return true;
}

QStringList BtSession::skipDeletedFile(const QString& path)
{
    QStringList emptied;
    if (!d->ses)
        return emptied;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif
    const QString target = QDir::cleanPath(QDir::fromNativeSeparators(path));
    for (const auto& [id, h] : d->handles) {
        const auto row = d->rows.find(id);
        if (!h.is_valid() || row == d->rows.end())
            continue;
        const QString savePath = QDir::cleanPath(QDir::fromNativeSeparators(row->second.savePath));
        const auto linkedHere = d->located.find(id);
        const bool hasLinks = linkedHere != d->located.end();
        if (!hasLinks && (savePath.isEmpty() || !target.startsWith(savePath + u'/', kPathCase)))
            continue;
        const auto ti = d->torrentFile(id);
        if (!ti || !ti->is_valid())
            continue;
        const QString relative = target.mid(savePath.size() + 1);
        const lt::file_storage& fs = ti->layout();
        std::optional<lt::file_index_t> hit;
        for (const lt::file_index_t f : fs.file_range()) {
            // a file used in place is only at its found path
            if (hasLinks) {
                if (const auto l = linkedHere->second.find(int(f)); l != linkedHere->second.end()) {
                    if (!fs.pad_file_at(f) && QDir::cleanPath(QDir::fromNativeSeparators(l->second)).compare(target, kPathCase) == 0) {
                        hit = f;
                        break;
                    }
                    continue;
                }
            }
            if (!fs.pad_file_at(f) && target.startsWith(savePath + u'/', kPathCase)
                && QDir::fromNativeSeparators(QString::fromStdString(fs.file_path(f))).compare(relative, kPathCase) == 0) {
                hit = f;
                break;
            }
        }
        if (!hit)
            continue;

        // a change still on its way counts: libtorrent updates its list only once the disk job is done
        const auto pending = d->pendingPriorities.find(id);
        std::vector<lt::download_priority_t> prio =
            pending != d->pendingPriorities.end() ? pending->second.priorities : h.get_file_priorities();
        prio.resize(static_cast<std::size_t>(fs.num_files()), lt::default_priority);
        prio[static_cast<std::size_t>(static_cast<int>(*hit))] = lt::dont_download;
        d->prioritizeFiles(h, id, prio);
        logInfo(QStringLiteral("BitTorrent: \"%1\" deleted, skipped in %2").arg(relative, row->second.name));

        bool wantsMore = false;
        for (const lt::file_index_t f : fs.file_range()) {
            if (!fs.pad_file_at(f) && fs.file_size(f) > 0
                && prio[static_cast<std::size_t>(static_cast<int>(f))] != lt::dont_download) {
                wantsMore = true;
                break;
            }
        }
        if (!wantsMore)
            emptied << id;

        // a skipped file's pieces stay "have" (served, and a read of the gone file errors
        // the torrent) until a recheck drops them; skipped, they are never fetched again
        d->recheckDue.insert(id);
        if (d->recheckDue.size() == 1) {
            constexpr int kRecheckDelayMs = 2000;   // a batch delete: one recheck per torrent
            QTimer::singleShot(kRecheckDelayMs, this, [this] {
                for (const QString& due : std::exchange(d->recheckDue, {})) {
                    if (const auto dh = d->handle(due); dh.is_valid()) {
                        dh.clear_error();
                        dh.force_recheck();
                    }
                }
            });
        }
    }
    return emptied;
}

bool BtSession::moveStorage(const QString& id, const QString& dir)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    const QString target = QDir::cleanPath(dir);
    if (auto it = d->rows.find(id); it != d->rows.end() && QDir::cleanPath(it->second.savePath) == target)
        return true;
    if (!QDir().mkpath(target)) {
        logError(QStringLiteral("BitTorrent: cannot create %1 to move a torrent there").arg(target));
        return false;
    }
    d->moving.insert(id);
    // files already at the target are taken as they are, never overwritten
    h.move_storage(QDir::toNativeSeparators(target).toStdString(), lt::move_flags_t::dont_replace);
    h.post_status();
    return true;
}

bool BtSession::reannounce(const QString& id)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    h.force_reannounce();
    h.force_dht_announce();
    return true;
}

bool BtSession::setFilePriorities(const QString& id, const QList<int>& priorities)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    std::vector<lt::download_priority_t> prio;
    prio.reserve(static_cast<std::size_t>(priorities.size()));
    for (int p : priorities)
        prio.emplace_back(static_cast<std::uint8_t>(std::clamp(p, 0, 7)));
    d->prioritizeFiles(h, id, std::move(prio));
    return true;
}

std::optional<BtTorrentOptions> BtSession::options(const QString& id) const
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return std::nullopt;
    BtTorrentOptions o;
    if (auto it = d->seedLimits.find(id); it != d->seedLimits.end())
        o = it->second;
    o.downloadLimitKb = std::max(0, h.download_limit()) / 1024;
    o.uploadLimitKb = std::max(0, h.upload_limit()) / 1024;
    const int peers = h.max_connections();
    // 0 = the configured default per torrent
    o.maxPeers = peers <= 0 || peers >= kLtNoLimit || peers == d->settings.maxConnectionsPerTorrent ? 0 : peers;
    o.i2pMode = d->i2pModeOf(id);
    return o;
}

bool BtSession::setOptions(const QString& id, const BtTorrentOptions& options)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    // Every peer is in the session's class as well as the torrent's, so these never
    // exceed the session caps, which follow BitTorrent's share of the global limits.
    h.set_download_limit(options.downloadLimitKb > 0 ? options.downloadLimitKb * 1024 : 0);
    h.set_upload_limit(options.uploadLimitKb > 0 ? options.uploadLimitKb * 1024 : 0);
    h.set_max_connections(options.maxPeers > 0 ? options.maxPeers : d->settings.maxConnectionsPerTorrent);
    if (options.i2pMode && *options.i2pMode != d->i2pModeOf(id))
        setI2pMode(id, *options.i2pMode);

    BtTorrentOptions limits;
    limits.ratioMode = options.ratioMode;
    // 0 = not given: the defaults the GUI offers
    limits.ratioPercent = options.ratioPercent > 0 ? options.ratioPercent : 100;
    limits.seedTimeMode = options.seedTimeMode;
    limits.seedTimeMinutes = options.seedTimeMinutes > 0 ? options.seedTimeMinutes : 60;
    const bool isDefault = limits.ratioMode == BtLimitMode::Global && limits.seedTimeMode == BtLimitMode::Global;
    auto it = d->seedLimits.find(id);
    if (isDefault ? it != d->seedLimits.end() : (it == d->seedLimits.end() || !(it->second == limits))) {
        if (isDefault)
            d->seedLimits.erase(it);
        else
            d->seedLimits[id] = limits;
        // a new limit is the user's word: it applies again
        d->limitOverride.remove(id);
        d->saveSeedLimits();
    }
    d->saveResume(h);
    return true;
}

void BtSession::setI2pMode(const QString& id, BtI2pMode mode)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return;
    const bool flag = mode == BtI2pMode::I2pOnly;
    h.set_flags(flag ? lt::torrent_flags::i2p_torrent : lt::torrent_flags_t{}, lt::torrent_flags::i2p_torrent);
    if (auto r = d->rows.find(id); r != d->rows.end())
        r->second.i2p = flag;
    // first: the clearnet trackers come back before Direct edits the list
    if (!flag)
        d->leaveI2pOnly(id, h);
    if (mode == BtI2pMode::Direct) {
        QStringList i2pUrls;
        for (const auto& a : d->ask(id, Impl::Ask::Trackers).trackers)
            if (BtAddSource::isI2pTracker(a.url))
                i2pUrls << QString::fromStdString(a.url);
        removeTrackers(id, i2pUrls);
        d->forgetI2pListWait(id);
        d->peerGate.drop(h.info_hashes(), BtPeerGate::Drop::I2p);
    } else {
        // private torrents keep their own trackers (as BtAddSource::applyI2p)
        if (const auto ti = d->torrentFile(id); ti && !ti->priv()) {
            addTrackers(id, d->settings.i2pTrackers);
        } else if (!ti) {
            // no metadata: same rule as a new magnet
            QStringList missing;
            bool ownTracker = false;
            const auto& current = d->ask(id, Impl::Ask::Trackers).trackers;
            for (const auto& a : current)
                ownTracker |= !d->settings.i2pTrackers.contains(QString::fromStdString(a.url));
            for (const QString& url : d->settings.i2pTrackers)
                if (std::ranges::find(current, url.toStdString(), &lt::announce_entry::url) == current.end())
                    missing << url;
            if (!flag && ownTracker) {
                d->i2pListWait.try_emplace(id);
            } else if (!missing.isEmpty()) {
                addTrackers(id, missing);
                d->i2pListWait[id] << missing;
            }
            d->saveI2pListWait();
        }
        if (flag) {
            d->enterI2pOnly(id, h);
            d->peerGate.drop(h.info_hashes(), BtPeerGate::Drop::Direct);
        }
    }
    d->saveResume(h);
    logInfo(QStringLiteral("BitTorrent: %1 now %2").arg(d->rows.contains(id) ? d->rows[id].name : id,
            mode == BtI2pMode::Direct ? QStringLiteral("direct only") : flag ? QStringLiteral("I2P only")
                                                                            : QStringLiteral("I2P and direct")));
}

bool BtSession::addTrackers(const QString& id, const QStringList& urls)
{
    return editTrackers(id, {urls, {}, false}).has_value();
}

bool BtSession::removeTrackers(const QString& id, const QStringList& urls)
{
    return editTrackers(id, {{}, urls, false}).has_value();
}

std::optional<bool> BtSession::editTrackers(const QString& id, const BtTrackerEdit& edit)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return std::nullopt;
    // the cache, not h.trackers(): that waits for libtorrent's thread
    const std::vector<lt::announce_entry> current = d->ask(id, Impl::Ask::Trackers).trackers;
    QList<BtTrackerEntry> before;
    before.reserve(static_cast<qsizetype>(current.size()));
    for (const auto& a : current)
        before.append({QString::fromStdString(a.url), static_cast<int>(a.tier)});
    QList<BtTrackerEntry> after = editTrackerList(before, edit);
    if (const auto strict = d->i2pOnly.find(id); strict != d->i2pOnly.end()) {
        // I2P only: a clearnet tracker waits until the torrent leaves it
        auto& aside = strict->second.trackers;
        bool setAside = false;
        for (const BtTrackerEntry& e : after) {
            if (BtAddSource::isI2pTracker(e.url.toStdString()))
                continue;
            const std::string url = e.url.toStdString();
            if (std::ranges::find(aside, url, &std::pair<std::string, int>::first) == aside.end())
                aside.emplace_back(url, e.tier);
            setAside = true;
        }
        after.removeIf([](const BtTrackerEntry& e) { return !BtAddSource::isI2pTracker(e.url.toStdString()); });
        if (setAside)
            d->saveI2pOnly();
    }
    {
        std::vector<std::string> urls;
        std::vector<int> tiers;
        for (const BtTrackerEntry& e : after) {
            urls.push_back(e.url.toStdString());
            tiers.push_back(e.tier);
        }
        BtAddSource::spreadI2pTiers(urls, tiers);
        for (qsizetype i = 0; i < after.size(); ++i)
            after[i].tier = tiers[static_cast<std::size_t>(i)];
    }
    if (after == before)
        return false;

    // kept entries keep their state; new ones start fresh
    std::vector<lt::announce_entry> next;
    next.reserve(static_cast<std::size_t>(after.size()));
    bool added = false;
    for (const BtTrackerEntry& e : after) {
        const auto old = std::ranges::find_if(current, [&e](const lt::announce_entry& a) {
            return QString::fromStdString(a.url) == e.url;
        });
        if (old != current.end()) {
            next.push_back(*old);
            next.back().tier = static_cast<std::uint8_t>(e.tier);
            continue;
        }
        lt::announce_entry a(e.url.toStdString());
        a.tier = static_cast<std::uint8_t>(e.tier);
        next.push_back(a);
        added = true;
    }
    h.replace_trackers(next);
    d->setCachedTrackers(id, next);
    if (added)
        h.force_reannounce();
    d->saveResume(h);
    return true;
}

QList<BtTorrentRow> BtSession::torrents() const
{
    QList<BtTorrentRow> out;
    out.reserve(static_cast<qsizetype>(d->rows.size()));
    for (const auto& [id, row] : d->rows)
        out.append(row);
    return out;
}

std::optional<BtTorrentRow> BtSession::torrent(const QString& id) const
{
    auto it = d->rows.find(id);
    if (it == d->rows.end())
        return std::nullopt;
    return it->second;
}

QList<BtFileRow> BtSession::files(const QString& id) const
{
    if (!d->handle(id).is_valid() && !d->adding.contains(id))
        return {};
    return d->fileRows(id, d->ask(id, Impl::Ask::Files));
}

std::optional<QList<BtFileRow>> BtSession::currentFiles(const QString& id) const
{
    if (!d->handle(id).is_valid())
        return QList<BtFileRow>{};
    Impl::Detail& dt = d->details[id];
    if (dt.progressCurrent)
        return d->fileRows(id, dt);
    // forced: the answer to an earlier ask may predate the change
    d->ask(id, Impl::Ask::Files, true);
    dt.filesWaited = true;
    return std::nullopt;
}

QList<BtSavedTorrent> BtSession::savedTorrents(const QString& stateDir, const QString& downloadDir)
{
    QList<BtSavedTorrent> out;
    const QDir resumeDir(QDir(stateDir).filePath(kResumeDir));
    for (const QString& file : resumeDir.entryList({QStringLiteral("*") + kResumeSuffix}, QDir::Files)) {
        const QByteArray data = readAll(resumeDir.filePath(file));
        lt::error_code ec;
        const lt::add_torrent_params atp = readResume(data, ec);
        if (ec || !atp.ti || !atp.ti->is_valid())
            continue;
        const lt::torrent_info& ti = *atp.ti;
        const lt::file_storage& fs = ti.layout();
        const bool seed = static_cast<bool>(atp.flags & lt::torrent_flags::seed_mode);
        const auto have = [&](lt::piece_index_t p) {
            return seed || (static_cast<int>(p) < atp.have_pieces.size() && atp.have_pieces.get_bit(p));
        };
        BtSavedTorrent t;
        BtTorrentRow& r = t.row;
        r.id = BtTorrentView::idOf(ti.info_hashes());
        r.name = QString::fromStdString(ti.name());
        r.savePath = atp.save_path.empty() ? downloadDir : QString::fromStdString(atp.save_path);
        r.state = BtState::Paused;
        r.isPrivate = ti.priv();
        r.i2p = static_cast<bool>(atp.flags & lt::torrent_flags::i2p_torrent);
        qint64 wanted = 0;
        qint64 wantedDone = 0;
        for (lt::file_index_t i : fs.file_range()) {
            BtFileRow f;
            f.index = static_cast<int>(i);
            f.path = QString::fromStdString(fs.file_path(i)).replace(QLatin1Char('\\'), QLatin1Char('/'));
            f.size = fs.file_size(i);
            f.pad = fs.pad_file_at(i);
            const auto idx = static_cast<std::size_t>(f.index);
            f.priority = idx < atp.file_priorities.size() ? static_cast<int>(static_cast<std::uint8_t>(atp.file_priorities[idx])) : 4;
            bool complete = true;
            if (f.size > 0) {
                const lt::piece_index_t first = fs.map_file(i, 0, 0).piece;
                const lt::piece_index_t last = fs.map_file(i, f.size - 1, 1).piece;
                for (lt::piece_index_t p = first; complete && p <= last; ++p)
                    complete = have(p);
            }
            f.done = complete ? f.size : 0;
            r.totalSize += f.pad ? 0 : f.size;
            r.haveBytes += f.pad ? 0 : f.done;
            if (!f.pad && f.priority > 0) {
                wanted += f.size;
                wantedDone += f.done;
            }
            t.files.append(f);
        }
        r.progress = wanted > 0 ? double(wantedDone) / double(wanted) : 1.0;
        out.append(std::move(t));
    }
    return out;
}

QList<BtPeerRow> BtSession::peers(const QString& id) const
{
    if (!d->handle(id).is_valid())
        return {};
    return BtTorrentView::peers(d->ask(id, Impl::Ask::Peers).peers);
}

QList<BtTrackerRow> BtSession::trackers(const QString& id) const
{
    if (!d->handle(id).is_valid() && !d->adding.contains(id))
        return {};
    return BtTorrentView::trackers(d->ask(id, Impl::Ask::Trackers).trackers);
}

QByteArray BtSession::availabilityMap(const QString& id) const
{
    if (!d->handle(id).is_valid())
        return {};
    return BtTorrentView::availabilityMap(d->ask(id, Impl::Ask::Availability).availability);
}

BtStatus BtSession::status() const
{
    BtStatus s;
    s.running = d->ses != nullptr;
    if (!s.running)
        return s;
    s.listenPort = d->ses->listen_port();
    s.listening = d->listening;
    s.dhtNodes = d->dhtNodes;
    for (const auto& [ep, n] : d->dhtNodesBySocket)
        (ep.address().is_v6() ? s.dhtNodesV6 : s.dhtNodesV4) += n;
    s.i2pEnabled = d->settings.i2pEnabled;
    s.i2pSam = d->settings.hasSam();
    s.i2pError = d->i2pError;
    s.i2pSamHost = d->settings.i2pSamHost;
    s.i2pSamPort = d->settings.i2pSamPort;
    s.i2pMixed = d->settings.i2pMixed;
    s.i2pTunnelLength = d->settings.i2pTunnelLength;
    s.i2pTunnelQuantity = d->settings.i2pTunnelQuantity;
    s.externalIPv4 = d->externalIPv4;
    s.externalIPv6 = d->externalIPv6;
    s.torrents = static_cast<int>(d->rows.size());
    for (const auto& [id, row] : d->rows) {
        s.rateDown += row.rateDown;
        s.rateUp += row.rateUp;
        if (row.i2p) {
            ++s.i2pTorrents;
            s.i2pPeers += row.peers;
        }
    }
    return s;
}

BtJoinDiskIo* BtSession::diskIo() const
{
    return d->io;
}

bool BtSession::attachJoin(const QString& id, std::shared_ptr<BtJoinSink> sink)
{
    if (!sink || (!d->handles.contains(id) && !d->adding.contains(id)))
        return false;
    d->joins[id] = std::move(sink);
    d->attachSink(id);
    return true;
}

void BtSession::detachJoin(const QString& id)
{
    const lt::info_hash_t hashes = d->hashesOf(id);
    if (auto* disk = d->io.load(); disk && (hashes.has_v1() || hashes.has_v2()))
        disk->detach(hashes);
    d->joins.erase(id);
}

QByteArray BtSession::infoSection(const QString& id) const
{
    const auto ti = d->torrentFile(id);
    if (!ti || !ti->is_valid())
        return {};
    const auto info = ti->info_section();
    return QByteArray(info.data(), qsizetype(info.size()));
}

std::optional<BtLayout> BtSession::layout(const QString& id) const
{
    const auto ti = d->torrentFile(id);
    const auto row = d->rows.find(id);
    if (!ti || !ti->is_valid() || row == d->rows.end())
        return std::nullopt;
    // was: status(query_save_path), a sync call; the row has it (no move_storage here)
    const std::string savePath = row->second.savePath.toStdString();
    const lt::file_storage& fs = ti->layout();
    BtLayout out;
    out.pieceLength = ti->piece_length();
    out.pieces = ti->num_pieces();
    out.total = fs.total_size();
    for (const lt::file_index_t f : fs.file_range())
        out.files.append({d->filePath(id, fs, f, row->second.savePath), fs.file_size(f), fs.file_offset(f), fs.pad_file_at(f)});
    return out;
}

bool BtSession::addPiece(const QString& id, int piece, const QByteArray& data)
{
    const auto h = d->handle(id);
    if (!h.is_valid() || data.isEmpty())
        return false;
    // no overwrite_existing: blocks a peer already gave are skipped. Rewriting them into a piece
    // libtorrent has hashed but not flushed trips disk_cache.cpp:283 (and the bytes are the same)
    h.add_piece(lt::piece_index_t(piece), std::vector<char>(data.begin(), data.end()));
    return true;
}

void BtSession::raisePieces(const QString& id, const QList<int>& pieces)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return;
    for (const int p : pieces)
        h.set_piece_deadline(lt::piece_index_t(p), 0);
}

std::optional<BtStreamSpan> BtSession::streamFrom(const QString& id, int fileIndex, qint64 offset, bool fetch)
{
    const auto ti = d->torrentFile(id);
    const auto row = d->rows.find(id);
    if (!ti || !ti->is_valid() || row == d->rows.end())
        return std::nullopt;
    const lt::file_storage& fs = ti->layout();
    if (fileIndex < 0) {
        for (const lt::file_index_t f : fs.file_range())
            if (!fs.pad_file_at(f) && (fileIndex < 0 || fs.file_size(f) > fs.file_size(lt::file_index_t(fileIndex))))
                fileIndex = static_cast<int>(f);
    }
    if (fileIndex < 0 || fileIndex >= fs.num_files() || fs.pad_file_at(lt::file_index_t(fileIndex)))
        return std::nullopt;
    const lt::file_index_t file(fileIndex);

    BtStreamSpan out;
    out.path = d->filePath(id, fs, file, row->second.savePath);
    const auto name = fs.file_name(file);
    out.name = QString::fromUtf8(name.data(), qsizetype(name.size()));
    out.size = fs.file_size(file);
    if (out.size <= 0) {
        out.complete = true;
        return out;
    }

    // a passed piece is on disk once a flush asked after it has come back
    static const QBitArray kNone;
    const auto hv = d->have.find(id);
    const QBitArray& have = hv == d->have.end() ? kNone : hv->second;
    const auto un = d->unflushed.find(id);
    const auto fl = d->flushingPieces.find(id);
    auto hasPiece = [&](int p) { return p < have.size() && have.testBit(p); };
    auto onDisk = [&](int p) {
        return hasPiece(p) && (un == d->unflushed.end() || !un->second.contains(p))
            && (fl == d->flushingPieces.end() || !fl->second.contains(p));
    };

    const qint64 base = fs.file_offset(file);
    const qint64 pieceLen = ti->piece_length();
    const int firstPiece = static_cast<int>(base / pieceLen);
    const int lastPiece = static_cast<int>((base + out.size - 1) / pieceLen);
    out.complete = true;
    for (int p = firstPiece; p <= lastPiece && out.complete; ++p)
        out.complete = onDisk(p);
    if (out.complete) {
        out.availableEnd = out.size;
        return out;
    }
    offset = std::clamp<qint64>(offset, 0, out.size);
    if (offset >= out.size) {
        out.availableEnd = offset;
        return out;
    }
    int p = static_cast<int>((base + offset) / pieceLen);
    while (p <= lastPiece && onDisk(p))
        ++p;
    out.availableEnd = std::clamp<qint64>(p * pieceLen - base, offset, out.size);
    if (!fetch)
        return out;

    const auto h = d->handle(id);
    if (!h.is_valid())
        return out;
    // passed but in the write cache: on disk with the next flush
    if (p <= lastPiece && hasPiece(p) && !d->flushing.contains(id))
        d->flush(h, id);
    // little left ahead of the player: the next pieces before all others
    constexpr qint64 kStreamLeadBytes = 32 * 1024 * 1024;
    if (out.availableEnd - offset >= kStreamLeadBytes)
        return out;
    int missing = p;
    while (missing <= lastPiece && hasPiece(missing))
        ++missing;
    if (missing > lastPiece)
        return out;
    auto& focus = d->streamFocus[id];
    if (focus == missing)
        return out;
    focus = missing;
    h.clear_piece_deadlines();
    const int span = static_cast<int>(std::max<qint64>(2, (kStreamLeadBytes + pieceLen - 1) / pieceLen));
    int deadline = 0;
    for (int q = missing; q <= lastPiece && q < missing + span; ++q) {
        if (!hasPiece(q)) {
            h.set_piece_deadline(lt::piece_index_t(q), deadline);
            deadline += 250;
        }
    }
    return out;
}

bool BtSession::flushCache(const QString& id)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    d->flush(h, id);
    return true;
}

QBitArray BtSession::havePieces(const QString& id) const
{
    // was: status(query_pieces), a sync call into libtorrent's thread
    const auto it = d->have.find(id);
    return it == d->have.end() ? QBitArray() : it->second;
}

QString BtSession::ed3kLink(const QString& id) const
{
    const auto it = d->links.find(id);
    return it == d->links.end() ? QString() : it->second;
}

void BtSession::drainAlerts()
{
    if (!d->ses)
        return;
    std::vector<lt::alert*> alerts;
    d->ses->pop_alerts(&alerts);
    // pointers die at the next pop_alerts(): copy what is kept
    for (lt::alert* a : alerts) {
        // No SAM: the placeholder bridge fails on each retry (1 s doubling to 5 min,
        // fix-i2p-reconnect-backoff.patch), not worth a log line each time
        if (lt::alert_cast<lt::i2p_alert>(a) && !d->settings.hasSam())
            continue;
        forwardAlert(a);
        // libtorrent posts nothing when the SAM session comes up: a .i2p tracker that got
        // past "waiting for the acceptor" proves it, so an old bridge error is over
        if (!d->i2pError.isEmpty()) {
            const auto* te = lt::alert_cast<lt::tracker_error_alert>(a);
            const auto* tr = lt::alert_cast<lt::tracker_reply_alert>(a);
            const lt::tracker_alert* ta = te ? static_cast<const lt::tracker_alert*>(te) : tr;
            if (ta && BtAddSource::isI2pTracker(ta->tracker_url())
                && !(te && te->error == lt::errors::no_i2p_endpoint)) {
                d->i2pError.clear();
                emit statusChanged();
            }
        }
        d->noteI2pName(a);
        if (auto* ss = lt::alert_cast<lt::session_stats_alert>(a)) {
            const auto counters = ss->counters();
            if (!d->rateClock.isValid())
                d->rateClock.start();
            const qint64 now = d->rateClock.elapsed();
            const auto at = [&counters](int idx) {
                return idx >= 0 && idx < counters.size() ? counters[idx] : std::int64_t(0);
            };
            const Impl::RateSample sample{now, at(d->recvIdx), at(d->sentIdx), at(d->recvPayloadIdx),
                                          at(d->sentPayloadIdx), at(d->failedIdx), at(d->redundantIdx)};
            const std::optional<Impl::RateSample> prev =
                d->rateSamples.empty() ? std::nullopt : std::optional(d->rateSamples.back());
            d->rateSamples.push_back(sample);
            while (d->rateSamples.size() > 2 && now - d->rateSamples.front().ms > kRateHistoryMs)
                d->rateSamples.pop_front();
            d->countSample(prev ? &*prev : nullptr, sample, at(d->peersIdx));
        } else if (auto* su = lt::alert_cast<lt::state_update_alert>(a)) {
            for (const lt::torrent_status& st : su->status) {
                BtTorrentRow row = d->row(st);
                const QString id = row.id;
                if (!d->handles.contains(id))
                    continue;
                // file progress asked before this change is old now
                if (auto old = d->rows.find(id); old == d->rows.end() || old->second.done != row.done
                    || old->second.state != row.state)
                    if (auto dt = d->details.find(id); dt != d->details.end())
                        dt->second.progressCurrent = false;
                d->rows[id] = row;
                // every status update asks for the pieces (all flags)
                QBitArray& bits = d->have[id];
                bits.resize(st.pieces.size());
                for (int i = 0; i < st.pieces.size(); ++i)
                    bits.setBit(i, st.pieces.get_bit(lt::piece_index_t(i)));
                emit torrentChanged(id);
                if (d->checkWaited.remove(id) && d->joins.contains(id))
                    emit torrentChecked(id);
                d->enforceSeedLimits(st, row);
            }
        } else if (auto* at = lt::alert_cast<lt::add_torrent_alert>(a)) {
            const auto& p = at->params;
            const QString id = at->handle.is_valid() ? BtTorrentView::idOf(at->handle.info_hashes())
                                                     : BtTorrentView::idOf(p.ti ? p.ti->info_hashes() : p.info_hashes);
            const auto pending = d->adding.extract(id);
            d->resuming.remove(id);
            if (at->error) {
                logWarning(QStringLiteral("BitTorrent: could not add: %1").arg(message(at)));
                // the row add() put up goes again
                if (!pending.empty() && !pending.mapped().removed && !d->handles.contains(id)) {
                    d->rows.erase(id);
                    d->sessionUpBase.erase(id);
                    d->details.erase(id);
                    if (d->links.erase(id) > 0)
                        d->saveLinks();
                    if (d->stopAtMeta.remove(id))
                        d->saveStops();
                    d->forgetI2pListWait(id);
                    d->i2pOnlySweep.remove(id);
                    if (d->i2pOnly.erase(id) > 0) {
                        d->peerGate.setStrict(pending.mapped().hashes, false);
                        d->saveI2pOnly();
                    }
                    emit torrentRemoved(id);
                }
                continue;
            }
            if (d->handles.contains(id))
                continue;
            if (!pending.empty() && pending.mapped().removed) {
                d->ses->remove_torrent(at->handle, *pending.mapped().removed);
                continue;
            }
            d->handles[id] = at->handle;
            d->noteMeta(id, p);
            d->seedDetail(id, p);
            if (!pending.empty() && pending.mapped().prioritiesChanged)
                d->prioritizeFiles(at->handle, id, d->details[id].priorities);
            d->applyPreviewPieces(at->handle, id);
            // was: row(handle.status()), a sync call per torrent; the real one follows
            d->rows[id] = d->row(addedStatus(p, at->handle, at->handle.info_hashes()));
            at->handle.post_status();
            d->attachSink(id);
            if (d->i2pOnlySweep.remove(id) && d->i2pOnly.contains(id))
                d->enterI2pOnly(id, at->handle);
            // added by add(): kept across a restart from now on
            if (!pending.empty())
                d->saveResume(at->handle);
            emit torrentAdded(id);
        } else if (auto* rd = lt::alert_cast<lt::save_resume_data_alert>(a)) {
            d->outstandingResume = std::max(0, d->outstandingResume - 1);
            const QString id = BtTorrentView::idOf(rd->params.info_hashes);
            d->noteMeta(id, rd->params);
            if (d->handles.contains(id))
                writeLater(d->resumePath(id), std::move(rd->params));
        } else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
            d->outstandingResume = std::max(0, d->outstandingResume - 1);
        } else if (auto* tf = lt::alert_cast<lt::torrent_finished_alert>(a)) {
            const QString id = BtTorrentView::idOf(tf->handle.info_hashes());
            // stopped at metadata: nothing is wanted yet, not a finished download
            const bool download = !d->metaStopped.contains(id);
            // also posted for a seed loaded at start: only a download counts
            if (auto it = d->rows.find(id); download && it != d->rows.end()
                && (it->second.state == BtState::Downloading
                    || it->second.state == BtState::DownloadingMetadata))
                if (BtCounters* c = Impl::counters())
                    ++c->torrentsCompleted;
            if (download)
                logInfo(QStringLiteral("BitTorrent: %1").arg(message(tf)));
            d->saveResume(tf->handle);
            // the last blocks may still be in the disk cache: announced once on disk
            if (download) {
                d->finishFlush.insert(id);
                d->flush(tf->handle, id);
            }
        } else if (auto* cf = lt::alert_cast<lt::cache_flushed_alert>(a)) {
            // also posted on removal: only a torrent still in the list
            const QString id = BtTorrentView::idOf(cf->handle.info_hashes());
            const auto fl = d->flushing.find(id);
            if (fl != d->flushing.end() && --fl->second > 0)
                continue;   // a later ask still runs
            if (fl != d->flushing.end())
                d->flushing.erase(fl);
            d->flushingPieces.erase(id);
            if (d->handles.contains(id)) {
                if (d->finishFlush.remove(id))
                    emit torrentFinished(id);
                emit cacheFlushed(id);
            }
        } else if (auto* md = lt::alert_cast<lt::metadata_received_alert>(a)) {
            const QString id = BtTorrentView::idOf(md->handle.info_hashes());
            if (const auto wait = d->i2pListWait.find(id); wait != d->i2pListWait.end()) {
                const QStringList added = wait->second;
                d->i2pListWait.erase(wait);
                d->saveI2pListWait();
                const auto ti = md->handle.torrent_file();
                const auto row = d->rows.find(id);
                const bool wantsList = d->settings.i2pEnabled
                                    && (d->settings.i2pMixed || (row != d->rows.end() && row->second.i2p));
                if (ti && ti->priv())
                    removeTrackers(id, added);
                else if (added.isEmpty() && wantsList)
                    addTrackers(id, d->settings.i2pTrackers);
            }
            if (d->stopAtMeta.contains(id))
                d->stopAtMetadata(md->handle, id);
            d->saveResume(md->handle);
            d->applyPreviewPieces(md->handle, id);
            emit torrentChanged(id);
            emit metadataReceived(id);
        } else if (auto* pf = lt::alert_cast<lt::piece_finished_alert>(a)) {
            const QString id = BtTorrentView::idOf(pf->handle.info_hashes());
            if (auto hv = d->have.find(id); hv != d->have.end() && int(pf->piece_index) < hv->second.size())
                hv->second.setBit(int(pf->piece_index));
            d->unflushed[id].insert(int(pf->piece_index));
            if (d->joins.contains(id))
                emit pieceResult(id, static_cast<int>(pf->piece_index), true);
            d->pieceCopied(this, id, int(pf->piece_index), true);
        } else if (auto* tc = lt::alert_cast<lt::torrent_checked_alert>(a)) {
            // torrentChecked once the pieces of the check are in (post_status asks all flags)
            if (const QString id = BtTorrentView::idOf(tc->handle.info_hashes()); d->joins.contains(id)) {
                d->checkWaited.insert(id);
                tc->handle.post_status();
            }
        } else if (auto* fpr = lt::alert_cast<lt::file_progress_alert>(a)) {
            if (auto dt = d->details.find(BtTorrentView::idOf(fpr->handle.info_hashes())); dt != d->details.end()) {
                dt->second.progress.assign(fpr->files.begin(), fpr->files.end());
                dt->second.progressCurrent = true;
                if (std::exchange(dt->second.filesWaited, false))
                    emit torrentChanged(dt->first);
            }
        } else if (auto* fps = lt::alert_cast<lt::file_priorities_alert>(a)) {
            if (auto dt = d->details.find(BtTorrentView::idOf(fps->handle.info_hashes())); dt != d->details.end())
                dt->second.priorities.assign(fps->priorities.begin(), fps->priorities.end());
        } else if (auto* pi = lt::alert_cast<lt::peer_info_alert>(a)) {
            if (auto dt = d->details.find(BtTorrentView::idOf(pi->handle.info_hashes())); dt != d->details.end())
                dt->second.peers = pi->peer_info;
        } else if (auto* tl = lt::alert_cast<lt::tracker_list_alert>(a)) {
            if (auto dt = d->details.find(BtTorrentView::idOf(tl->handle.info_hashes())); dt != d->details.end()) {
                Impl::Detail& detail = dt->second;
                detail.trackerAsks = std::max(detail.trackerAsks - 1, 0);
                if (detail.staleTrackerAsks > 0)
                    --detail.staleTrackerAsks;
                else
                    detail.trackers = tl->trackers;
            }
        } else if (auto* pa = lt::alert_cast<lt::piece_availability_alert>(a)) {
            if (auto dt = d->details.find(BtTorrentView::idOf(pa->handle.info_hashes())); dt != d->details.end())
                dt->second.availability = pa->piece_availability;
        } else if (lt::alert_cast<lt::listen_succeeded_alert>(a)) {
            // one per interface and transport: the port is in the start line
            d->listening = true;
            emit statusChanged();
        } else if (auto* ex = lt::alert_cast<lt::external_ip_alert>(a)) {
            const lt::address addr = ex->external_address;
            QString& slot = addr.is_v6() ? d->externalIPv6 : d->externalIPv4;
            if (const QString ip = QString::fromStdString(addr.to_string()); ip != slot) {
                slot = ip;
                emit statusChanged();
            }
        } else if (auto* lf = lt::alert_cast<lt::listen_failed_alert>(a)) {
            logWarning(QStringLiteral("BitTorrent: %1").arg(message(lf)));
            emit statusChanged();
        } else if (auto* ia = lt::alert_cast<lt::i2p_alert>(a)) {
            // libtorrent retries the SAM bridge itself (session_impl::schedule_i2p_reconnect).
            // No SAM: dropped at the top of the loop (placeholder bridge fails on purpose)
            const QString err = QString::fromStdString(ia->error.message());
            if (err != d->i2pError)
                logWarning(QStringLiteral("BitTorrent: I2P SAM bridge %1:%2: %3")
                               .arg(d->settings.i2pSamHost).arg(d->settings.i2pSamPort).arg(err));
            d->i2pError = err;
            emit statusChanged();
        } else if (auto* pb = lt::alert_cast<lt::peer_blocked_alert>(a)) {
            // its line went to Torrent Verbose (forwardAlert, ip_block)
            if (pb->reason == lt::peer_blocked_alert::ip_filter)
                ++d->blockedPeers;
        } else if (auto* ds = lt::alert_cast<lt::dht_stats_alert>(a)) {
            // one alert per DHT instance; an empty endpoint = no DHT running
            const lt::udp::endpoint ep = ds->local_endpoint;
            if (ep == lt::udp::endpoint{}) {
                d->dhtNodesBySocket.clear();
            } else {
                int nodes = 0;
                for (const auto& bucket : ds->routing_table)
                    nodes += bucket.num_nodes;
                d->dhtNodesBySocket[ep] = nodes;
            }
            d->dhtNodes = 0;
            for (const auto& [_, n] : d->dhtNodesBySocket)
                d->dhtNodes += n;
        } else if (auto* fp = lt::alert_cast<lt::file_prio_alert>(a)) {
            const QString id = BtTorrentView::idOf(fp->handle.info_hashes());
            d->filePrioritiesApplied(id);
            d->applyPreviewPieces(fp->handle, id);
            if (fp->error)
                logWarning(QStringLiteral("BitTorrent: %1").arg(message(a)));
        } else if (auto* sm = lt::alert_cast<lt::storage_moved_alert>(a)) {
            const QString id = BtTorrentView::idOf(sm->handle.info_hashes());
            const bool asked = d->moving.remove(id);
            logInfo(QStringLiteral("BitTorrent: %1").arg(message(a)));
            // a write error stays with the torrent until cleared: it restarts at the new place.
            // A full disk is no error to libtorrent: it seeds only (upload mode) for a while.
            const auto row = d->rows.find(id);
            if (asked && sm->handle.is_valid()
                && ((row != d->rows.end() && row->second.state == BtState::Error)
                    || (sm->handle.flags() & lt::torrent_flags::upload_mode))) {
                sm->handle.clear_error();
                sm->handle.unset_flags(lt::torrent_flags::upload_mode);
                sm->handle.set_flags(lt::torrent_flags::auto_managed);
                sm->handle.resume();
            }
            d->saveResume(sm->handle);   // the new save_path survives a restart
            sm->handle.post_status();
            emit torrentChanged(id);
        } else if (auto* tp = lt::alert_cast<lt::torrent_paused_alert>(a)) {
            const QString id = BtTorrentView::idOf(tp->handle.info_hashes());
            if (const auto l = d->locating.find(id); l != d->locating.end() && l->second.waitingPause)
                d->linkLocated(id);
        } else if (auto* fr = lt::alert_cast<lt::file_renamed_alert>(a)) {
            d->fileLinked(BtTorrentView::idOf(fr->handle.info_hashes()), int(fr->index), true);
        } else if (auto* ff = lt::alert_cast<lt::file_rename_failed_alert>(a)) {
            logWarning(QStringLiteral("BitTorrent: %1").arg(message(a)));
            d->fileLinked(BtTorrentView::idOf(ff->handle.info_hashes()), int(ff->index), false);
        } else if (lt::alert_cast<lt::torrent_deleted_alert>(a) || lt::alert_cast<lt::torrent_delete_failed_alert>(a)) {
            const auto* del = lt::alert_cast<lt::torrent_deleted_alert>(a);
            const auto* bad = lt::alert_cast<lt::torrent_delete_failed_alert>(a);
            auto after = d->deleteAfter.extract(BtTorrentView::idOf(del ? del->info_hashes : bad->info_hashes));
            if (!after.empty()) {
                // never the found files: only the torrent's own, then folders left empty
                QThreadPool::globalInstance()->start([job = std::move(after.mapped())] {
                    const QString base = QDir::cleanPath(job.savePath);
                    for (const QString& path : job.paths) {
                        QFile::remove(path);
                        for (QString dir = QFileInfo(path).absolutePath();
                             dir.startsWith(base + u'/') && QDir().rmdir(dir);
                             dir = QFileInfo(dir).absolutePath()) {}
                    }
                });
            }
        } else if (auto* mf = lt::alert_cast<lt::storage_moved_failed_alert>(a)) {
            d->moving.remove(BtTorrentView::idOf(mf->handle.info_hashes()));
            logError(QStringLiteral("BitTorrent: %1").arg(message(a)));
            mf->handle.post_status();
        } else if (lt::alert_cast<lt::torrent_error_alert>(a) || lt::alert_cast<lt::file_error_alert>(a)) {
            if (auto* fe = lt::alert_cast<lt::file_error_alert>(a))
                d->pendingPriorities.erase(BtTorrentView::idOf(fe->handle.info_hashes()));
            // performance alerts: Torrent tab only (forwardAlert), they repeat each second
            logWarning(QStringLiteral("BitTorrent: %1").arg(message(a)));
        } else if (lt::alert_cast<lt::tracker_error_alert>(a) || lt::alert_cast<lt::hash_failed_alert>(a)) {
            if (auto* hf = lt::alert_cast<lt::hash_failed_alert>(a)) {
                if (BtCounters* c = Impl::counters())
                    ++c->hashFailures;
                const QString id = BtTorrentView::idOf(hf->handle.info_hashes());
                // libtorrent counts failed bytes without a state update: ask for one (async,
                // status() would wait for libtorrent's thread)
                if (d->handles.contains(id))
                    hf->handle.post_status();
                if (d->joins.contains(id))
                    emit pieceResult(id, static_cast<int>(hf->piece_index), false);
                d->pieceCopied(this, id, int(hf->piece_index), false);
            }
        }
    }
}

void BtSession::tick()
{
    if (!d->ses)
        return;
    d->ses->post_torrent_updates();
    d->ses->post_dht_stats();
    // was: need_save_resume_data() first, a sync call: froze the core while libtorrent's
    // thread was busy (report 27). only_if_modified checks on libtorrent's thread instead
    ++d->ticks;
    if (d->ticks % kResumeEveryTicks == 0) {
        auto flags = lt::torrent_handle::save_info_dict | lt::torrent_handle::if_metadata_changed
            | lt::torrent_handle::if_state_changed | lt::torrent_handle::if_config_changed
            | lt::torrent_handle::if_download_progress;
        if (d->ticks % kCountersEveryTicks == 0)
            flags |= lt::torrent_handle::if_counters_changed;
        for (const auto& [id, h] : d->handles)
            d->saveResume(h, flags);
    }
    if (d->ticks % kI2pNameRetryTicks == 0)
        d->retryI2pNames();
}

BtAddResult BtSession::Impl::add(BtSession& q, lt::add_torrent_params atp, const QString& savePath, bool paused,
                                 std::optional<bool> i2pOnly, std::optional<bool> stopAfterMetadata, const QString& link,
                                 const QList<int>& wanted)
{
    const lt::info_hash_t hashes = atp.ti ? atp.ti->info_hashes() : atp.info_hashes;
    // was: find_torrent(), a sync call; the list here knows it as well
    if (const QString id = listedId(hashes); !id.isEmpty()) {
        // another file of a torrent already here: switch it on there
        if (!wanted.isEmpty())
            wantFiles(handle(id), id, wanted);
        return {BtAddOutcome::Duplicate, id, QStringLiteral("already in the list")};
    }
    // only the picked files: the rest start skipped
    if (atp.ti && !wanted.isEmpty()) {
        const int files = atp.ti->num_files();
        std::vector<lt::download_priority_t> prio(static_cast<std::size_t>(files), lt::dont_download);
        bool any = false;
        for (const int f : wanted) {
            if (f >= 0 && f < files && !atp.ti->layout().pad_file_at(lt::file_index_t{f})) {
                prio[static_cast<std::size_t>(f)] = lt::default_priority;
                any = true;
            }
        }
        if (any)
            atp.file_priorities = std::move(prio);
    }
    atp.save_path = (savePath.isEmpty() ? settings.downloadDir : savePath).toStdString();
    if (paused) {
        atp.flags |= lt::torrent_flags::paused;
        atp.flags &= ~lt::torrent_flags::auto_managed;
    } else {
        atp.flags |= lt::torrent_flags::auto_managed;
        atp.flags &= ~lt::torrent_flags::paused;
    }
    defaultConnections(atp);
    I2pListWait listWait;
    const bool i2p = BtAddSource::applyI2p(atp, settings, i2pOnly, &listWait);
    // asked for I2P only (not just an .i2p tracker): strict, also while the session mixes
    const bool strict = i2p && i2pOnly.value_or(settings.i2pEnabled && settings.i2pOnlyNew);
    std::optional<I2pOnly> strictOf;
    if (strict) {
        strictOf.emplace();
        stripForI2pOnly(atp, *strictOf);
    }
    const bool stopAtMetaPending = BtAddSource::applyStopAfterMetadata(atp, settings, stopAfterMetadata);
    const lt::add_torrent_params metaOf = [&atp] {
        lt::add_torrent_params p;
        p.comment = atp.comment;
        p.created_by = atp.created_by;
        p.creation_date = atp.creation_date;
        p.trackers = atp.trackers;
        p.tracker_tiers = atp.tracker_tiers;
        p.file_priorities = atp.file_priorities;
        return p;
    }();
    // was: add_torrent() and status(), sync calls: the row comes from the parameters,
    // the handle with the add_torrent_alert (drainAlerts)
    const QString id = BtTorrentView::idOf(hashes);
    if (id.isEmpty())
        return {BtAddOutcome::Invalid, {}, QStringLiteral("no info hash")};
    noteMeta(id, metaOf);
    seedDetail(id, metaOf);
    rows[id] = row(addedStatus(atp, {}, hashes));
    adding[id] = Adding{hashes, atp.ti, false, std::nullopt};
    if (!link.isEmpty()) {
        links[id] = link;
        saveLinks();
    }
    if (stopAtMetaPending) {
        stopAtMeta.insert(id);
        saveStops();
    }
    if (listWait.held || !listWait.added.empty()) {
        QStringList urls;
        for (const std::string& u : listWait.added)
            urls << QString::fromStdString(u);
        i2pListWait[id] = urls;
        saveI2pListWait();
    }
    if (strictOf) {
        this->i2pOnly[id] = std::move(*strictOf);   // the member, not the parameter
        i2pOnlySweep.insert(id);
        peerGate.setStrict(hashes, true);
        saveI2pOnly();
    }
    ses->async_add_torrent(std::move(atp));
    // the flag alone means I2P only unless the session mixes (i2pModeOf)
    logInfo(QStringLiteral("BitTorrent: added %1%2").arg(rows[id].name, !i2p ? QString()
                                                         : strict || !settings.i2pMixed ? QStringLiteral(" (I2P only)")
                                                                                        : QStringLiteral(" (I2P and direct)")));
    if (BtCounters* c = counters())
        ++c->torrentsAdded;
    return {BtAddOutcome::Added, id, {}};
}

void BtSession::Impl::wantFiles(const lt::torrent_handle& h, const QString& id, const QList<int>& wanted)
{
    const auto pendingAdd = adding.find(id);
    const auto ti = torrentFile(id);
    if (!ti)
        return;
    const int files = ti->num_files();
    // a change still on its way counts, not the state libtorrent reports before it
    const auto pending = pendingPriorities.find(id);
    std::vector<lt::download_priority_t> prio =
        pending != pendingPriorities.end() ? pending->second.priorities : details[id].priorities;
    prio.resize(static_cast<std::size_t>(files), lt::default_priority);
    bool changed = false;
    for (const int f : wanted) {
        if (f < 0 || f >= files || ti->layout().pad_file_at(lt::file_index_t{f}))
            continue;
        auto& p = prio[static_cast<std::size_t>(f)];
        if (p == lt::dont_download) {
            p = lt::default_priority;
            changed = true;
        }
    }
    if (!changed)
        return;
    if (h.is_valid()) {
        prioritizeFiles(h, id, std::move(prio));
    } else if (pendingAdd != adding.end()) {
        // still being added: sent when the handle comes
        details[id].priorities = std::move(prio);
        pendingAdd->second.prioritiesChanged = true;
    }
}

void BtSession::Impl::applyPreviewPieces(const lt::torrent_handle& h, const QString& id)
{
    if (!settings.previewPiecesFirst || !h.is_valid())
        return;
    const auto ti = torrentFile(id);
    if (!ti || !ti->is_valid())
        return;
    const lt::file_storage& fs = ti->layout();
    const auto dt = details.find(id);
    const qint64 pieceLen = ti->piece_length();
    // ~ an eD2K preview part at each end, at least one piece
    constexpr qint64 kPreviewBytes = 9'728'000;
    const qint64 ends = std::max(pieceLen, kPreviewBytes);
    std::vector<std::pair<lt::piece_index_t, lt::download_priority_t>> raise;
    for (const lt::file_index_t f : fs.file_range()) {
        const auto i = static_cast<std::size_t>(static_cast<int>(f));
        if (fs.pad_file_at(f)
            || (dt != details.end() && i < dt->second.priorities.size() && dt->second.priorities[i] == lt::dont_download))
            continue;
        const qint64 size = fs.file_size(f);
        const auto name = fs.file_name(f);
        // as an eD2K download: only a file over two parts that needs its tail to play
        if (size <= 2 * kPreviewBytes
            || !PartFile::previewNeedsLastPart(QString::fromUtf8(name.data(), qsizetype(name.size()))))
            continue;
        const qint64 base = fs.file_offset(f);
        auto add = [&](qint64 from, qint64 to) {
            for (qint64 p = from / pieceLen; p <= to / pieceLen; ++p)
                raise.emplace_back(lt::piece_index_t(static_cast<int>(p)), lt::top_priority);
        };
        add(base, std::min(base + size, base + ends) - 1);
        add(std::max(base, base + size - ends), base + size - 1);
    }
    if (!raise.empty())
        h.prioritize_pieces(raise);
}

void BtSession::Impl::stopAtMetadata(const lt::torrent_handle& h, const QString& id)
{
    stopAtMeta.remove(id);
    saveStops();
    h.unset_flags(lt::torrent_flags::auto_managed);
    h.pause();
    const auto ti = h.torrent_file();
    if (ti)
        prioritizeFiles(h, id, std::vector<lt::download_priority_t>(static_cast<std::size_t>(ti->num_files()),
                                                                    lt::default_priority));
    metaStopped.insert(id);
    logInfo(QStringLiteral("BitTorrent: %1 stopped after its metadata: untick the files to skip, then resume")
                .arg(ti ? QString::fromStdString(ti->name()) : id));
}

void BtSession::Impl::noteI2pName(const lt::alert* a)
{
    const auto* ta = lt::alert_cast<lt::tracker_error_alert>(a);
    const auto* tr = lt::alert_cast<lt::tracker_reply_alert>(a);
    const lt::tracker_alert* t = ta ? static_cast<const lt::tracker_alert*>(ta) : tr;
    if (!t || !BtAddSource::isI2pTracker(t->tracker_url()))
        return;
    const QString id = BtTorrentView::idOf(t->handle.info_hashes());
    if (tr) {
        if (auto it = i2pNameMisses.find(id); it != i2pNameMisses.end()) {
            it->second.erase(t->tracker_url());
            if (it->second.empty())
                i2pNameMisses.erase(it);
        }
        return;
    }
    if (ta->error.category() == lt::i2p_category() && ta->error.value() == lt::i2p_error::invalid_key
        && handles.contains(id))
        i2pNameMisses[id].try_emplace(ta->tracker_url(), 0);
}

void BtSession::Impl::retryI2pNames()
{
    if (!settings.hasSam())
        return;
    for (auto it = i2pNameMisses.begin(); it != i2pNameMisses.end();) {
        const lt::torrent_handle h = handle(it->first);
        auto& urls = it->second;
        for (auto u = urls.begin(); u != urls.end();) {
            if (!h.is_valid() || ++u->second > kI2pNameRetries) {
                u = urls.erase(u);
                continue;
            }
            h.force_reannounce(0, u->first, lt::torrent_handle::ignore_min_interval);
            ++u;
        }
        it = urls.empty() ? i2pNameMisses.erase(it) : std::next(it);
    }
}

void BtSession::Impl::loadSeedLimits()
{
    seedLimits.clear();
    const QJsonObject root = QJsonDocument::fromJson(readAll(optionsPath())).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        BtTorrentOptions limits;
        limits.ratioMode = limitMode(o.value(QStringLiteral("ratioMode")).toInt());
        limits.ratioPercent = std::max(1, o.value(QStringLiteral("ratioPercent")).toInt(100));
        limits.seedTimeMode = limitMode(o.value(QStringLiteral("seedTimeMode")).toInt());
        limits.seedTimeMinutes = std::max(1, o.value(QStringLiteral("seedTimeMinutes")).toInt(60));
        seedLimits[it.key()] = limits;
    }
}

void BtSession::Impl::saveSeedLimits() const
{
    QJsonObject root;
    for (const auto& [id, o] : seedLimits)
        root.insert(id, QJsonObject{
                            {QStringLiteral("ratioMode"), static_cast<int>(o.ratioMode)},
                            {QStringLiteral("ratioPercent"), o.ratioPercent},
                            {QStringLiteral("seedTimeMode"), static_cast<int>(o.seedTimeMode)},
                            {QStringLiteral("seedTimeMinutes"), o.seedTimeMinutes},
                        });
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    writeQueued(optionsPath(), json);
}

void BtSession::Impl::loadI2pOnly()
{
    i2pOnly.clear();
    const QJsonObject root = QJsonDocument::fromJson(readAll(i2pOnlyPath())).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        I2pOnly s;
        for (const QJsonValue& t : o.value(QStringLiteral("trackers")).toArray()) {
            const QJsonObject e = t.toObject();
            s.trackers.emplace_back(e.value(QStringLiteral("url")).toString().toStdString(),
                                    std::clamp(e.value(QStringLiteral("tier")).toInt(), 0, 255));
        }
        for (const QJsonValue& w : o.value(QStringLiteral("webSeeds")).toArray())
            s.webSeeds.push_back(w.toString().toStdString());
        const auto flags = lt::torrent_flags_t(static_cast<std::uint64_t>(o.value(QStringLiteral("flagsSet")).toInteger()));
        s.flagsSet = flags & kI2pOnlyOff;
        i2pOnly[it.key()] = std::move(s);
    }
}

void BtSession::Impl::saveI2pOnly() const
{
    if (i2pOnly.empty()) {
        BackgroundFileWriter::instance().discard(i2pOnlyPath());
        QFile::remove(i2pOnlyPath());
        return;
    }
    QJsonObject root;
    for (const auto& [id, s] : i2pOnly) {
        QJsonArray trackers;
        for (const auto& [url, tier] : s.trackers)
            trackers.append(QJsonObject{{QStringLiteral("url"), QString::fromStdString(url)}, {QStringLiteral("tier"), tier}});
        QJsonArray webSeeds;
        for (const std::string& w : s.webSeeds)
            webSeeds.append(QString::fromStdString(w));
        root.insert(id, QJsonObject{
                            {QStringLiteral("trackers"), trackers},
                            {QStringLiteral("webSeeds"), webSeeds},
                            {QStringLiteral("flagsSet"), static_cast<qint64>(static_cast<std::uint64_t>(s.flagsSet))},
                        });
    }
    writeQueued(i2pOnlyPath(), QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void BtSession::Impl::stripForI2pOnly(lt::add_torrent_params& atp, I2pOnly& s)
{
    atp.tracker_tiers.resize(atp.trackers.size(), 0);
    std::vector<std::string> trackers;
    std::vector<int> tiers;
    for (std::size_t i = 0; i < atp.trackers.size(); ++i) {
        if (BtAddSource::isI2pTracker(atp.trackers[i])) {
            trackers.push_back(std::move(atp.trackers[i]));
            tiers.push_back(atp.tracker_tiers[i]);
        } else {
            s.trackers.emplace_back(std::move(atp.trackers[i]), atp.tracker_tiers[i]);
        }
    }
    atp.trackers = std::move(trackers);
    atp.tracker_tiers = std::move(tiers);
    s.webSeeds = std::move(atp.url_seeds);
    atp.url_seeds.clear();
    s.flagsSet = kI2pOnlyOff & ~atp.flags;
    atp.flags |= kI2pOnlyOff;
}

void BtSession::Impl::enterI2pOnly(const QString& id, const lt::torrent_handle& h)
{
    I2pOnly& s = i2pOnly[id];   // already strict: what came since goes aside too
    std::vector<lt::announce_entry> keep;
    bool moved = false;
    for (const lt::announce_entry& a : h.trackers()) {
        if (BtAddSource::isI2pTracker(a.url)) {
            keep.push_back(a);
            continue;
        }
        if (std::ranges::find(s.trackers, a.url, &std::pair<std::string, int>::first) == s.trackers.end())
            s.trackers.emplace_back(a.url, a.tier);
        moved = true;
    }
    if (moved) {
        h.replace_trackers(keep);
        setCachedTrackers(id, keep);
    }
    for (const std::string& w : h.url_seeds()) {
        if (std::ranges::find(s.webSeeds, w) == s.webSeeds.end())
            s.webSeeds.push_back(w);
        h.remove_url_seed(w);
    }
    s.flagsSet |= kI2pOnlyOff & ~h.flags();
    h.set_flags(kI2pOnlyOff, kI2pOnlyOff);
    // the peers found in the clear: never dialled
    lt::torrent_handle(h).clear_peers();
    peerGate.setStrict(h.info_hashes(), true);
    saveI2pOnly();
}

void BtSession::Impl::leaveI2pOnly(const QString& id, const lt::torrent_handle& h)
{
    const auto it = i2pOnly.find(id);
    if (it == i2pOnly.end())
        return;
    const I2pOnly s = std::move(it->second);
    i2pOnly.erase(it);
    i2pOnlySweep.remove(id);
    saveI2pOnly();
    peerGate.setStrict(h.info_hashes(), false);
    // only what it switched off: DHT, PEX and LSD as they were before
    h.set_flags({}, s.flagsSet);
    std::vector<lt::announce_entry> trackers = h.trackers();
    for (const auto& [url, tier] : s.trackers) {
        if (std::ranges::find(trackers, url, &lt::announce_entry::url) != trackers.end())
            continue;
        lt::announce_entry a(url);
        a.tier = static_cast<std::uint8_t>(tier);
        trackers.push_back(a);
    }
    std::ranges::stable_sort(trackers, {}, &lt::announce_entry::tier);
    h.replace_trackers(trackers);
    setCachedTrackers(id, trackers);
    for (const std::string& w : s.webSeeds)
        h.add_url_seed(w);
    h.force_reannounce();
    if (s.flagsSet & lt::torrent_flags::disable_dht)
        h.force_dht_announce();
}

void BtSession::Impl::enforceSeedLimits(const lt::torrent_status& st, const BtTorrentRow& row)
{
    // is_finished: a torrent with only some files wanted never is_seeding, yet uploads
    const bool done = row.wantedDone
                      && (st.state == lt::torrent_status::finished || st.state == lt::torrent_status::seeding);
    if (!done || row.paused || limitOverride.contains(row.id))
        return;
    const SeedLimitHit hit = seedLimitHit(row);
    if (!hit.ratio && !hit.time)
        return;
    st.handle.unset_flags(lt::torrent_flags::auto_managed);
    st.handle.pause();
    logInfo(QStringLiteral("BitTorrent: seeding of %1 stopped, %2 reached").arg(row.name, hit.ratio ? QStringLiteral("ratio") : QStringLiteral("seed time")));
}

BtSession::Impl::SeedLimitHit BtSession::Impl::seedLimitHit(const BtTorrentRow& row) const
{
    int ratioPercent = settings.seedRatioPercent;
    int seedMinutes = settings.seedTimeLimitMinutes;
    if (auto it = seedLimits.find(row.id); it != seedLimits.end()) {
        if (it->second.ratioMode != BtLimitMode::Global)
            ratioPercent = it->second.ratioMode == BtLimitMode::Custom ? it->second.ratioPercent : 0;
        if (it->second.seedTimeMode != BtLimitMode::Global)
            seedMinutes = it->second.seedTimeMode == BtLimitMode::Custom ? it->second.seedTimeMinutes : 0;
    }
    // same ratio as the Torrents list shows (#218), pad bytes left out
    const double ratio = shareRatio(row.uploaded, row.downloaded, row.haveBytes);
    return {ratioPercent > 0 && ratio != kShareRatioNone
                && (ratio == kShareRatioInfinite || ratio * 100.0 >= ratioPercent),
            seedMinutes > 0 && row.finishedSeconds >= qint64(seedMinutes) * 60};
}

lt::settings_pack BtSession::Impl::pack(const BtSettings& s) const
{
    BtSettings resolved = s;
    if (resolved.listenPort == 0) {
        if (randomPort == 0)
            randomPort = freeTcpUdpPort();
        resolved.listenPort = randomPort;   // still 0 when none found: libtorrent picks
    }
    lt::settings_pack p = BtSettingsMap::toPack(resolved);
    if (rateLimits) {
        p.set_int(lt::settings_pack::download_rate_limit, rateLimits->first);
        p.set_int(lt::settings_pack::upload_rate_limit, rateLimits->second);
    }
    return p;
}

qint64 BtSession::Impl::rate(qint64 RateSample::*field, qint64 windowMs) const
{
    if (rateSamples.size() < 2)
        return 0;
    const RateSample& last = rateSamples.back();
    // the newest sample at least windowMs old, else the oldest
    const RateSample* first = &rateSamples.front();
    for (auto it = rateSamples.crbegin() + 1; it != rateSamples.crend(); ++it) {
        if (last.ms - it->ms >= windowMs) {
            first = &*it;
            break;
        }
    }
    const qint64 ms = last.ms - first->ms;
    return ms > 0 ? std::max<qint64>(0, last.*field - first->*field) * 1000 / ms : 0;
}

BtCounters* BtSession::Impl::counters()
{
    return theApp.statistics ? &theApp.statistics->btSession() : nullptr;
}

void BtSession::Impl::countSample(const RateSample* prev, const RateSample& now, qint64 peers) const
{
    BtCounters* c = counters();
    if (!c)
        return;
    const RateSample zero;
    const RateSample& p = prev ? *prev : zero;
    const auto delta = [&](qint64 RateSample::*field) {
        return static_cast<uint64>(std::max<qint64>(0, now.*field - p.*field));
    };
    const uint64 payloadDown = delta(&RateSample::recvPayload);
    const uint64 payloadUp = delta(&RateSample::sentPayload);
    c->payloadDownBytes += payloadDown;
    c->payloadUpBytes += payloadUp;
    c->wireDownBytes += delta(&RateSample::recv);
    c->wireUpBytes += delta(&RateSample::sent);
    c->failedBytes += delta(&RateSample::failed);
    c->redundantBytes += delta(&RateSample::redundant);
    raiseCounter(c->peakPeers, static_cast<uint64>(std::max<qint64>(0, peers)));
    if (!prev)
        return;   // no interval yet: no time, no rate
    const uint64 ms = static_cast<uint64>(std::max<qint64>(0, now.ms - p.ms));
    if (payloadDown > 0)
        c->downloadTimeMs += ms;
    if (payloadUp > 0)
        c->uploadTimeMs += ms;
    // over the demand() window: one 500 ms interval is too spiky for a record
    raiseCounter(c->maxDownRate, static_cast<uint64>(rate(&RateSample::recvPayload, kRateWindowMs)));
    raiseCounter(c->maxUpRate, static_cast<uint64>(rate(&RateSample::sentPayload, kRateWindowMs)));
}

void BtSession::locateCopyNext(const QString& id)
{
    const auto it = d->locating.find(id);
    if (it == d->locating.end())
        return;
    auto& l = it->second;
    const auto h = d->handle(id);
    const auto ti = d->torrentFile(id);
    const QBitArray have = havePieces(id);
    // the next pieces still missing, a batch in memory at a time
    constexpr qint64 kCopyBatch = 16 * 1024 * 1024;
    struct Job {
        int piece;
        int file;
        QString path;
    };
    std::vector<Job> jobs;
    qint64 bytes = 0;
    if (h.is_valid() && ti) {
        for (auto c = l.copies.begin(); c != l.copies.end() && bytes < kCopyBatch;) {
            auto& pieces = c->second.pieces;
            std::size_t k = 0;
            for (; k < pieces.size() && bytes < kCopyBatch; ++k) {
                const int p = pieces.at(k);
                if (p < have.size() && have.testBit(p))
                    continue;
                jobs.push_back({p, c->first, c->second.path});
                bytes += ti->piece_size(lt::piece_index_t(p));
            }
            pieces.erase(pieces.begin(), pieces.begin() + std::ptrdiff_t(k));
            c = pieces.empty() ? l.copies.erase(c) : std::next(c);
        }
    }
    if (jobs.empty()) {
        l.copies.clear();
        l.report.copied = int(l.copiedFiles.size());
        if (l.report.copied > 0)
            logInfo(QStringLiteral("BitTorrent: %1: proved pieces of %2 located file(s) copied into the download").arg(id).arg(l.report.copied));
        locateLink(id);
        return;
    }
    const quint64 run = l.run;
    const quint64 batch = ++l.copyBatch;
    const auto cancel = l.cancel;
    QPointer<BtSession> self(this);
    QThreadPool::globalInstance()->start([self, id, run, batch, cancel, ti, jobs = std::move(jobs)] {
        std::vector<std::tuple<int, int, std::vector<char>>> data;
        for (const auto& j : jobs) {
            if (cancel->load())
                return;
            auto piece = readLocatedPiece(*ti, j.piece, j.file, j.path);
            if (!piece.empty())
                data.emplace_back(j.piece, j.file, std::move(piece));
        }
        QMetaObject::invokeMethod(self.data(), [self, id, run, batch, data = std::move(data)]() mutable {
            if (!self)
                return;
            const auto it = self->d->locating.find(id);
            if (it == self->d->locating.end() || it->second.run != run || it->second.copyBatch != batch)
                return;
            const auto h = self->d->handle(id);
            if (!h.is_valid() || data.empty()) {
                self->locateCopyNext(id);
                return;
            }
            // libtorrent hashes each piece again before it writes it
            for (auto& [piece, file, bytes] : data) {
                it->second.copyWait[piece] = file;
                h.add_piece(lt::piece_index_t(piece), std::move(bytes));
            }
            // add_piece says nothing while checking, or for a piece a peer brought meanwhile
            constexpr int kCopyWaitMs = 60000;
            QTimer::singleShot(kCopyWaitMs, self.data(), [self, id, run, batch] {
                const auto w = self->d->locating.find(id);
                if (w == self->d->locating.end() || w->second.run != run || w->second.copyBatch != batch || w->second.copyWait.empty())
                    return;
                auto& l = w->second;
                const QBitArray have = self->havePieces(id);
                std::erase_if(l.copyWait, [&](const auto& p) { return p.first < have.size() && have.testBit(p.first); });
                if (!l.copyWait.empty()) {
                    logWarning(QStringLiteral("BitTorrent: %1: pieces of located files not written; copy stopped").arg(id));
                    l.copyWait.clear();
                    l.copies.clear();
                }
                self->locateCopyNext(id);
            });
        }, Qt::QueuedConnection);
    });
}

void BtSession::locateLink(const QString& id)
{
    const auto it = d->locating.find(id);
    if (it == d->locating.end())
        return;
    auto& l = it->second;
    const auto handle = d->handle(id);
    if (l.links.empty() || !handle.is_valid()) {
        // nothing linked: no stop, no recheck
        l.wasPaused = true;
        d->finishLocate(id);
        return;
    }
    // stopped first: no write may land in a file while it changes place
    const auto flags = handle.flags();
    l.wasAuto = bool(flags & lt::torrent_flags::auto_managed);
    l.wasPaused = bool(flags & lt::torrent_flags::paused);
    handle.unset_flags(lt::torrent_flags::auto_managed);
    if (l.wasPaused) {
        d->linkLocated(id);
        return;
    }
    l.waitingPause = true;
    handle.pause();
    constexpr int kPauseWaitMs = 30000;
    const quint64 run = l.run;
    QPointer<BtSession> self(this);
    QTimer::singleShot(kPauseWaitMs, this, [self, id, run] {
        const auto w = self->d->locating.find(id);
        if (w == self->d->locating.end() || w->second.run != run || !w->second.waitingPause)
            return;
        logWarning(QStringLiteral("BitTorrent: %1 did not stop; files not linked").arg(id));
        w->second.report.failed += int(w->second.links.size());
        w->second.links.clear();
        self->d->finishLocate(id);
    });
}

} // namespace eMule::bt
