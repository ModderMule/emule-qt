#include "IndexerSearch.h"

#include "IndexerClient.h"
#include "utils/Log.h"

#include <QPointer>

#include <utility>

namespace eMule::indexer {

IndexerSearch::IndexerSearch(quint32 id, IndexerClient* client, IndexerQuery query,
                             QList<IndexerConfig> accounts, int maxPages, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_client(client)
    , m_query(std::move(query))
    , m_maxPages(std::max(1, maxPages))
{
    m_accounts.reserve(accounts.size());
    for (auto& account : accounts) {
        AccountState state;
        state.config = std::move(account);
        m_accounts.append(state);
    }
}

const IndexerResult* IndexerSearch::find(const QString& resultId) const
{
    for (const auto& row : m_results) {
        if (row.id == resultId)
            return &row;
    }
    return nullptr;
}

const IndexerConfig* IndexerSearch::accountFor(const IndexerResult& result) const
{
    for (const auto& state : m_accounts) {
        if (state.config.displayName() == result.indexerName)
            return &state.config;
    }
    return nullptr;
}

void IndexerSearch::start(const QHash<QString, IndexerCaps>& caps)
{
    for (auto& state : m_accounts) {
        if (const auto it = caps.constFind(state.config.key()); it != caps.constEnd())
            state.caps = *it;
    }

    if (m_accounts.isEmpty()) {
        emit finished(m_id, {});
        m_emittedFinished = true;
        return;
    }

    emit progress(m_id, 0, accountsTotal());
    for (int i = 0; i < m_accounts.size(); ++i)
        requestPage(i);
}

void IndexerSearch::stop()
{
    if (m_stopped)
        return;
    m_stopped = true;

    // Do not abort the client here: it is shared with every other search, and
    // aborting it would cancel their replies too. The in-flight pages simply
    // stop producing rows, which onPage() enforces.
    checkFinished();
}

void IndexerSearch::requestPage(int accountIndex)
{
    AccountState& state = m_accounts[accountIndex];

    IndexerQuery pageQuery = m_query;
    pageQuery.offset = state.offset;
    if (state.caps.limitMax > 0)
        pageQuery.limit = std::min(pageQuery.limit, state.caps.limitMax);

    state.active = true;
    ++m_inFlight;

    // QPointer, not `this`. stop() deliberately does not abort the client — it is
    // shared with every other search — so a reply can still be in flight when
    // IndexerSearchList::removeSearch() deleteLater()s this object, and the
    // callback would then run on freed memory. QPointer is cleared by ~QObject,
    // which is exactly the moment that has to be caught.
    QPointer<IndexerSearch> self(this);
    m_client->search(state.config, pageQuery,
                     state.caps.isEmpty() ? nullptr : &state.caps,
                     [self, accountIndex](bool ok, const IndexerSearchPage& page,
                                          const QString& error) {
        if (!self)
            return;
        self->onPage(accountIndex, ok, page, error);
    });
}

void IndexerSearch::onPage(int accountIndex, bool ok, const IndexerSearchPage& page,
                           const QString& error)
{
    --m_inFlight;

    AccountState& state = m_accounts[accountIndex];
    state.active = false;
    ++state.pagesFetched;

    if (m_stopped) {
        accountFinished(accountIndex);
        return;
    }

    if (!ok) {
        // One indexer failing must not end the search. Record it, drop that
        // account, and let the rest run — with three configured, a provider
        // being down should cost a third of the results, not all of them.
        m_errors.append(QStringLiteral("%1: %2").arg(state.config.displayName(), error));
        logWarning(QStringLiteral("Indexer search: %1 failed — %2")
                       .arg(state.config.displayName(), error));
        accountFinished(accountIndex);
        return;
    }

    QList<IndexerResult> fresh;
    fresh.reserve(page.results.size());
    for (const auto& row : page.results) {
        const QString key = row.dedupKey();
        if (m_seen.contains(key))
            continue;
        m_seen.insert(key);
        m_results.append(row);
        fresh.append(row);
    }

    if (!fresh.isEmpty())
        emit resultsReady(m_id, fresh);

    if (page.total >= 0)
        state.total = page.total;
    state.offset += int(page.results.size());

    // Keep paging only while every condition holds. A short page means the
    // indexer has run out; the page cap is the quota guard; and `total` closes
    // the loop for indexers that keep answering with a full page past the end.
    const bool fullPage = int(page.results.size()) >= std::max(1, m_query.limit)
                          || (state.caps.limitMax > 0
                              && int(page.results.size()) >= state.caps.limitMax);
    const bool moreToCome = state.total < 0 || state.offset < state.total;

    if (fullPage && moreToCome && state.pagesFetched < m_maxPages && !page.results.isEmpty()) {
        requestPage(accountIndex);
        return;
    }

    accountFinished(accountIndex);
}

void IndexerSearch::accountFinished(int accountIndex)
{
    Q_UNUSED(accountIndex);
    ++m_done;
    emit progress(m_id, m_done, accountsTotal());
    checkFinished();
}

void IndexerSearch::checkFinished()
{
    if (m_emittedFinished)
        return;
    if (m_inFlight > 0)
        return;
    if (!m_stopped && m_done < accountsTotal())
        return;

    m_emittedFinished = true;
    emit finished(m_id, m_errors.join(QStringLiteral("; ")));
}

} // namespace eMule::indexer
