#include "IndexerSearchList.h"

#include "IndexerCapsStore.h"
#include "IndexerClient.h"
#include "IndexerSearch.h"

#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <vector>

namespace eMule::indexer {

IndexerSearchList* theIndexerSearchList = nullptr;

namespace {

/// A restored tab older than this can no longer be grabbed.
constexpr qint64 kGrabCacheMaxAgeSecs = 30 * 24 * 3600;
constexpr qsizetype kGrabCacheMaxEntries = 5000;

} // namespace

IndexerSearchList::IndexerSearchList(QObject* parent)
    : QObject(parent)
    , m_client(new IndexerClient(this))
{
    applyPreferences();
    loadGrabCache();
}

IndexerSearchList::~IndexerSearchList()
{
    // Abort first, and explicitly. m_client is a child, so it would otherwise be
    // destroyed by ~QObject — *after* this class's own members are gone — and its
    // destructor aborts the replies, whose handlers reach back in here. Doing it
    // now means every callback that is going to run has run while everything it
    // touches is still alive.
    m_client->abortAll();
    clearSearches();
    flushGrabCache();
}

void IndexerSearchList::applyPreferences()
{
    m_accounts = thePrefs.indexers();
    m_maxPages = thePrefs.indexerMaxPages();
    m_resultLimit = thePrefs.indexerResultLimit();
    m_capsRefreshDays = thePrefs.indexerCapsRefreshDays();

    // Forget the caps of accounts that are gone, so a new account reusing a name
    // does not inherit a stranger's category tree.
    QHash<QString, IndexerCaps> kept;
    for (const auto& account : m_accounts) {
        const QString key = account.key();
        if (const auto it = m_caps.constFind(key); it != m_caps.constEnd()) {
            kept.insert(key, *it);
            continue;
        }

        IndexerCaps cached;
        if (IndexerCapsStore::load(account, cached))
            kept.insert(key, cached);
    }
    m_caps = kept;

    // Refresh in the background. A stale or missing cache never blocks a search:
    // an ungated query is what every newznab implementation accepts anyway, so
    // the cost of not knowing is a greyed-out field the user could have used.
    for (const auto& account : m_accounts) {
        if (!account.enabled)
            continue;
        const auto it = m_caps.constFind(account.key());
        const bool stale = it == m_caps.constEnd()
                           || IndexerCapsStore::isStale(*it, m_capsRefreshDays);
        if (!stale)
            continue;

        probeCaps(account, [](bool, const IndexerCaps&, const QString&) {});
    }
}

const IndexerCaps* IndexerSearchList::capsFor(const QString& name) const
{
    const auto it = m_caps.constFind(name.trimmed().toCaseFolded());
    return it == m_caps.constEnd() ? nullptr : &*it;
}

void IndexerSearchList::probeCaps(const IndexerConfig& config, ProbeCallback done)
{
    QPointer<IndexerSearchList> self(this);
    m_client->probeCaps(config, [self, config, done = std::move(done)](
                                    bool ok, const IndexerCaps& caps, const QString& error) {
        if (!self)
            return;
        if (ok) {
            self->m_caps.insert(config.key(), caps);
            IndexerCapsStore::save(config, caps);
        } else {
            logWarning(QStringLiteral("Indexers: could not read capabilities for %1 — %2")
                           .arg(config.displayName(), error));
        }
        if (done)
            done(ok, caps, error);
    });
}

quint32 IndexerSearchList::startSearch(const IndexerQuery& query, IndexerKind wanted,
                                       const QStringList& onlyNames, QString& error)
{
    error.clear();

    QList<IndexerConfig> chosen;
    for (const auto& account : m_accounts) {
        if (!account.enabled)
            continue;
        if (wanted == IndexerKind::Newznab && !account.servesUsenet())
            continue;
        if (wanted == IndexerKind::Torznab && !account.servesTorrents())
            continue;
        if (!onlyNames.isEmpty() && !onlyNames.contains(account.name, Qt::CaseInsensitive))
            continue;
        chosen.append(account);
    }

    if (chosen.isEmpty()) {
        // Saying so beats an empty result list, which reads as "nothing matched"
        // when the truth is "nothing was asked".
        error = m_accounts.isEmpty()
                    ? tr("No search indexer is configured. Add one under Options › Indexers.")
                    : tr("No enabled indexer can answer this search.");
        return 0;
    }

    IndexerQuery effective = query;
    if (effective.limit <= 0)
        effective.limit = m_resultLimit;

    const quint32 id = m_nextSearchId++;
    auto* search = new IndexerSearch(id, m_client, effective, chosen, m_maxPages, this);

    connect(search, &IndexerSearch::resultsReady, this, &IndexerSearchList::resultsReady);
    connect(search, &IndexerSearch::resultsReady, this,
            [this](quint32, const QList<IndexerResult>& rows) { rememberGrabs(rows); });
    connect(search, &IndexerSearch::progress, this, &IndexerSearchList::searchProgress);
    connect(search, &IndexerSearch::finished, this, &IndexerSearchList::searchFinished);

    m_searches.insert(id, search);
    search->start(m_caps);
    if (theApp.statistics)
        ++theApp.statistics->indexerSession().searches;
    return id;
}

bool IndexerSearchList::stopSearch(quint32 searchId)
{
    const auto it = m_searches.constFind(searchId);
    if (it == m_searches.constEnd())
        return false;
    (*it)->stop();
    return true;
}

bool IndexerSearchList::removeSearch(quint32 searchId)
{
    const auto it = m_searches.find(searchId);
    if (it == m_searches.end())
        return false;

    IndexerSearch* search = *it;
    m_searches.erase(it);
    search->stop();
    // deleteLater, not delete: removeSearch can be reached from a slot connected
    // to the search's own finished() signal.
    search->deleteLater();
    return true;
}

void IndexerSearchList::clearSearches()
{
    const auto searches = m_searches;
    m_searches.clear();
    for (IndexerSearch* search : searches) {
        search->stop();
        search->deleteLater();
    }
}

const IndexerSearch* IndexerSearchList::search(quint32 searchId) const
{
    const auto it = m_searches.constFind(searchId);
    return it == m_searches.constEnd() ? nullptr : *it;
}

void IndexerSearchList::grab(quint32 searchId, const QString& resultId, GrabCallback done)
{
    const auto it = m_searches.constFind(searchId);
    if (it != m_searches.constEnd()) {
        const IndexerSearch* search = *it;
        if (const IndexerResult* result = search->find(resultId)) {
            const IndexerConfig* account = search->accountFor(*result);
            if (!account) {
                done(false, {}, {}, {},
                     tr("The indexer \"%1\" is no longer configured.").arg(result->indexerName));
                return;
            }
            fetchGrab(*account, result->downloadUrl, result->title, result->password, std::move(done));
            return;
        }
    }

    // search gone (restored tab) or row dropped: the cache still knows the URL
    const auto cached = m_grabCache.constFind(resultId);
    if (cached == m_grabCache.constEnd()) {
        done(false, {}, {}, {},
             it == m_searches.constEnd() ? tr("That search is no longer open.")
                                         : tr("That result is no longer in the list."));
        return;
    }
    const IndexerConfig* account = accountNamed(cached->indexerName);
    if (!account) {
        done(false, {}, {}, {},
             tr("The indexer \"%1\" is no longer configured.").arg(cached->indexerName));
        return;
    }
    fetchGrab(*account, cached->downloadUrl, cached->title, cached->password, std::move(done));
}

void IndexerSearchList::flushGrabCache()
{
    if (m_grabCacheTimer)
        m_grabCacheTimer->stop();
    if (!m_grabCacheDirty)
        return;
    m_grabCacheDirty = false;

    pruneGrabCache();
    QJsonArray arr;
    for (auto it = m_grabCache.cbegin(); it != m_grabCache.cend(); ++it) {
        arr.append(QJsonObject{
            {QStringLiteral("id"),       it.key()},
            {QStringLiteral("indexer"),  it->indexerName},
            {QStringLiteral("title"),    it->title},
            {QStringLiteral("password"), it->password},
            {QStringLiteral("url"),      it->downloadUrl.toString(QUrl::FullyEncoded)},
            {QStringLiteral("savedAt"),  it->savedAt},
        });
    }

    QSaveFile file(grabCachePath());
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                                {QStringLiteral("grabs"), arr}})
                          .toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) {
        logWarning(QStringLiteral("Indexers: could not store the grab cache: %1").arg(file.errorString()));
    }
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void IndexerSearchList::rememberGrabs(const QList<IndexerResult>& rows)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const auto& row : rows) {
        if (row.id.isEmpty() || !row.downloadUrl.isValid())
            continue;
        m_grabCache.insert(row.id, {row.indexerName, row.title, row.password, row.downloadUrl, now});
    }
    if (!rows.isEmpty())
        scheduleGrabCacheSave();
}

void IndexerSearchList::loadGrabCache()
{
    QFile file(grabCachePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != 1)
        return;

    for (const QJsonValue& v : root.value(QStringLiteral("grabs")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        const QUrl url(o.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
        if (id.isEmpty() || !url.isValid())
            continue;
        m_grabCache.insert(id, {o.value(QStringLiteral("indexer")).toString(),
                                o.value(QStringLiteral("title")).toString(),
                                o.value(QStringLiteral("password")).toString(), url,
                                static_cast<qint64>(o.value(QStringLiteral("savedAt")).toDouble())});
    }
    const auto before = m_grabCache.size();
    pruneGrabCache();
    m_grabCacheDirty = m_grabCache.size() != before;
}

void IndexerSearchList::scheduleGrabCacheSave()
{
    m_grabCacheDirty = true;
    if (!m_grabCacheTimer) {
        m_grabCacheTimer = new QTimer(this);
        m_grabCacheTimer->setSingleShot(true);
        m_grabCacheTimer->setInterval(5000);
        connect(m_grabCacheTimer, &QTimer::timeout, this, &IndexerSearchList::flushGrabCache);
    }
    if (!m_grabCacheTimer->isActive())
        m_grabCacheTimer->start();
}

void IndexerSearchList::pruneGrabCache()
{
    const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - kGrabCacheMaxAgeSecs;
    m_grabCache.removeIf([cutoff](const auto& it) { return it.value().savedAt < cutoff; });
    if (m_grabCache.size() <= kGrabCacheMaxEntries)
        return;

    // oldest out
    std::vector<qint64> ages;
    ages.reserve(static_cast<size_t>(m_grabCache.size()));
    for (const auto& g : std::as_const(m_grabCache))
        ages.push_back(g.savedAt);
    const auto nth = ages.end() - kGrabCacheMaxEntries;
    std::ranges::nth_element(ages, nth);
    const qint64 keepFrom = *nth;
    m_grabCache.removeIf([keepFrom](const auto& it) { return it.value().savedAt < keepFrom; });
}

QString IndexerSearchList::grabCachePath()
{
    const QString dir = QDir(thePrefs.configDir()).filePath(QStringLiteral("Indexers"));
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("GrabCache.json"));
}

const IndexerConfig* IndexerSearchList::accountNamed(const QString& displayName) const
{
    const auto it = std::ranges::find_if(m_accounts, [&displayName](const IndexerConfig& a) {
        return a.displayName() == displayName;
    });
    return it == m_accounts.cend() ? nullptr : &*it;
}

void IndexerSearchList::fetchGrab(const IndexerConfig& account, const QUrl& url, const QString& name,
                                  const QString& password, GrabCallback done)
{
    m_client->fetch(account, url,
                    [name, password, done = std::move(done)](bool ok, const QByteArray& body,
                                                             const QString& error) {
        done(ok, body, name, password, error);
    });
}

} // namespace eMule::indexer
