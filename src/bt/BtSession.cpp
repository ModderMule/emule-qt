#include "BtSession.h"

#include "add/BtAddSource.h"
#include "session/BtJoinDiskIo.h"
#include "session/BtSettingsMap.h"
#include "torrent/BtTorrentView.h"
#include "app/AppContext.h"
#include "stats/NetworkCounters.h"
#include "stats/Statistics.h"
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

#include <atomic>
#include <deque>
#include <map>
#include <optional>

namespace lt = libtorrent;

namespace eMule::bt {

namespace {

constexpr int kTickMs = 1000;
/// resume data of changed torrents every 5 min
constexpr int kResumeEveryTicks = 300;
/// stop() waits this long for resume data
constexpr qint64 kStopWaitMs = 5000;
/// Counter samples kept for demand(): the longest rate window plus a sample
constexpr qint64 kRateHistoryMs = 3000;
constexpr qint64 kRateWindowMs = 2000;
constexpr qint64 kRecentWindowMs = 500;
/// libtorrent reports "no limit" on max_connections as (1 << 24) - 1
constexpr int kLtNoLimit = (1 << 24) - 1;

const QString kSessionFile = QStringLiteral("session.dat");
const QString kResumeDir = QStringLiteral("resume");
const QString kResumeSuffix = QStringLiteral(".fastresume");
/// per-torrent seed limits; speeds and peers are in the resume data
const QString kOptionsFile = QStringLiteral("torrent-options.json");
/// the ed3k:file links torrents were added with (torrent.md §4.1)
const QString kLinksFile = QStringLiteral("ed3k-links.json");

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

private:
    QString m_id;
    std::weak_ptr<BtJoinSink> m_sink;
};

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
    /// ed3k joins by id; attached to each disk I/O the session makes
    std::map<QString, std::shared_ptr<BtJoinSink>> joins;
    /// ed3k:file links by id (kLinksFile)
    std::map<QString, QString> links;
    void attachSink(const QString& id) const
    {
        BtJoinDiskIo* disk = io.load();
        const auto j = joins.find(id);
        const auto h = handle(id);
        if (disk && j != joins.end() && h.is_valid())
            disk->attach(h.info_hashes(), std::make_shared<SinkHook>(id, j->second));
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
    BtAddResult add(BtSession& q, lt::add_torrent_params atp, const QString& savePath, bool paused, const QString& link = {});
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
    // libtorrent's thread: only wake ours (research doc §5.2)
    d->ses->set_alert_notify([this] { QMetaObject::invokeMethod(this, &BtSession::drainAlerts, Qt::QueuedConnection); });

    d->loadSeedLimits();
    d->loadLinks();

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
        d->defaultConnections(atp);
        d->ses->async_add_torrent(std::move(atp));
        ++loaded;
    }
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
    for (const auto& [id, h] : d->handles)
        if (h.is_valid() && h.need_save_resume_data())
            d->saveResume(h);

    // drain until every resume data came, or the time is up
    QElapsedTimer waited;
    waited.start();
    while (d->outstandingResume > 0 && waited.elapsed() < kStopWaitMs) {
        if (!d->ses->wait_for_alert(lt::milliseconds(200)))
            continue;
        std::vector<lt::alert*> alerts;
        d->ses->pop_alerts(&alerts);
        for (lt::alert* a : alerts) {
            if (auto* rd = lt::alert_cast<lt::save_resume_data_alert>(a)) {
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
    d->rows.clear();
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
    logDebug(QStringLiteral("BitTorrent: IP filter of %1 range(s)").arg(v4.size() + v6.size()));
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

BtAddResult BtSession::addTorrentFile(const QByteArray& data, const QString& savePath, bool paused)
{
    if (!d->ses)
        return {BtAddOutcome::NotRunning, {}, QStringLiteral("BitTorrent is off")};
    auto atp = BtAddSource::fromTorrentFile(data);
    if (!atp)
        return {BtAddOutcome::Invalid, {}, atp.error()};
    return d->add(*this, std::move(*atp), savePath, paused);
}

std::expected<BtTorrentPreview, QString> BtSession::inspectTorrentFile(const QByteArray& data)
{
    auto atp = BtAddSource::fromTorrentFile(data);
    if (!atp)
        return std::unexpected(atp.error());
    return BtTorrentView::preview(*atp);
}

BtAddResult BtSession::addMagnet(const QString& uri, const QString& savePath, bool paused)
{
    if (!d->ses)
        return {BtAddOutcome::NotRunning, {}, QStringLiteral("BitTorrent is off")};
    auto atp = BtAddSource::fromMagnet(uri);
    if (!atp)
        return {BtAddOutcome::Invalid, {}, atp.error()};
    return d->add(*this, std::move(*atp), savePath, paused, BtAddSource::ed3kLinkOf(uri));
}

bool BtSession::remove(const QString& id, bool deleteFiles)
{
    const auto h = d->handle(id);
    if (!d->ses || !h.is_valid())
        return false;
    d->ses->remove_torrent(h, deleteFiles ? lt::session_handle::delete_files : lt::remove_flags_t{});
    d->handles.erase(id);
    d->rows.erase(id);
    d->limitOverride.remove(id);
    if (d->links.erase(id) > 0)
        d->saveLinks();
    if (auto* disk = d->io.load(); disk && d->joins.contains(id))
        disk->detach(h.info_hashes());
    d->joins.erase(id);
    if (d->seedLimits.erase(id) > 0)
        d->saveSeedLimits();
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
    h.prioritize_files(prio);
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
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    const auto current = h.trackers();
    int tier = 0;
    QSet<QString> known;
    for (const auto& a : current) {
        tier = std::max(tier, static_cast<int>(a.tier));
        known.insert(QString::fromStdString(a.url));
    }
    bool added = false;
    for (const QString& raw : urls) {
        const QString url = raw.trimmed();
        if (url.isEmpty() || known.contains(url))
            continue;
        lt::announce_entry e(url.toStdString());
        e.tier = static_cast<std::uint8_t>(current.empty() ? 0 : tier);
        h.add_tracker(e);
        known.insert(url);
        added = true;
    }
    if (added) {
        h.force_reannounce();
        d->saveResume(h);
    }
    return true;
}

bool BtSession::removeTrackers(const QString& id, const QStringList& urls)
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return false;
    const QSet<QString> drop(urls.cbegin(), urls.cend());
    std::vector<lt::announce_entry> keep;
    for (const auto& a : h.trackers())
        if (!drop.contains(QString::fromStdString(a.url)))
            keep.push_back(a);
    h.replace_trackers(keep);
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
    const auto h = d->handle(id);
    return h.is_valid() ? BtTorrentView::files(h) : QList<BtFileRow>{};
}

QList<BtPeerRow> BtSession::peers(const QString& id) const
{
    const auto h = d->handle(id);
    return h.is_valid() ? BtTorrentView::peers(h) : QList<BtPeerRow>{};
}

QList<BtTrackerRow> BtSession::trackers(const QString& id) const
{
    const auto h = d->handle(id);
    return h.is_valid() ? BtTorrentView::trackers(h) : QList<BtTrackerRow>{};
}

QByteArray BtSession::availabilityMap(const QString& id) const
{
    const auto h = d->handle(id);
    return h.is_valid() ? BtTorrentView::availabilityMap(h) : QByteArray{};
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
    if (!sink || !d->handles.contains(id))
        return false;
    d->joins[id] = std::move(sink);
    d->attachSink(id);
    return true;
}

void BtSession::detachJoin(const QString& id)
{
    const auto h = d->handle(id);
    if (auto* disk = d->io.load(); disk && h.is_valid())
        disk->detach(h.info_hashes());
    d->joins.erase(id);
}

QByteArray BtSession::infoSection(const QString& id) const
{
    const auto h = d->handle(id);
    const auto ti = h.is_valid() ? h.torrent_file() : nullptr;
    if (!ti || !ti->is_valid())
        return {};
    const auto info = ti->info_section();
    return QByteArray(info.data(), qsizetype(info.size()));
}

std::optional<BtLayout> BtSession::layout(const QString& id) const
{
    const auto h = d->handle(id);
    const auto ti = h.is_valid() ? h.torrent_file() : nullptr;
    if (!ti || !ti->is_valid())
        return std::nullopt;
    const std::string savePath = h.status(lt::torrent_handle::query_save_path).save_path;
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
    h.add_piece(lt::piece_index_t(piece), std::vector<char>(data.begin(), data.end()), lt::torrent_handle::overwrite_existing);
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

QBitArray BtSession::havePieces(const QString& id) const
{
    const auto h = d->handle(id);
    if (!h.is_valid())
        return {};
    const auto st = h.status(lt::torrent_handle::query_pieces);
    QBitArray out(st.pieces.size());
    for (int i = 0; i < st.pieces.size(); ++i)
        if (st.pieces.get_bit(lt::piece_index_t(i)))
            out.setBit(i);
    return out;
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
                d->rows[id] = row;
                emit torrentChanged(id);
                d->enforceSeedLimits(st, row);
            }
        } else if (auto* at = lt::alert_cast<lt::add_torrent_alert>(a)) {
            if (at->error) {
                logWarning(QStringLiteral("BitTorrent: could not add: %1").arg(message(at)));
                continue;
            }
            const QString id = BtTorrentView::idOf(at->handle.info_hashes());
            if (d->handles.contains(id))
                continue;
            d->handles[id] = at->handle;
            d->noteMeta(id, at->params);
            d->rows[id] = d->row(at->handle.status());
            d->attachSink(id);
            emit torrentAdded(id);
        } else if (auto* rd = lt::alert_cast<lt::save_resume_data_alert>(a)) {
            d->outstandingResume = std::max(0, d->outstandingResume - 1);
            const QString id = BtTorrentView::idOf(rd->params.info_hashes);
            d->noteMeta(id, rd->params);
            if (d->handles.contains(id) && !writeAtomic(d->resumePath(id), lt::write_resume_data_buf(rd->params)))
                logWarning(QStringLiteral("BitTorrent: could not write resume data of %1").arg(id));
        } else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
            d->outstandingResume = std::max(0, d->outstandingResume - 1);
        } else if (auto* tf = lt::alert_cast<lt::torrent_finished_alert>(a)) {
            const QString id = BtTorrentView::idOf(tf->handle.info_hashes());
            // also posted for a seed loaded at start: only a download counts
            if (auto it = d->rows.find(id); it != d->rows.end()
                && (it->second.state == BtState::Downloading
                    || it->second.state == BtState::DownloadingMetadata))
                if (BtCounters* c = Impl::counters())
                    ++c->torrentsCompleted;
            logInfo(QStringLiteral("BitTorrent: %1").arg(message(tf)));
            d->saveResume(tf->handle);
            emit torrentFinished(id);
        } else if (auto* md = lt::alert_cast<lt::metadata_received_alert>(a)) {
            d->saveResume(md->handle);
            const QString id = BtTorrentView::idOf(md->handle.info_hashes());
            emit torrentChanged(id);
            emit metadataReceived(id);
        } else if (auto* pf = lt::alert_cast<lt::piece_finished_alert>(a)) {
            if (const QString id = BtTorrentView::idOf(pf->handle.info_hashes()); d->joins.contains(id))
                emit pieceResult(id, static_cast<int>(pf->piece_index), true);
        } else if (auto* tc = lt::alert_cast<lt::torrent_checked_alert>(a)) {
            if (const QString id = BtTorrentView::idOf(tc->handle.info_hashes()); d->joins.contains(id))
                emit torrentChecked(id);
        } else if (auto* ls = lt::alert_cast<lt::listen_succeeded_alert>(a)) {
            // one per interface and transport: the port is in the start line
            d->listening = true;
            logDebug(QStringLiteral("BitTorrent: %1").arg(message(ls)));
            emit statusChanged();
        } else if (auto* lf = lt::alert_cast<lt::listen_failed_alert>(a)) {
            logWarning(QStringLiteral("BitTorrent: %1").arg(message(lf)));
            emit statusChanged();
        } else if (auto* pb = lt::alert_cast<lt::peer_blocked_alert>(a)) {
            if (pb->reason == lt::peer_blocked_alert::ip_filter) {
                ++d->blockedPeers;
                // ep: `endpoint` is gone at ABI 4 (Linux static build)
                const auto* blocked = std::get_if<lt::aux::noexcept_movable<lt::tcp::endpoint>>(&pb->ep);
                logDebug(QStringLiteral("BitTorrent: IP filter blocked %1")
                               .arg(blocked ? QString::fromStdString(blocked->address().to_string())
                                            : QStringLiteral("i2p peer")));
            }
        } else if (auto* ds = lt::alert_cast<lt::dht_stats_alert>(a)) {
            int nodes = 0;
            for (const auto& bucket : ds->routing_table)
                nodes += bucket.num_nodes;
            d->dhtNodes = nodes;
        } else if (lt::alert_cast<lt::torrent_error_alert>(a) || lt::alert_cast<lt::file_error_alert>(a)
                   || lt::alert_cast<lt::performance_alert>(a)) {
            logWarning(QStringLiteral("BitTorrent: %1").arg(message(a)));
        } else if (lt::alert_cast<lt::tracker_error_alert>(a) || lt::alert_cast<lt::hash_failed_alert>(a)) {
            if (auto* hf = lt::alert_cast<lt::hash_failed_alert>(a)) {
                if (BtCounters* c = Impl::counters())
                    ++c->hashFailures;
                const QString id = BtTorrentView::idOf(hf->handle.info_hashes());
                // libtorrent counts failed bytes without a state update: refresh the row here
                if (d->handles.contains(id) && hf->handle.is_valid()) {
                    d->rows[id] = d->row(hf->handle.status());
                    emit torrentChanged(id);
                }
                if (d->joins.contains(id))
                    emit pieceResult(id, static_cast<int>(hf->piece_index), false);
            }
            logDebug(QStringLiteral("BitTorrent: %1").arg(message(a)));
        }
    }
}

void BtSession::tick()
{
    if (!d->ses)
        return;
    d->ses->post_torrent_updates();
    d->ses->post_dht_stats();
    if (++d->ticks % kResumeEveryTicks == 0)
        for (const auto& [id, h] : d->handles)
            if (h.is_valid() && h.need_save_resume_data())
                d->saveResume(h, lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified);
}

BtAddResult BtSession::Impl::add(BtSession& q, lt::add_torrent_params atp, const QString& savePath, bool paused, const QString& link)
{
    const lt::info_hash_t hashes = atp.ti ? atp.ti->info_hashes() : atp.info_hashes;
    if (auto found = ses->find_torrent(hashes.get_best()); found.is_valid())
        return {BtAddOutcome::Duplicate, BtTorrentView::idOf(found.info_hashes()), QStringLiteral("already in the list")};
    atp.save_path = (savePath.isEmpty() ? settings.downloadDir : savePath).toStdString();
    if (paused) {
        atp.flags |= lt::torrent_flags::paused;
        atp.flags &= ~lt::torrent_flags::auto_managed;
    } else {
        atp.flags |= lt::torrent_flags::auto_managed;
        atp.flags &= ~lt::torrent_flags::paused;
    }
    defaultConnections(atp);
    const lt::add_torrent_params metaOf = [&atp] {
        lt::add_torrent_params p;
        p.comment = atp.comment;
        p.created_by = atp.created_by;
        p.creation_date = atp.creation_date;
        return p;
    }();
    lt::error_code ec;
    lt::torrent_handle h = ses->add_torrent(std::move(atp), ec);
    if (ec || !h.is_valid())
        return {BtAddOutcome::Failed, {}, QString::fromStdString(ec.message())};
    const QString id = BtTorrentView::idOf(h.info_hashes());
    handles[id] = h;
    noteMeta(id, metaOf);
    rows[id] = row(h.status());
    if (!link.isEmpty()) {
        links[id] = link;
        saveLinks();
    }
    // kept across a restart from now on
    saveResume(h);
    logInfo(QStringLiteral("BitTorrent: added %1").arg(rows[id].name));
    if (BtCounters* c = counters())
        ++c->torrentsAdded;
    emit q.torrentAdded(id);
    return {BtAddOutcome::Added, id, {}};
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
