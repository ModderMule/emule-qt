/// @file Ed3kHosterBridge.cpp
/// @brief Hoster links over IPC — implementation.

#include "Ed3kHosterBridge.h"

#include "Ed3kBridge.h"
#include "Ed3kOffGate.h"
#include "PushCoalescer.h"
#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "stats/NetworkCounters.h"
#include "stats/Statistics.h"

#include <QCborArray>
#include <QCborMap>

#include <algorithm>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kHosterJoin.h"
#include "Ed3kResolver.h"
#ifdef EMULE_HAVE_HOSTER_JD
#include "Ed3kJavaRuntime.h"
#endif
#endif

namespace eMule::Ed3kHosterBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

#ifdef EMULE_HAVE_ED3K
ed3k::Ed3kHosterJoin* s_join = nullptr;
ed3k::Ed3kResolver* s_resolver = nullptr;
ed3k::Ed3kJavaRuntime* s_java = nullptr;
/// item pushes come per block: one per item and window
constexpr int kItemWindowMs = 500;

QCborMap itemToCbor(const ed3k::Ed3kHosterJoin::Item& it)
{
    return QCborMap{{QStringLiteral("id"), qint64(it.id)},
                    {QStringLiteral("url"), it.url},
                    {QStringLiteral("host"), it.host},
                    {QStringLiteral("name"), it.name},
                    {QStringLiteral("size"), qint64(it.size)},
                    {QStringLiteral("done"), qint64(it.done)},
                    {QStringLiteral("speed"), qint64(it.speed)},
                    {QStringLiteral("state"), int(it.state)},
                    {QStringLiteral("reason"), int(it.reason)},
                    {QStringLiteral("reasonArg"), it.reasonArg},
                    {QStringLiteral("waitUntil"), it.waitUntil},
                    {QStringLiteral("verified"), it.verified},
                    {QStringLiteral("file"), it.file},
                    {QStringLiteral("root"), it.rootHex},
                    {QStringLiteral("added"), it.added},
                    {QStringLiteral("mirrorNo"), it.mirrorNo},
                    {QStringLiteral("mirrors"), qint64(it.mirrors.size())},
                    {QStringLiteral("packageId"), qint64(it.packageId)},
                    {QStringLiteral("package"), it.package}};
}

/// GetHosterStats: the counters from Statistics, the live part from the items.
QCborMap hosterStatsToCbor(const ed3k::Ed3kHosterJoin& join)
{
    using S = ed3k::Ed3kHosterJoin::State;
    const Statistics* stats = theApp.statistics;
    QCborMap out;
    out.insert(QStringLiteral("session"), countersToCbor(stats ? stats->hosterSession() : HosterCounters{}));
    out.insert(QStringLiteral("cumulative"), countersToCbor(stats ? stats->cumulativeHoster() : thePrefs.cumHoster()));

    qint64 rate = 0, downloading = 0, queued = 0, waiting = 0, puzzle = 0, paused = 0, needsUser = 0,
           complete = 0, failed = 0, total = 0, done = 0;
    const QList<ed3k::Ed3kHosterJoin::Item> items = join.items();
    for (const auto& it : items) {
        switch (it.state) {
        case S::Downloading: ++downloading; break;
        case S::Matching:
        case S::Looking:
        case S::Checking:
        case S::Queued: ++queued; break;
        case S::Waiting: ++waiting; break;
        case S::Puzzle: ++puzzle; break;
        case S::Paused: ++paused; break;
        case S::NeedsUser:
        case S::NoResolver: ++needsUser; break;
        case S::Complete: ++complete; break;
        case S::Gone:
        case S::Failed: ++failed; break;
        }
        rate += qint64(it.speed);
        // what is still to come: a complete item is done, a failed one won't be
        if (it.state != S::Complete && it.state != S::Gone && it.state != S::Failed) {
            total += qint64(it.size);
            done += qint64(std::min(it.done, it.size));
        }
    }
    const QCborMap counts{{QStringLiteral("count"), qint64(items.size())},
                          {QStringLiteral("downloading"), downloading},
                          {QStringLiteral("queued"), queued},
                          {QStringLiteral("waiting"), waiting},
                          {QStringLiteral("puzzle"), puzzle},
                          {QStringLiteral("paused"), paused},
                          {QStringLiteral("needsUser"), needsUser},
                          {QStringLiteral("complete"), complete},
                          {QStringLiteral("failed"), failed},
                          {QStringLiteral("totalBytes"), total},
                          {QStringLiteral("doneBytes"), done},
                          {QStringLiteral("leftBytes"), std::max<qint64>(0, total - done)}};
    out.insert(QStringLiteral("current"), QCborMap{{QStringLiteral("downRate"), rate}, {QStringLiteral("items"), counts}});
    return out;
}

QCborMap resultToCbor(const ed3k::ResolverEvent& r)
{
    const bool ok = r.kind == ed3k::ResolverEvent::Kind::Account;
    return QCborMap{{QStringLiteral("valid"), ok && r.valid},
                    {QStringLiteral("premium"), r.premium},
                    {QStringLiteral("expires"), r.expires},
                    {QStringLiteral("trafficLeft"), r.trafficLeft},
                    {QStringLiteral("error"), ok ? QString() : (r.code.isEmpty() ? QStringLiteral("failed") : r.code)}};
}
#endif

IpcMessage failure(int seqId, const QString& error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

} // namespace

void setHoster(ed3k::Ed3kHosterJoin* join, ed3k::Ed3kResolver* resolver, ed3k::Ed3kJavaRuntime* java)
{
#ifdef EMULE_HAVE_ED3K
    s_join = join;
    s_resolver = resolver;
    s_java = java;
#else
    Q_UNUSED(join) Q_UNUSED(resolver) Q_UNUSED(java)
#endif
}

bool isHosterRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    return t >= 1100 && t <= 1109;
}

bool carriesSecret(IpcMsgType type)
{
    return type == IpcMsgType::CheckHosterAccount;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isHosterRequest(msg.type()))
        return false;
    // ed3k off: nothing under config/ed3k is written
    if (Ed3kBridge::refuseWhileOff(msg, {IpcMsgType::GetHosterItems, IpcMsgType::GetHosterStats}, reply))
        return true;
    const int seq = msg.seqId();
#ifdef EMULE_HAVE_ED3K
    if (!s_join) {
        reply(failure(seq, QStringLiteral("no hoster join")));
        return true;
    }
    switch (msg.type()) {
    case IpcMsgType::GetHosterItems: {
        QCborArray items;
        for (const auto& it : s_join->items())
            items.append(itemToCbor(it));
        reply(IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("items"), items}, {QStringLiteral("status"), statusToCbor()}}));
        break;
    }
    case IpcMsgType::GetHosterStats:
        reply(IpcMessage::makeResult(seq, true, hosterStatsToCbor(*s_join)));
        break;
    case IpcMsgType::AddHosterLinks: {
        QCborArray ids;
        // field 1: one list of mirrors per file (web search rows); else the pasted text
        QList<QStringList> files;
        for (const QCborValue& f : msg.field(1).toArray()) {
            QStringList urls;
            for (const QCborValue& u : f.toArray())
                urls << u.toString();
            files.append(urls);
        }
        // field 2: a new package of that name; field 3: an existing package to join
        const QString package = msg.fieldString(2);
        const auto packageId = quint32(msg.field(3).toInteger());
        const auto added = files.isEmpty() ? s_join->addLinks(msg.fieldString(0), package, packageId)
                                           : s_join->addMirrored(files, package, packageId);
        for (const quint32 id : added)
            ids.append(qint64(id));
        reply(IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("ids"), ids}}));
        break;
    }
    case IpcMsgType::HosterAction: {
        const auto id = quint32(msg.field(0).toInteger());
        const QString op = msg.fieldString(1);
        if (!s_join->item(id)) {
            reply(failure(seq, QStringLiteral("no such item")));
            break;
        }
        if (op == QLatin1StringView("pause"))
            s_join->pause(id);
        else if (op == QLatin1StringView("resume"))
            s_join->resume(id);
        else if (op == QLatin1StringView("remove") || op == QLatin1StringView("delete"))
            s_join->remove(id, op == QLatin1StringView("delete"));
        else {
            reply(failure(seq, QStringLiteral("unknown op")));
            break;
        }
        reply(IpcMessage::makeResult(seq, true, QCborValue{}));
        break;
    }
    case IpcMsgType::AnswerHosterPuzzle:
        s_join->answerPuzzle(quint64(msg.field(0).toInteger()), msg.fieldString(1));
        reply(IpcMessage::makeResult(seq, true, QCborValue{}));
        break;
    case IpcMsgType::CheckHosterLinks: {
        if (!thePrefs.ed3kHoster().checkSearchLinks) {
            reply(failure(seq, QStringLiteral("link checks are off")));
            break;
        }
        QStringList urls;
        for (const QCborValue& u : msg.field(0).toArray())
            urls << u.toString();
        if (!s_join->checkLinks(urls)) {
            reply(failure(seq, QStringLiteral("hoster downloads are off")));
            break;
        }
        reply(IpcMessage::makeResult(seq, true, QCborValue{}));
        break;
    }
    case IpcMsgType::CheckHosterAccount:
        s_join->checkAccount(msg.fieldString(0).trimmed().toLower(), msg.fieldString(1), msg.fieldString(2));
        reply(IpcMessage::makeResult(seq, true, QCborValue{}));
        break;
    case IpcMsgType::DownloadJavaRuntime:
    case IpcMsgType::LocateJavaRuntime:
#ifdef EMULE_HAVE_HOSTER_JD
        if (!s_java) {
            reply(failure(seq, QStringLiteral("no Java runtime lookup")));
            break;
        }
        if (msg.type() == IpcMsgType::DownloadJavaRuntime) {
            // only for a user who switched hoster downloads on
            if (!thePrefs.ed3kHoster().enabled || !s_java->download()) {
                reply(failure(seq, QStringLiteral("hoster downloads are off")));
                break;
            }
        } else
            s_java->locate(thePrefs.ed3kHoster().java);
        reply(IpcMessage::makeResult(seq, true, QCborValue{}));
#else
        reply(failure(seq, QStringLiteral("built without the hoster resolver")));
#endif
        break;
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown hoster request")));
        break;
    }
#else
    reply(failure(seq, QStringLiteral("built without ed3k")));
#endif
    return true;
}

void connectPushes(ed3k::Ed3kHosterJoin& join, const std::function<void(const IpcMessage&)>& broadcast)
{
#ifdef EMULE_HAVE_ED3K
    auto* coalescer = new Ipc::PushCoalescer(&join);
    QObject::connect(coalescer, &Ipc::PushCoalescer::ready, &join, [broadcast](const IpcMessage& msg) { broadcast(msg); });
    QObject::connect(&join, &ed3k::Ed3kHosterJoin::itemChanged, coalescer, [coalescer, j = &join](quint32 id) {
        coalescer->post(IpcMsgType::PushHosterItem, [j, id] {
            IpcMessage msg(IpcMsgType::PushHosterItem, 0);
            if (const auto it = j->item(id))
                msg.append(itemToCbor(*it));
            return msg;
        }, kItemWindowMs, id);
    });
    QObject::connect(&join, &ed3k::Ed3kHosterJoin::itemRemoved, &join, [broadcast](quint32 id) {
        IpcMessage msg(IpcMsgType::PushHosterItemRemoved, 0);
        msg.append(qint64(id));
        broadcast(msg);
    });
    QObject::connect(&join, &ed3k::Ed3kHosterJoin::puzzleAsked, &join,
                     [broadcast](quint32 id, quint64 puzzle, const QByteArray& image, const QString& host,
                                 const QString& explain, int timeoutS) {
                         IpcMessage msg(IpcMsgType::PushHosterPuzzle, 0);
                         msg.append(qint64(id));
                         msg.append(qint64(puzzle));
                         msg.append(QCborValue(image));
                         msg.append(host);
                         msg.append(explain);
                         msg.append(qint64(timeoutS));
                         broadcast(msg);
                     });
    QObject::connect(&join, &ed3k::Ed3kHosterJoin::linksExpanded, &join,
                     [broadcast](quint32 id, const QList<ed3k::ResolverLink>& links) {
                         QCborArray list;
                         for (const auto& l : links)
                             list.append(QCborMap{{QStringLiteral("url"), l.url},
                                                  {QStringLiteral("name"), l.name},
                                                  {QStringLiteral("host"), l.host},
                                                  {QStringLiteral("size"), qint64(l.size)}});
                         IpcMessage msg(IpcMsgType::PushHosterLinks, 0);
                         msg.append(qint64(id));
                         msg.append(list);
                         broadcast(msg);
                     });
    QObject::connect(&join, &ed3k::Ed3kHosterJoin::accountChecked, &join,
                     [broadcast](const QString& host, const QString& user, const ed3k::ResolverEvent& r) {
                         IpcMessage msg(IpcMsgType::PushHosterAccount, 0);
                         msg.append(host);
                         msg.append(user);
                         msg.append(resultToCbor(r));
                         broadcast(msg);
                     });
    QObject::connect(&join, &ed3k::Ed3kHosterJoin::linkChecked, &join,
                     [broadcast, &join](const QString& url, const ed3k::Ed3kHosterJoin::LinkCheck& c) {
                         IpcMessage msg(IpcMsgType::PushHosterLinkCheck, 0);
                         msg.append(url);
                         msg.append(QCborMap{{QStringLiteral("status"), int(c.status)},
                                             {QStringLiteral("host"), c.host},
                                             {QStringLiteral("name"), c.name},
                                             {QStringLiteral("size"), qint64(c.size)},
                                             {QStringLiteral("account"), join.accountTier(c.host)}});
                         broadcast(msg);
                     });
#else
    Q_UNUSED(join) Q_UNUSED(broadcast)
#endif
}

QCborMap statusToCbor()
{
    QCborMap out;
#ifdef EMULE_HAVE_ED3K
    if (!s_join)
        return out;
#ifdef EMULE_HAVE_HOSTER_JD
    out.insert(QStringLiteral("compiled"), true);
#else
    out.insert(QStringLiteral("compiled"), false);
#endif
    out.insert(QStringLiteral("enabled"), thePrefs.hosterDownloadsOn());
    if (s_resolver)
        out.insert(QStringLiteral("resolver"), QCborMap{{QStringLiteral("available"), s_resolver->available()},
                                                        {QStringLiteral("reason"), s_resolver->unavailableReason()},
                                                        {QStringLiteral("description"), s_resolver->description()}});
#ifdef EMULE_HAVE_HOSTER_JD
    if (s_java) {
        QCborMap j{{QStringLiteral("state"), int(s_java->state())},
                   {QStringLiteral("error"), s_java->error()},
                   {QStringLiteral("progress"), s_java->progress()}};
        if (const auto rt = s_java->runtime()) {
            j.insert(QStringLiteral("path"), rt->java);
            j.insert(QStringLiteral("version"), rt->feature);
            j.insert(QStringLiteral("vendor"), rt->vendor);
        }
        out.insert(QStringLiteral("java"), j);
    }
#endif
#endif
    return out;
}

} // namespace eMule::Ed3kHosterBridge
