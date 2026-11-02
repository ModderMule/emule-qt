/// @file Ed3kOwnSetsBridge.cpp
/// @brief My ed3k Sets over IPC — implementation.

#include "Ed3kOwnSetsBridge.h"
#include "Ed3kOffGate.h"

#include <QCborArray>
#include <QCoreApplication>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kOwnSets.h"

#include <QObject>
#endif

namespace eMule::Ed3kOwnSetsBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

#ifdef EMULE_HAVE_ED3K
ed3k::Ed3kOwnSets* s_sets = nullptr;
#endif

IpcMessage failure(int seqId, const QString& error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
QCborMap toCbor(const ed3k::Ed3kOwnSets::Info& i)
{
    return QCborMap{
        {QStringLiteral("id"), i.id},
        {QStringLiteral("title"), i.title},
        {QStringLiteral("folder"), i.folder},
        {QStringLiteral("files"), i.files},
        {QStringLiteral("size"), qint64(i.size)},
        {QStringLiteral("seq"), qint64(i.seq)},
        {QStringLiteral("kind"), int(i.kind)},
        {QStringLiteral("published"), i.published},
        {QStringLiteral("building"), i.building},
        {QStringLiteral("scope"), i.scope},
        {QStringLiteral("sold"), i.sold},
        {QStringLiteral("stopped"), i.stopped},
        // a hidden set's link is a credential: GetEd3kOwnSetLink only (i.link is empty)
        {QStringLiteral("link"), i.link},
        {QStringLiteral("error"), i.error},
    };
}

void answer(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    const int seq = msg.seqId();
    const auto ok = [&](const QCborValue& data = {}) { reply(IpcMessage::makeResult(seq, true, data)); };
    auto& sets = *s_sets;
    switch (msg.type()) {
    case IpcMsgType::GetEd3kOwnSets:
        ok(ownSetsToCbor());
        break;
    case IpcMsgType::PublishEd3kOwnSet: {
        QStringList files;
        for (const auto& v : msg.fieldArray(1))
            files << v.toString();
        const qint64 kind = msg.fieldInt(3);
        if (kind < 1 || kind > 3) {
            reply(failure(seq, QCoreApplication::translate("eMule::Ed3kOwnSetsBridge", "Unknown kind of set.")));
            break;
        }
        auto r = sets.create(msg.fieldString(2), msg.fieldString(0), files, ed3k::Ed3kOwnSets::Kind(kind));
        if (!r) {
            reply(failure(seq, r.error()));
            break;
        }
        // a hidden set's link: GetEd3kOwnSetLink
        const auto i = sets.info(*r);
        ok(QCborMap{{QStringLiteral("id"), *r}, {QStringLiteral("link"), i ? i->link : QString()}});
        break;
    }
    case IpcMsgType::Ed3kOwnSetControl: {
        const QString id = msg.fieldString(1);
        std::expected<void, QString> r = std::unexpected(QCoreApplication::translate("eMule::Ed3kOwnSetsBridge", "Unknown action."));
        switch (msg.fieldInt(0)) {
        case 1:
            r = sets.publishNewVersion(id);
            break;
        case 2:
            r = sets.unpublish(id);
            break;
        case 3:
            r = sets.retitle(id, msg.fieldString(2));
            break;
        default:
            break;
        }
        if (r)
            ok();
        else
            reply(failure(seq, r.error()));
        break;
    }
    case IpcMsgType::GetEd3kOwnSetLink: {
        // a hidden set's link is a credential: in this answer only, never logged
        const QString id = msg.fieldString(0);
        const auto i = sets.info(id);
        if (!i) {
            reply(failure(seq, QCoreApplication::translate("eMule::Ed3kOwnSetsBridge", "This set is not yours.")));
            break;
        }
        ok(QCborMap{{QStringLiteral("link"), sets.linkOf(id)}, {QStringLiteral("hidden"), i->kind == ed3k::Ed3kOwnSets::Kind::Hidden}});
        break;
    }
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown ed3k request")));
        break;
    }
}
#endif

} // namespace

void setOwnSets(ed3k::Ed3kOwnSets* sets)
{
#ifdef EMULE_HAVE_ED3K
    s_sets = sets;
#else
    (void)sets;
#endif
}

bool isOwnSetsRequest(IpcMsgType type)
{
    return type == IpcMsgType::GetEd3kOwnSets || type == IpcMsgType::PublishEd3kOwnSet || type == IpcMsgType::Ed3kOwnSetControl
           || type == IpcMsgType::GetEd3kOwnSetLink;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isOwnSetsRequest(msg.type()))
        return false;
    // ed3k off: nothing under config/ed3k is written
    if (Ed3kBridge::refuseWhileOff(msg, {IpcMsgType::GetEd3kOwnSets}, reply))
        return true;
#ifdef EMULE_HAVE_ED3K
    if (s_sets) {
        answer(msg, reply);
        return true;
    }
#endif
    if (msg.type() == IpcMsgType::GetEd3kOwnSets)
        reply(IpcMessage::makeResult(msg.seqId(), true, ownSetsToCbor()));
    else
        reply(failure(msg.seqId(), QCoreApplication::translate("eMule::Ed3kOwnSetsBridge", "ed3k is not available.")));
    return true;
}

QCborMap ownSetsToCbor()
{
    QCborArray list;
    bool running = false;
#ifdef EMULE_HAVE_ED3K
    if (s_sets) {
        running = s_sets->isStarted();
        for (const auto& i : s_sets->list())
            list.append(toCbor(i));
    }
#endif
    return QCborMap{{QStringLiteral("running"), running}, {QStringLiteral("sets"), list}};
}

void connectPushes(ed3k::Ed3kOwnSets& sets, std::function<void(const IpcMessage&)> broadcast)
{
#ifdef EMULE_HAVE_ED3K
    QObject::connect(&sets, &ed3k::Ed3kOwnSets::changed, &sets, [broadcast] {
        IpcMessage m(IpcMsgType::PushEd3kOwnSetsChanged, 0);
        m.append(ownSetsToCbor());
        broadcast(m);
    });
#else
    (void)sets;
    (void)broadcast;
#endif
}

} // namespace eMule::Ed3kOwnSetsBridge
