#pragma once

/// @file IndexerSearchList.h
/// @brief The daemon's indexer session: accounts, capability cache, searches.
///
/// Mirrors core/search/SearchList's shape — searches are created, keyed by an
/// id, kept until dropped, and report through signals — but deliberately does
/// not share its model. An ED2K result is an MD4 hash with source counts; an
/// indexer result is a title, an age and a URL.
///
/// This is also the module's façade: DaemonApp owns one, publishes it as
/// theIndexerSearchList, and the IPC handlers reach it there. Same arrangement,
/// for the same reason, as usenet::theUsenetSession — AppContext cannot hold it,
/// because AppContext is core and core must not depend on eMule::Indexer.

#include "IndexerCaps.h"
#include "IndexerConfig.h"
#include "IndexerQuery.h"
#include "IndexerResult.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace eMule::indexer {

class IndexerClient;
class IndexerSearch;

class IndexerSearchList : public QObject {
    Q_OBJECT

public:
    explicit IndexerSearchList(QObject* parent = nullptr);
    ~IndexerSearchList() override;

    /// Re-read the account list and the search settings, load each account's
    /// cached capabilities, and re-probe the ones that have gone stale. Called
    /// at startup and whenever the Options page saves, so a new API key takes
    /// effect without a restart.
    void applyPreferences();

    [[nodiscard]] QList<IndexerConfig> accounts() const { return m_accounts; }

    /// Cached capabilities for @p name, or null when nothing has been probed.
    [[nodiscard]] const IndexerCaps* capsFor(const QString& name) const;

    /// Start a search across every enabled account serving @p wanted.
    ///
    /// @p onlyNames restricts it further when the user picked specific indexers;
    /// empty means all of them. Returns 0 with @p error set when nothing is
    /// configured — which is a message worth showing, not an empty result list.
    quint32 startSearch(const IndexerQuery& query, IndexerKind wanted,
                        const QStringList& onlyNames, QString& error);

    bool stopSearch(quint32 searchId);
    bool removeSearch(quint32 searchId);
    void clearSearches();

    [[nodiscard]] const IndexerSearch* search(quint32 searchId) const;

    using ProbeCallback = std::function<void(bool ok, const IndexerCaps& caps,
                                             const QString& error)>;

    /// Probe one account's capabilities and cache the answer. Backs both the
    /// Options page's Test button and the staleness refresh.
    void probeCaps(const IndexerConfig& config, ProbeCallback done);

    /// @p password is the archive passphrase the feed advertised, empty when it
    /// advertised none. Carried out with the payload because the IndexerResult
    /// itself never leaves this module.
    using GrabCallback = std::function<void(bool ok, const QByteArray& payload,
                                            const QString& name, const QString& password,
                                            const QString& error)>;

    /// Fetch one result's .nzb. Runs here rather than in the GUI because the URL
    /// carries the API key, and the GUI is never given one.
    void grab(quint32 searchId, const QString& resultId, GrabCallback done);

signals:
    void resultsReady(quint32 searchId, const QList<eMule::indexer::IndexerResult>& rows);
    void searchProgress(quint32 searchId, int done, int total);
    void searchFinished(quint32 searchId, const QString& error);

private:
    IndexerClient* m_client = nullptr;
    QList<IndexerConfig> m_accounts;

    /// Keyed by IndexerConfig::key().
    QHash<QString, IndexerCaps> m_caps;

    QHash<quint32, IndexerSearch*> m_searches;
    quint32 m_nextSearchId = 1;

    int m_maxPages = 3;
    int m_resultLimit = 100;
    int m_capsRefreshDays = 7;
};

/// The daemon's indexer session, published for the IPC handlers. Null until
/// DaemonApp constructs it; every handler must check.
extern IndexerSearchList* theIndexerSearchList;

} // namespace eMule::indexer
