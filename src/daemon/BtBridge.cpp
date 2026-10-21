#include "BtBridge.h"

#include "app/AppConfig.h"
#include "prefs/Preferences.h"

#include <QCborArray>
#include <QDir>

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
    };
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
        });
    return QCborMap{
        {QStringLiteral("files"), files},
        {QStringLiteral("peers"), peers},
        {QStringLiteral("trackers"), trackers},
    };
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
