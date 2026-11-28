/// @file BtCreateBridge.cpp
/// @brief Torrent creation jobs (#191) — see BtCreateBridge.h.

#include "BtCreateBridge.h"
#include "utils/ArgStr.h"

#include "BtBridge.h"
#include "app/AppConfig.h"
#include "utils/Log.h"

#include <QCborArray>
#include <QCborMap>
#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QThreadPool>

#ifdef EMULE_HAVE_BITTORRENT
#include "BtSession.h"
#include "BtTypes.h"
#include "create/BtTorrentCreator.h"
#endif

namespace eMule::BtCreateBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

std::function<void(const IpcMessage&)> s_push;
std::function<void(const QString&)> s_seeded;

#ifdef EMULE_HAVE_BITTORRENT

/// a made torrent waits this long for GetCreatedTorrent
constexpr qint64 kKeepResultMs = 60 * 60 * 1000;

struct Job {
    QPointer<bt::BtTorrentCreator> creator;
    bool seed = false;
};
struct Done {
    QByteArray data;
    QString name;
    qint64 at = 0;
};

QHash<int, Job> s_jobs;
QHash<int, Done> s_done;
int s_nextJob = 1;

QThreadPool& pool()
{
    // hashing is disk-bound: one torrent at a time, the rest queue
    static QThreadPool* p = [] {
        auto* tp = new QThreadPool;
        tp->setMaxThreadCount(1);
        tp->setObjectName(QStringLiteral("BtTorrentCreator"));
        return tp;
    }();
    return *p;
}

void push(IpcMsgType type, const QCborMap& m)
{
    if (!s_push)
        return;
    IpcMessage msg(type, 0);
    msg.append(m);
    s_push(msg);
}

QStringList stringsOf(const QCborValue& v)
{
    QStringList out;
    for (const auto& e : v.toArray())
        out << e.toString();
    return out;
}

bt::BtCreateParams paramsOf(const QCborMap& m)
{
    bt::BtCreateParams p;
    p.path = m.value(QStringLiteral("path")).toString();
    p.onlyFiles = stringsOf(m.value(QStringLiteral("onlyFiles")));
    const int fmt = int(m.value(QStringLiteral("format")).toInteger(int(bt::BtTorrentFormat::Hybrid)));
    p.format = fmt == 0 ? bt::BtTorrentFormat::V1 : fmt == 1 ? bt::BtTorrentFormat::V2 : bt::BtTorrentFormat::Hybrid;
    p.pieceSize = int(m.value(QStringLiteral("pieceSize")).toInteger());
    p.isPrivate = m.value(QStringLiteral("private")).toBool();
    p.ignoreDotfiles = m.value(QStringLiteral("ignoreDotfiles")).toBool(true);
    p.trackers = stringsOf(m.value(QStringLiteral("trackers")));
    p.webSeeds = stringsOf(m.value(QStringLiteral("webSeeds")));
    p.comment = m.value(QStringLiteral("comment")).toString();
    p.source = m.value(QStringLiteral("source")).toString();
    p.creator = kUserAgent;
    return p;
}

void dropOldResults()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    s_done.removeIf([now](const auto& it) { return now - it.value().at > kKeepResultMs; });
}

/// a job ended: keep the bytes, seed when asked, tell the clients
void finish(int jobId, const bt::BtCreateResult& r)
{
    const Job job = s_jobs.take(jobId);
    if (job.creator)
        job.creator->deleteLater();
    QCborMap out{{QStringLiteral("jobId"), jobId},
                 {QStringLiteral("ok"), r.ok},
                 {QStringLiteral("cancelled"), r.cancelled},
                 {QStringLiteral("error"), r.error},
                 {QStringLiteral("name"), r.name},
                 {QStringLiteral("pieces"), r.pieces},
                 {QStringLiteral("pieceSize"), r.pieceSize}};
    if (r.ok) {
        dropOldResults();
        s_done.insert(jobId, Done{r.data, r.name, QDateTime::currentMSecsSinceEpoch()});
        logInfo(QStringLiteral("BitTorrent: created torrent \"%1\" (%2 pieces of %3 KiB)")
                    .arg(argStr(r.name), argStr(r.pieces), argStr(r.pieceSize / 1024)));
        if (job.seed) {
            bt::BtSession* ses = BtBridge::session();
            if (!ses || !ses->isRunning()) {
                out.insert(QStringLiteral("seedError"), QStringLiteral("BitTorrent is off"));
            } else {
                const auto added = ses->addCreatedTorrent(r.data, r.savePath);
                if (added.outcome == bt::BtAddOutcome::Added || added.outcome == bt::BtAddOutcome::Duplicate) {
                    out.insert(QStringLiteral("seedId"), added.id);
                    if (added.outcome == bt::BtAddOutcome::Added && s_seeded)
                        s_seeded(added.id);
                } else {
                    out.insert(QStringLiteral("seedError"), added.message);
                }
            }
        }
    } else if (!r.cancelled) {
        logWarning(QStringLiteral("BitTorrent: creating torrent \"%1\" failed: %2").arg(r.name, r.error));
    }
    push(IpcMsgType::PushTorrentCreated, out);
}

void create(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    const QCborMap m = msg.fieldMap(0);
    bt::BtCreateParams p = paramsOf(m);
    if (p.path.isEmpty()) {
        reply(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("No path")));
        return;
    }
    const int jobId = s_nextJob++;
    auto* creator = new bt::BtTorrentCreator(std::move(p));
    QObject::connect(creator, &bt::BtTorrentCreator::progress, creator, [jobId](int pct) {
        push(IpcMsgType::PushTorrentCreation, QCborMap{{QStringLiteral("jobId"), jobId}, {QStringLiteral("percent"), pct}});
    });
    QObject::connect(creator, &bt::BtTorrentCreator::finished, creator,
                     [jobId](const bt::BtCreateResult& r) { finish(jobId, r); });
    s_jobs.insert(jobId, Job{creator, m.value(QStringLiteral("seed")).toBool()});
    pool().start(creator);
    reply(IpcMessage::makeResult(msg.seqId(), true, jobId));
}

#endif

} // namespace

void setPushSink(std::function<void(const IpcMessage&)> sink)
{
    s_push = std::move(sink);
}

void setSeededHook(std::function<void(const QString& id)> hook)
{
    s_seeded = std::move(hook);
}

bool isCreateRequest(IpcMsgType type)
{
    return type == IpcMsgType::CreateTorrent || type == IpcMsgType::CancelTorrentCreation
           || type == IpcMsgType::GetCreatedTorrent;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isCreateRequest(msg.type()))
        return false;
#ifndef EMULE_HAVE_BITTORRENT
    reply(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("BitTorrent engine unavailable")));
#else
    switch (msg.type()) {
    case IpcMsgType::CreateTorrent:
        create(msg, reply);
        break;
    case IpcMsgType::CancelTorrentCreation: {
        const auto it = s_jobs.constFind(int(msg.fieldInt(0)));
        const bool found = it != s_jobs.cend() && it->creator;
        if (found)
            it->creator->cancel();
        reply(IpcMessage::makeResult(msg.seqId(), found));
        break;
    }
    case IpcMsgType::GetCreatedTorrent: {
        const auto it = s_done.constFind(int(msg.fieldInt(0)));
        if (it == s_done.cend()) {
            reply(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("No such torrent")));
            break;
        }
        reply(IpcMessage::makeResult(msg.seqId(), true,
                                     QCborMap{{QStringLiteral("data"), it->data}, {QStringLiteral("name"), it->name}}));
        break;
    }
    default:
        return false;
    }
#endif
    return true;
}

void shutdown()
{
#ifdef EMULE_HAVE_BITTORRENT
    for (const Job& j : std::as_const(s_jobs))
        if (j.creator)
            j.creator->cancel();
    pool().waitForDone();
    for (const Job& j : std::as_const(s_jobs))
        delete j.creator.data();
    s_jobs.clear();
    s_done.clear();
#endif
    s_push = nullptr;
    s_seeded = nullptr;
}

} // namespace eMule::BtCreateBridge
