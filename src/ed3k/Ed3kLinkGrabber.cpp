/// @file Ed3kLinkGrabber.cpp
/// @brief The Link Grabber — implementation.

#include "Ed3kLinkGrabber.h"

#include "Ed3kHosterJoin.h"
#include "Ed3kStore.h"
#include "utils/BackgroundFileWriter.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace eMule::ed3k {

namespace {

constexpr int kEmitMs = 200;
constexpr int kSaveMs = 2000;
constexpr qsizetype kMaxName = 255;

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QStringList cleanPasswords(const QStringList& in)
{
    QStringList out;
    for (const QString& pw : in)
        if (const QString t = pw.trimmed(); !t.isEmpty() && !out.contains(t))
            out << t.left(kMaxName);
    return out;
}

QStringList mergePasswords(QStringList a, const QStringList& b)
{
    for (const QString& pw : b)
        if (!a.contains(pw))
            a << pw;
    return a;
}

QString cleanName(const QString& name)
{
    QString n = name.simplified().left(kMaxName);
    // a folder under incoming: no separators
    n.replace(QLatin1Char('/'), QLatin1Char('_'));
    n.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return n;
}

QString urlFileName(const QString& url)
{
    return QUrl(url).fileName();
}

} // namespace

Ed3kLinkGrabber::Ed3kLinkGrabber(const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_configDir(configDir)
{
    m_emit.setSingleShot(true);
    m_emit.setInterval(kEmitMs);
    // also with nothing in it: the crawling count moved
    connect(&m_emit, &QTimer::timeout, this, [this] { emit changed(std::exchange(m_delta, {})); });
    m_save.setSingleShot(true);
    m_save.setInterval(kSaveMs);
    connect(&m_save, &QTimer::timeout, this, &Ed3kLinkGrabber::save);
}

Ed3kLinkGrabber::~Ed3kLinkGrabber()
{
    if (m_save.isActive())
        save();
}

void Ed3kLinkGrabber::setResolver(Ed3kResolver* resolver)
{
    if (m_resolver)
        disconnect(m_resolver, nullptr, this, nullptr);
    m_resolver = resolver;
    m_crawls.clear();
    if (!m_resolver)
        return;
    connect(m_resolver, &Ed3kResolver::taskEvent, this, &Ed3kLinkGrabber::onTaskEvent);
    // plugins fetched or Java found: the waiting crawls go out
    connect(m_resolver, &Ed3kResolver::stateChanged, this, &Ed3kLinkGrabber::pump);
}

void Ed3kLinkGrabber::setEnabled(bool on, bool all)
{
    m_on = on;
    m_all = all;
}

bool Ed3kLinkGrabber::canDeepCrawl() const
{
    return m_resolver && m_resolver->canDeepCrawl();
}

quint64 Ed3kLinkGrabber::grab(const QString& text, const QString& source, bool deep, const QString& package,
                              const QStringList& passwords, Mode mode, bool clipboard)
{
    if (!m_resolver || text.size() > kMaxTextBytes)
        return 0;
    QStringList urls = Ed3kHosterJoin::linksIn(text);
    // already in the list: nothing to crawl (a folder link is crawled again: it may have grown)
    if (mode == Mode::List)
        urls.removeIf([this](const QString& u) { return m_byUrl.contains(u); });
    if (urls.isEmpty() || m_queue.size() + urls.size() > kMaxQueuedCrawls)
        return 0;
    const quint64 token = m_nextToken++;
    Grab g;
    g.source = source.left(1024);
    g.package = cleanName(package);
    g.passwords = cleanPasswords(passwords);
    g.mode = mode;
    g.open = int(urls.size());
    m_grabs[token] = g;
    for (const QString& u : std::as_const(urls))
        m_queue.append(Crawl{token, u, deep, clipboard});
    logHosterDebug([&]() -> QString {
        return QStringLiteral("link grabber: %1 link(s) to crawl from %2%3")
            .arg(urls.size())
            .arg(source.isEmpty() ? QStringLiteral("?") : source, deep ? QStringLiteral(" (deep)") : QString());
    });
    if (!m_resolver->available())
        m_resolver->wantPlugins();   // none shipped: fetched on the first link
    pump();
    m_emit.start();   // the crawling count
    return token;
}

int Ed3kLinkGrabber::add(const QList<ResolverLink>& links, const QString& source, const QString& package,
                         const QStringList& passwords)
{
    const QString given = cleanName(package);
    const QStringList givenPw = cleanPasswords(passwords);
    QStringList urls;
    for (const ResolverLink& l : links) {
        if (l.url.isEmpty() || m_byUrl.contains(l.url))
            continue;
        if (int(m_links.size()) >= kMaxLinks) {
            logHosterWarning(QStringLiteral("link grabber: the list is full (%1 links)").arg(kMaxLinks));
            break;
        }
        Link k;
        k.id = m_nextLink++;
        k.url = l.url;
        k.name = cleanName(l.name);
        k.host = l.host;
        k.size = l.size;
        k.dupe = m_backend.isItem && m_backend.isItem(l.url);
        QString pkg = cleanName(l.package);
        if (pkg.isEmpty())
            pkg = given;
        if (pkg.isEmpty()) {
            const QString n = !k.name.isEmpty() ? k.name : urlFileName(k.url);
            pkg = packageNameFor(n);
            k.autoNamed = k.name.isEmpty();
        }
        if (pkg.isEmpty())
            pkg = QUrl(k.url).host();
        k.packageId = packageFor(pkg, source, mergePasswords(cleanPasswords(l.passwords), givenPw));
        m_byUrl.insert(k.url, k.id);
        m_links[k.id] = k;
        touchLink(k.id);
        urls << k.url;
    }
    startChecks(urls);
    if (!urls.isEmpty())
        logHoster(QStringLiteral("link grabber: %1 link(s) added from %2")
                      .arg(urls.size())
                      .arg(source.isEmpty() ? QStringLiteral("?") : source));
    return int(urls.size());
}

void Ed3kLinkGrabber::onLinkChecked(const QString& url, int status, const QString& host, const QString& name,
                                    std::uint64_t size)
{
    const auto byUrl = m_byUrl.constFind(url);
    if (byUrl == m_byUrl.cend())
        return;
    const auto it = m_links.find(*byUrl);
    if (it == m_links.end())
        return;
    Link& k = it->second;
    k.status = Status(std::clamp(status, 0, 3));
    if (!host.isEmpty())
        k.host = host;
    if (!name.isEmpty())
        k.name = cleanName(name);
    if (size > 0)
        k.size = size;
    touchLink(k.id);
    // no name when added: it joins the package its name says
    if (k.autoNamed && !k.name.isEmpty()) {
        k.autoNamed = false;
        const QString want = packageNameFor(k.name);
        const auto p = m_packages.find(k.packageId);
        if (!want.isEmpty() && p != m_packages.end() && p->second.name != want) {
            const Package old = p->second;
            k.packageId = packageFor(want, old.source, old.passwords);
            touchPackage(old.id);
            touchPackage(k.packageId);
            dropEmptyPackages();
        }
    }
}

bool Ed3kLinkGrabber::editPackages(const QList<quint32>& ids, const QString& name,
                                   const std::optional<QStringList>& passwords)
{
    const QString n = cleanName(name);
    bool any = false;
    for (const quint32 id : ids) {
        const auto it = m_packages.find(id);
        if (it == m_packages.end())
            continue;
        if (!n.isEmpty())
            it->second.name = n;
        if (passwords)
            it->second.passwords = cleanPasswords(*passwords);
        touchPackage(id);
        any = true;
    }
    return any;
}

void Ed3kLinkGrabber::setLinksEnabled(const QList<quint32>& linkIds, bool on)
{
    for (const quint32 id : linkIds)
        if (const auto it = m_links.find(id); it != m_links.end() && it->second.enabled != on) {
            it->second.enabled = on;
            touchLink(id);
        }
}

quint32 Ed3kLinkGrabber::moveLinks(const QList<quint32>& linkIds, quint32 packageId, const QString& newName)
{
    quint32 target = m_packages.contains(packageId) ? packageId : 0;
    if (target == 0) {
        const QString n = cleanName(newName);
        if (n.isEmpty())
            return 0;
        // a new package: the passwords of the packages its links leave
        QStringList pw;
        QString source;
        for (const quint32 id : linkIds)
            if (const Link* k = link(id))
                if (const Package* p = package(k->packageId)) {
                    pw = mergePasswords(pw, p->passwords);
                    if (source.isEmpty())
                        source = p->source;
                }
        Package p;
        p.id = m_nextPackage++;
        p.name = n;
        p.passwords = pw;
        p.source = source;
        p.added = nowMs();
        m_packages[p.id] = p;
        target = p.id;
        touchPackage(target);
    }
    for (const quint32 id : linkIds) {
        const auto it = m_links.find(id);
        if (it == m_links.end() || it->second.packageId == target)
            continue;
        touchPackage(it->second.packageId);
        it->second.packageId = target;
        it->second.autoNamed = false;
        touchLink(id);
    }
    touchPackage(target);
    dropEmptyPackages();
    return m_packages.contains(target) ? target : 0;
}

void Ed3kLinkGrabber::splitByHost(const QList<quint32>& packageIds)
{
    for (const quint32 pid : packageIds) {
        const auto pit = m_packages.find(pid);
        if (pit == m_packages.end())
            continue;
        const Package base = pit->second;
        // the first hoster stays; each other one gets "<name> (<host>)"
        QStringList hosts;
        QHash<QString, QList<quint32>> byHost;
        for (const auto& [id, k] : m_links) {
            if (k.packageId != pid)
                continue;
            const QString h = k.host.isEmpty() ? QUrl(k.url).host() : k.host;
            if (!hosts.contains(h))
                hosts << h;
            byHost[h] << id;
        }
        for (qsizetype i = 1; i < hosts.size(); ++i) {
            Package p = base;
            p.id = m_nextPackage++;
            p.name = cleanName(QStringLiteral("%1 (%2)").arg(base.name, hosts[i]));
            p.added = nowMs();
            m_packages[p.id] = p;
            for (const quint32 id : byHost.value(hosts[i])) {
                m_links[id].packageId = p.id;
                touchLink(id);
            }
            touchPackage(p.id);
        }
        touchPackage(pid);
    }
}

void Ed3kLinkGrabber::remove(const QList<quint32>& linkIds, const QList<quint32>& packageIds)
{
    const QSet<quint32> pkgs(packageIds.cbegin(), packageIds.cend());
    QSet<quint32> drop(linkIds.cbegin(), linkIds.cend());
    for (const auto& [id, k] : m_links)
        if (pkgs.contains(k.packageId))
            drop.insert(id);
    for (const quint32 id : std::as_const(drop)) {
        const auto it = m_links.find(id);
        if (it == m_links.end())
            continue;
        touchPackage(it->second.packageId);
        m_byUrl.remove(it->second.url);
        m_links.erase(it);
        m_delta.links.remove(id);
        m_delta.removedLinks.insert(id);
    }
    for (const quint32 pid : pkgs)
        if (m_packages.erase(pid)) {
            m_delta.packages.remove(pid);
            m_delta.removedPackages.insert(pid);
        }
    dropEmptyPackages();
    m_emit.start();
    m_save.start();
}

bool Ed3kLinkGrabber::cleanup(const QString& mode)
{
    std::function<bool(const Link&)> pick;
    if (mode == QLatin1StringView("offline"))
        pick = [](const Link& k) { return k.status == Status::Offline; };
    else if (mode == QLatin1StringView("disabled"))
        pick = [](const Link& k) { return !k.enabled; };
    else if (mode == QLatin1StringView("dupes"))
        pick = [](const Link& k) { return k.dupe; };
    else if (mode == QLatin1StringView("all"))
        pick = [](const Link&) { return true; };
    else
        return false;
    QList<quint32> ids;
    for (const auto& [id, k] : m_links)
        if (pick(k))
            ids << id;
    remove(ids, {});
    if (mode == QLatin1StringView("all"))
        abortCrawls();
    return true;
}

void Ed3kLinkGrabber::recheck(const QList<quint32>& linkIds, const QList<quint32>& packageIds)
{
    const QSet<quint32> pkgs(packageIds.cbegin(), packageIds.cend());
    QSet<quint32> ids(linkIds.cbegin(), linkIds.cend());
    for (const auto& [id, k] : m_links)
        if (pkgs.contains(k.packageId))
            ids.insert(id);
    QStringList urls;
    for (const quint32 id : std::as_const(ids))
        if (const auto it = m_links.find(id); it != m_links.end()) {
            it->second.dupe = m_backend.isItem && m_backend.isItem(it->second.url);
            urls << it->second.url;
        }
    startChecks(urls);
}

void Ed3kLinkGrabber::abortCrawls()
{
    if (m_resolver)
        for (const auto& [task, t] : m_crawls)
            m_resolver->cancel(task);
    m_crawls.clear();
    m_queue.clear();
    // an asking grab ends with what it has
    for (auto& [token, g] : m_grabs)
        if (g.mode == Mode::Ask)
            emit grabbed(token, g.source, g.found, g.found.isEmpty() ? QStringLiteral("cancelled") : QString());
    m_grabs.clear();
    m_emit.start();
}

QList<quint32> Ed3kLinkGrabber::confirm(const QList<quint32>& packageIds, const QList<quint32>& linkIds,
                                        bool includeOffline)
{
    QList<quint32> added;
    if (!m_backend.add)
        return added;
    const QSet<quint32> pkgs(packageIds.cbegin(), packageIds.cend());
    const QSet<quint32> picked(linkIds.cbegin(), linkIds.cend());
    // per package, in the list's order
    std::map<quint32, QList<quint32>> byPackage;
    for (const auto& [id, k] : m_links) {
        if (!pkgs.contains(k.packageId) && !picked.contains(id))
            continue;
        if (!k.enabled || k.dupe || (!includeOffline && k.status == Status::Offline))
            continue;
        byPackage[k.packageId] << id;
    }
    QList<quint32> done;
    for (const auto& [pid, ids] : byPackage) {
        const Package* p = package(pid);
        QList<QStringList> files;
        for (const quint32 id : ids)
            files.append(QStringList{m_links[id].url});
        added += m_backend.add(files, p ? p->name : QString(), p ? p->passwords : QStringList{});
        done += ids;
        logHoster(QStringLiteral("link grabber: %1 link(s) of %2 start downloading")
                      .arg(ids.size())
                      .arg(p ? p->name : QStringLiteral("?")));
    }
    remove(done, {});
    return added;
}

QList<Ed3kLinkGrabber::Package> Ed3kLinkGrabber::packages() const
{
    QList<Package> out;
    out.reserve(qsizetype(m_packages.size()));
    for (const auto& [id, p] : m_packages)
        out << p;
    return out;
}

QList<Ed3kLinkGrabber::Link> Ed3kLinkGrabber::links() const
{
    QList<Link> out;
    out.reserve(qsizetype(m_links.size()));
    for (const auto& [id, k] : m_links)
        out << k;
    return out;
}

const Ed3kLinkGrabber::Package* Ed3kLinkGrabber::package(quint32 id) const
{
    const auto it = m_packages.find(id);
    return it == m_packages.end() ? nullptr : &it->second;
}

const Ed3kLinkGrabber::Link* Ed3kLinkGrabber::link(quint32 id) const
{
    const auto it = m_links.find(id);
    return it == m_links.end() ? nullptr : &it->second;
}

QString Ed3kLinkGrabber::packageNameFor(const QString& fileName)
{
    QString n = fileName.trimmed();
    if (n.isEmpty())
        return {};
    // volumes of one archive set: the set's name (JD's archive packagizer)
    static const QList<QRegularExpression> volume{
        QRegularExpression(QStringLiteral(R"(\.part\d+\.rar$)"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"(\.(rar|zip|7z|tar|gz|bz2|xz)\.\d{2,4}$)"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"(\.[rz]\d{2,3}$)"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"(\.\d{3}$)")),
    };
    for (const QRegularExpression& re : volume)
        if (const auto m = re.match(n); m.hasMatch())
            return cleanName(n.left(m.capturedStart()));
    // a single file: its name without the extension
    if (const qsizetype dot = n.lastIndexOf(QLatin1Char('.')); dot > 0 && n.size() - dot <= 6)
        n.truncate(dot);
    return cleanName(n);
}

void Ed3kLinkGrabber::load()
{
    if (m_loaded)
        return;
    m_loaded = true;
    const auto raw = BackgroundFileWriter::readFile(file());   // a queued save is newer
    if (!raw)
        return;
    const QJsonObject root = QJsonDocument::fromJson(*raw).object();
    for (const QJsonValue& v : root.value(QStringLiteral("packages")).toArray()) {
        const QJsonObject o = v.toObject();
        Package p;
        p.id = quint32(o.value(QStringLiteral("id")).toInteger());
        p.name = cleanName(o.value(QStringLiteral("name")).toString());
        for (const QJsonValue& pw : o.value(QStringLiteral("passwords")).toArray())
            p.passwords << pw.toString();
        p.source = o.value(QStringLiteral("source")).toString();
        p.added = o.value(QStringLiteral("added")).toInteger();
        if (p.id == 0 || m_packages.contains(p.id))
            continue;
        m_packages[p.id] = p;
        m_nextPackage = std::max(m_nextPackage, p.id + 1);
    }
    for (const QJsonValue& v : root.value(QStringLiteral("links")).toArray()) {
        const QJsonObject o = v.toObject();
        Link k;
        k.id = quint32(o.value(QStringLiteral("id")).toInteger());
        k.packageId = quint32(o.value(QStringLiteral("pkg")).toInteger());
        k.url = o.value(QStringLiteral("url")).toString();
        k.name = o.value(QStringLiteral("name")).toString();
        k.host = o.value(QStringLiteral("host")).toString();
        k.size = std::uint64_t(std::max<qint64>(0, o.value(QStringLiteral("size")).toInteger()));
        const int st = o.value(QStringLiteral("status")).toInt(int(Status::Unknown));
        // a check that ran when the core stopped: not known
        k.status = st == int(Status::Online) || st == int(Status::Offline) ? Status(st) : Status::Unknown;
        k.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        k.autoNamed = o.value(QStringLiteral("autoNamed")).toBool(false);
        if (k.id == 0 || k.url.isEmpty() || !m_packages.contains(k.packageId) || m_byUrl.contains(k.url)
            || int(m_links.size()) >= kMaxLinks)
            continue;
        m_links[k.id] = k;
        m_byUrl.insert(k.url, k.id);
        m_nextLink = std::max(m_nextLink, k.id + 1);
    }
    dropEmptyPackages();
    touchAll();
    m_save.stop();   // nothing new to write
}

void Ed3kLinkGrabber::save()
{
    m_save.stop();
    QJsonArray pkgs;
    for (const auto& [id, p] : m_packages)
        pkgs.append(QJsonObject{{QStringLiteral("id"), qint64(id)},
                                {QStringLiteral("name"), p.name},
                                {QStringLiteral("passwords"), QJsonArray::fromStringList(p.passwords)},
                                {QStringLiteral("source"), p.source},
                                {QStringLiteral("added"), p.added}});
    QJsonArray links;
    for (const auto& [id, k] : m_links) {
        QJsonObject o{{QStringLiteral("id"), qint64(id)},
                      {QStringLiteral("pkg"), qint64(k.packageId)},
                      {QStringLiteral("url"), k.url},
                      {QStringLiteral("name"), k.name},
                      {QStringLiteral("host"), k.host},
                      {QStringLiteral("size"), qint64(k.size)},
                      {QStringLiteral("status"), int(k.status)},
                      {QStringLiteral("enabled"), k.enabled}};
        if (k.autoNamed)
            o.insert(QStringLiteral("autoNamed"), true);
        links.append(o);
    }
    // none kept and no file yet: create nothing under config/ed3k (#105)
    auto& writer = BackgroundFileWriter::instance();
    if (links.isEmpty() && pkgs.isEmpty() && !QFileInfo::exists(file()) && !writer.pending(file()))
        return;
    // off the main thread (report 43); the writer logs a failed write
    writer.write(file(),
                 QJsonDocument(QJsonObject{{QStringLiteral("packages"), pkgs}, {QStringLiteral("links"), links}})
                     .toJson(QJsonDocument::Indented),
                 {});
}

// ---- private ----

void Ed3kLinkGrabber::onTaskEvent(Ed3kResolver::TaskId task, const ResolverEvent& ev)
{
    const auto it = m_crawls.find(task);
    if (it == m_crawls.end() || !ev.isFinal())
        return;
    const Task t = it->second;
    m_crawls.erase(it);
    using K = ResolverEvent::Kind;
    if (t.kind == TaskKind::Match) {
        // the clipboard: only links a plugin takes are crawled
        if (ev.kind != K::Matched || ev.host.isEmpty()) {
            crawlDone(t.crawl.token, {}, QStringLiteral("noPlugin"));
        } else if (m_resolver) {
            const auto x = t.crawl.deep ? m_resolver->expandDeep(t.crawl.url) : m_resolver->expand(t.crawl.url);
            m_crawls[x] = Task{TaskKind::Expand, t.crawl};
            return;
        }
    } else if (ev.kind == K::Links) {
        crawlDone(t.crawl.token, ev.links, {});
    } else {
        crawlDone(t.crawl.token, {}, ev.kind == K::Gone ? QStringLiteral("gone") : ev.code);
    }
    pump();
}

void Ed3kLinkGrabber::pump()
{
    if (!m_resolver || !m_resolver->available())
        return;
    while (int(m_crawls.size()) < kMaxCrawls && !m_queue.isEmpty()) {
        const Crawl c = m_queue.takeFirst();
        if (!m_grabs.contains(c.token))
            continue;
        if (c.clipboard)
            m_crawls[m_resolver->match(c.url)] = Task{TaskKind::Match, c};
        else
            m_crawls[c.deep ? m_resolver->expandDeep(c.url) : m_resolver->expand(c.url)] = Task{TaskKind::Expand, c};
    }
    m_emit.start();
}

void Ed3kLinkGrabber::crawlDone(quint64 token, const QList<ResolverLink>& links, const QString& code)
{
    const auto it = m_grabs.find(token);
    if (it == m_grabs.end())
        return;
    Grab& g = it->second;
    --g.open;
    if (!code.isEmpty())
        g.code = code;
    if (g.mode == Mode::List)
        add(links, g.source, g.package, g.passwords);
    else
        for (const ResolverLink& l : links)
            if (std::none_of(g.found.cbegin(), g.found.cend(), [&l](const ResolverLink& f) { return f.url == l.url; })) {
                ResolverLink x = l;
                if (x.package.isEmpty())
                    x.package = g.package;
                x.passwords = mergePasswords(cleanPasswords(x.passwords), g.passwords);
                g.found << x;
            }
    if (g.open > 0)
        return;
    const Grab done = g;
    m_grabs.erase(it);
    if (done.mode == Mode::Ask)
        emit grabbed(token, done.source, done.found, done.found.isEmpty() ? done.code : QString());
    m_emit.start();
}

quint32 Ed3kLinkGrabber::packageFor(const QString& name, const QString& source, const QStringList& passwords)
{
    for (auto& [id, p] : m_packages)
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) {
            if (const QStringList pw = mergePasswords(p.passwords, passwords); pw != p.passwords) {
                p.passwords = pw;
                touchPackage(id);
            }
            return id;
        }
    Package p;
    p.id = m_nextPackage++;
    p.name = name;
    p.passwords = passwords;
    p.source = source.left(1024);
    p.added = nowMs();
    m_packages[p.id] = p;
    touchPackage(p.id);
    return p.id;
}

void Ed3kLinkGrabber::dropEmptyPackages()
{
    QSet<quint32> used;
    for (const auto& [id, k] : m_links)
        used.insert(k.packageId);
    for (auto it = m_packages.begin(); it != m_packages.end();) {
        if (used.contains(it->first)) {
            ++it;
            continue;
        }
        m_delta.packages.remove(it->first);
        m_delta.removedPackages.insert(it->first);
        it = m_packages.erase(it);
        m_emit.start();
        m_save.start();
    }
}

void Ed3kLinkGrabber::touchLink(quint32 id)
{
    m_delta.links.insert(id);
    if (const Link* k = link(id))
        m_delta.packages.insert(k->packageId);   // its sums
    m_emit.start();
    m_save.start();
}

void Ed3kLinkGrabber::touchPackage(quint32 id)
{
    if (!m_packages.contains(id))
        return;
    m_delta.packages.insert(id);
    m_emit.start();
    m_save.start();
}

void Ed3kLinkGrabber::touchAll()
{
    m_delta = {};
    m_delta.all = true;
    m_emit.start();
    m_save.start();
}

void Ed3kLinkGrabber::startChecks(const QStringList& urls)
{
    if (urls.isEmpty())
        return;
    // before the check: a known result comes back inside it
    for (const QString& u : urls)
        if (const auto id = m_byUrl.value(u); id != 0) {
            m_links[id].status = Status::Checking;
            touchLink(id);
        }
    if (m_backend.check && m_backend.check(urls))
        return;
    for (const QString& u : urls)
        if (const auto id = m_byUrl.value(u); id != 0)
            m_links[id].status = Status::Unknown;
}

QString Ed3kLinkGrabber::file() const
{
    return QDir(stateDir(m_configDir)).filePath(QStringLiteral("hoster/linkgrabber.json"));
}

} // namespace eMule::ed3k
