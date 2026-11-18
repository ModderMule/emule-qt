/// @file DaemonIndexerWebBackend.cpp
/// @brief The REST API's indexer searches — implementation.

#include "DaemonIndexerWebBackend.h"

#include "IndexerGrab.h"
#include "IndexerSearch.h"
#include "IndexerSearchList.h"

namespace eMule {

namespace {

QCborMap summaryOf(const indexer::IndexerSearch& search)
{
    const indexer::IndexerQuery& q = search.query();
    QCborArray categories;
    for (int c : q.categories)
        categories.append(c);
    return QCborMap{
        {QStringLiteral("id"), qint64(search.id())},
        {QStringLiteral("network"), search.torrentsOnly() ? QStringLiteral("torrent") : QStringLiteral("usenet")},
        {QStringLiteral("query"), q.text},
        {QStringLiteral("mode"), q.mode},
        {QStringLiteral("categories"), categories},
        {QStringLiteral("running"), search.isRunning()},
        {QStringLiteral("accountsDone"), search.accountsDone()},
        {QStringLiteral("accountsTotal"), search.accountsTotal()},
        {QStringLiteral("count"), qint64(search.results().size())},
    };
}

} // namespace

bool DaemonIndexerWebBackend::available() const
{
    return indexer::theIndexerSearchList != nullptr;
}

IndexerWebBackend::StartResult DaemonIndexerWebBackend::start(const Query& query)
{
    indexer::IndexerQuery q;
    q.text = query.text;
    q.categories = query.categories;
    q.mode = query.mode;
    q.season = query.season;
    q.episode = query.episode;
    q.imdbId = query.imdbId;
    q.minSize = query.minSize;
    q.maxSize = query.maxSize;
    const IndexerStartResult r = startIndexerSearch(std::move(q), query.torrents, query.indexers);
    return {.id = r.id, .error = r.error, .noIndexer = r.noIndexer};
}

QCborArray DaemonIndexerWebBackend::searches() const
{
    QCborArray out;
    if (!indexer::theIndexerSearchList)
        return out;
    for (quint32 id : indexer::theIndexerSearchList->searchIds())
        if (const auto* s = indexer::theIndexerSearchList->search(id))
            out.append(summaryOf(*s));
    return out;
}

std::optional<QCborMap> DaemonIndexerWebBackend::search(quint32 id) const
{
    const auto* s = indexer::theIndexerSearchList ? indexer::theIndexerSearchList->search(id) : nullptr;
    if (!s)
        return std::nullopt;
    return summaryOf(*s);
}

std::optional<QCborArray> DaemonIndexerWebBackend::results(quint32 id) const
{
    const auto* s = indexer::theIndexerSearchList ? indexer::theIndexerSearchList->search(id) : nullptr;
    if (!s)
        return std::nullopt;
    QCborArray rows;
    for (const indexer::IndexerResult& r : s->results())
        rows.append(indexerResultToCbor(r, s->query().text));
    return rows;
}

bool DaemonIndexerWebBackend::stop(quint32 id)
{
    return indexer::theIndexerSearchList && indexer::theIndexerSearchList->stopSearch(id);
}

bool DaemonIndexerWebBackend::remove(quint32 id)
{
    return indexer::theIndexerSearchList && indexer::theIndexerSearchList->removeSearch(id);
}

void DaemonIndexerWebBackend::grab(quint32 searchId, const QString& resultId, const GrabOptions& options,
                                   std::function<void(const GrabResult&)> done)
{
    grabIndexerResult({.searchId = searchId, .resultId = resultId, .force = options.force,
                       .category = options.category, .priority = options.priority, .paused = options.paused},
                      [done = std::move(done)](const IndexerGrabOutcome& out) {
        GrabResult r{.ok = out.ok, .id = out.id, .error = out.error};
        switch (out.target) {
        case IndexerGrabOutcome::Target::Torrent: r.target = QStringLiteral("torrent"); break;
        case IndexerGrabOutcome::Target::Usenet:
            r.target = QStringLiteral("usenet");
            r.usenetOutcome = out.usenetOutcome;
            break;
        case IndexerGrabOutcome::Target::None: break;
        }
        done(r);
    });
}

} // namespace eMule
