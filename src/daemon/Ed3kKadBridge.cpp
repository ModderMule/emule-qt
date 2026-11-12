/// @file Ed3kKadBridge.cpp
/// @brief The ed3k Kad window over IPC — implementation.

#include "Ed3kKadBridge.h"

#include "utils/Log.h"

#include <QCborArray>
#include <QCborMap>
#include <QPointer>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kBootstrap.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "geo/IP2Country.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>
#endif

namespace eMule::Ed3kKadBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

/// error 0: no ed3k node (not built or not running); else an ed3k::Error.
IpcMessage failure(int seqId, int error, const QString& message = {})
{
    QCborMap m{{QStringLiteral("error"), error}};
    if (!message.isEmpty())
        m.insert(QStringLiteral("message"), message);
    return IpcMessage::makeResult(seqId, false, m);
}

#ifdef EMULE_HAVE_ED3K
namespace ffi = ::ed3k;

QPointer<ed3k::Ed3kSession> s_session;
ed3k::Ed3kBootstrap* s_bootstrap = nullptr;

template <std::size_t N>
QString hexOf(const std::uint8_t (&b)[N])
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(b), qsizetype(N)).toHex());
}

/// One row per node: a node in both tables is "dual", shown with the family dialled first
/// (IPv6, 250 ms ahead of IPv4, packmule dial.rs §8.3) unless IPv6 Kad is offline.
QCborArray contactsToCbor(const std::vector<ed3k_kad_contact>& all, bool v6First)
{
    std::map<QByteArray, std::vector<const ed3k_kad_contact*>> byId;
    std::vector<QByteArray> order;
    for (const auto& c : all) {
        QByteArray id(reinterpret_cast<const char*>(c.id), qsizetype(sizeof c.id));
        auto& rows = byId[id];
        if (rows.empty())
            order.push_back(id);
        rows.push_back(&c);
    }
    QCborArray out;
    for (const auto& id : order) {
        const auto& rows = byId[id];
        const bool dual = rows.size() > 1;
        const ed3k_kad_contact* c = rows.front();
        for (const auto* r : rows)
            if (r->family == (v6First ? 6u : 4u))
                c = r;
        const QHostAddress ip = ed3k::toQHostAddress(ffi::Address::from(c->addr));
        // all zero = own address of that family not adopted yet (no node is its own contact)
        const bool known = std::ranges::any_of(c->distance, [](std::uint8_t b) { return b != 0; });
        out.append(QCborMap{
            {QStringLiteral("id"), hexOf(c->id)},
            {QStringLiteral("ip"), ip.toString()},
            {QStringLiteral("port"), qint64(c->addr.port)},
            {QStringLiteral("family"), qint64(c->family)},
            {QStringLiteral("dual"), dual},
            {QStringLiteral("cc"), countryCodeOf(ip)},
            {QStringLiteral("distance"), known ? hexOf(c->distance) : QString()},
            {QStringLiteral("fails"), qint64(c->fails)},
            {QStringLiteral("dialledIn"), c->dialled_in != 0},
            {QStringLiteral("checkInS"), qint64(c->check_in_s)},
            {QStringLiteral("agent"), QString::fromUtf8(reinterpret_cast<const char*>(c->agent),
                                                        qsizetype(strnlen(reinterpret_cast<const char*>(c->agent), sizeof c->agent)))},
        });
    }
    return out;
}

QCborArray lookupsToCbor(const std::vector<ed3k_kad_lookup_info>& all)
{
    QCborArray out;
    for (const auto& l : all) {
        const auto* p = reinterpret_cast<const char*>(l.purpose);
        out.append(QCborMap{
            {QStringLiteral("id"), qint64(l.id)},
            {QStringLiteral("purpose"), QString::fromUtf8(p, qsizetype(strnlen(p, sizeof l.purpose)))},
            {QStringLiteral("target"), hexOf(l.target)},
            {QStringLiteral("kind"), qint64(l.kind)},
            {QStringLiteral("store"), l.store != 0},
            {QStringLiteral("answered"), qint64(l.answered)},
            {QStringLiteral("asked"), qint64(l.asked)},
            {QStringLiteral("candidates"), qint64(l.candidates)},
            {QStringLiteral("ended"), l.ended != 0},
            {QStringLiteral("endedAgoMs"), qint64(l.ended_ago_ms)},
        });
    }
    return out;
}

QCborArray graphToCbor(const std::vector<ed3k_kad_lookup_node>& all)
{
    QCborArray out;
    for (const auto& n : all) {
        QCborArray from;
        for (std::uint8_t i = 0; i < n.from_len && i < 4; ++i)
            from.append(qint64(n.from[i]));
        out.append(QCborMap{
            {QStringLiteral("id"), hexOf(n.id)},
            {QStringLiteral("distance"), hexOf(n.distance)},
            {QStringLiteral("from"), from},
            {QStringLiteral("asked"), n.asked != 0},
            {QStringLiteral("askedAgoMs"), qint64(n.asked_ago_ms)},
            {QStringLiteral("answered"), n.answered != 0},
            {QStringLiteral("failed"), n.failed != 0},
            {QStringLiteral("closer"), n.closer != 0},
        });
    }
    return out;
}
#endif

} // namespace

void setSession(ed3k::Ed3kSession* session)
{
#ifdef EMULE_HAVE_ED3K
    s_session = session;
#else
    (void)session;
#endif
}

void setBootstrap(ed3k::Ed3kBootstrap* bootstrap)
{
#ifdef EMULE_HAVE_ED3K
    s_bootstrap = bootstrap;
#else
    (void)bootstrap;
#endif
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    const IpcMsgType t = msg.type();
    if (t != IpcMsgType::GetEd3kKadContacts && t != IpcMsgType::GetEd3kKadLookups && t != IpcMsgType::GetEd3kKadLookupGraph
        && t != IpcMsgType::ExportEd3kNodeList && t != IpcMsgType::FetchEd3kNodeLists)
        return false;
    const int seq = msg.seqId();
#ifdef EMULE_HAVE_ED3K
    auto* e = s_session ? s_session->engine() : nullptr;
    if (!e) {
        reply(failure(seq, 0));
        return true;
    }
    const auto now = s_session->now();
    switch (t) {
    case IpcMsgType::GetEd3kKadContacts:
        if (auto all = e->kadContacts(now)) {
            const auto stats = e->stats(now);
            const bool v6First = !stats || (stats->kad_offline & 2u) == 0;
            reply(IpcMessage::makeResult(seq, true, contactsToCbor(*all, v6First)));
        }
        else
            reply(failure(seq, int(all.error())));
        break;
    case IpcMsgType::GetEd3kKadLookups:
        if (auto all = e->kadLookups(now))
            reply(IpcMessage::makeResult(seq, true, lookupsToCbor(*all)));
        else
            reply(failure(seq, int(all.error())));
        break;
    case IpcMsgType::GetEd3kKadLookupGraph:
        if (auto all = e->kadLookupGraph(now, std::uint64_t(msg.fieldInt(0))))
            reply(IpcMessage::makeResult(seq, true, graphToCbor(*all)));
        else
            reply(failure(seq, int(all.error())));
        break;
    case IpcMsgType::ExportEd3kNodeList: {
        if (!s_bootstrap) {
            reply(failure(seq, 0));
            break;
        }
        auto file = s_bootstrap->exportList(msg.fieldString(0));
        if (!file) {
            reply(failure(seq, int(ffi::Error::NotFound), file.error()));
            break;
        }
        logInfo(QStringLiteral("ed3k: node list exported (%1 bytes)").arg(file->size()));
        reply(IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("file"), *file}}));
        break;
    }
    case IpcMsgType::FetchEd3kNodeLists:
        if (s_bootstrap && s_bootstrap->fetchNow())
            reply(IpcMessage::makeResult(seq, true));
        else
            reply(failure(seq, int(ffi::Error::NotFound), QStringLiteral("no node list source, or a fetch runs")));
        break;
    default:
        break;
    }
#else
    reply(failure(seq, 0));
#endif
    return true;
}

} // namespace eMule::Ed3kKadBridge
