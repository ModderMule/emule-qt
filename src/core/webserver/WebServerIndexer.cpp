#include "pch.h"
/// @file WebServerIndexer.cpp
/// @brief The /api/v1/indexers and /api/v1/indexer operations: newznab (Usenet) and
///        torznab (torrent) indexer searches. Core never links the indexer module;
///        the searches go through a backend DaemonApp installs.

#include "webserver/WebServer.h"
#include "webserver/ApiOpBuilder.h"
#include "prefs/IndexerConfig.h"
#include "prefs/Preferences.h"

#include <QCborMap>
#include <QJsonArray>
#include <QPromise>

#include <memory>

namespace eMule {

using namespace api;
using namespace api::build;

namespace {

using L = QLatin1StringView;

[[nodiscard]] QString indexerKindName(IndexerKind kind)
{
    switch (kind) {
    case IndexerKind::Torznab: return s("torrent");
    case IndexerKind::Both:    return s("both");
    case IndexerKind::Newznab: break;
    }
    return s("usenet");
}

[[nodiscard]] Param searchIdParam()
{
    return pathInt(s("id"), s("Id returned by POST /indexer/searches.")).atLeast(1);
}

[[nodiscard]] Result indexerUnavailable()
{
    return error(503, s("Indexer searches are not available yet"));
}

[[nodiscard]] QList<int> intsOf(const QJsonValue& array)
{
    QList<int> out;
    for (const QJsonValue& v : array.toArray())
        out.append(v.toInt());
    return out;
}

[[nodiscard]] QStringList stringsOf(const QJsonValue& array)
{
    QStringList out;
    for (const QJsonValue& v : array.toArray())
        if (const QString t = v.toString().trimmed(); !t.isEmpty())
            out.append(t);
    return out;
}

/// A grab's answer: 200 with where it went, else the Usenet add's codes (409 asks
/// a question "force" answers), 422 a refused torrent, 502 a failed fetch.
[[nodiscard]] Result grabResult(const IndexerWebBackend::GrabResult& r)
{
    if (r.ok) {
        QJsonObject body{{s("target"), r.target}, {s("id"), r.id}};
        if (r.usenetOutcome >= 0)
            body.insert(L("outcome"), r.usenetOutcome);
        return ok(body);
    }
    int code = 502;
    if (r.target == L("usenet")) {
        switch (UsenetWebAddOutcome(r.usenetOutcome)) {
        case UsenetWebAddOutcome::Duplicate:
        case UsenetWebAddOutcome::AlreadyDownloaded:
        case UsenetWebAddOutcome::EpisodeDuplicate: code = 409; break;
        case UsenetWebAddOutcome::Invalid:          code = 422; break;
        default:                                    code = 500; break;
        }
    } else if (r.target == L("torrent")) {
        code = 422;
    }
    QJsonObject body{{s("error"), QJsonObject{{s("code"), code}, {s("message"), r.error}}}};
    if (!r.target.isEmpty())
        body.insert(L("target"), r.target);
    if (r.usenetOutcome >= 0)
        body.insert(L("outcome"), r.usenetOutcome);
    return {code, body};
}

} // namespace

void WebServer::addIndexerOperations()
{
    const auto add = [this](Operation op) { m_api.add(std::move(op)); };

    {
        Operation op = make("indexers.list", Method::Get, "/api/v1/indexers", "Indexers",
                            "List the configured indexers",
                            "newznab (usenet) and torznab (torrent) accounts set up in Options; "
                            "\"both\" serves either search. Names go in the \"indexers\" of a search.");
        op.flags = ReadOnly;
        op.mcpTool = s("indexers_list");
        op.handler = [](const Call&) {
            QJsonArray rows;
            for (const IndexerConfig& config : thePrefs.indexers()) {
                rows.append(QJsonObject{{s("name"), config.name},
                                        {s("network"), indexerKindName(config.kind)},
                                        {s("enabled"), config.enabled},
                                        {s("hasApiKey"), !config.apiKey.isEmpty()}});
            }
            return ok(rows);
        };
        add(op);
    }
    {
        Operation op = make("indexer.searches", Method::Get, "/api/v1/indexer/searches", "Indexers",
                            "List indexer searches",
                            "Every newznab/torznab search, the app's own included: running says "
                            "whether accounts are still paging, count is the rows so far.");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("indexer_searches_list");
        op.handler = [this](const Call& c) {
            if (!m_indexerBackend || !m_indexerBackend->available())
                return indexerUnavailable();
            QJsonArray rows;
            for (const QCborValue& v : m_indexerBackend->searches())
                rows.append(v.toMap().toJsonObject());
            return pageOf(c, rows);
        };
        add(op);
    }
    {
        Operation op = make("indexer.start", Method::Post, "/api/v1/indexer/searches", "Indexers",
                            "Search the indexers",
                            "Asks every enabled newznab (network usenet) or torznab (network "
                            "torrent) account at once; rows arrive as each answers, so poll the "
                            "results until running is false. Needs a query, categories or an "
                            "IMDb id. 409 when no indexer serves the network.");
        op.mcpTool = s("indexer_search_start");
        op.params = {
            bodyStr(s("query"), s("Words to search for.")).range(0, 255),
            bodyStr(s("network"), s("usenet (newznab) or torrent (torznab).")).oneOf({s("usenet"), s("torrent")})
                .def(s("usenet")),
            bodyStr(s("mode"), s("search; tvsearch and movie take season/episode and imdb.")).oneOf(
                {s("search"), s("tvsearch"), s("movie")}).def(s("search")),
            bodyArray(s("categories"), s("newznab category ids, e.g. 2000 movies, 5000 TV.")).of(ParamType::Integer)
                .range(0, 50),
            bodyStr(s("season"), s("tvsearch: season number.")).range(0, 10),
            bodyStr(s("episode"), s("tvsearch: episode number.")).range(0, 10),
            bodyStr(s("imdb"), s("IMDb id, with or without \"tt\".")).range(0, 20),
            bodyInt(s("minSize"), s("Smallest size in bytes.")).atLeast(0),
            bodyInt(s("maxSize"), s("Largest size in bytes; 0 = no limit.")).atLeast(0),
            bodyArray(s("indexers"), s("Only these indexer names (GET /indexers); default all.")).range(0, 50),
        };
        op.handler = [this](const Call& c) {
            if (!m_indexerBackend || !m_indexerBackend->available())
                return indexerUnavailable();
            IndexerWebBackend::Query q;
            q.text = c.str(L("query"));
            q.torrents = c.str(L("network")) == L("torrent");
            q.mode = c.str(L("mode"));
            q.categories = intsOf(c.args.value(L("categories")));
            q.season = c.str(L("season"));
            q.episode = c.str(L("episode"));
            q.imdbId = c.str(L("imdb"));
            q.minSize = c.integer(L("minSize"));
            q.maxSize = c.integer(L("maxSize"));
            q.indexers = stringsOf(c.args.value(L("indexers")));
            const IndexerWebBackend::StartResult r = m_indexerBackend->start(q);
            if (r.id == 0)
                return error(r.noIndexer ? 409 : 400, r.error);
            return ok(QJsonObject{{s("id"), qint64(r.id)}});
        };
        add(op);
    }
    {
        Operation op = make("indexer.get", Method::Get, "/api/v1/indexer/searches/{id}", "Indexers",
                            "One indexer search");
        op.flags = ReadOnly;
        op.mcpTool = s("indexer_search_get");
        op.params = {searchIdParam()};
        op.handler = [this](const Call& c) {
            if (!m_indexerBackend || !m_indexerBackend->available())
                return indexerUnavailable();
            const auto summary = m_indexerBackend->search(quint32(c.integer(L("id"))));
            if (!summary)
                return error(404, s("Search not found"));
            return ok(summary->toJsonObject());
        };
        add(op);
    }
    {
        Operation op = make("indexer.results", Method::Get, "/api/v1/indexer/searches/{id}/results",
                            "Indexers", "Results of an indexer search",
                            "In arrival order. isUsenet false: a torrent row. grabs and seeders "
                            "say how popular a release is; password: the indexer flags an "
                            "archive password; confidence and fakeReasons judge the row.");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("indexer_search_results");
        op.params = {searchIdParam()};
        op.handler = [this](const Call& c) {
            if (!m_indexerBackend || !m_indexerBackend->available())
                return indexerUnavailable();
            const auto rows = m_indexerBackend->results(quint32(c.integer(L("id"))));
            if (!rows)
                return error(404, s("Search not found"));
            return pageOf(c, rows->toJsonArray());
        };
        add(op);
    }
    {
        Operation op = make("indexer.download", Method::Post, "/api/v1/indexer/searches/{id}/download",
                            "Indexers", "Download an indexer result",
                            "Fetches the row's .nzb, .torrent or magnet (one of the indexer's "
                            "daily grabs) and adds it: an NZB to the Usenet queue, a torrent to "
                            "Torrents. Answers once it is added. 409 asks a question — already "
                            "downloaded — that \"force\" answers.");
        op.mcpTool = s("indexer_download");
        op.params = {
            searchIdParam(),
            bodyStr(s("result"), s("The row's id from the results.")).req().range(1, 1024),
            bodyInt(s("category"), s("Category index from GET /categories; 0 = none.")).atLeast(0).def(0),
            bodyInt(s("priority"), s("Usenet: -2 (lowest) to 2 (highest).")).range(-2, 2).def(0),
            bodyBool(s("paused"), s("Add paused.")).def(false),
            bodyBool(s("force"), s("Usenet: add even though downloaded before.")).def(false),
        };
        op.asyncHandler = [this](const Call& c) -> QFuture<Result> {
            if (!m_indexerBackend || !m_indexerBackend->available())
                return Registry::finished(indexerUnavailable());
            const auto id = quint32(c.integer(L("id")));
            if (!m_indexerBackend->search(id))
                return Registry::finished(error(404, s("Search not found")));
            if (c.integer(L("category")) >= qint64(thePrefs.categoryCount()))
                return Registry::finished(error(400, s("Unknown category")));
            auto promise = std::make_shared<QPromise<Result>>();
            promise->start();
            QFuture<Result> future = promise->future();
            m_indexerBackend->grab(id, c.str(L("result")),
                                   {.force = c.flag(L("force")), .category = int(c.integer(L("category"))),
                                    .priority = int(c.integer(L("priority"))), .paused = c.flag(L("paused"))},
                                   [promise](const IndexerWebBackend::GrabResult& r) {
                promise->addResult(grabResult(r));
                promise->finish();
            });
            return future;
        };
        add(op);
    }
    {
        Operation op = make("indexer.stop", Method::Post, "/api/v1/indexer/searches/{id}/stop", "Indexers",
                            "Stop an indexer search", "The rows so far stay.");
        op.mcpTool = s("indexer_search_stop");
        op.params = {searchIdParam()};
        op.handler = [this](const Call& c) {
            if (!m_indexerBackend || !m_indexerBackend->available())
                return indexerUnavailable();
            if (!m_indexerBackend->stop(quint32(c.integer(L("id")))))
                return error(404, s("Search not found"));
            return ok(QJsonObject{{s("stopped"), true}});
        };
        add(op);
    }
    {
        Operation op = make("indexer.remove", Method::Delete, "/api/v1/indexer/searches/{id}", "Indexers",
                            "Stop an indexer search and drop its results");
        op.mcpTool = s("indexer_search_remove");
        op.params = {searchIdParam()};
        op.handler = [this](const Call& c) {
            if (!m_indexerBackend || !m_indexerBackend->available())
                return indexerUnavailable();
            if (!m_indexerBackend->remove(quint32(c.integer(L("id")))))
                return error(404, s("Search not found"));
            return ok(QJsonObject{{s("removed"), true}});
        };
        add(op);
    }
}

} // namespace eMule
