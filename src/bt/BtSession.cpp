#include "BtSession.h"

#include "add/BtAddSource.h"
#include "session/BtJoinDiskIo.h"
#include "session/BtSettingsMap.h"
#include "torrent/BtTorrentView.h"
#include "utils/Log.h"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/kademlia/dht_state.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/write_resume_data.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

#include <atomic>
#include <map>

namespace lt = libtorrent;

namespace eMule::bt {

namespace {

constexpr int kTickMs = 1000;
/// resume data of changed torrents every 5 min
constexpr int kResumeEveryTicks = 300;
/// stop() waits this long for resume data
constexpr qint64 kStopWaitMs = 5000;

const QString kSessionFile = QStringLiteral("session.dat");
const QString kResumeDir = QStringLiteral("resume");
const QString kResumeSuffix = QStringLiteral(".fastresume");

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

QString message(const lt::alert* a)
{
    return QString::fromStdString(a->message());
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
    int ticks = 0;
    int outstandingResume = 0;
    bool listening = false;
    int dhtNodes = 0;

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
    BtAddResult add(BtSession& q, lt::add_torrent_params atp, const QString& savePath, bool paused);
    void enforceSeedLimits(const lt::torrent_status& st, const BtTorrentRow& row);
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
    params.settings = BtSettingsMap::toPack(settings);
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
    logInfo(QStringLiteral("BitTorrent: stopped"));
    emit statusChanged();
}

bool BtSession::isRunning() const
{
    return d->ses != nullptr;
}

void BtSession::applySettings(const BtSettings& settings)
{
    d->settings = settings;
    if (d->ses)
        d->ses->apply_settings(BtSettingsMap::toPack(settings));
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

BtAddResult BtSession::addMagnet(const QString& uri, const QString& savePath, bool paused)
{
    if (!d->ses)
        return {BtAddOutcome::NotRunning, {}, QStringLiteral("BitTorrent is off")};
    auto atp = BtAddSource::fromMagnet(uri);
    if (!atp)
        return {BtAddOutcome::Invalid, {}, atp.error()};
    return d->add(*this, std::move(*atp), savePath, paused);
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

void BtSession::drainAlerts()
{
    if (!d->ses)
        return;
    std::vector<lt::alert*> alerts;
    d->ses->pop_alerts(&alerts);
    // pointers die at the next pop_alerts(): copy what is kept
    for (lt::alert* a : alerts) {
        if (auto* su = lt::alert_cast<lt::state_update_alert>(a)) {
            for (const lt::torrent_status& st : su->status) {
                BtTorrentRow row = BtTorrentView::row(st);
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
            d->rows[id] = BtTorrentView::row(at->handle.status());
            emit torrentAdded(id);
        } else if (auto* rd = lt::alert_cast<lt::save_resume_data_alert>(a)) {
            d->outstandingResume = std::max(0, d->outstandingResume - 1);
            const QString id = BtTorrentView::idOf(rd->params.info_hashes);
            if (d->handles.contains(id) && !writeAtomic(d->resumePath(id), lt::write_resume_data_buf(rd->params)))
                logWarning(QStringLiteral("BitTorrent: could not write resume data of %1").arg(id));
        } else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
            d->outstandingResume = std::max(0, d->outstandingResume - 1);
        } else if (auto* tf = lt::alert_cast<lt::torrent_finished_alert>(a)) {
            const QString id = BtTorrentView::idOf(tf->handle.info_hashes());
            logInfo(QStringLiteral("BitTorrent: %1").arg(message(tf)));
            d->saveResume(tf->handle);
            emit torrentFinished(id);
        } else if (auto* md = lt::alert_cast<lt::metadata_received_alert>(a)) {
            d->saveResume(md->handle);
            emit torrentChanged(BtTorrentView::idOf(md->handle.info_hashes()));
        } else if (auto* ls = lt::alert_cast<lt::listen_succeeded_alert>(a)) {
            // one per interface and transport: the port is in the start line
            d->listening = true;
            logDebug(QStringLiteral("BitTorrent: %1").arg(message(ls)));
            emit statusChanged();
        } else if (auto* lf = lt::alert_cast<lt::listen_failed_alert>(a)) {
            logWarning(QStringLiteral("BitTorrent: %1").arg(message(lf)));
            emit statusChanged();
        } else if (auto* ds = lt::alert_cast<lt::dht_stats_alert>(a)) {
            int nodes = 0;
            for (const auto& bucket : ds->routing_table)
                nodes += bucket.num_nodes;
            d->dhtNodes = nodes;
        } else if (lt::alert_cast<lt::torrent_error_alert>(a) || lt::alert_cast<lt::file_error_alert>(a)
                   || lt::alert_cast<lt::performance_alert>(a)) {
            logWarning(QStringLiteral("BitTorrent: %1").arg(message(a)));
        } else if (lt::alert_cast<lt::tracker_error_alert>(a) || lt::alert_cast<lt::hash_failed_alert>(a)) {
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

BtAddResult BtSession::Impl::add(BtSession& q, lt::add_torrent_params atp, const QString& savePath, bool paused)
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
    lt::error_code ec;
    lt::torrent_handle h = ses->add_torrent(std::move(atp), ec);
    if (ec || !h.is_valid())
        return {BtAddOutcome::Failed, {}, QString::fromStdString(ec.message())};
    const QString id = BtTorrentView::idOf(h.info_hashes());
    handles[id] = h;
    rows[id] = BtTorrentView::row(h.status());
    // kept across a restart from now on
    saveResume(h);
    logInfo(QStringLiteral("BitTorrent: added %1").arg(rows[id].name));
    emit q.torrentAdded(id);
    return {BtAddOutcome::Added, id, {}};
}

void BtSession::Impl::enforceSeedLimits(const lt::torrent_status& st, const BtTorrentRow& row)
{
    if (!st.is_seeding || row.paused || limitOverride.contains(row.id))
        return;
    const BtSettings& s = settings;
    const qint64 base = std::max<qint64>({st.all_time_download, st.total_done, 1});
    const bool ratioReached = s.seedRatioPercent > 0 && st.all_time_upload * 100 >= base * s.seedRatioPercent;
    const bool timeReached = s.seedTimeLimitMinutes > 0 && st.seeding_duration.count() >= qint64(s.seedTimeLimitMinutes) * 60;
    if (!ratioReached && !timeReached)
        return;
    st.handle.unset_flags(lt::torrent_flags::auto_managed);
    st.handle.pause();
    logInfo(QStringLiteral("BitTorrent: seeding of %1 stopped, %2 reached").arg(row.name, ratioReached ? QStringLiteral("ratio") : QStringLiteral("seed time")));
}

} // namespace eMule::bt
