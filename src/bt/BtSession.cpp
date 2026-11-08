#include "BtSession.h"

#include "add/BtAddSource.h"
#include "session/BtJoinDiskIo.h"
#include "session/BtSettingsMap.h"
#include "torrent/BtTorrentView.h"
#include "app/AppContext.h"
#include "stats/NetworkCounters.h"
#include "stats/Statistics.h"
#include "utils/BackgroundFileWriter.h"
#include "utils/Log.h"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <QTimer>

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <optional>

namespace lt = libtorrent;

namespace eMule::bt {

namespace {

constexpr int kTickMs = 1000;
/// resume data of changed torrents every 5 min
constexpr int kResumeEveryTicks = 300;
/// a torrent that only moved data: its totals every 30 min (and at stop). libtorrent
/// marks the totals changed each second a torrent is active, so every seed counted as
/// modified and re-saved with its info dict every 5 min (report 28)
constexpr int kCountersEveryTicks = 1800;
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
/// magnets to stop once their metadata is in (BtAddSource::applyStopAfterMetadata)
const QString kStopsFile = QStringLiteral("stop-after-metadata.json");

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

QByteArray readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

bool writeAtomic(const QString& path, const std::vector<char>& data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(data.data(), static_cast<qint64>(data.size())) != static_cast<qint64>(data.size())) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
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
    QTimer* timer = nullptr;
    std::map<QString, lt::torrent_handle> handles;
    std::map<QString, BtTorrentRow> rows;
    /// stopped by a seed limit, and resumed by the user since: the limit stays off
    QSet<QString> limitOverride;
    /// seed limits that differ from the global ones
    std::map<QString, BtTorrentOptions> seedLimits;
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
    int dhtNodes = 0;
    /// last SAM error; cleared when the SAM bridge changes
    QString i2pError;
    /// ed3k joins by id; attached to each disk I/O the session makes
    std::map<QString, std::shared_ptr<BtJoinSink>> joins;
    /// ed3k:file links by id (kLinksFile)
    std::map<QString, QString> links;
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
        if (!writeAtomic(linksPath(), std::vector<char>(json.begin(), json.end())))
            logWarning(QStringLiteral("BitTorrent: could not write %1").arg(linksPath()));
    }
    /// ids stopped at metadata (kStopsFile); default_dont_download is not in the resume data
    QSet<QString> stopAtMeta;
    /// stopped at metadata, not resumed since: nothing wanted, so a finish is not a download
    QSet<QString> metaStopped;
    /// finished, cache flush asked: pread_disk_io holds written blocks in memory, so
    /// torrentFinished waits for cache_flushed_alert (files complete on disk)
    QSet<QString> finishFlush;
    /// flushToDisk tickets: last given, last answered. A cache_flushed_alert answers these
    /// first: the only other ones are libtorrent's own at finish (covers every write) and ours
    struct Flushes {
        quint64 asked = 0;
        quint64 answered = 0;
    };
    std::map<QString, Flushes> askedFlush;
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
            QFile::remove(stopsPath());
            return;
        }
        QJsonArray ids;
        for (const QString& id : stopAtMeta)
            ids.append(id);
        const QByteArray json = QJsonDocument(ids).toJson(QJsonDocument::Compact);
        if (!writeAtomic(stopsPath(), std::vector<char>(json.begin(), json.end())))
            logWarning(QStringLiteral("BitTorrent: could not write %1").arg(stopsPath()));
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
        case Ask::Trackers: h.post_trackers(); break;
        case Ask::Availability:
            // piece_availability asserts without metadata
            if (const auto ti = h.torrent_file(); ti && ti->is_valid())
                h.post_piece_availability();
            break;
        }
        return dt;
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
        QList<BtFileRow> out = BtTorrentView::files(*ti, dt.progress, dt.priorities);
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
    BtAddResult add(BtSession& q, lt::add_torrent_params atp, const QString& savePath, bool paused,
                    std::optional<bool> i2pOnly, std::optional<bool> stopAfterMetadata,
                    const QString& link = {}, const QList<int>& wanted = {});
    /// switch @p wanted files of a listed torrent on, the others as they are
    void wantFiles(const lt::torrent_handle& h, const QString& id, const QList<int>& wanted);
    void enforceSeedLimits(const lt::torrent_status& st, const BtTorrentRow& row);
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
    /// BtTorrentView::row() plus what torrent_info no longer has
    [[nodiscard]] BtTorrentRow row(const lt::torrent_status& st) const
    {
        BtTorrentRow r = BtTorrentView::row(st);
        if (auto it = meta.find(r.id); it != meta.end()) {
            r.comment = it->second.comment;
            r.creator = it->second.creator;
            r.createdAt = it->second.createdAt;
        }
        return r;
    }
    /// unset or unlimited per-torrent connections → the configured default
    void defaultConnections(lt::add_torrent_params& atp) const
    {
        if (atp.max_connections <= 0 || atp.max_connections >= kLtNoLimit)
            atp.max_connections = settings.maxConnectionsPerTorrent;
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
    d->loadLinks();
    d->loadStops();
    const qsizetype stopsAtLoad = d->stopAtMeta.size();

    // the torrents of the last run
    const QDir resumeDir(QDir(settings.stateDir).filePath(kResumeDir));
    int loaded = 0;
    for (const QString& file : resumeDir.entryList({QStringLiteral("*") + kResumeSuffix}, QDir::Files)) {
        const QByteArray data = readAll(resumeDir.filePath(file));
        lt::error_code ec;
        lt::add_torrent_params atp = lt::read_resume_data(lt::span<char const>(data.constData(), static_cast<std::ptrdiff_t>(data.size())), ec);
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
        d->defaultConnections(atp);
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
    d->rows.clear();
    d->finishFlush.clear();
    d->askedFlush.clear();
    d->have.clear();
    d->checkWaited.clear();
    d->outstandingResume = 0;
    d->listening = false;
    d->dhtNodes = 0;
    d->rateSamples.clear();
    d->meta.clear();
    logInfo(QStringLiteral("BitTorrent: stopped"));
    emit statusChanged();
}

bool BtSession::isRunning() const
{
    return d->ses != nullptr;
}

void BtSession::applySettings(const BtSettings& settings)
{
    const int oldPerTorrent = d->settings.maxConnectionsPerTorrent;
    const bool wasBlocked = d->settings.networkBlocked;
    if (d->settings.i2pSamHost != settings.i2pSamHost || d->settings.i2pSamPort != settings.i2pSamPort
        || d->settings.i2pEnabled != settings.i2pEnabled)
        d->i2pError.clear();
    d->settings = settings;
    if (!d->ses)
        return;
    if (settings.networkBlocked && !wasBlocked) {
        d->ses->pause();
        d->listening = false;
    }
    d->ses->apply_settings(d->pack(settings));
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
    const lt::remove_flags_t flags = deleteFiles ? lt::session_handle::delete_files : lt::remove_flags_t{};
    if (h.is_valid())
        d->ses->remove_torrent(h, flags);
    else
        pending->second.removed = flags;   // removed when libtorrent has added it
    d->handles.erase(id);
    d->rows.erase(id);
    d->limitOverride.remove(id);
    d->metaStopped.remove(id);
    d->finishFlush.remove(id);
    d->askedFlush.erase(id);
    d->have.erase(id);
    d->checkWaited.remove(id);
    d->pendingPriorities.erase(id);
    d->details.erase(id);
    if (d->links.erase(id) > 0)
        d->saveLinks();
    if (d->stopAtMeta.remove(id))
        d->saveStops();
    if (auto* disk = d->io.load(); disk && d->joins.contains(id))
        disk->detach(hashes);
    d->joins.erase(id);
    if (d->seedLimits.erase(id) > 0)
        d->saveSeedLimits();
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

bool BtSession::resume(const QString& id)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    // a seed the user resumes keeps seeding past the limits
    if (auto it = d->rows.find(id); it != d->rows.end() && it->second.paused && it->second.size > 0 && it->second.done >= it->second.size)
        d->limitOverride.insert(id);
    d->metaStopped.remove(id);
    h.set_flags(lt::torrent_flags::auto_managed);
    h.resume();
    return true;
}

bool BtSession::recheck(const QString& id)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    h.force_recheck();
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
    const QList<BtTrackerEntry> after = editTrackerList(before, edit);
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
            continue;
        }
        lt::announce_entry a(e.url.toStdString());
        a.tier = static_cast<std::uint8_t>(e.tier);
        next.push_back(a);
        added = true;
    }
    h.replace_trackers(next);
    d->details[id].trackers = next;
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
    s.i2pEnabled = d->settings.i2pEnabled;
    s.i2pSam = d->settings.hasSam();
    s.i2pError = d->i2pError;
    s.torrents = static_cast<int>(d->rows.size());
    for (const auto& [id, row] : d->rows) {
        s.rateDown += row.rateDown;
        s.rateUp += row.rateUp;
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
        out.files.append({QString::fromStdString(fs.file_path(f, savePath)), fs.file_size(f), fs.file_offset(f), fs.pad_file_at(f)});
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

quint64 BtSession::flushToDisk(const QString& id)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return 0;
    h.flush_cache();
    return ++d->askedFlush[id].asked;
}

void BtSession::raisePieces(const QString& id, const QList<int>& pieces)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return;
    for (const int p : pieces)
        h.set_piece_deadline(lt::piece_index_t(p), 0);
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
        // No SAM: the placeholder bridge fails each second (libtorrent 2.1.2 resets its
        // backoff in update_i2p_bridge), not worth a log line each time
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
            if (at->error) {
                logWarning(QStringLiteral("BitTorrent: could not add: %1").arg(message(at)));
                // the row add() put up goes again
                if (!pending.empty() && !pending.mapped().removed && !d->handles.contains(id)) {
                    d->rows.erase(id);
                    d->details.erase(id);
                    if (d->links.erase(id) > 0)
                        d->saveLinks();
                    if (d->stopAtMeta.remove(id))
                        d->saveStops();
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
            // was: row(handle.status()), a sync call per torrent; the real one follows
            d->rows[id] = d->row(addedStatus(p, at->handle, at->handle.info_hashes()));
            at->handle.post_status();
            d->attachSink(id);
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
                tf->handle.flush_cache();
            }
        } else if (auto* cf = lt::alert_cast<lt::cache_flushed_alert>(a)) {
            // also posted on removal: only a torrent still in the list
            const QString id = BtTorrentView::idOf(cf->handle.info_hashes());
            if (!d->handles.contains(id)) {
            } else if (auto it = d->askedFlush.find(id); it != d->askedFlush.end() && it->second.answered < it->second.asked) {
                emit torrentFlushed(id, ++it->second.answered);
            } else if (d->finishFlush.remove(id)) {
                emit torrentFinished(id);
            }
        } else if (auto* md = lt::alert_cast<lt::metadata_received_alert>(a)) {
            const QString id = BtTorrentView::idOf(md->handle.info_hashes());
            if (d->stopAtMeta.contains(id))
                d->stopAtMetadata(md->handle, id);
            d->saveResume(md->handle);
            emit torrentChanged(id);
            emit metadataReceived(id);
        } else if (auto* pf = lt::alert_cast<lt::piece_finished_alert>(a)) {
            const QString id = BtTorrentView::idOf(pf->handle.info_hashes());
            if (auto hv = d->have.find(id); hv != d->have.end() && int(pf->piece_index) < hv->second.size())
                hv->second.setBit(int(pf->piece_index));
            if (d->joins.contains(id))
                emit pieceResult(id, static_cast<int>(pf->piece_index), true);
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
            if (auto dt = d->details.find(BtTorrentView::idOf(tl->handle.info_hashes())); dt != d->details.end())
                dt->second.trackers = tl->trackers;
        } else if (auto* pa = lt::alert_cast<lt::piece_availability_alert>(a)) {
            if (auto dt = d->details.find(BtTorrentView::idOf(pa->handle.info_hashes())); dt != d->details.end())
                dt->second.availability = pa->piece_availability;
        } else if (lt::alert_cast<lt::listen_succeeded_alert>(a)) {
            // one per interface and transport: the port is in the start line
            d->listening = true;
            emit statusChanged();
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
            int nodes = 0;
            for (const auto& bucket : ds->routing_table)
                nodes += bucket.num_nodes;
            d->dhtNodes = nodes;
        } else if (auto* fp = lt::alert_cast<lt::file_prio_alert>(a)) {
            d->filePrioritiesApplied(BtTorrentView::idOf(fp->handle.info_hashes()));
            if (fp->error)
                logWarning(QStringLiteral("BitTorrent: %1").arg(message(a)));
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
    const bool i2p = BtAddSource::applyI2p(atp, settings, i2pOnly);
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
    ses->async_add_torrent(std::move(atp));
    logInfo(QStringLiteral("BitTorrent: added %1%2").arg(rows[id].name, i2p ? QStringLiteral(" (I2P only)") : QString()));
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
    if (!writeAtomic(optionsPath(), std::vector<char>(json.cbegin(), json.cend())))
        logWarning(QStringLiteral("BitTorrent: could not write %1").arg(kOptionsFile));
}

void BtSession::Impl::enforceSeedLimits(const lt::torrent_status& st, const BtTorrentRow& row)
{
    if (!st.is_seeding || row.paused || limitOverride.contains(row.id))
        return;
    int ratioPercent = settings.seedRatioPercent;
    int seedMinutes = settings.seedTimeLimitMinutes;
    if (auto it = seedLimits.find(row.id); it != seedLimits.end()) {
        if (it->second.ratioMode != BtLimitMode::Global)
            ratioPercent = it->second.ratioMode == BtLimitMode::Custom ? it->second.ratioPercent : 0;
        if (it->second.seedTimeMode != BtLimitMode::Global)
            seedMinutes = it->second.seedTimeMode == BtLimitMode::Custom ? it->second.seedTimeMinutes : 0;
    }
    const qint64 base = std::max<qint64>({st.all_time_download, st.total_done, 1});
    const bool ratioReached = ratioPercent > 0 && st.all_time_upload * 100 >= base * ratioPercent;
    const bool timeReached = seedMinutes > 0 && st.seeding_duration.count() >= qint64(seedMinutes) * 60;
    if (!ratioReached && !timeReached)
        return;
    st.handle.unset_flags(lt::torrent_flags::auto_managed);
    st.handle.pause();
    logInfo(QStringLiteral("BitTorrent: seeding of %1 stopped, %2 reached").arg(row.name, ratioReached ? QStringLiteral("ratio") : QStringLiteral("seed time")));
}

lt::settings_pack BtSession::Impl::pack(const BtSettings& s) const
{
    lt::settings_pack p = BtSettingsMap::toPack(s);
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

} // namespace eMule::bt
