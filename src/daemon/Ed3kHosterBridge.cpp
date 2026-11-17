/// @file Ed3kHosterBridge.cpp
/// @brief Hoster links over IPC — implementation.

#include "Ed3kHosterBridge.h"

#include "ArchiveContentsBridge.h"

#include "Ed3kBridge.h"
#include "Ed3kOffGate.h"
#include "Ed3kUploadsBridge.h"
#include "PushCoalescer.h"
#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "stats/NetworkCounters.h"
#include "stats/Statistics.h"

#include <QCborArray>
#include <QCborMap>
#include <QJsonObject>
#include <QFileInfo>
#include <QHash>
#include <QUrl>

#include <algorithm>
#include <optional>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kHosterCatalog.h"
#include "Ed3kHosterJoin.h"
#include "Ed3kLinkGrabber.h"
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
ed3k::Ed3kLinkGrabber* s_grabber = nullptr;
ed3k::Ed3kHosterCatalog* s_catalog = nullptr;
/// favicons asked for at once (GetHosterFavicons)
constexpr qsizetype kMaxIconAsk = 256;
/// openContainerAndAdd() tokens: their results stay in the daemon, no GUI asks
QHash<quint64, std::function<void(int, const QString&)>> s_quietContainers;
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

QCborArray linksToCbor(const QList<ed3k::ResolverLink>& links)
{
    QCborArray list;
    for (const auto& l : links) {
        QCborMap m{{QStringLiteral("url"), l.url},
                   {QStringLiteral("name"), l.name},
                   {QStringLiteral("host"), l.host},
                   {QStringLiteral("size"), qint64(l.size)}};
        if (!l.package.isEmpty())
            m.insert(QStringLiteral("package"), l.package);
        if (!l.passwords.isEmpty())
            m.insert(QStringLiteral("passwords"), QCborArray::fromStringList(l.passwords));
        list.append(m);
    }
    return list;
}

QCborMap grabberPackageToCbor(const ed3k::Ed3kLinkGrabber::Package& p)
{
    return QCborMap{{QStringLiteral("id"), qint64(p.id)},
                    {QStringLiteral("name"), p.name},
                    {QStringLiteral("passwords"), QCborArray::fromStringList(p.passwords)},
                    {QStringLiteral("source"), p.source},
                    {QStringLiteral("added"), p.added}};
}

QCborMap grabberLinkToCbor(const ed3k::Ed3kLinkGrabber::Link& k)
{
    return QCborMap{{QStringLiteral("id"), qint64(k.id)},
                    {QStringLiteral("packageId"), qint64(k.packageId)},
                    {QStringLiteral("url"), k.url},
                    {QStringLiteral("name"), k.name},
                    {QStringLiteral("host"), k.host},
                    {QStringLiteral("size"), qint64(k.size)},
                    {QStringLiteral("status"), int(k.status)},
                    {QStringLiteral("enabled"), k.enabled},
                    {QStringLiteral("dupe"), k.dupe}};
}

QCborArray accountHostsToCbor(const QList<ed3k::AccountHost>& hosts)
{
    QCborArray out;
    for (const ed3k::AccountHost& h : hosts) {
        QCborMap m{{QStringLiteral("host"), h.host}};
        if (!h.name.isEmpty())
            m.insert(QStringLiteral("name"), h.name);
        if (h.multi)
            m.insert(QStringLiteral("multi"), true);
        if (h.solver)
            m.insert(QStringLiteral("solver"), true);
        out.append(m);
    }
    return out;
}

QList<quint32> idsOf(const QCborValue& v)
{
    QList<quint32> out;
    for (const QCborValue& x : v.toArray())
        if (const qint64 id = x.toInteger(); id > 0)
            out << quint32(id);
    return out;
}

/// Whole list (GetLinkGrabber, a push with all).
QCborMap grabberToCbor(const ed3k::Ed3kLinkGrabber& g)
{
    QCborArray pkgs;
    for (const auto& p : g.packages())
        pkgs.append(grabberPackageToCbor(p));
    QCborArray links;
    for (const auto& k : g.links())
        links.append(grabberLinkToCbor(k));
    return QCborMap{{QStringLiteral("packages"), pkgs},
                    {QStringLiteral("links"), links},
                    {QStringLiteral("crawling"), g.crawling()},
                    {QStringLiteral("deep"), g.canDeepCrawl()}};
}

QCborValue grabberRequest(const IpcMessage& msg, QString* error)
{
    auto& g = *s_grabber;
    switch (msg.type()) {
    case IpcMsgType::GetLinkGrabber:
        return grabberToCbor(g);
    case IpcMsgType::GrabLinks: {
        if (!thePrefs.hosterDownloadsOn() || !s_resolver) {
            *error = QStringLiteral("hoster downloads are off");
            return {};
        }
        const bool ask = msg.field(5).toBool();
        if (!ask && !g.isEnabled()) {
            *error = QStringLiteral("the Link Grabber is off");
            return {};
        }
        QStringList pw;
        for (const QCborValue& v : msg.field(4).toArray())
            pw << v.toString();
        const quint64 token = g.grab(msg.fieldString(0), msg.fieldString(1), msg.field(2).toBool(), msg.fieldString(3),
                                     pw, ask ? ed3k::Ed3kLinkGrabber::Mode::Ask : ed3k::Ed3kLinkGrabber::Mode::List,
                                     msg.field(6).toBool());
        if (token == 0) {
            *error = QStringLiteral("no new links");
            return {};
        }
        return QCborMap{{QStringLiteral("token"), qint64(token)}};
    }
    case IpcMsgType::EditGrabberPackages: {
        std::optional<QStringList> pw;
        if (msg.field(2).isArray()) {
            pw = QStringList{};
            for (const QCborValue& v : msg.field(2).toArray())
                *pw << v.toString();
        }
        if (!g.editPackages(idsOf(msg.field(0)), msg.fieldString(1), pw)) {
            *error = QStringLiteral("no such package");
            return {};
        }
        return QCborValue{};
    }
    case IpcMsgType::SetGrabberLinksEnabled:
        g.setLinksEnabled(idsOf(msg.field(0)), msg.field(1).toBool());
        return QCborValue{};
    case IpcMsgType::MoveGrabberLinks: {
        if (const QList<quint32> split = idsOf(msg.field(3)); !split.isEmpty()) {
            g.splitByHost(split);
            return QCborValue{};
        }
        const quint32 id = g.moveLinks(idsOf(msg.field(0)), quint32(msg.field(1).toInteger()), msg.fieldString(2));
        if (id == 0) {
            *error = QStringLiteral("no package");
            return {};
        }
        return QCborMap{{QStringLiteral("packageId"), qint64(id)}};
    }
    case IpcMsgType::RemoveGrabberLinks:
        if (const QString mode = msg.fieldString(2); !mode.isEmpty()) {
            if (!g.cleanup(mode)) {
                *error = QStringLiteral("unknown cleanup");
                return {};
            }
        } else {
            g.remove(idsOf(msg.field(0)), idsOf(msg.field(1)));
        }
        return QCborValue{};
    case IpcMsgType::RecheckGrabberLinks:
        g.recheck(idsOf(msg.field(0)), idsOf(msg.field(1)));
        return QCborValue{};
    case IpcMsgType::ConfirmGrabberLinks: {
        QCborArray ids;
        for (const quint32 id : g.confirm(idsOf(msg.field(0)), idsOf(msg.field(1)), msg.field(2).toBool()))
            ids.append(qint64(id));
        return QCborMap{{QStringLiteral("ids"), ids}};
    }
    case IpcMsgType::AbortGrabberCrawls:
        g.abortCrawls();
        return QCborValue{};
    default:
        *error = QStringLiteral("unknown request");
        return {};
    }
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

/// What sites.json keeps of a login's check (no session, no hoster list): only what is known
QJsonObject storedCheck(bool valid, bool premium, qint64 expires, qint64 trafficLeft, qint64 trafficMax, double balance,
                        const QString& currency, int hosts)
{
    QJsonObject o{{QStringLiteral("valid"), valid}};
    if (premium)
        o.insert(QStringLiteral("premium"), true);
    if (expires > 0)
        o.insert(QStringLiteral("expires"), double(expires));
    if (trafficLeft >= 0)
        o.insert(QStringLiteral("trafficLeft"), double(trafficLeft));
    if (trafficMax > 0)
        o.insert(QStringLiteral("trafficMax"), double(trafficMax));
    if (balance >= 0)
        o.insert(QStringLiteral("balance"), balance);
    if (!currency.isEmpty())
        o.insert(QStringLiteral("currency"), currency);
    if (hosts >= 0)
        o.insert(QStringLiteral("hosts"), hosts);
    return o;
}

QCborMap resultToCbor(const ed3k::ResolverEvent& r)
{
    const bool ok = r.kind == ed3k::ResolverEvent::Kind::Account;
    QCborMap out{{QStringLiteral("valid"), ok && r.valid},
                 {QStringLiteral("premium"), r.premium},
                 {QStringLiteral("expires"), r.expires},
                 {QStringLiteral("trafficLeft"), r.trafficLeft},
                 {QStringLiteral("trafficMax"), r.trafficMax},
                 {QStringLiteral("balance"), r.balance},   // a solver service: its credit; -1 none
                 {QStringLiteral("currency"), r.currency},
                 {QStringLiteral("error"), ok ? QString() : (r.code.isEmpty() ? QStringLiteral("failed") : r.code)}};
    // a multi-hoster: the hosters it downloads with their quotas (never its session state)
    if (ok && r.multi) {
        QCborArray hosts;
        for (const ed3k::DebridHost& h : r.hosts)
            hosts.append(QCborMap{{QStringLiteral("domain"), h.domain},
                                  {QStringLiteral("status"), h.status},
                                  {QStringLiteral("linksLeft"), h.linksLeft},
                                  {QStringLiteral("linksMax"), h.linksMax},
                                  {QStringLiteral("trafficLeft"), h.trafficLeft},
                                  {QStringLiteral("trafficMax"), h.trafficMax},
                                  {QStringLiteral("factor"), h.factor},
                                  {QStringLiteral("unavailableS"), h.unavailableS}});
        out.insert(QStringLiteral("multi"), true);
        out.insert(QStringLiteral("hosts"), hosts);
    }
    // a browser login waits: the user opens this URL; the result follows
    if (r.kind == ed3k::ResolverEvent::Kind::Login) {
        out.insert(QStringLiteral("error"), QStringLiteral("login"));
        out.insert(QStringLiteral("login"), r.text);
        out.insert(QStringLiteral("timeout"), r.timeoutS);
    }
    // a confirmation code waits (2FA, a "new sign in" mail): AnswerHosterAccountCode
    if (r.kind == ed3k::ResolverEvent::Kind::Code) {
        out.insert(QStringLiteral("error"), QStringLiteral("code"));
        out.insert(QStringLiteral("codeText"), r.text);
        out.insert(QStringLiteral("codeTitle"), r.name);
        out.insert(QStringLiteral("timeout"), r.timeoutS);
    }
    return out;
}
#endif

IpcMessage failure(int seqId, const QString& error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
/// A container's links, one package per name with its passwords; with the Link Grabber
/// taking all, into it instead. How many links went in.
int addContainerLinks(const QList<ed3k::ResolverLink>& links, const QString& fileName)
{
    const QString fallback = QFileInfo(fileName).completeBaseName();
    if (s_grabber && s_grabber->takesAll())
        return s_grabber->add(links, fileName, fallback);
    QStringList packages;
    for (const auto& l : links)
        if (!packages.contains(l.package))
            packages << l.package;
    int added = 0;
    for (const QString& pkg : std::as_const(packages)) {
        QList<QStringList> files;
        QStringList passwords;
        for (const auto& l : links) {
            if (l.package != pkg)
                continue;
            files.append(QStringList{l.url});
            for (const QString& pw : l.passwords)
                if (!pw.isEmpty() && !passwords.contains(pw))
                    passwords << pw;
        }
        added += int(s_join->addMirrored(files, pkg.isEmpty() ? fallback : pkg, 0, passwords).size());
    }
    return added;
}
#endif

} // namespace

void setCatalog(ed3k::Ed3kHosterCatalog* catalog, const std::function<void(const IpcMessage&)>& broadcast)
{
#ifdef EMULE_HAVE_ED3K
    s_catalog = catalog;
    if (!catalog)
        return;
    QObject::connect(catalog, &ed3k::Ed3kHosterCatalog::accountHostsChanged, catalog,
                     [broadcast](const QList<ed3k::AccountHost>& hosts) {
                         IpcMessage msg(IpcMsgType::PushHosterCatalog, 0);
                         msg.append(QCborMap{{QStringLiteral("hosts"), accountHostsToCbor(hosts)}});
                         broadcast(msg);
                     });
    QObject::connect(catalog, &ed3k::Ed3kHosterCatalog::iconReady, catalog,
                     [broadcast](const QString& host, const QByteArray& png) {
                         IpcMessage msg(IpcMsgType::PushHosterCatalog, 0);
                         msg.append(QCborMap{{QStringLiteral("icons"), QCborMap{{host, QCborValue(png)}}}});
                         broadcast(msg);
                     });
    QObject::connect(catalog, &ed3k::Ed3kHosterCatalog::countryReady, catalog,
                     [broadcast](const QString& host, const QString& cc) {
                         IpcMessage msg(IpcMsgType::PushHosterCatalog, 0);
                         msg.append(QCborMap{{QStringLiteral("countries"), QCborMap{{host, cc}}}});
                         broadcast(msg);
                     });
#else
    Q_UNUSED(catalog) Q_UNUSED(broadcast)
#endif
}

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

#ifdef EMULE_HAVE_ED3K
namespace {

/// The item of join owner "h<id>"
std::optional<ed3k::Ed3kHosterJoin::Item> itemOfOwner(const QString& owner)
{
    if (!s_join || !owner.startsWith(u'h'))
        return std::nullopt;
    bool ok = false;
    const quint32 id = owner.mid(1).toUInt(&ok);
    return ok ? s_join->item(id) : std::nullopt;
}

} // namespace
#endif

QCborMap aliasOf([[maybe_unused]] const QString& owner)
{
#ifdef EMULE_HAVE_ED3K
    const auto item = itemOfOwner(owner);
    if (!item)
        return {};
    return QCborMap{{QStringLiteral("network"), QStringLiteral("hoster")},
                    {QStringLiteral("name"), item->name},
                    {QStringLiteral("id"), item->url},
                    {QStringLiteral("host"), item->host}};
#else
    return {};
#endif
}

void appendDownloading([[maybe_unused]] QCborArray& out, [[maybe_unused]] const QString& owner)
{
#ifdef EMULE_HAVE_ED3K
    const auto item = itemOfOwner(owner);
    if (!item || item->state != ed3k::Ed3kHosterJoin::State::Downloading || item->speed == 0)
        return;
    const QUrl url(item->url);
    out.append(QCborMap{
        {QStringLiteral("network"), QStringLiteral("hoster")},
        {QStringLiteral("isUrl"), true},
        {QStringLiteral("userName"), item->host.isEmpty() ? url.host() : item->host},
        // the details key of the row: one per item
        {QStringLiteral("userHash"), QString(QStringLiteral("hoster:") + owner)},
        {QStringLiteral("software"), QStringLiteral("HTTP")},
        {QStringLiteral("softwareId"), -1},
        {QStringLiteral("reqFileName"), item->name},
        {QStringLiteral("downloadState"), QStringLiteral("Downloading")},
        {QStringLiteral("datarate"), qint64(item->speed)},
        {QStringLiteral("addr"), url.host()},
        {QStringLiteral("port"), url.port(url.scheme() == QLatin1String("http") ? 80 : 443)},
        {QStringLiteral("isConnected"), true},
        {QStringLiteral("noViewSharedFiles"), true},
        {QStringLiteral("sourceFrom"), -1},
    });
#endif
}

std::optional<SingleTransfer::Job> transferJob([[maybe_unused]] const QString& owner)
{
#ifdef EMULE_HAVE_ED3K
    const auto item = itemOfOwner(owner);
    if (!item)
        return std::nullopt;
    using S = SingleTransfer::RowState;
    using H = ed3k::Ed3kHosterJoin::State;
    SingleTransfer::Job j;
    j.name = item->name;
    j.size = qint64(item->size);
    j.done = qint64(item->done);
    j.rate = item->state == H::Downloading ? qint64(item->speed) : 0;
    // the hoster while it sends, and the ed3k streams of its root
    j.sources = item->peerSources + (item->state == H::Downloading ? 1 : 0);
    j.transferring = j.rate > 0 ? j.sources : 0;
    j.path = item->file;
    j.addedOn = item->added / 1000;   // ms → s, as the ed3k rows
    j.error = item->reasonArg;
    switch (item->state) {
    case H::Paused: j.state = S::Paused; break;
    case H::Complete: j.state = S::Complete; break;
    case H::Gone:
    case H::NeedsUser:
    case H::NoResolver:
    case H::Failed: j.state = S::Error; break;
    default: break;
    }
    return j;
#else
    return std::nullopt;
#endif
}

bool transferControl([[maybe_unused]] const QString& owner, [[maybe_unused]] int action, qint64 /*value*/)
{
#ifdef EMULE_HAVE_ED3K
    const auto item = itemOfOwner(owner);
    if (!item)
        return false;
    switch (action) {
    case 0: s_join->pause(item->id); return true;
    case 1: s_join->resume(item->id); return true;
    // the unfinished file always goes; a complete one only with 7 (Cancel/Delete)
    case 2: s_join->remove(item->id, false); return true;
    case 7: s_join->remove(item->id, true); return true;
    default: return false;
    }
#else
    return false;
#endif
}

QStringList owners()
{
    QStringList out;
#ifdef EMULE_HAVE_ED3K
    if (s_join)
        for (const auto& item : s_join->items())
            out << QStringLiteral("h%1").arg(item.id);
#endif
    return out;
}

QString moveTo([[maybe_unused]] const QString& id, [[maybe_unused]] const QString& dir)
{
#ifdef EMULE_HAVE_ED3K
    bool ok = false;
    const quint32 n = id.toUInt(&ok);
    if (!s_join || !ok)
        return QStringLiteral("no such item");
    return s_join->moveTo(n, dir);
#else
    return QStringLiteral("no hoster downloads");
#endif
}

void setGrabber(ed3k::Ed3kLinkGrabber* grabber)
{
#ifdef EMULE_HAVE_ED3K
    s_grabber = grabber;
#else
    Q_UNUSED(grabber)
#endif
}

void openContainerAndAdd(const QByteArray& data, const QString& fileName,
                         std::function<void(int added, const QString& error)> done)
{
#ifdef EMULE_HAVE_ED3K
    if (!thePrefs.hosterDownloadsOn() || !s_resolver || !s_join) {
        done(0, QStringLiteral("hoster downloads are off"));
        return;
    }
    const quint64 token = s_join->openContainer(data, fileName);
    if (token == 0) {
        done(0, data.isEmpty() ? QStringLiteral("empty file") : QStringLiteral("cannot open the container"));
        return;
    }
    s_quietContainers.insert(token, std::move(done));
#else
    Q_UNUSED(data) Q_UNUSED(fileName)
    done(0, QStringLiteral("hoster downloads are off"));
#endif
}

bool isHosterRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    // 1139: AddClickNLoad, after the block was full; 1210-1218: the Link Grabber; 1219-1221:
    // account dialogs
    return (t >= 1100 && t <= 1109) || type == IpcMsgType::AddClickNLoad || (t >= 1210 && t <= 1221);
}

bool carriesSecret(IpcMsgType type)
{
    return type == IpcMsgType::CheckHosterAccount || type == IpcMsgType::AnswerHosterAccountCode;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isHosterRequest(msg.type()))
        return false;
    // ed3k off: nothing under config/ed3k is written
    if (Ed3kBridge::refuseWhileOff(msg, {IpcMsgType::GetHosterItems, IpcMsgType::GetHosterStats, IpcMsgType::GetLinkGrabber},
                                   reply))
        return true;
    const int seq = msg.seqId();
#ifdef EMULE_HAVE_ED3K
    if (msg.type() == IpcMsgType::GetHosterAccountHosts || msg.type() == IpcMsgType::GetHosterFavicons) {
        if (!s_catalog || !thePrefs.hosterDownloadsOn()) {
            reply(failure(seq, QStringLiteral("hoster downloads are off")));
            return true;
        }
        if (msg.type() == IpcMsgType::GetHosterAccountHosts) {
            bool asking = false;
            const auto hosts = s_catalog->accountHosts(&asking);
            reply(IpcMessage::makeResult(seq, true,
                                         QCborMap{{QStringLiteral("hosts"), accountHostsToCbor(hosts)},
                                                  {QStringLiteral("asking"), asking}}));
            return true;
        }
        QStringList hosts;
        for (const QCborValue& v : msg.field(0).toArray())
            if (hosts.size() < kMaxIconAsk)
                hosts << v.toString();
        QCborMap icons;
        for (const auto& [host, png] : s_catalog->icons(hosts).asKeyValueRange())
            icons.insert(host, QCborValue(png));
        QCborMap countries;
        for (const auto& [host, cc] : s_catalog->countries(hosts).asKeyValueRange())
            countries.insert(host, cc);
        reply(IpcMessage::makeResult(seq, true,
                                     QCborMap{{QStringLiteral("icons"), icons}, {QStringLiteral("countries"), countries}}));
        return true;
    }
    if (const int t = static_cast<int>(msg.type()); t >= 1210 && t <= 1218) {
        if (!s_grabber) {
            reply(failure(seq, QStringLiteral("no link grabber")));
            return true;
        }
        QString error;
        const QCborValue v = grabberRequest(msg, &error);
        reply(error.isEmpty() ? IpcMessage::makeResult(seq, true, v) : failure(seq, error));
        return true;
    }
    if (!s_join) {
        reply(failure(seq, QStringLiteral("no hoster join")));
        return true;
    }
    switch (msg.type()) {
    case IpcMsgType::GetHosterItems: {
        // field 0 true: only the ed3k sources of the items joined with ed3k (a poll)
        if (msg.fieldBool(0)) {
            QCborMap sources;
            for (const auto& it : s_join->items())
                if (QCborArray rows = Ed3kUploadsBridge::jobSources(QStringLiteral("hoster"), QStringLiteral("h%1").arg(it.id));
                    !rows.isEmpty())
                    sources.insert(qint64(it.id), rows);
            reply(IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("sources"), sources}}));
            break;
        }
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
        // field 4: archive passwords (a container's)
        QStringList passwords;
        for (const QCborValue& pw : msg.field(4).toArray())
            if (const QString t = pw.toString(); !t.isEmpty())
                passwords << t;
        // field 5: archive entries to extract (hoster.md §6.1): {first volume name: [path]}
        const QHash<QString, QStringList> kept = ArchiveContentsBridge::entriesFromCbor(msg.field(5).toMap());
        if (files.isEmpty() && (!passwords.isEmpty() || !kept.isEmpty()))
            for (const QString& url : ed3k::Ed3kHosterJoin::linksIn(msg.fieldString(0)))
                files.append(QStringList{url});
        const auto added = files.isEmpty() ? s_join->addLinks(msg.fieldString(0), package, packageId)
                                           : s_join->addMirrored(files, package, packageId, passwords, kept);
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
        else if (op == QLatin1StringView("retry"))
            s_join->retry(id);
        else if (op == QLatin1StringView("remove") || op == QLatin1StringView("delete"))
            s_join->remove(id, op == QLatin1StringView("delete"));
        else if (op == QLatin1StringView("password")) {
            if (!s_join->setArchivePassword(id, msg.fieldString(2))) {
                reply(failure(seq, QStringLiteral("no password wanted")));
                break;
            }
        } else {
            reply(failure(seq, QStringLiteral("unknown op")));
            break;
        }
        reply(IpcMessage::makeResult(seq, true, QCborValue{}));
        break;
    }
    case IpcMsgType::AnswerHosterPuzzle:
        s_join->answerPuzzle(quint64(msg.field(0).toInteger()), msg.fieldString(1), msg.field(2).toBool());
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
    case IpcMsgType::OpenHosterContainer: {
        if (!thePrefs.hosterDownloadsOn() || !s_resolver) {
            reply(failure(seq, QStringLiteral("hoster downloads are off")));
            break;
        }
        const QByteArray data = msg.field(1).toByteArray();
        const quint64 token = s_join->openContainer(data, msg.fieldString(0));
        if (token == 0) {
            reply(failure(seq, data.isEmpty() ? QStringLiteral("empty file") : QStringLiteral("cannot open the container")));
            break;
        }
        reply(IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("token"), qint64(token)}}));
        break;
    }
    case IpcMsgType::AddClickNLoad: {
        if (!thePrefs.hosterDownloadsOn() || !s_resolver) {
            reply(failure(seq, QStringLiteral("hoster downloads are off")));
            break;
        }
        ed3k::Ed3kHosterJoin::ClickNLoad post;
        post.urls = msg.fieldString(0);
        post.crypted = msg.fieldString(1);
        post.jk = msg.fieldString(2);
        post.k = msg.fieldString(3);
        post.package = msg.fieldString(4);
        for (const QCborValue& pw : msg.field(5).toArray())
            post.passwords << pw.toString();
        post.source = msg.fieldString(6);
        const quint64 token = s_join->openClickNLoad(post);
        if (token == 0) {
            reply(failure(seq, QStringLiteral("no links, or too many Click'n'Load posts at once")));
            break;
        }
        reply(IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("token"), qint64(token)}}));
        break;
    }
    case IpcMsgType::CheckHosterAccount:
        s_join->checkAccount(msg.fieldString(0).trimmed().toLower(), msg.fieldString(1), msg.fieldString(2));
        reply(IpcMessage::makeResult(seq, true, QCborValue{}));
        break;
    case IpcMsgType::AnswerHosterAccountCode:
        if (!s_join->answerAccountCode(msg.fieldString(0).trimmed().toLower(), msg.fieldString(1), msg.fieldString(2))) {
            reply(failure(seq, QStringLiteral("no check of this login waits for a code")));
            break;
        }
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
        } else if (!s_java->locate(thePrefs.ed3kHoster().java)) {
            reply(failure(seq, QStringLiteral("Java is being looked for or downloaded already")));
            break;
        }
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
                     [broadcast, j = &join](quint32 id, const QList<ed3k::ResolverLink>& links) {
                         // the Link Grabber on: a folder's links go there, no dialog
                         if (s_grabber && s_grabber->isEnabled()) {
                             const auto it = j->item(id);
                             s_grabber->add(links, it ? it->url : QString(), it ? it->name : QString());
                             return;
                         }
                         IpcMessage msg(IpcMsgType::PushHosterLinks, 0);
                         msg.append(qint64(id));
                         msg.append(linksToCbor(links));
                         broadcast(msg);
                     });
    QObject::connect(&join, &ed3k::Ed3kHosterJoin::containerOpened, &join,
                     [broadcast](quint64 token, const QString& fileName, const QList<ed3k::ResolverLink>& links,
                                 const QString& code, const QString& text) {
                         if (const auto quiet = s_quietContainers.take(token)) {
                             if (links.isEmpty())
                                 quiet(0, text.isEmpty() ? code : code + QStringLiteral(": ") + text);
                             else
                                 quiet(addContainerLinks(links, fileName), QString());
                             return;
                         }
                         IpcMessage msg(IpcMsgType::PushHosterLinks, 0);
                         QCborMap c{{QStringLiteral("token"), qint64(token)},
                                    {QStringLiteral("fileName"), fileName},
                                    {QStringLiteral("code"), code},
                                    {QStringLiteral("text"), text}};
                         // "open all links in the Link Grabber": the GUI only switches to it
                         const bool grab = s_grabber && s_grabber->takesAll() && !links.isEmpty();
                         if (grab) {
                             c.insert(QStringLiteral("grabbed"), true);
                             c.insert(QStringLiteral("count"),
                                      s_grabber->add(links, fileName, QFileInfo(fileName).completeBaseName()));
                         }
                         msg.append(qint64(0));
                         msg.append(grab ? QCborArray{} : linksToCbor(links));
                         msg.append(c);
                         broadcast(msg);
                     });
    QObject::connect(&join, &ed3k::Ed3kHosterJoin::clickNLoadOpened, &join,
                     [broadcast](quint64 token, const QString& source, const QList<ed3k::ResolverLink>& links,
                                 const QString& code, const QString& text) {
                         IpcMessage msg(IpcMsgType::PushHosterLinks, 0);
                         QCborMap c{{QStringLiteral("token"), qint64(token)},
                                    {QStringLiteral("cnl"), true},
                                    {QStringLiteral("source"), source},
                                    {QStringLiteral("code"), code},
                                    {QStringLiteral("text"), text}};
                         const bool grab = s_grabber && s_grabber->takesAll() && !links.isEmpty();
                         if (grab) {
                             const QString host = QUrl(source).host();
                             c.insert(QStringLiteral("grabbed"), true);
                             c.insert(QStringLiteral("count"), s_grabber->add(links, source, host.isEmpty() ? source.left(80) : host));
                         }
                         msg.append(qint64(0));
                         msg.append(grab ? QCborArray{} : linksToCbor(links));
                         msg.append(c);
                         broadcast(msg);
                     });
    QObject::connect(&join, &ed3k::Ed3kHosterJoin::accountChecked, &join,
                     [broadcast](const QString& host, const QString& user, const ed3k::ResolverEvent& r) {
                         // an answer (not a network failure, a login still waiting): kept for the next run
                         if (s_catalog && r.kind == ed3k::ResolverEvent::Kind::Account)
                             s_catalog->noteAccount(host, user,
                                                    storedCheck(r.valid, r.premium, r.expires, r.trafficLeft, r.trafficMax,
                                                                r.balance, r.currency, r.multi ? int(r.hosts.size()) : -1));
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

void connectGrabberPushes(ed3k::Ed3kLinkGrabber& grabber, const std::function<void(const IpcMessage&)>& broadcast)
{
#ifdef EMULE_HAVE_ED3K
    QObject::connect(&grabber, &ed3k::Ed3kLinkGrabber::changed, &grabber,
                     [broadcast, g = &grabber](const ed3k::Ed3kLinkGrabber::Delta& d) {
                         QCborMap m;
                         if (d.all) {
                             m = grabberToCbor(*g);
                             m.insert(QStringLiteral("all"), true);
                         } else {
                             QCborArray pkgs;
                             for (const quint32 id : d.packages)
                                 if (const auto* p = g->package(id))
                                     pkgs.append(grabberPackageToCbor(*p));
                             QCborArray links;
                             for (const quint32 id : d.links)
                                 if (const auto* k = g->link(id))
                                     links.append(grabberLinkToCbor(*k));
                             QCborArray rp;
                             for (const quint32 id : d.removedPackages)
                                 rp.append(qint64(id));
                             QCborArray rl;
                             for (const quint32 id : d.removedLinks)
                                 rl.append(qint64(id));
                             m = QCborMap{{QStringLiteral("packages"), pkgs},
                                          {QStringLiteral("links"), links},
                                          {QStringLiteral("removedPackages"), rp},
                                          {QStringLiteral("removedLinks"), rl},
                                          {QStringLiteral("crawling"), g->crawling()}};
                         }
                         IpcMessage msg(IpcMsgType::PushLinkGrabber, 0);
                         msg.append(m);
                         broadcast(msg);
                     });
    // a GrabLinks ask: the GUI that sent it asks which to add, as for a container
    QObject::connect(&grabber, &ed3k::Ed3kLinkGrabber::grabbed, &grabber,
                     [broadcast](quint64 token, const QString& source, const QList<ed3k::ResolverLink>& links,
                                 const QString& code) {
                         IpcMessage msg(IpcMsgType::PushHosterLinks, 0);
                         msg.append(qint64(0));
                         msg.append(linksToCbor(links));
                         msg.append(QCborMap{{QStringLiteral("token"), qint64(token)},
                                             {QStringLiteral("grab"), true},
                                             {QStringLiteral("source"), source},
                                             {QStringLiteral("code"), code}});
                         broadcast(msg);
                     });
#else
    Q_UNUSED(grabber) Q_UNUSED(broadcast)
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
    const Ed3kHosterPrefs h = thePrefs.ed3kHoster();
    out.insert(QStringLiteral("clipboard"), h.clipboard);
    out.insert(QStringLiteral("linkGrabber"), h.linkGrabber);
    out.insert(QStringLiteral("grabAll"), h.linkGrabber && h.grabAll);
    out.insert(QStringLiteral("deepCrawl"), s_grabber && s_grabber->canDeepCrawl());
    if (s_resolver)
        out.insert(QStringLiteral("resolver"), QCborMap{{QStringLiteral("available"), s_resolver->available()},
                                                        {QStringLiteral("reason"), s_resolver->unavailableReason()},
                                                        {QStringLiteral("description"), s_resolver->description()},
                                                        {QStringLiteral("version"), s_resolver->engineVersion()},
                                                        {QStringLiteral("pending"), s_resolver->pendingVersion()}});
    if (s_resolver) {
        // captcha solvers: what the resolver found when it last got them (no passwords)
        QCborArray solvers;
        for (const ed3k::SolverAccountState& st : s_resolver->solverStates())
            solvers.append(QCborMap{{QStringLiteral("host"), st.host},
                                    {QStringLiteral("valid"), st.valid},
                                    {QStringLiteral("balance"), st.balance},
                                    {QStringLiteral("currency"), st.currency},
                                    {QStringLiteral("code"), st.code}});
        out.insert(QStringLiteral("solvers"), solvers);
    }
    // every login's last check, also from an earlier run (no passwords)
    if (s_catalog) {
        QList<std::pair<QString, QString>> logins;
        for (const QList<Ed3kHosterAccount>* list : {&h.accounts, &h.debrid, &h.solvers})
            for (const Ed3kHosterAccount& a : *list)
                logins.append({a.host, a.user});
        s_catalog->keepAccounts(logins);
        // the solvers the resolver checked at its start
        if (s_resolver)
            for (const ed3k::SolverAccountState& st : s_resolver->solverStates())
                if (st.valid || st.code == QLatin1StringView("login"))
                    for (const Ed3kHosterAccount& a : std::as_const(h.solvers))
                        if (a.host == st.host)
                            s_catalog->noteAccount(a.host, a.user, storedCheck(st.valid, false, 0, -1, -1, st.balance,
                                                                               st.currency, -1));
        QCborArray accounts;
        for (const auto& [host, user] : std::as_const(logins))
            if (const QJsonObject st = s_catalog->account(host, user); !st.isEmpty()) {
                QCborMap m = QCborMap::fromJsonObject(st);
                m.insert(QStringLiteral("host"), host);
                m.insert(QStringLiteral("user"), user);
                accounts.append(m);
            }
        out.insert(QStringLiteral("accounts"), accounts);
    }
    // multi-hoster logins: what their last check found (no passwords)
    QCborArray debrid;
    for (const auto& d : s_join->debridSummaries())
        debrid.append(QCborMap{{QStringLiteral("host"), d.host},
                               {QStringLiteral("user"), d.user},
                               {QStringLiteral("known"), d.known},
                               {QStringLiteral("valid"), d.valid},
                               {QStringLiteral("premium"), d.premium},
                               {QStringLiteral("expires"), d.expires},
                               {QStringLiteral("trafficLeft"), d.trafficLeft},
                               {QStringLiteral("trafficMax"), d.trafficMax},
                               {QStringLiteral("checked"), d.checked},
                               {QStringLiteral("hosts"), d.hosts},
                               {QStringLiteral("working"), d.working}});
    out.insert(QStringLiteral("debrid"), debrid);
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
