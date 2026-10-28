#include "BtBridge.h"

#include "BandwidthCoordinator.h"
#include "app/AppConfig.h"
#include "app/AppContext.h"
#include "ipfilter/IPFilter.h"
#include "prefs/Preferences.h"
#include "stats/NetworkCounters.h"
#include "stats/Statistics.h"

#include <QCborArray>
#include <QDir>
#include <QStringList>

#include <algorithm>
#include <climits>
#include <optional>

#ifdef EMULE_HAVE_BITTORRENT
#include "BtSession.h"
#include "BtTypes.h"
#endif

namespace eMule::BtBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

bt::BtSession* s_session = nullptr;

IpcMessage unavailable(const IpcMessage& msg)
{
    return IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("BitTorrent engine unavailable"));
}

#ifdef EMULE_HAVE_BITTORRENT

QCborMap toCbor(const bt::BtTorrentRow& r)
{
    return QCborMap{
        {QStringLiteral("id"), r.id},
        {QStringLiteral("name"), r.name},
        {QStringLiteral("savePath"), r.savePath},
        {QStringLiteral("state"), static_cast<int>(r.state)},
        {QStringLiteral("size"), r.size},
        {QStringLiteral("done"), r.done},
        {QStringLiteral("progress"), r.progress},
        {QStringLiteral("rateDown"), r.rateDown},
        {QStringLiteral("rateUp"), r.rateUp},
        {QStringLiteral("peers"), r.peers},
        {QStringLiteral("seeds"), r.seeds},
        {QStringLiteral("peersTotal"), r.peersTotal},
        {QStringLiteral("seedsTotal"), r.seedsTotal},
        {QStringLiteral("downloaded"), r.downloaded},
        {QStringLiteral("uploaded"), r.uploaded},
        {QStringLiteral("failedBytes"), r.failedBytes},
        {QStringLiteral("error"), r.error},
        {QStringLiteral("hashV1"), r.hashV1},
        {QStringLiteral("hashV2"), r.hashV2},
        {QStringLiteral("paused"), r.paused},
        {QStringLiteral("hasMetadata"), r.hasMetadata},
        {QStringLiteral("private"), r.isPrivate},
        {QStringLiteral("addedAt"), r.addedAt},
        {QStringLiteral("completedAt"), r.completedAt},
        {QStringLiteral("seedingSeconds"), r.seedingSeconds},
        {QStringLiteral("queuePosition"), r.queuePosition},
        {QStringLiteral("pieceMap"), r.pieceMap},
        {QStringLiteral("pieces"), r.pieces},
        {QStringLiteral("pieceLength"), r.pieceLength},
        {QStringLiteral("numFiles"), r.numFiles},
        {QStringLiteral("eta"), r.eta},
        {QStringLiteral("comment"), r.comment},
        {QStringLiteral("creator"), r.creator},
        {QStringLiteral("createdAt"), r.createdAt},
        {QStringLiteral("lastActivity"), r.lastActivity},
        {QStringLiteral("activeSeconds"), r.activeSeconds},
        {QStringLiteral("haveBytes"), r.haveBytes},
        {QStringLiteral("totalSize"), r.totalSize},
        {QStringLiteral("availability"), r.availability},
    };
}

QCborMap optionsToCbor(const bt::BtTorrentOptions& o)
{
    return QCborMap{
        {QStringLiteral("downloadLimitKb"), o.downloadLimitKb},
        {QStringLiteral("uploadLimitKb"), o.uploadLimitKb},
        {QStringLiteral("maxPeers"), o.maxPeers},
        {QStringLiteral("ratioMode"), static_cast<int>(o.ratioMode)},
        {QStringLiteral("ratioPercent"), o.ratioPercent},
        {QStringLiteral("seedTimeMode"), static_cast<int>(o.seedTimeMode)},
        {QStringLiteral("seedTimeMinutes"), o.seedTimeMinutes},
    };
}

bt::BtLimitMode limitModeOf(qint64 v)
{
    return v == 1 ? bt::BtLimitMode::Unlimited : v == 2 ? bt::BtLimitMode::Custom : bt::BtLimitMode::Global;
}

bt::BtTorrentOptions optionsFromCbor(const QCborMap& m)
{
    bt::BtTorrentOptions o;
    // Never above the global limits (0 = none): the IPC and REST paths both end here
    const auto perTorrent = [&m](const QString& key, uint32 globalKb) {
        const qint64 kb = std::max<qint64>(0, m.value(key).toInteger());
        return static_cast<int>(globalKb > 0 && kb > globalKb ? qint64(globalKb)
                                                              : std::min<qint64>(kb, INT_MAX / 1024));
    };
    o.downloadLimitKb = perTorrent(QStringLiteral("downloadLimitKb"), thePrefs.maxDownload());
    o.uploadLimitKb = perTorrent(QStringLiteral("uploadLimitKb"), thePrefs.maxUpload());
    o.maxPeers = static_cast<int>(std::max<qint64>(0, m.value(QStringLiteral("maxPeers")).toInteger()));
    o.ratioMode = limitModeOf(m.value(QStringLiteral("ratioMode")).toInteger());
    o.ratioPercent = static_cast<int>(m.value(QStringLiteral("ratioPercent")).toInteger(100));
    o.seedTimeMode = limitModeOf(m.value(QStringLiteral("seedTimeMode")).toInteger());
    o.seedTimeMinutes = static_cast<int>(m.value(QStringLiteral("seedTimeMinutes")).toInteger(60));
    return o;
}

QStringList stringsOf(const QCborArray& arr)
{
    QStringList out;
    for (const auto& v : arr)
        out.append(v.toString());
    return out;
}

QCborMap addResultToCbor(const bt::BtAddResult& r)
{
    return QCborMap{
        {QStringLiteral("outcome"), static_cast<int>(r.outcome)},
        {QStringLiteral("id"), r.id},
        {QStringLiteral("message"), r.message},
    };
}

QCborMap detailsToCbor(const QString& id)
{
    QCborArray files;
    for (const auto& f : s_session->files(id))
        files.append(QCborMap{
            {QStringLiteral("index"), f.index},
            {QStringLiteral("path"), f.path},
            {QStringLiteral("size"), f.size},
            {QStringLiteral("done"), f.done},
            {QStringLiteral("priority"), f.priority},
            {QStringLiteral("pad"), f.pad},
        });
    QCborArray peers;
    for (const auto& p : s_session->peers(id))
        peers.append(QCborMap{
            {QStringLiteral("address"), p.address},
            {QStringLiteral("client"), p.client},
            {QStringLiteral("progress"), p.progress},
            {QStringLiteral("rateDown"), p.rateDown},
            {QStringLiteral("rateUp"), p.rateUp},
            {QStringLiteral("downloaded"), p.downloaded},
            {QStringLiteral("uploaded"), p.uploaded},
            {QStringLiteral("seed"), p.seed},
            {QStringLiteral("encrypted"), p.encrypted},
            {QStringLiteral("utp"), p.utp},
            {QStringLiteral("source"), p.source},
            {QStringLiteral("flags"), p.flags},
        });
    QCborArray trackers;
    for (const auto& t : s_session->trackers(id))
        trackers.append(QCborMap{
            {QStringLiteral("url"), t.url},
            {QStringLiteral("tier"), t.tier},
            {QStringLiteral("working"), t.working},
            {QStringLiteral("message"), t.message},
            {QStringLiteral("seeds"), t.seeds},
            {QStringLiteral("peers"), t.peers},
            {QStringLiteral("downloaded"), t.downloaded},
            {QStringLiteral("nextAnnounce"), t.nextAnnounce},
            {QStringLiteral("updating"), t.updating},
            {QStringLiteral("fails"), t.fails},
            {QStringLiteral("hasError"), t.hasError},
        });
    QCborMap out{
        {QStringLiteral("files"), files},
        {QStringLiteral("peers"), peers},
        {QStringLiteral("trackers"), trackers},
        {QStringLiteral("availabilityMap"), s_session->availabilityMap(id)},
    };
    if (const auto o = s_session->options(id))
        out.insert(QStringLiteral("options"), optionsToCbor(*o));
    return out;
}

#endif // EMULE_HAVE_BITTORRENT

} // namespace

void setSession(bt::BtSession* session)
{
    s_session = session;
}

bt::BtSession* session()
{
    return s_session;
}

bool available()
{
#ifdef EMULE_HAVE_BITTORRENT
    return true;
#else
    return false;
#endif
}

quint16 listenPort()
{
#ifdef EMULE_HAVE_BITTORRENT
    if (!s_session)
        return 0;
    const bt::BtStatus st = s_session->status();
    return st.running && st.listening ? st.listenPort : quint16(0);
#else
    return 0;
#endif
}

bool isBtRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    return t >= 800 && t <= 899;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isBtRequest(msg.type()))
        return false;
#ifndef EMULE_HAVE_BITTORRENT
    reply(unavailable(msg));
    return true;
#else
    const int seq = msg.seqId();
    if (msg.type() == IpcMsgType::GetBtStatus) {
        reply(IpcMessage::makeResult(seq, true, statusToCbor()));
        return true;
    }
    if (msg.type() == IpcMsgType::GetBtStats) {
        reply(IpcMessage::makeResult(seq, true, statsToCbor()));
        return true;
    }
    if (!s_session || !s_session->isRunning()) {
        // the list is empty, not an error, while BitTorrent is off
        if (msg.type() == IpcMsgType::GetTorrents)
            reply(IpcMessage::makeResult(seq, true, QCborArray{}));
        else
            reply(unavailable(msg));
        return true;
    }
    const QString id = msg.fieldString(0);
    auto boolResult = [&](bool ok) {
        reply(ok ? IpcMessage::makeResult(seq, true, true) : IpcMessage::makeError(seq, 404, QStringLiteral("Torrent not found")));
    };
    switch (msg.type()) {
    case IpcMsgType::GetTorrents: {
        QCborArray rows;
        for (const auto& r : s_session->torrents())
            rows.append(toCbor(r));
        reply(IpcMessage::makeResult(seq, true, rows));
        break;
    }
    case IpcMsgType::AddTorrentFile: {
        const QByteArray data = msg.field(0).toByteArray();
        reply(IpcMessage::makeResult(seq, true, addResultToCbor(s_session->addTorrentFile(data, msg.fieldString(1), msg.fieldBool(2)))));
        break;
    }
    case IpcMsgType::AddMagnet:
        reply(IpcMessage::makeResult(seq, true, addResultToCbor(s_session->addMagnet(id, msg.fieldString(1), msg.fieldBool(2)))));
        break;
    case IpcMsgType::RemoveTorrent: boolResult(s_session->remove(id, msg.fieldBool(1))); break;
    case IpcMsgType::PauseTorrent: boolResult(s_session->pause(id)); break;
    case IpcMsgType::ResumeTorrent: boolResult(s_session->resume(id)); break;
    case IpcMsgType::RecheckTorrent: boolResult(s_session->recheck(id)); break;
    case IpcMsgType::ReannounceTorrent: boolResult(s_session->reannounce(id)); break;
    case IpcMsgType::GetTorrentDetails:
        if (!s_session->torrent(id))
            boolResult(false);
        else
            reply(IpcMessage::makeResult(seq, true, detailsToCbor(id)));
        break;
    case IpcMsgType::SetTorrentFilePriorities: {
        QList<int> prio;
        for (const auto& v : msg.fieldArray(1))
            prio.append(static_cast<int>(v.toInteger()));
        boolResult(s_session->setFilePriorities(id, prio));
        break;
    }
    case IpcMsgType::SetTorrentOptions:
        boolResult(s_session->setOptions(id, optionsFromCbor(msg.fieldMap(1))));
        break;
    case IpcMsgType::AddTorrentTrackers:
        boolResult(s_session->addTrackers(id, stringsOf(msg.fieldArray(1))));
        break;
    case IpcMsgType::RemoveTorrentTrackers:
        boolResult(s_session->removeTrackers(id, stringsOf(msg.fieldArray(1))));
        break;
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown BitTorrent request")));
        break;
    }
    return true;
#endif
}

QCborMap rowToCbor(const QString& id)
{
#ifdef EMULE_HAVE_BITTORRENT
    if (s_session)
        if (auto row = s_session->torrent(id))
            return toCbor(*row);
#else
    Q_UNUSED(id);
#endif
    return {};
}

QCborMap statsToCbor()
{
    const Statistics* stats = theApp.statistics;
    QCborMap out;
    out.insert(QStringLiteral("session"), countersToCbor(stats ? stats->btSession() : BtCounters{}));
    out.insert(QStringLiteral("cumulative"),
               countersToCbor(stats ? stats->cumulativeBt() : thePrefs.cumBt()));

    QCborMap current;
    qint64 rateDown = 0, rateUp = 0, peers = 0;
    qint64 totalBytes = 0, doneBytes = 0;
    int downloading = 0, seeding = 0, paused = 0, checking = 0, queued = 0, error = 0;
    int count = 0;
    bool running = false;
    int dhtNodes = 0;
#ifdef EMULE_HAVE_BITTORRENT
    if (s_session && s_session->isRunning()) {
        running = true;
        dhtNodes = s_session->status().dhtNodes;
        for (const bt::BtTorrentRow& r : s_session->torrents()) {
            ++count;
            rateDown += r.rateDown;
            rateUp += r.rateUp;
            peers += r.peers;
            totalBytes += r.size;
            doneBytes += r.done;
            using S = bt::BtState;
            if (r.paused || r.state == S::Paused)
                ++paused;
            else if (r.state == S::Error)
                ++error;
            else if (r.state == S::CheckingFiles || r.state == S::CheckingResume)
                ++checking;
            else if (r.state == S::Queued)
                ++queued;
            else if (r.state == S::Downloading || r.state == S::DownloadingMetadata)
                ++downloading;
            else if (r.state == S::Seeding || r.state == S::Finished)
                ++seeding;
        }
    }
#endif
    const auto* coordinator = BandwidthCoordinator::instance();
    current.insert(QStringLiteral("running"), running);
    current.insert(QStringLiteral("downRate"), rateDown);
    current.insert(QStringLiteral("upRate"), rateUp);
    current.insert(QStringLiteral("downLimitKb"), coordinator ? coordinator->snapshot().btDownKb : qint64(0));
    current.insert(QStringLiteral("upLimitKb"), coordinator ? coordinator->snapshot().btUpKb : qint64(0));
    current.insert(QStringLiteral("peers"), peers);
    current.insert(QStringLiteral("dhtNodes"), dhtNodes);
    current.insert(QStringLiteral("torrents"), QCborMap{
        {QStringLiteral("count"),       count},
        {QStringLiteral("downloading"), downloading},
        {QStringLiteral("seeding"),     seeding},
        {QStringLiteral("paused"),      paused},
        {QStringLiteral("checking"),    checking},
        {QStringLiteral("queued"),      queued},
        {QStringLiteral("error"),       error},
        {QStringLiteral("totalBytes"),  totalBytes},
        {QStringLiteral("doneBytes"),   doneBytes},
        {QStringLiteral("leftBytes"),   std::max<qint64>(0, totalBytes - doneBytes)},
    });
    out.insert(QStringLiteral("current"), current);
    return out;
}

EngineRates ratesKb()
{
#ifdef EMULE_HAVE_BITTORRENT
    if (s_session && s_session->isRunning()) {
        const bt::BtStatus st = s_session->status();
        return {static_cast<float>(st.rateDown) / 1024.0f, static_cast<float>(st.rateUp) / 1024.0f};
    }
#endif
    return {};
}

void insertRates(QCborMap& stats)
{
    const EngineRates r = ratesKb();
    stats.insert(QStringLiteral("rateDownBt"), static_cast<double>(r.down));
    stats.insert(QStringLiteral("rateUpBt"), static_cast<double>(r.up));
}

QCborMap statusToCbor()
{
    QCborMap out{{QStringLiteral("available"), available()}, {QStringLiteral("running"), false}};
#ifdef EMULE_HAVE_BITTORRENT
    if (!s_session)
        return out;
    const bt::BtStatus s = s_session->status();
    out.insert(QStringLiteral("running"), s.running);
    out.insert(QStringLiteral("listenPort"), s.listenPort);
    out.insert(QStringLiteral("listening"), s.listening);
    out.insert(QStringLiteral("dhtNodes"), s.dhtNodes);
    out.insert(QStringLiteral("torrents"), s.torrents);
    out.insert(QStringLiteral("rateDown"), s.rateDown);
    out.insert(QStringLiteral("rateUp"), s.rateUp);
#endif
    return out;
}

QCborMap addTorrentData(const QByteArray& data, bool paused)
{
#ifdef EMULE_HAVE_BITTORRENT
    if (s_session && s_session->isRunning())
        return addResultToCbor(s_session->addTorrentFile(data, {}, paused));
#else
    Q_UNUSED(data)
    Q_UNUSED(paused)
#endif
    return QCborMap{{QStringLiteral("outcome"), 3},   // BtAddOutcome::NotRunning
                    {QStringLiteral("message"), QStringLiteral("The BitTorrent engine is not running.")}};
}

QCborMap inspectTorrent(const QByteArray& data, QString& error)
{
#ifdef EMULE_HAVE_BITTORRENT
    const auto preview = bt::BtSession::inspectTorrentFile(data);
    if (!preview) {
        error = preview.error();
        return {};
    }
    QCborArray files;
    for (const auto& f : preview->files)
        files.append(QCborMap{
            {QStringLiteral("index"), f.index},
            {QStringLiteral("path"), f.path},
            {QStringLiteral("size"), f.size},
            {QStringLiteral("pad"), f.pad},
        });
    QCborArray trackers;
    for (const auto& t : preview->trackers)
        trackers.append(QCborMap{{QStringLiteral("url"), t.url}, {QStringLiteral("tier"), t.tier}});
    QCborArray webSeeds;
    for (const auto& url : preview->webSeeds)
        webSeeds.append(url);
    return QCborMap{
        {QStringLiteral("id"), preview->id},
        {QStringLiteral("name"), preview->name},
        {QStringLiteral("infoHashV1"), preview->hashV1},
        {QStringLiteral("infoHashV2"), preview->hashV2},
        {QStringLiteral("totalSize"), preview->totalSize},
        {QStringLiteral("pieceLength"), preview->pieceLength},
        {QStringLiteral("pieceCount"), preview->pieces},
        {QStringLiteral("private"), preview->isPrivate},
        {QStringLiteral("comment"), preview->comment},
        {QStringLiteral("creator"), preview->creator},
        {QStringLiteral("creationDate"), preview->createdAt},
        {QStringLiteral("files"), files},
        {QStringLiteral("trackers"), trackers},
        {QStringLiteral("webSeeds"), webSeeds},
    };
#else
    Q_UNUSED(data)
    error = QStringLiteral("BitTorrent is not built into this daemon.");
    return {};
#endif
}

void applyIpFilter(bool onlyIfLevelChanged)
{
#ifdef EMULE_HAVE_BITTORRENT
    static std::optional<uint32> appliedLevel;
    if (!s_session || !theApp.ipFilter)
        return;
    const uint32 level = thePrefs.ipFilterLevel();
    if (onlyIfLevelChanged && appliedLevel == level)
        return;
    appliedLevel = level;
    // already merged into disjoint segments, strictest level per segment
    std::vector<bt::BtIpRange4> v4;
    for (const IPFilterEntry& e : theApp.ipFilter->entries())
        if (e.level < level)
            v4.push_back({e.start, e.end});
    std::vector<bt::BtIpRange6> v6;
    for (const IPFilterEntry6& e : theApp.ipFilter->entries6())
        if (e.level < level)
            v6.push_back({e.start, e.end});
    s_session->setIpFilter(v4, v6);
#else
    Q_UNUSED(onlyIfLevelChanged)
#endif
}

#ifdef EMULE_HAVE_BITTORRENT
bt::BtSettings settingsFromPrefs()
{
    bt::BtSettings s;
    s.listenPort = thePrefs.btListenPort();
    s.downloadDir = thePrefs.btDownloadDir().isEmpty() ? thePrefs.incomingDir() : thePrefs.btDownloadDir();
    s.stateDir = QDir(thePrefs.configDir()).filePath(QStringLiteral("BT"));
    s.maxDownloadKb = thePrefs.btMaxDownloadKb();
    s.maxUploadKb = thePrefs.btMaxUploadKb();
    s.dht = thePrefs.btDht();
    s.lsd = thePrefs.btLsd();
    s.pex = thePrefs.btPex();
    s.encryption = thePrefs.btEncryption();
    s.maxActiveDownloads = thePrefs.btMaxActiveDownloads();
    s.maxActiveSeeds = thePrefs.btMaxActiveSeeds();
    s.maxConnections = thePrefs.btMaxConnections();
    s.maxConnectionsPerTorrent = thePrefs.btMaxConnectionsPerTorrent();
    s.seedRatioPercent = thePrefs.btSeedRatioPercent();
    s.seedTimeLimitMinutes = thePrefs.btSeedTimeLimitMinutes();
    s.userAgent = QStringLiteral("eMuleQt/") + kAppVersion;
    s.versionMajor = EMULE_VERSION_MAJOR;
    s.versionMinor = EMULE_VERSION_MINOR;
    s.versionRevision = EMULE_VERSION_PATCH;
    return s;
}
#endif

} // namespace eMule::BtBridge
