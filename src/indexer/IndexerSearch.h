#pragma once

/// @file IndexerSearch.h
/// @brief One user search, fanned out across every configured indexer.
///
/// Each account is paged independently and results arrive as they land, so a
/// fast indexer is not held up by a slow one. The search ends when every account
/// has run out of pages, hit its page cap, or failed — and a failure is never
/// fatal to the search: with three indexers configured, one being down should
/// cost a third of the results, not all of them.

#include "IndexerCaps.h"
#include "IndexerConfig.h"
#include "IndexerQuery.h"
#include "IndexerResult.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

namespace eMule::indexer {

class IndexerClient;

class IndexerSearch : public QObject {
    Q_OBJECT

public:
    IndexerSearch(quint32 id, IndexerClient* client, IndexerQuery query,
                  QList<IndexerConfig> accounts, int maxPages, QObject* parent = nullptr);

    [[nodiscard]] quint32 id() const { return m_id; }
    [[nodiscard]] const IndexerQuery& query() const { return m_query; }
    [[nodiscard]] const QList<IndexerResult>& results() const { return m_results; }
    [[nodiscard]] bool isRunning() const { return m_inFlight > 0; }

    /// Rows found so far, and how far the fan-out has got.
    [[nodiscard]] int accountsDone() const { return m_done; }
    [[nodiscard]] int accountsTotal() const { return int(m_accounts.size()); }

    [[nodiscard]] const IndexerResult* find(const QString& resultId) const;

    /// The account a result came from, so a grab uses the right API key.
    /// Null when the account has since been removed from the configuration.
    [[nodiscard]] const IndexerConfig* accountFor(const IndexerResult& result) const;

    /// @p caps is keyed by IndexerConfig::key(); an account with no entry is
    /// queried ungated, which every newznab implementation accepts.
    void start(const QHash<QString, IndexerCaps>& caps);

    /// Stop paging. In-flight replies are abandoned by the client, and the rows
    /// already collected stay — a stopped search is a shorter search, not a lost
    /// one.
    void stop();

signals:
    void resultsReady(quint32 searchId, const QList<eMule::indexer::IndexerResult>& rows);
    void progress(quint32 searchId, int done, int total);

    /// @p error is a summary of the accounts that failed, empty when all were
    /// fine. It is informational: rows may well have arrived anyway.
    void finished(quint32 searchId, const QString& error);

private:
    struct AccountState {
        IndexerConfig config;
        IndexerCaps caps;
        int pagesFetched = 0;
        int offset = 0;
        int total = -1;
        bool active = false;
    };

    void requestPage(int accountIndex);
    void onPage(int accountIndex, bool ok, const IndexerSearchPage& page,
                const QString& error);
    void accountFinished(int accountIndex);
    void checkFinished();

    quint32 m_id = 0;
    IndexerClient* m_client = nullptr;
    IndexerQuery m_query;
    QList<AccountState> m_accounts;
    int m_maxPages = 3;

    QList<IndexerResult> m_results;

    /// Cross-indexer dedup. Heuristic by construction — see
    /// IndexerResult::dedupKey().
    QSet<QString> m_seen;

    QStringList m_errors;
    int m_inFlight = 0;
    int m_done = 0;
    bool m_stopped = false;
    bool m_emittedFinished = false;
};

} // namespace eMule::indexer
