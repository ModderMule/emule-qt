/// @file Ed3kHosterCatalog.cpp
/// @brief Account plugins and favicons of the resolver — implementation.

#include "Ed3kHosterCatalog.h"

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

namespace eMule::ed3k {

namespace {

constexpr qsizetype kMaxIconBytes = 256 * 1024;

} // namespace

Ed3kHosterCatalog::Ed3kHosterCatalog(const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_configDir(configDir)
{
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
        if (fetch && !m_queued.contains(host)) {
            m_queued.insert(host);
            m_queue.push_back(host);
        }
    }
    pump();
    return out;
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
    const auto it = m_fetches.find(task);
    if (it == m_fetches.end() || !ev.isFinal())
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
    }
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
                      o.value(QStringLiteral("multi")).toBool(), o.value(QStringLiteral("solver")).toBool()};
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
