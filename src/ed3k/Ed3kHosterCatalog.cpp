/// @file Ed3kHosterCatalog.cpp
/// @brief Account plugins and favicons of the resolver — implementation.

#include "Ed3kHosterCatalog.h"

#include "Ed3kStore.h"
#include "geo/CdnRanges.h"
#include "utils/BackgroundFileWriter.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>

namespace eMule::ed3k {

namespace {

constexpr qsizetype kMaxIconBytes = 256 * 1024;
constexpr QLatin1StringView kCdn("~");   // countries.json: only CDN addresses

} // namespace

Ed3kHosterCatalog::Ed3kHosterCatalog(const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_configDir(configDir)
    , m_dns([](const QString& name, QObject* context, std::function<void(QList<QHostAddress>)> done) {
        QHostInfo::lookupHost(name, context, [done = std::move(done)](const QHostInfo& info) {
            done(info.error() == QHostInfo::NoError ? info.addresses() : QList<QHostAddress>{});
        });
    })
{
}

Ed3kHosterCatalog::~Ed3kHosterCatalog()
{
    writeSites();   // a check just before quit
}

void Ed3kHosterCatalog::setResolver(Ed3kResolver* resolver)
{
    if (m_resolver)
        disconnect(m_resolver, nullptr, this, nullptr);
    m_resolver = resolver;
    m_fetches.clear();
    m_queued.clear();
    m_queue.clear();
    m_hostsTask = 0;
    if (!m_resolver)
        return;
    connect(m_resolver, &Ed3kResolver::taskEvent, this, &Ed3kHosterCatalog::onTaskEvent);
    connect(m_resolver, &Ed3kResolver::stateChanged, this, &Ed3kHosterCatalog::onStateChanged);
}

QList<AccountHost> Ed3kHosterCatalog::accountHosts(bool* asking)
{
    loadHosts();
    // waiting for plugins or Java: stateChanged asks
    if (!m_hostsAsked && m_hostsTask == 0 && !m_waitAvailable)
        askHosts();
    if (asking)
        *asking = m_hostsTask != 0 || m_waitAvailable;
    return m_hosts;
}

QHash<QString, QByteArray> Ed3kHosterCatalog::icons(const QStringList& hosts)
{
    QHash<QString, QByteArray> out;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList toFetch;
    for (const QString& raw : hosts) {
        const QString host = raw.trimmed().toLower();
        if (!validHost(host) || out.contains(host))
            continue;
        const QFileInfo png(iconFile(host, false));
        bool fetch = !m_failedRun.contains(host);
        if (png.exists()) {
            QFile f(png.filePath());
            if (f.size() <= kMaxIconBytes && f.open(QIODevice::ReadOnly))
                out.insert(host, f.readAll());
            fetch = fetch && png.lastModified().daysTo(now) >= kRefreshDays;
        } else if (const QFileInfo none(iconFile(host, true)); none.exists()) {
            out.insert(host, {});   // known: no icon
            fetch = fetch && none.lastModified().daysTo(now) >= kNoneDays;
        }
        if (fetch)
            toFetch << host;
    }
    toFront(m_queue, m_queued, toFetch);
    pump();
    return out;
}

void Ed3kHosterCatalog::setGeo(std::function<bool()> ready, std::function<QString(const QHostAddress&)> countryOf)
{
    m_geoReady = std::move(ready);
    m_countryOf = std::move(countryOf);
}

void Ed3kHosterCatalog::setDnsLookup(DnsLookup lookup)
{
    m_dns = std::move(lookup);
}

QHash<QString, QString> Ed3kHosterCatalog::countries(const QStringList& hosts)
{
    QHash<QString, QString> out;
    if (!m_countryOf)
        return out;
    loadCountries();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const bool canLook = m_dnsAllowed && m_dns && (!m_geoReady || m_geoReady());
    QStringList toLook;
    for (const QString& raw : hosts) {
        const QString host = raw.trimmed().toLower();
        if (!validHost(host) || out.contains(host))
            continue;
        bool look = canLook && !m_lookupFailedRun.contains(host);
        if (const auto it = m_countries.constFind(host); it != m_countries.cend()) {
            out.insert(host, it->first == kCdn ? QString() : it->first);
            look = look && now - it->second >= qint64(kCountryDays) * 86400;
        }
        if (look)
            toLook << host;
    }
    toFront(m_lookupQueue, m_lookupQueued, toLook);
    pumpLookups();
    return out;
}

void Ed3kHosterCatalog::noteServer(const QString& site, const QString& server)
{
    const QString name = server.trimmed().toLower();
    const QString host = site.trimmed().toLower();
    const QString key = siteOf(host);
    if (!validServer(name) || !validHost(key))
        return;
    loadSites();
    QHash<QString, qint64>& known = m_sites[key].servers;
    const bool isNew = !known.contains(name);
    known.insert(name, QDateTime::currentSecsSinceEpoch());
    saveSites();
    if (!isNew)
        return;
    logHosterDebug([&]() -> QString { return QStringLiteral("hoster: %1 downloads from %2").arg(key, name); });
    // a better name than the site's: look again now
    loadCountries();
    QStringList again{host};
    if (key != host)
        again << key;
    for (const QString& h : std::as_const(again)) {
        if (const auto it = m_countries.find(h); it != m_countries.end())
            it->second = 0;
        m_lookupFailedRun.remove(h);
    }
    (void)countries(again);
}

QStringList Ed3kHosterCatalog::servers(const QString& host)
{
    loadSites();
    const QHash<QString, qint64> known = m_sites.value(siteOf(host.trimmed().toLower())).servers;
    QList<std::pair<qint64, QString>> list;
    for (const auto& [name, seen] : known.asKeyValueRange())
        list.append({seen, name});
    std::ranges::sort(list, std::greater{});
    QStringList out;
    for (const auto& p : std::as_const(list))
        out << p.second;
    return out;
}

void Ed3kHosterCatalog::noteAccount(const QString& host, const QString& user, QJsonObject status)
{
    const QString key = siteOf(host.trimmed().toLower());
    if (!validHost(key))
        return;
    loadSites();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QJsonObject& known = m_sites[key].accounts[user];
    const qint64 was = qint64(known.take(QStringLiteral("checked")).toDouble());
    // the same answer again: only its time, written now and then
    const bool same = known == status;
    status.insert(QStringLiteral("checked"), double(same && now - was < kAccountWriteS ? was : now));
    known = status;
    if (!same || now - was >= kAccountWriteS)
        saveSites();
}

QJsonObject Ed3kHosterCatalog::account(const QString& host, const QString& user)
{
    loadSites();
    return m_sites.value(siteOf(host.trimmed().toLower())).accounts.value(user);
}

void Ed3kHosterCatalog::keepAccounts(const QList<std::pair<QString, QString>>& logins)
{
    loadSites();
    QSet<QString> keep;
    for (const auto& [host, user] : logins)
        keep.insert(siteOf(host.trimmed().toLower()) + QLatin1Char('|') + user);
    bool changed = false;
    for (auto it = m_sites.begin(); it != m_sites.end(); ++it) {
        const QString site = it.key();
        changed |= it->accounts.removeIf([&](QHash<QString, QJsonObject>::iterator a) {
            return !keep.contains(site + QLatin1Char('|') + a.key());
        }) > 0;
    }
    if (changed)
        saveSites();
}

bool Ed3kHosterCatalog::behindCdn(const QHostAddress& address)
{
    return isCdnAddress(address);
}

bool Ed3kHosterCatalog::validHost(const QString& host)
{
    if (host.isEmpty() || host.size() > 253 || !host.contains(u'.') || host.startsWith(u'.') || host.endsWith(u'.')
        || host.startsWith(u'-') || host.contains(QLatin1StringView("..")))
        return false;
    for (const QChar c : host)
        if (!((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'.' || c == u'-'))
            return false;
    return true;
}

// ---- private ----

void Ed3kHosterCatalog::onTaskEvent(Ed3kResolver::TaskId task, const ResolverEvent& ev)
{
    using K = ResolverEvent::Kind;
    if (task != 0 && task == m_hostsTask) {
        if (!ev.isFinal())
            return;
        m_hostsTask = 0;
        if (ev.kind == K::AccountHosts && !ev.accountHosts.isEmpty()) {
            m_hostsAsked = true;
            if (ev.accountHosts != m_hosts) {
                m_hosts = ev.accountHosts;
                saveHosts();
            }
            logHosterDebug([&]() -> QString { return QStringLiteral("hoster: %1 account plugins").arg(m_hosts.size()); });
        } else if (ev.kind == K::Failed && ev.code == QLatin1StringView("unavailable") && m_resolver) {
            // no plugins or Java yet: the account dialog is a reason to fetch them
            m_waitAvailable = true;
            m_resolver->wantPlugins();
        } else {
            m_hostsAsked = true;   // an old sidecar without the op: the shipped lists do
        }
        emit accountHostsChanged(m_hosts);
        return;
    }
    if (ev.isFinal() && m_fetches.contains(task))
        finishFetch(task, ev);
}

void Ed3kHosterCatalog::finishFetch(Ed3kResolver::TaskId task, const ResolverEvent& ev)
{
    using K = ResolverEvent::Kind;
    const auto it = m_fetches.find(task);
    if (it == m_fetches.end())
        return;
    const QString host = it.value();
    m_fetches.erase(it);
    m_queued.remove(host);
    QByteArray png;
    if (ev.kind == K::Icon && !ev.image.isEmpty() && ev.image.size() <= kMaxIconBytes && ev.image.startsWith("\x89PNG")) {
        png = ev.image;
        QFile::remove(iconFile(host, true));
        BackgroundFileWriter::instance().write(iconFile(host, false), png, {});
    } else if (ev.kind == K::Failed && ev.code == QLatin1StringView("none")) {
        BackgroundFileWriter::instance().write(iconFile(host, true), QByteArray(), {});
        m_failedRun.insert(host);
    } else {
        m_failedRun.insert(host);
    }
    emit iconReady(host, png);
    pump();
}

void Ed3kHosterCatalog::onStateChanged()
{
    if (!m_resolver || !m_resolver->available())
        return;
    m_failedRun.clear();   // Java or the plugins came: ask those sites again
    if (!m_waitAvailable)
        return;
    m_waitAvailable = false;
    askHosts();
}

void Ed3kHosterCatalog::askHosts()
{
    if (!m_resolver)
        return;
    m_hostsTask = m_resolver->accountHosts();
}

void Ed3kHosterCatalog::pump()
{
    while (m_resolver && m_fetches.size() < kMaxFetches && !m_queue.empty()) {
        const QString host = m_queue.front();
        m_queue.pop_front();
        const auto task = m_resolver->favicon(host);
        if (task == 0) {
            m_queued.remove(host);
            m_failedRun.insert(host);
            continue;
        }
        m_fetches.insert(task, host);
        // JD tries every page and name with 10 s timeouts: minutes for a dead site
        QTimer::singleShot(kFetchTimeoutMs, this, [this, task] {
            if (!m_fetches.contains(task))
                return;
            if (m_resolver)
                m_resolver->cancel(task);
            ResolverEvent none;
            none.kind = ResolverEvent::Kind::Failed;
            none.code = QStringLiteral("none");
            finishFetch(task, none);
        });
    }
}

void Ed3kHosterCatalog::pumpLookups()
{
    while (m_lookups.size() < kMaxLookups && !m_lookupQueue.empty()) {
        const QString host = m_lookupQueue.front();
        m_lookupQueue.pop_front();
        m_lookups.insert(host, {});
        // where downloads really came from, then the link's own name
        QStringList names = servers(host).mid(0, kServerNames);
        names.removeAll(host);
        names << host;
        resolveStage(host, names);
    }
}

void Ed3kHosterCatalog::resolveStage(const QString& host, const QStringList& names)
{
    Lookup& l = m_lookups[host];
    l.names = names;
    l.results = QList<QList<QHostAddress>>(names.size());
    l.pending = int(names.size());
    for (int i = 0; i < names.size(); ++i)
        m_dns(names.at(i), this, [this, host, i](QList<QHostAddress> addresses) { onResolved(host, i, addresses); });
}

void Ed3kHosterCatalog::onResolved(const QString& host, int index, const QList<QHostAddress>& addresses)
{
    const auto it = m_lookups.find(host);
    if (it == m_lookups.end() || index >= it->results.size())
        return;
    it->results[index] = addresses;
    if (--it->pending > 0)
        return;
    // the first name (in the plugin's order) with an address outside a CDN
    for (const QList<QHostAddress>& list : std::as_const(it->results)) {
        for (const QHostAddress& a : list) {
            if (behindCdn(a)) {
                it->sawCdn = true;
                continue;
            }
            const QString cc = m_countryOf ? m_countryOf(a) : QString();
            if (!cc.isEmpty()) {
                finishLookup(host, cc.toUpper(), true);
                return;
            }
        }
    }
    if (!it->subs) {
        if (const QStringList subs = subsOf(host); !subs.isEmpty()) {
            it->subs = true;
            resolveStage(host, subs);
            return;
        }
    }
    // only a CDN: no country (unless the plugins' names are still to come); no address: again next run
    finishLookup(host, it->sawCdn ? QString(kCdn) : QString(), it->sawCdn && m_hostsAsked);
}

void Ed3kHosterCatalog::finishLookup(const QString& host, const QString& cc, bool known)
{
    m_lookups.remove(host);
    m_lookupQueued.remove(host);
    if (known) {
        m_countries.insert(host, {cc, QDateTime::currentSecsSinceEpoch()});
        saveCountries();
    } else if (cc != kCdn) {
        m_lookupFailedRun.insert(host);
    }
    logHosterDebug([&]() -> QString { return QStringLiteral("hoster: %1 server country %2").arg(host, !known ? QStringLiteral("?") : cc == kCdn ? QStringLiteral("none (CDN)") : cc); });
    emit countryReady(host, cc == kCdn ? QString() : cc);
    pumpLookups();
}

QStringList Ed3kHosterCatalog::subsOf(const QString& raw)
{
    loadHosts();
    const QString host = siteOf(raw);
    for (const AccountHost& h : std::as_const(m_hosts))
        if (h.host == host) {
            QStringList out;
            for (const QString& n : h.subs)
                if (validHost(n) && n != raw)
                    out << n;
            if (host != raw)
                out << host;   // a link's alias: the plugin's main name too
            return out;
        }
    return {};
}

QString Ed3kHosterCatalog::siteOf(const QString& host)
{
    loadHosts();
    const QString h = host.startsWith(QLatin1StringView("www.")) ? host.mid(4) : host;
    for (const AccountHost& a : std::as_const(m_hosts))
        if (a.host == h || a.subs.contains(h))
            return a.host;
    return h;
}

bool Ed3kHosterCatalog::validServer(const QString& name)
{
    return validHost(name) || (!name.isEmpty() && !QHostAddress(name).isNull());
}

void Ed3kHosterCatalog::loadSites()
{
    if (m_sitesLoaded)
        return;
    m_sitesLoaded = true;
    const auto raw = BackgroundFileWriter::readFile(QDir(dir()).filePath(QStringLiteral("sites.json")));
    if (!raw)
        return;
    const QJsonObject o = QJsonDocument::fromJson(*raw).object();
    for (auto site = o.begin(); site != o.end(); ++site) {
        if (!validHost(site.key()))
            continue;
        const QJsonObject entry = site.value().toObject();
        Site& s = m_sites[site.key()];
        const QJsonObject names = entry.value(QStringLiteral("servers")).toObject();
        for (auto n = names.begin(); n != names.end(); ++n)
            if (validServer(n.key()))
                s.servers.insert(n.key(), qint64(n.value().toDouble()));
        const QJsonObject accounts = entry.value(QStringLiteral("accounts")).toObject();
        for (auto a = accounts.begin(); a != accounts.end(); ++a)
            s.accounts.insert(a.key(), a.value().toObject());
    }
}

void Ed3kHosterCatalog::saveSites()
{
    if (m_sitesDirty)
        return;
    m_sitesDirty = true;
    QTimer::singleShot(2000, this, [this] { writeSites(); });
}

void Ed3kHosterCatalog::writeSites()
{
    if (!m_sitesDirty)
        return;
    m_sitesDirty = false;
    QJsonObject o;
    for (const auto& [site, entry] : m_sites.asKeyValueRange()) {
        QJsonObject e;
        if (!entry.servers.isEmpty()) {
            QJsonObject n;
            for (const auto& [name, seen] : entry.servers.asKeyValueRange())
                n.insert(name, double(seen));
            e.insert(QStringLiteral("servers"), n);
        }
        if (!entry.accounts.isEmpty()) {
            QJsonObject a;
            for (const auto& [user, st] : entry.accounts.asKeyValueRange())
                a.insert(user, st);
            e.insert(QStringLiteral("accounts"), a);
        }
        if (!e.isEmpty())
            o.insert(site, e);
    }
    BackgroundFileWriter::instance().write(QDir(dir()).filePath(QStringLiteral("sites.json")),
                                           QJsonDocument(o).toJson(QJsonDocument::Compact), {});
}

void Ed3kHosterCatalog::toFront(std::deque<QString>& queue, QSet<QString>& queued, const QStringList& hosts)
{
    // last first: hosts[0] ends up in front
    for (auto h = hosts.crbegin(); h != hosts.crend(); ++h) {
        if (queued.contains(*h)) {
            const auto at = std::ranges::find(queue, *h);
            if (at == queue.end())
                continue;   // running
            queue.erase(at);
        }
        queued.insert(*h);
        queue.push_front(*h);
    }
}

void Ed3kHosterCatalog::loadCountries()
{
    if (m_countriesLoaded)
        return;
    m_countriesLoaded = true;
    const auto raw = BackgroundFileWriter::readFile(QDir(dir()).filePath(QStringLiteral("countries.json")));
    if (!raw)
        return;
    const QJsonObject o = QJsonDocument::fromJson(*raw).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonArray a = it.value().toArray();
        const QString cc = a.at(0).toString();
        if (validHost(it.key()) && (cc == kCdn || (cc.size() == 2 && cc.at(0).isLetter() && cc.at(1).isLetter())))
            m_countries.insert(it.key(), {cc, qint64(a.at(1).toDouble())});
    }
}

void Ed3kHosterCatalog::saveCountries()
{
    // one write for a page of rows
    if (m_countriesDirty)
        return;
    m_countriesDirty = true;
    QTimer::singleShot(2000, this, [this] {
        m_countriesDirty = false;
        QJsonObject o;
        for (const auto& [host, v] : m_countries.asKeyValueRange())
            o.insert(host, QJsonArray{v.first, double(v.second)});
        BackgroundFileWriter::instance().write(QDir(dir()).filePath(QStringLiteral("countries.json")),
                                               QJsonDocument(o).toJson(QJsonDocument::Compact), {});
    });
}

void Ed3kHosterCatalog::loadHosts()
{
    if (m_hostsLoaded)
        return;
    m_hostsLoaded = true;
    const auto raw = BackgroundFileWriter::readFile(QDir(dir()).filePath(QStringLiteral("accounthosts.json")));
    if (!raw)
        return;
    for (const QJsonValue& v : QJsonDocument::fromJson(*raw).object().value(QStringLiteral("hosts")).toArray()) {
        const QJsonObject o = v.toObject();
        AccountHost h{o.value(QStringLiteral("host")).toString(), o.value(QStringLiteral("name")).toString(),
                      o.value(QStringLiteral("multi")).toBool(), o.value(QStringLiteral("solver")).toBool(), {}};
        for (const QJsonValue& sub : o.value(QStringLiteral("subs")).toArray())
            h.subs << sub.toString();
        if (validHost(h.host))
            m_hosts.append(h);
    }
}

void Ed3kHosterCatalog::saveHosts()
{
    QJsonArray list;
    for (const AccountHost& h : std::as_const(m_hosts)) {
        QJsonObject o{{QStringLiteral("host"), h.host}};
        if (!h.name.isEmpty())
            o.insert(QStringLiteral("name"), h.name);
        if (h.multi)
            o.insert(QStringLiteral("multi"), true);
        if (h.solver)
            o.insert(QStringLiteral("solver"), true);
        if (!h.subs.isEmpty())
            o.insert(QStringLiteral("subs"), QJsonArray::fromStringList(h.subs));
        list.append(o);
    }
    const QString rev = m_resolver ? m_resolver->engineVersion() : QString();
    BackgroundFileWriter::instance().write(
        QDir(dir()).filePath(QStringLiteral("accounthosts.json")),
        QJsonDocument(QJsonObject{{QStringLiteral("jdRevision"), rev}, {QStringLiteral("hosts"), list}}).toJson(QJsonDocument::Compact),
        {});
}

QString Ed3kHosterCatalog::dir() const
{
    return QDir(stateDir(m_configDir)).filePath(QStringLiteral("hoster"));
}

QString Ed3kHosterCatalog::iconFile(const QString& host, bool none) const
{
    return QDir(dir()).filePath(QStringLiteral("favicons/%1.%2").arg(host, none ? QStringLiteral("none") : QStringLiteral("png")));
}

} // namespace eMule::ed3k
