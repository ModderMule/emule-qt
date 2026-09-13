#include "IndexerSearchList.h"

#include "IndexerCapsStore.h"
#include "IndexerClient.h"
#include "IndexerSearch.h"

#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "utils/Log.h"

#include <QPointer>

namespace eMule::indexer {

IndexerSearchList* theIndexerSearchList = nullptr;

IndexerSearchList::IndexerSearchList(QObject* parent)
    : QObject(parent)
    , m_client(new IndexerClient(this))
{
    applyPreferences();
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
    if (it == m_searches.constEnd()) {
        done(false, {}, {}, {}, tr("That search is no longer open."));
        return;
    }

    const IndexerSearch* search = *it;
    const IndexerResult* result = search->find(resultId);
    if (!result) {
        done(false, {}, {}, {}, tr("That result is no longer in the list."));
        return;
    }

    const IndexerConfig* account = search->accountFor(*result);
    if (!account) {
        done(false, {}, {}, {},
             tr("The indexer \"%1\" is no longer configured.").arg(result->indexerName));
        return;
    }

    const QString name = result->title;
    const QString password = result->password;
    m_client->fetch(*account, result->downloadUrl,
                    [name, password, done = std::move(done)](bool ok, const QByteArray& body,
                                                             const QString& error) {
        done(ok, body, name, password, error);
    });
}

} // namespace eMule::indexer
