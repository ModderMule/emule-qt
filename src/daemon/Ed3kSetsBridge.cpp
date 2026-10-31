/// @file Ed3kSetsBridge.cpp
/// @brief Followed ed3k sets and live streams over IPC — implementation.

#include "Ed3kSetsBridge.h"
#include "Ed3kOffGate.h"

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QStringList>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kLive.h"
#include "Ed3kLiveBroadcast.h"
#include "Ed3kNames.h"
#include "Ed3kSets.h"
#include "Ed3kSiteServer.h"

#include <QObject>

#include <cstring>
#endif

namespace eMule::Ed3kSetsBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

#ifdef EMULE_HAVE_ED3K
ed3k::Ed3kSets* s_sets = nullptr;
ed3k::Ed3kLive* s_live = nullptr;
ed3k::Ed3kSiteServer* s_sites = nullptr;
ed3k::Ed3kLiveBroadcast* s_broadcast = nullptr;
#endif

IpcMessage failure(int seqId, const QString& error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
namespace ffi = ::ed3k;

QString hexOf(std::span<const std::uint8_t> b)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size())).toHex());
}

QCborArray strings(const QStringList& l)
{
    QCborArray a;
    for (const QString& s : l)
        a.append(s);
    return a;
}

QCborMap toCbor(const ed3k::Ed3kSets::Info& i)
{
    QCborArray members;
    for (const auto& m : i.members)
        members.append(QCborMap{
            {QStringLiteral("path"), m.path},
            {QStringLiteral("root"), hexOf(m.root.bytes)},
            {QStringLiteral("size"), qint64(m.size)},
            {QStringLiteral("skipped"), m.skipped},
            {QStringLiteral("state"), int(m.state)},
            {QStringLiteral("done"), qint64(m.done)},
        });
    QCborArray parts;
    for (const auto& p : i.parts)
        parts.append(QCborMap{
            {QStringLiteral("author"), hexOf(p.author)},
            {QStringLiteral("seq"), qint64(p.seq)},
            {QStringLiteral("dir"), p.dir},
            {QStringLiteral("name"), p.name},
            {QStringLiteral("files"), p.files},
            {QStringLiteral("size"), qint64(p.size)},
        });
    return QCborMap{
        {QStringLiteral("id"), qint64(i.id)},
        {QStringLiteral("link"), i.link},
        {QStringLiteral("title"), i.title},
        {QStringLiteral("author"), i.authorKey},
        {QStringLiteral("hidden"), i.hidden},
        {QStringLiteral("pinned"), i.pinned},
        {QStringLiteral("followed"), i.followed},
        {QStringLiteral("policy"), int(i.policy)},
        {QStringLiteral("seq"), i.seq ? qint64(*i.seq) : qint64(-1)},
        {QStringLiteral("pendingSeq"), i.pendingSeq ? qint64(*i.pendingSeq) : qint64(-1)},
        {QStringLiteral("added"), strings(i.added)},
        {QStringLiteral("changed"), strings(i.changed)},
        {QStringLiteral("removed"), strings(i.removed)},
        {QStringLiteral("dir"), i.dir},
        {QStringLiteral("error"), i.error},
        {QStringLiteral("totalSize"), qint64(i.totalSize)},
        {QStringLiteral("members"), members},
        {QStringLiteral("parts"), parts},
        {QStringLiteral("pendingMounts"), strings(i.pendingMounts)},
        {QStringLiteral("paidScope"), i.paidScope},
    };
}

QCborMap toCbor(const ed3k::Ed3kLive::Info& i)
{
    return QCborMap{
        {QStringLiteral("id"), i.id},
        {QStringLiteral("title"), i.title},
        {QStringLiteral("media"), i.media},
        {QStringLiteral("codecs"), i.codecs},
        {QStringLiteral("state"), int(i.state)},
        {QStringLiteral("neighbours"), i.neighbours},
        {QStringLiteral("segments"), qint64(i.segments)},
        {QStringLiteral("bytes"), qint64(i.bytes)},
        {QStringLiteral("viewers"), i.viewers},
        {QStringLiteral("restarts"), qint64(i.restarts)},
        {QStringLiteral("url"), i.url},
    };
}

QCborMap toCbor(const ed3k::Ed3kLiveBroadcast::Info& i)
{
    // a broadcast of ours, in the same list as the watched streams
    return QCborMap{
        {QStringLiteral("id"), i.id},
        {QStringLiteral("title"), i.title},
        {QStringLiteral("state"), int(ed3k::Ed3kLive::State::Playing)},
        {QStringLiteral("neighbours"), i.neighbours},
        {QStringLiteral("segments"), qint64(i.segments)},
        {QStringLiteral("bytes"), qint64(i.bytes)},
        {QStringLiteral("broadcast"), true},
        {QStringLiteral("link"), i.link},
        {QStringLiteral("folder"), i.folder},
        {QStringLiteral("private"), i.isPrivate},
        {QStringLiteral("error"), i.error},
    };
}

ed3k::Ed3kSets::Policy policyOf(qint64 v)
{
    return v == 1 ? ed3k::Ed3kSets::Policy::Download : ed3k::Ed3kSets::Policy::Ask;
}

void answer(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    const int seq = msg.seqId();
    const auto ok = [&](const QCborValue& data = {}) { reply(IpcMessage::makeResult(seq, true, data)); };
    const auto unknownSet = [&] { reply(failure(seq, QCoreApplication::translate("eMule::Ed3kSetsBridge", "Unknown ed3k set."))); };
    const auto id = std::uint32_t(std::clamp<qint64>(msg.fieldInt(0), 0, 0xFFFFFFFF));
    auto& sets = *s_sets;
    switch (msg.type()) {
    case IpcMsgType::GetEd3kSets:
        ok(setsToCbor());
        break;
    case IpcMsgType::FollowEd3kSet:
        if (auto r = sets.follow(msg.fieldString(0), policyOf(msg.fieldInt(1))))
            ok(QCborMap{{QStringLiteral("id"), qint64(*r)}});
        else
            reply(failure(seq, r.error()));
        break;
    case IpcMsgType::UnfollowEd3kSet:
        sets.unfollow(id) ? ok() : unknownSet();
        break;
    case IpcMsgType::SetEd3kSetPolicy:
        sets.setPolicy(id, policyOf(msg.fieldInt(1))) ? ok() : unknownSet();
        break;
    case IpcMsgType::SetEd3kSetSelection: {
        QStringList paths;
        for (const auto& v : msg.fieldArray(1))
            paths.append(v.toString());
        sets.setSkipped(id, paths) ? ok() : unknownSet();
        break;
    }
    case IpcMsgType::ApplyEd3kSetVersion:
        sets.applyVersion(id) ? ok() : unknownSet();
        break;
    case IpcMsgType::DownloadEd3kSetParts: {
        std::optional<ffi::Key32> author;
        if (const QByteArray raw = QByteArray::fromHex(msg.fieldString(1).toLatin1()); raw.size() == 32) {
            ffi::Key32 k{};
            std::memcpy(k.data(), raw.constData(), k.size());
            author = k;
        }
        if (sets.downloadParts(id, author))
            ok();
        else
            reply(failure(seq, QCoreApplication::translate("eMule::Ed3kSetsBridge", "The parts of this set cannot be downloaded now.")));
        break;
    }
    case IpcMsgType::WatchEd3kLive: {
        if (!s_live) {
            reply(failure(seq, QCoreApplication::translate("eMule::Ed3kSetsBridge", "ed3k is not available.")));
            break;
        }
        if (auto r = s_live->watch(msg.fieldString(0)))
            ok(QCborMap{{QStringLiteral("id"), *r}, {QStringLiteral("url"), s_live->urlOf(*r)}});
        else
            reply(failure(seq, r.error()));
        break;
    }
    case IpcMsgType::LeaveEd3kLive:
        // a broadcast of ours ends (liveEnd), a watched stream is left
        if (s_broadcast && s_broadcast->stop(msg.fieldString(0)))
            ok();
        else if (s_live && s_live->leave(msg.fieldString(0)))
            ok();
        else
            reply(failure(seq, QCoreApplication::translate("eMule::Ed3kSetsBridge", "Unknown ed3k live stream.")));
        break;
    case IpcMsgType::GetEd3kLive:
        ok(liveToCbor());
        break;
    case IpcMsgType::BroadcastEd3kLive: {
        if (!s_broadcast) {
            reply(failure(seq, QCoreApplication::translate("eMule::Ed3kSetsBridge", "ed3k is not available.")));
            break;
        }
        ed3k::Ed3kLiveBroadcast::Options o;
        o.folder = msg.fieldString(0);
        o.title = msg.fieldString(1);
        o.isPrivate = msg.fieldBool(2);
        o.playlist = msg.fieldString(3);
        o.codecs = msg.fieldString(4);
        if (auto r = s_broadcast->start(o))
            ok(QCborMap{{QStringLiteral("id"), r->id}, {QStringLiteral("link"), r->link}});
        else
            reply(failure(seq, r.error()));
        break;
    }
    case IpcMsgType::GetEd3kSetSite: {
        const auto set = ed3k::Ed3kNames::parseSetLink(msg.fieldString(0));
        if (!set) {
            reply(failure(seq, QCoreApplication::translate("eMule::Ed3kSetsBridge", "This ed3k set link has no website.")));
            break;
        }
        ok(QCborMap{{QStringLiteral("url"), s_sites ? s_sites->originUrl(set->key, set->id, set->path) : QString()}});
        break;
    }
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown ed3k request")));
        break;
    }
}
#endif

} // namespace

void setBroadcast(ed3k::Ed3kLiveBroadcast* broadcast)
{
#ifdef EMULE_HAVE_ED3K
    s_broadcast = broadcast;
#else
    (void)broadcast;
#endif
}

void connectBroadcastPushes(ed3k::Ed3kLiveBroadcast& broadcast, std::function<void(const IpcMessage&)> send)
{
#ifdef EMULE_HAVE_ED3K
    QObject::connect(&broadcast, &ed3k::Ed3kLiveBroadcast::changed, &broadcast, [send] {
        IpcMessage m(IpcMsgType::PushEd3kLiveChanged, 0);
        m.append(liveToCbor());
        send(m);
    });
#else
    (void)broadcast;
    (void)send;
#endif
}

void setSets(ed3k::Ed3kSets* sets, ed3k::Ed3kLive* live, ed3k::Ed3kSiteServer* sites)
{
#ifdef EMULE_HAVE_ED3K
    s_sets = sets;
    s_live = live;
    s_sites = sites;
#else
    (void)sets;
    (void)live;
    (void)sites;
#endif
}

bool isSetsRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    return (t >= 1110 && t <= 1119) || t == 1144 || t == static_cast<int>(IpcMsgType::BroadcastEd3kLive);
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isSetsRequest(msg.type()))
        return false;
    // ed3k off: nothing under config/ed3k is written
    if (Ed3kBridge::refuseWhileOff(msg, {IpcMsgType::GetEd3kSets, IpcMsgType::GetEd3kLive, IpcMsgType::GetEd3kSetSite}, reply))
        return true;
#ifdef EMULE_HAVE_ED3K
    if (s_sets) {
        answer(msg, reply);
        return true;
    }
#endif
    if (msg.type() == IpcMsgType::GetEd3kSets)
        reply(IpcMessage::makeResult(msg.seqId(), true, setsToCbor()));
    else if (msg.type() == IpcMsgType::GetEd3kLive)
        reply(IpcMessage::makeResult(msg.seqId(), true, liveToCbor()));
    else
        reply(failure(msg.seqId(), QCoreApplication::translate("eMule::Ed3kSetsBridge", "ed3k is not available.")));
    return true;
}

QCborMap setsToCbor()
{
    QCborArray list;
    bool running = false;
#ifdef EMULE_HAVE_ED3K
    if (s_sets) {
        running = s_sets->isRunning();
        for (const auto& i : s_sets->list())
            list.append(toCbor(i));
    }
#endif
    return QCborMap{{QStringLiteral("running"), running}, {QStringLiteral("sets"), list}};
}

QCborMap liveToCbor()
{
    QCborArray list;
    bool running = false;
#ifdef EMULE_HAVE_ED3K
    if (s_live) {
        running = s_sets && s_sets->isRunning();
        for (const auto& i : s_live->list())
            list.append(toCbor(i));
    }
    if (s_broadcast)
        for (const auto& i : s_broadcast->list())
            list.append(toCbor(i));
#endif
    return QCborMap{{QStringLiteral("running"), running}, {QStringLiteral("streams"), list}};
}

void connectPushes(ed3k::Ed3kSets& sets, ed3k::Ed3kLive& live, ed3k::Ed3kSiteServer* sites,
                   std::function<void(const IpcMessage&)> broadcast)
{
#ifdef EMULE_HAVE_ED3K
    QObject::connect(&sets, &ed3k::Ed3kSets::changed, &sets, [broadcast] {
        IpcMessage m(IpcMsgType::PushEd3kSetsChanged, 0);
        m.append(setsToCbor());
        broadcast(m);
    });
    QObject::connect(&sets, &ed3k::Ed3kSets::newVersion, &sets,
                     [broadcast](std::uint32_t id, const QString& title, quint64 seq, const QStringList& added,
                                 const QStringList& changedPaths, const QStringList& removed) {
                         IpcMessage m(IpcMsgType::PushEd3kSetVersion, 0);
                         m.append(qint64(id));
                         m.append(title);
                         m.append(qint64(seq));
                         m.append(strings(added));
                         m.append(strings(changedPaths));
                         m.append(strings(removed));
                         broadcast(m);
                     });
    QObject::connect(&live, &ed3k::Ed3kLive::changed, &live, [broadcast] {
        IpcMessage m(IpcMsgType::PushEd3kLiveChanged, 0);
        m.append(liveToCbor());
        broadcast(m);
    });
    if (sites)
        QObject::connect(sites, &ed3k::Ed3kSiteServer::setOffered, sites, [broadcast](const QString& link) {
            IpcMessage m(IpcMsgType::PushEd3kSetOffer, 0);
            m.append(link);
            broadcast(m);
        });
#else
    (void)sets;
    (void)live;
    (void)sites;
    (void)broadcast;
#endif
}

} // namespace eMule::Ed3kSetsBridge
