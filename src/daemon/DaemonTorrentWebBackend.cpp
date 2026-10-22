/// @file DaemonTorrentWebBackend.cpp
/// @brief The REST API's view of the BitTorrent engine — implementation.

#include "DaemonTorrentWebBackend.h"

#ifdef EMULE_HAVE_BITTORRENT

#include "BtBridge.h"
#include "BtSession.h"
#include "IpcMessage.h"

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

bool DaemonTorrentWebBackend::running() const
{
    return BtBridge::session() && BtBridge::session()->isRunning();
}

QCborMap DaemonTorrentWebBackend::status() const
{
    return BtBridge::statusToCbor();
}

QCborArray DaemonTorrentWebBackend::torrents() const
{
    return ask(IpcMsgType::GetTorrents, {}).toArray();
}

QCborMap DaemonTorrentWebBackend::torrent(const QString& id) const
{
    return BtBridge::rowToCbor(id);
}

QCborMap DaemonTorrentWebBackend::details(const QString& id) const
{
    return ask(IpcMsgType::GetTorrentDetails, {id}).toMap();
}

TorrentWebAddResult DaemonTorrentWebBackend::addTorrentFile(const QByteArray& data,
                                                            const QString& savePath, bool paused)
{
    return addResult(ask(IpcMsgType::AddTorrentFile, {data, savePath, paused}));
}

TorrentWebAddResult DaemonTorrentWebBackend::addMagnet(const QString& uri, const QString& savePath,
                                                       bool paused)
{
    return addResult(ask(IpcMsgType::AddMagnet, {uri, savePath, paused}));
}

bool DaemonTorrentWebBackend::remove(const QString& id, bool deleteFiles)
{
    return askBool(IpcMsgType::RemoveTorrent, {id, deleteFiles});
}

bool DaemonTorrentWebBackend::pause(const QString& id)
{
    return askBool(IpcMsgType::PauseTorrent, {id});
}

bool DaemonTorrentWebBackend::resume(const QString& id)
{
    return askBool(IpcMsgType::ResumeTorrent, {id});
}

bool DaemonTorrentWebBackend::recheck(const QString& id)
{
    return askBool(IpcMsgType::RecheckTorrent, {id});
}

bool DaemonTorrentWebBackend::reannounce(const QString& id)
{
    return askBool(IpcMsgType::ReannounceTorrent, {id});
}

bool DaemonTorrentWebBackend::setFilePriorities(const QString& id, const QList<int>& priorities)
{
    QCborArray list;
    for (int p : priorities)
        list.append(p);
    return askBool(IpcMsgType::SetTorrentFilePriorities, {id, list});
}

bool DaemonTorrentWebBackend::setOptions(const QString& id, const QCborMap& options)
{
    return askBool(IpcMsgType::SetTorrentOptions, {id, options});
}

bool DaemonTorrentWebBackend::addTrackers(const QString& id, const QStringList& urls)
{
    return askBool(IpcMsgType::AddTorrentTrackers, {id, QCborArray::fromStringList(urls)});
}

bool DaemonTorrentWebBackend::removeTrackers(const QString& id, const QStringList& urls)
{
    return askBool(IpcMsgType::RemoveTorrentTrackers, {id, QCborArray::fromStringList(urls)});
}

QCborValue DaemonTorrentWebBackend::ask(IpcMsgType type, const QCborArray& fields)
{
    IpcMessage request(type, 1);
    for (const QCborValue& f : fields)
        request.append(f);
    IpcMessage answer;
    BtBridge::handleRequest(request, [&answer](const IpcMessage& m) { answer = m; });
    if (answer.type() != IpcMsgType::Result || !answer.fieldBool(0))
        return QCborValue(QCborValue::Undefined);
    return answer.fieldCount() > 1 ? answer.field(1) : QCborValue(true);
}

bool DaemonTorrentWebBackend::askBool(IpcMsgType type, const QCborArray& fields)
{
    return ask(type, fields).toBool(false);
}

TorrentWebAddResult DaemonTorrentWebBackend::addResult(const QCborValue& reply)
{
    TorrentWebAddResult out;
    if (!reply.isMap()) {
        out.outcome = TorrentWebAddOutcome::NotRunning;
        out.message = QStringLiteral("BitTorrent engine unavailable");
        return out;
    }
    const QCborMap m = reply.toMap();
    out.outcome = static_cast<TorrentWebAddOutcome>(m.value(QStringLiteral("outcome")).toInteger(4));
    out.id = m.value(QStringLiteral("id")).toString();
    out.message = m.value(QStringLiteral("message")).toString();
    return out;
}

} // namespace eMule

#endif // EMULE_HAVE_BITTORRENT
