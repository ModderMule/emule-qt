/// @file Ed3kNamesBridge.cpp
/// @brief ed3k readable names and websites over IPC — implementation.

#include "Ed3kNamesBridge.h"

#include <QCborArray>
#include <QCborMap>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kNames.h"
#include "Ed3kSiteServer.h"

#include <QObject>
#endif

namespace eMule::Ed3kNamesBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

ed3k::Ed3kNames* s_names = nullptr;
ed3k::Ed3kSiteServer* s_sites = nullptr;

/// error 0: no ed3k node (not built or not running); else an ed3k::Error.
IpcMessage failure(int seqId, int error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
namespace ffi = ::ed3k;
using ed3k::Ed3kNames;

QString hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(Ed3kNames::toBytes(bytes).toHex());
}

QCborArray keysToCbor(const std::vector<ffi::Key32>& keys)
{
    QCborArray out;
    for (const auto& k : keys)
        out.append(hexOf(k));
    return out;
}

QCborMap resolvedToCbor(const Ed3kNames::Resolved& r, bool viaSite)
{
    QCborArray warnings;
    for (const auto& w : r.warnings)
        warnings.append(QCborMap{{QStringLiteral("zone"), hexOf(w.zone)},
                                 {QStringLiteral("how"), w.how.how},
                                 {QStringLiteral("howA"), qint64(w.how.a)},
                                 {QStringLiteral("howB"), qint64(w.how.b)}});
    return QCborMap{
        {QStringLiteral("id"), qint64(r.id)},
        {QStringLiteral("name"), r.name},
        {QStringLiteral("zone"), hexOf(r.zone)},
        {QStringLiteral("label"), r.label},
        {QStringLiteral("links"), QCborArray::fromStringList(r.links)},
        {QStringLiteral("how"), r.how.how},
        {QStringLiteral("howA"), qint64(r.how.a)},
        {QStringLiteral("howB"), qint64(r.how.b)},
        {QStringLiteral("howTitle"), r.how.title},
        {QStringLiteral("warnings"), warnings},
        {QStringLiteral("ties"), keysToCbor(r.ties)},
        {QStringLiteral("confirm"), r.confirm},
        {QStringLiteral("webDisagree"), r.webDisagree},
        {QStringLiteral("viaSite"), viaSite},
    };
}

QCborMap namesToCbor(Ed3kNames& n)
{
    QCborArray pins;
    for (const auto& p : n.pins())
        pins.append(QCborMap{
            {QStringLiteral("name"), p.name},
            {QStringLiteral("zone"), hexOf(p.zone)},
            {QStringLiteral("since"), qint64(p.since)},
            {QStringLiteral("how"), p.how.how},
            {QStringLiteral("howA"), qint64(p.how.a)},
            {QStringLiteral("howB"), qint64(p.how.b)},
            {QStringLiteral("vouched"), p.vouched},
            {QStringLiteral("link"), p.link},
        });
    QCborArray pets;
    for (const auto& p : n.petnames())
        pets.append(QCborMap{{QStringLiteral("text"), p.text},
                             {QStringLiteral("zone"), hexOf(p.zone)},
                             {QStringLiteral("shareable"), p.shareable}});
    QCborArray lists;
    for (const auto& l : n.lists())
        lists.append(QCborMap{
            {QStringLiteral("id"), qint64(l.id)},
            {QStringLiteral("link"), l.link},
            {QStringLiteral("label"), l.label},
            {QStringLiteral("entries"), qint64(l.entries)},
            {QStringLiteral("refused"), l.refused},
        });
    return QCborMap{
        {QStringLiteral("running"), n.isRunning()},
        {QStringLiteral("sitePort"), s_sites ? qint64(s_sites->port()) : 0},
        {QStringLiteral("pins"), pins},
        {QStringLiteral("petnames"), pets},
        {QStringLiteral("lists"), lists},
    };
}

/// OpenEd3kName's answer: the links, and the website of the first set link.
QCborMap openedToCbor(Ed3kNames& n, const Ed3kNames::Resolved& r)
{
    QString url;
    if (const auto set = Ed3kNames::firstSet(r.links); set && s_sites) {
        (void)n.followSite(set->key, set->id);   // the browser asks at once
        url = s_sites->originUrl(set->key, set->id, set->path);
    }
    return QCborMap{{QStringLiteral("links"), QCborArray::fromStringList(r.links)}, {QStringLiteral("url"), url}};
}

void answer(const IpcMessage& msg, Ed3kNames& n, const std::function<void(const IpcMessage&)>& reply)
{
    const int seq = msg.seqId();
    const auto ok = [&](const QCborValue& data = {}) { reply(IpcMessage::makeResult(seq, true, data)); };
    const auto done = [&](const ffi::Result<>& r) { r ? ok() : reply(failure(seq, int(r.error()))); };
    const auto fail = [&](ffi::Error e) { reply(failure(seq, int(e))); };

    if (msg.type() == IpcMsgType::GetEd3kNames) {
        ok(namesToCbor(n));
        return;
    }
    if (!n.isRunning()) {
        reply(failure(seq, 0));
        return;
    }
    switch (msg.type()) {
    case IpcMsgType::ResolveEd3kName:
        if (auto id = n.resolve(msg.fieldString(0)))
            ok(QCborMap{{QStringLiteral("id"), qint64(*id)}});
        else
            fail(id.error());
        break;
    case IpcMsgType::OpenEd3kName:
        if (auto r = n.open(std::uint64_t(msg.fieldInt(0))))
            ok(openedToCbor(n, *r));
        else
            fail(r.error());
        break;
    case IpcMsgType::SetEd3kPetname: {
        const auto zone = Ed3kNames::keyFromText(msg.fieldString(1));
        if (!zone) {
            fail(ffi::Error::Argument);
            break;
        }
        done(n.setPetname(msg.fieldString(0), *zone, msg.fieldBool(2)));
        break;
    }
    case IpcMsgType::RemoveEd3kPetname:
        done(n.removePetname(msg.fieldString(0)));
        break;
    case IpcMsgType::UnpinEd3kName:
        done(n.unpin(msg.fieldString(0)));
        break;
    case IpcMsgType::VouchEd3kName:
        done(n.vouch(msg.fieldString(0), msg.fieldBool(1)));
        break;
    case IpcMsgType::FollowEd3kNameList:
        if (auto id = n.followList(msg.fieldString(0), msg.fieldString(1)))
            ok(QCborMap{{QStringLiteral("id"), qint64(*id)}});
        else
            fail(id.error());
        break;
    case IpcMsgType::UnfollowEd3kNameList:
        done(n.unfollowList(std::uint32_t(msg.fieldInt(0))));
        break;
    case IpcMsgType::MoveEd3kNameList:
        done(n.moveList(std::uint32_t(msg.fieldInt(0)), std::uint32_t(std::max<qint64>(0, msg.fieldInt(1)))));
        break;
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown ed3k request")));
        break;
    }
}
#endif

} // namespace

void setNames(ed3k::Ed3kNames* names, ed3k::Ed3kSiteServer* sites)
{
    s_names = names;
    s_sites = sites;
}

bool isNamesRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    return t >= 1010 && t <= 1019;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isNamesRequest(msg.type()))
        return false;
#ifdef EMULE_HAVE_ED3K
    if (s_names) {
        answer(msg, *s_names, reply);
        return true;
    }
#endif
    if (msg.type() == IpcMsgType::GetEd3kNames)
        reply(IpcMessage::makeResult(msg.seqId(), true,
                                     QCborMap{{QStringLiteral("running"), false},
                                              {QStringLiteral("sitePort"), 0},
                                              {QStringLiteral("pins"), QCborArray{}},
                                              {QStringLiteral("petnames"), QCborArray{}},
                                              {QStringLiteral("lists"), QCborArray{}}}));
    else
        reply(failure(msg.seqId(), 0));
    return true;
}

void connectPushes(ed3k::Ed3kNames& names, ed3k::Ed3kSiteServer* sites, std::function<void(const IpcMessage&)> broadcast)
{
#ifdef EMULE_HAVE_ED3K
    using N = ed3k::Ed3kNames;
    QObject::connect(&names, &N::resolved, &names, [broadcast](const N::Resolved& r) {
        IpcMessage m(IpcMsgType::PushEd3kNameResolved, 0);
        m.append(resolvedToCbor(r, false));
        broadcast(m);
    });
    QObject::connect(&names, &N::failed, &names, [broadcast](quint64 id, const QString& name, int why, const QList<QByteArray>& ties) {
        QCborArray t;
        for (const QByteArray& z : ties)
            t.append(QString::fromLatin1(z.toHex()));
        IpcMessage m(IpcMsgType::PushEd3kNameFailed, 0);
        m.append(QCborMap{{QStringLiteral("id"), qint64(id)},
                          {QStringLiteral("name"), name},
                          {QStringLiteral("why"), why},
                          {QStringLiteral("ties"), t}});
        broadcast(m);
    });
    QObject::connect(&names, &N::namesChanged, &names, [broadcast] {
        broadcast(IpcMessage(IpcMsgType::PushEd3kNamesChanged, 0));
    });
    QObject::connect(&names, &N::friendListOffered, &names, [broadcast](const QByteArray& node, const QString& link) {
        IpcMessage m(IpcMsgType::PushEd3kNameFriendList, 0);
        m.append(QString::fromLatin1(node.toHex()));
        m.append(link);
        broadcast(m);
    });
    if (sites)
        QObject::connect(sites, &ed3k::Ed3kSiteServer::confirmNeeded, sites, [broadcast](const N::Resolved& r) {
            IpcMessage m(IpcMsgType::PushEd3kNameResolved, 0);
            m.append(resolvedToCbor(r, true));
            broadcast(m);
        });
#else
    (void)names;
    (void)sites;
    (void)broadcast;
#endif
}

} // namespace eMule::Ed3kNamesBridge
