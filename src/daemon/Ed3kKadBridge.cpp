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
#include "Ed3kStore.h"
#include "geo/IP2Country.h"
#include "prefs/Preferences.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

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

/// "ip:port", "[v6]:port"
QString addrText(const QHostAddress& ip, std::uint16_t port)
{
    return ip.protocol() == QAbstractSocket::IPv6Protocol ? QStringLiteral("[%1]:%2").arg(ip.toString()).arg(port)
                                                          : QStringLiteral("%1:%2").arg(ip.toString()).arg(port);
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
        const auto* n = reinterpret_cast<const char*>(l.name);
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
            {QStringLiteral("name"), QString::fromUtf8(n, qsizetype(strnlen(n, sizeof l.name)))},
            {QStringLiteral("sent"), qint64(l.sent)},
            // -1: no responsible node told a load
            {QStringLiteral("load"), l.has_load != 0 ? qint64(l.load) : qint64(-1)},
        });
    }
    return out;
}

/// The shape of ops::kadIncoming; type = kind + 100 × op (0 find, 1 search, 2 store).
QCborArray incomingToCbor(const std::vector<ed3k_kad_incoming_info>& all)
{
    QCborArray out;
    for (const auto& r : all) {
        QString addr;
        QString cc;
        if (r.has_addr != 0) {
            const QHostAddress ip = ed3k::toQHostAddress(ffi::Address::from(r.addr));
            addr = ip.protocol() == QAbstractSocket::IPv6Protocol ? QStringLiteral("[%1]:%2").arg(ip.toString()).arg(r.addr.port)
                                                                   : QStringLiteral("%1:%2").arg(ip.toString()).arg(r.addr.port);
            cc = countryCodeOf(ip);
        }
        const auto* n = reinterpret_cast<const char*>(r.name);
        const bool store = r.op == 2;
        out.append(QCborMap{
            {QStringLiteral("id"), qint64(r.id)},
            {QStringLiteral("agoMs"), qint64(r.ago_ms)},
            {QStringLiteral("type"), qint64(r.kind) + 100 * qint64(r.op)},
            {QStringLiteral("target"), hexOf(r.target)},
            {QStringLiteral("peer"), hexOf(r.peer)},
            {QStringLiteral("name"), QString::fromUtf8(n, qsizetype(strnlen(n, sizeof r.name)))},
            {QStringLiteral("addr"), addr},
            {QStringLiteral("cc"), cc},
            {QStringLiteral("contacts"), qint64(r.contacts)},
            {QStringLiteral("results"), r.op == 1 ? qint64(r.results) : qint64(-1)},
            {QStringLiteral("records"), store ? qint64(r.offered) : qint64(-1)},
            {QStringLiteral("stored"), store ? qint64(r.stored) : qint64(-1)},
            {QStringLiteral("load"), r.responsible != 0 && r.op != 0 ? qint64(r.load) : qint64(-1)},
            {QStringLiteral("responsible"), r.responsible != 0},
            {QStringLiteral("answered"), r.error == 0},
            {QStringLiteral("error"), qint64(r.error)},
        });
    }
    return out;
}

/// The shape of ops::kadIncomingGraph without the row fields: ids are routing ids, `node`
/// the node ID.
QCborMap incomingGraphToCbor(const std::vector<ed3k_kad_incoming_node>& all)
{
    QCborMap out{{QStringLiteral("found"), true}};
    QCborArray handed;
    for (const auto& n : all) {
        QCborMap m{
            {QStringLiteral("id"), n.known != 0 ? hexOf(n.id) : QString()},
            {QStringLiteral("version"), qint64(-1)},
        };
        m.insert(QStringLiteral("node"), hexOf(n.node));
        if (n.has_addr != 0) {
            const QHostAddress ip = ed3k::toQHostAddress(ffi::Address::from(n.addr));
            m.insert(QStringLiteral("addr"), addrText(ip, n.addr.port));
            m.insert(QStringLiteral("cc"), countryCodeOf(ip));
        }
        switch (n.role) {
        case 0: out.insert(QStringLiteral("asker"), m); break;
        case 1: out.insert(QStringLiteral("own"), m); break;
        default: handed.append(m); break;
        }
    }
    out.insert(QStringLiteral("handed"), handed);
    return out;
}

QCborArray graphToCbor(const std::vector<ed3k_kad_lookup_node>& all)
{
    QCborArray out;
    for (const auto& n : all) {
        QCborArray from;
        for (std::uint8_t i = 0; i < n.from_len && i < 4; ++i)
            from.append(qint64(n.from[i]));
        const QHostAddress ip = ed3k::toQHostAddress(ffi::Address::from(n.addr));
        out.append(QCborMap{
            {QStringLiteral("id"), hexOf(n.id)},
            {QStringLiteral("addr"), ip.isNull() ? QString() : addrText(ip, n.addr.port)},
            {QStringLiteral("cc"), countryCodeOf(ip)},
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

/// decline.txt of the core: the running node's state dir, else the configured one
QString declinePath()
{
    const QString dir = s_session && !s_session->stateDir().isEmpty() ? s_session->stateDir() : ed3k::stateDir(thePrefs.configDir());
    return QDir(dir).filePath(QString::fromLatin1(ed3k::kDeclineFile));
}

/// GetEd3kDeclined's map
QCborMap declineState()
{
    QFile f(declinePath());
    const QString text = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    const bool running = s_session && s_session->isRunning();
    const ed3k::DeclineList list = running ? s_session->declined() : ed3k::parseDeclineList(text);
    QCborArray invalid;
    for (const QString& line : list.invalid)
        invalid.append(line);
    // declined-records.md §2 "Shared lists": the URLs and how their last fetch went
    QCborArray urls;
    for (const QString& u : thePrefs.ed3kDeclineUrls())
        urls.append(u);
    QCborArray urlStatus;
    if (s_session)
        for (const auto& st : s_session->declineUrls().status())
            urlStatus.append(QCborMap{
                {QStringLiteral("url"), st.url},
                {QStringLiteral("fetchedAt"), st.fetchedAt},
                {QStringLiteral("triedAt"), st.triedAt},
                {QStringLiteral("error"), st.error},
                {QStringLiteral("entries"), st.entries},
                {QStringLiteral("fetching"), st.fetching},
            });
    return QCborMap{
        {QStringLiteral("urls"), urls},
        {QStringLiteral("urlStatus"), urlStatus},
        {QStringLiteral("text"), text},
        {QStringLiteral("running"), running},
        {QStringLiteral("loaded"), QCborMap{
             {QStringLiteral("roots"), qint64(list.roots.size())},
             {QStringLiteral("ed2k"), qint64(list.ed2k.size())},
             {QStringLiteral("keys"), qint64(list.keys.size())},
             {QStringLiteral("kinds"), qint64(list.kinds.size())},
             {QStringLiteral("keywords"), qint64(list.keywords.size())},
         }},
        {QStringLiteral("invalid"), invalid},
        {QStringLiteral("dropped"), running ? qint64(s_session->declineDropped()) : qint64(0)},
    };
}

/// SetEd3kDeclined: the file written; empty: ok, else why not
QString writeDecline(const QString& text)
{
    const QString path = declinePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return QStringLiteral("cannot create %1").arg(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit())
        return QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
    if (s_session)
        s_session->reloadDeclineFile();
    return {};
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
        && t != IpcMsgType::GetEd3kKadIncoming && t != IpcMsgType::GetEd3kKadIncomingGraph
        && t != IpcMsgType::ExportEd3kNodeList && t != IpcMsgType::FetchEd3kNodeLists
        && t != IpcMsgType::GetEd3kDeclined && t != IpcMsgType::SetEd3kDeclined)
        return false;
    const int seq = msg.seqId();
#ifdef EMULE_HAVE_ED3K
    // the file, also with the node off
    if (t == IpcMsgType::GetEd3kDeclined) {
        reply(IpcMessage::makeResult(seq, true, declineState()));
        return true;
    }
    if (t == IpcMsgType::SetEd3kDeclined) {
        // field 1: the URL lists (pref ed3k.declineUrls), fetched again when changed
        if (msg.fieldCount() > 1) {
            QStringList urls;
            for (const auto& v : msg.fieldArray(1))
                if (const QString u = v.toString().trimmed(); !u.isEmpty() && !urls.contains(u))
                    urls << u;
            if (urls != thePrefs.ed3kDeclineUrls()) {
                thePrefs.setEd3kDeclineUrls(urls);
                thePrefs.saveInBackground();
            }
            if (s_session)
                s_session->setDeclineUrls(urls);
        }
        if (const QString error = writeDecline(msg.fieldString(0)); !error.isEmpty()) {
            logWarning(QStringLiteral("ed3k: %1").arg(error));
            reply(failure(seq, int(ffi::Error::Io), error));
        } else {
            logInfo(QStringLiteral("ed3k: %1 saved").arg(declinePath()));
            reply(IpcMessage::makeResult(seq, true, declineState()));
        }
        return true;
    }
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
    case IpcMsgType::GetEd3kKadIncoming:
        if (auto all = e->kadIncoming(now))
            reply(IpcMessage::makeResult(seq, true, incomingToCbor(*all)));
        else
            reply(failure(seq, int(all.error())));
        break;
    case IpcMsgType::GetEd3kKadIncomingGraph:
        if (auto all = e->kadIncomingGraph(std::uint64_t(msg.fieldInt(0))))
            reply(IpcMessage::makeResult(seq, true, incomingGraphToCbor(*all)));
        else if (all.error() == ffi::Error::NotFound)
            reply(IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("found"), false}}));
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
