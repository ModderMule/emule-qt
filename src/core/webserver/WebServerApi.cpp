#include "pch.h"
/// @file WebServerApi.cpp
/// @brief The REST operation table: every /api/v1 route, declared once.
///
/// Routing, validation, the OpenAPI document and the MCP tools all read this
/// table. A route that is not here does not exist.

#include "webserver/WebServer.h"

#include "app/AppConfig.h"
#include "app/AppContext.h"
#include "app/CoreInfo.h"
#include "app/CoreOps.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"
#include "kademlia/Kademlia.h"
#include "net/BindAddress.h"
#include "prefs/PreferenceSchema.h"
#include "prefs/Preferences.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "search/SearchStarter.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "webserver/ApiBackend.h"
#include "webserver/JsonSerializers.h"
#include "webserver/OpenApiWriter.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QJsonDocument>
#include <QPromise>
#include <QRegularExpression>
#include <QSysInfo>
#include <QUrlQuery>

#include <algorithm>

namespace eMule {

using namespace api;

namespace {

using L = QLatin1StringView;

[[nodiscard]] QString s(const char* text) { return QString::fromUtf8(text); }

[[nodiscard]] Operation make(const char* id, Method method, const char* path, const char* tag,
                             const char* summary, const char* description = "")
{
    Operation op;
    op.id = s(id);
    op.method = method;
    op.path = s(path);
    op.tag = s(tag);
    op.summary = s(summary);
    op.description = s(description);
    return op;
}

[[nodiscard]] Result fromStatus(const ops::Status& st,
                                const QJsonObject& body = {{QStringLiteral("ok"), true}})
{
    return st.ok() ? ok(body) : error(st.code, st.message);
}

[[nodiscard]] Result unavailable(const char* what)
{
    return error(503, QStringLiteral("%1 not available").arg(s(what)));
}

[[nodiscard]] Param hashParam()
{
    return pathStr(s("hash"), s("eD2K file hash: 32 hexadecimal digits.")).range(32, 32);
}

[[nodiscard]] QString severityName(int qtMsgType)
{
    switch (qtMsgType) {
    case QtDebugMsg:    return QStringLiteral("debug");
    case QtInfoMsg:     return QStringLiteral("info");
    case QtWarningMsg:  return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("error");
    case QtFatalMsg:    return QStringLiteral("fatal");
    default:            return QStringLiteral("info");
    }
}

/// debug < info < warning < error < fatal — QtMsgType is not in that order.
[[nodiscard]] int severityRank(const QString& name)
{
    static const QStringList order{QStringLiteral("debug"), QStringLiteral("info"),
                                   QStringLiteral("warning"), QStringLiteral("error"),
                                   QStringLiteral("fatal")};
    return static_cast<int>(order.indexOf(name));
}

[[nodiscard]] QString searchTypeName(SearchType type)
{
    return type == SearchType::Kademlia ? QStringLiteral("kad")
         : type == SearchType::MetaUsenet ? QStringLiteral("usenetServer")
         : type == SearchType::MetaTorrent ? QStringLiteral("torrentServer")
         : type == SearchType::Ed2kGlobal ? QStringLiteral("ed2kGlobal")
         : type == SearchType::Automatic ? QStringLiteral("automatic")
                                         : QStringLiteral("ed2kServer");
}

/// Download priority names the API takes; -1 for an unknown one.
[[nodiscard]] int downPriorityFromName(const QString& name)
{
    if (name == L("low"))    return 0;   // PR_LOW
    if (name == L("normal")) return 1;   // PR_NORMAL
    if (name == L("high"))   return 2;   // PR_HIGH
    return -1;
}

} // namespace

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

void WebServer::buildApiTable()
{
    const auto add = [this](Operation op) { m_api.add(std::move(op)); };

    // --- App ---------------------------------------------------------------
    {
        Operation op = make("app.health", Method::Get, "/api/v1/health", "App",
                            "Liveness probe",
                            "Answers without an API key and says nothing else. For a container "
                            "health check.");
        op.flags = ReadOnly | NoAuth;
        op.resultSchema = schemaObject({{s("status"), schemaString(s("Always \"ok\"."))}});
        op.handler = [](const Call&) { return ok(QJsonObject{{s("status"), s("ok")}}); };
        add(op);
    }
    {
        Operation op = make("app.info", Method::Get, "/api/v1/app", "App", "Application state",
                            "Name, version and how long the core has been running.");
        op.flags = ReadOnly;
        op.handler = [this](const Call&) {
            return ok(QJsonObject{
                {s("name"), s("eMuleQt")},
                {s("version"), QString(kAppVersion)},
                {s("contractVersion"), kContractVersion},
                {s("state"), s("running")},
                {s("uptimeSeconds"), m_statistics ? static_cast<qint64>(m_statistics->uptimeSecs())
                                                  : qint64(0)},
            });
        };
        add(op);
    }
    {
        Operation op = make("app.capabilities", Method::Get, "/api/v1/capabilities", "App",
                            "What this server offers",
                            "Feature switches and the id of every operation, so a client can "
                            "adapt instead of probing.");
        op.flags = ReadOnly;
        op.handler = [this](const Call&) {
            QJsonArray ids;
            for (const Operation& o : m_api.operations())
                ids.append(o.id);
            return ok(QJsonObject{
                {s("contractVersion"), kContractVersion},
                {s("version"), QString(kAppVersion)},
                {s("features"), QJsonObject{
                    {s("restApi"), m_config.restApiEnabled},
                    {s("mcp"), m_config.mcpEnabled},
                    {s("mcpReadOnly"), m_config.mcpReadOnly},
                    {s("webUi"), m_config.webUiEnabled},
                    {s("events"), m_eventHub != nullptr},
                    {s("usenet"), usenetAvailable()},
                }},
                {s("operations"), ids},
            });
        };
        add(op);
    }
    {
        Operation op = make("app.snapshot", Method::Get, "/api/v1/snapshot", "App",
                            "Everything at a glance",
                            "One bounded call for a dashboard: connection state of both "
                            "networks, transfer rates and the size of every list. Start here.");
        op.flags = ReadOnly;
        op.mcpTool = s("get_status");
        op.handler = [this](const Call&) { return apiSnapshot(); };
        add(op);
    }
    {
        Operation op = make("app.openapi", Method::Get, "/api/v1/openapi.json", "App",
                            "This API as an OpenAPI 3.1 document",
                            "Generated from the same table that routes the requests. Readable "
                            "without an API key.");
        op.flags = ReadOnly | NoAuth;
        op.handler = [this](const Call&) { return ok(openApiDocument()); };
        add(op);
    }
    {
        Operation op = make("app.shutdown", Method::Post, "/api/v1/app/shutdown", "App",
                            "Stop the daemon",
                            "Saves state and exits. Transfers stop; nothing restarts it.");
        op.flags = Destructive | NoMcpWrite;
        op.handler = [this](const Call&) {
            if (!m_apiBackend)
                return unavailable("Shutdown");
            logInfo(QStringLiteral("Shutdown requested over the REST API"));
            m_apiBackend->requestShutdown();
            return ok(QJsonObject{{s("shuttingDown"), true}});
        };
        add(op);
    }
    {
        Operation op = make("events.stream", Method::Get, "/api/v1/events", "Events",
                            "Server-sent event stream",
                            "text/event-stream. Each event has an id, a type such as "
                            "\"downloads.changed\" and a JSON object. Most events only say that a "
                            "list changed: fetch it again. Send Last-Event-ID to resume; a "
                            "\"sync.reset\" event means the gap could not be replayed and "
                            "everything has to be fetched again. A comment line arrives every "
                            "15 seconds as a heartbeat.");
        op.flags = ReadOnly | RawStream;
        op.params = {queryStr(s("topics"),
                              s("Comma-separated topics to receive, e.g. \"downloads,search\". "
                                "Default: all."))};
        add(op);
    }

    // --- Connection ----------------------------------------------------------
    {
        Operation op = make("connection.get", Method::Get, "/api/v1/connection", "Connection",
                            "eD2K server connection",
                            "A LowID means the TCP port is not reachable from outside: fewer "
                            "sources, slower downloads. netBlocked is true while the bound "
                            "network interface is missing; nothing connects then.");
        op.flags = ReadOnly;
        op.mcpTool = s("connection_get");
        op.handler = [this](const Call&) {
            if (!m_serverConnect)
                return unavailable("Server connection");
            const auto* current = m_serverConnect->currentServer();
            QJsonObject obj{
                {s("isConnected"), m_serverConnect->isConnected()},
                {s("isConnecting"), m_serverConnect->isConnecting()},
                {s("isLowID"), m_serverConnect->isLowID()},
                {s("clientID"), static_cast<qint64>(m_serverConnect->clientID())},
                {s("netBlocked"), !BindAddress::outboundAllowed()},
                {s("netBlockReason"), BindAddress::current().reason},
            };
            if (current) {
                obj[s("currentServer")] = QJsonObject{
                    {s("name"), current->name()},
                    {s("address"), current->address()},
                    {s("port"), current->port()},
                };
            }
            return ok(obj);
        };
        add(op);
    }
    {
        Operation op = make("connection.connect", Method::Post, "/api/v1/connection/connect",
                            "Connection", "Connect to any eD2K server",
                            "Starts connecting and returns at once; poll the connection or "
                            "listen for \"connection.changed\".");
        op.mcpTool = s("connection_connect");
        op.handler = [](const Call&) {
            return fromStatus(ops::connectToServer(nullptr), {{s("connecting"), true}});
        };
        add(op);
    }
    {
        Operation op = make("connection.disconnect", Method::Post,
                            "/api/v1/connection/disconnect", "Connection",
                            "Disconnect from the eD2K server",
                            "Also stops a connection attempt in progress. Kad is not affected.");
        op.mcpTool = s("connection_disconnect");
        op.handler = [](const Call&) {
            return fromStatus(ops::disconnectFromServer(), {{s("disconnected"), true}});
        };
        add(op);
    }

    // --- Downloads -------------------------------------------------------------
    {
        Operation op = make("downloads.list", Method::Get, "/api/v1/downloads", "Downloads",
                            "List downloads",
                            "The eD2K download queue. Sizes are bytes, datarate is bytes per "
                            "second. A completed download stays listed until cleared.");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("downloads_list");
        op.resultSchema = schemaPage(s("Download"));
        op.handler = [this](const Call& c) {
            if (!m_downloadQueue)
                return unavailable("Download queue");
            const auto& files = m_downloadQueue->files();
            return page(c, static_cast<qsizetype>(files.size()), [&files](qsizetype i) {
                return QJsonValue(toJson(*files[static_cast<size_t>(i)]));
            });
        };
        add(op);
    }
    {
        Operation op = make("downloads.add", Method::Post, "/api/v1/downloads", "Downloads",
                            "Add downloads by ed2k link",
                            "Takes one link or several. Each must be an ed2k://|file|… link. "
                            "Answers per link whether it was added or was already queued.");
        op.mcpTool = s("download_add");
        op.params = {
            bodyStr(s("link"), s("One ed2k://|file|name|size|hash|/ link.")),
            bodyArray(s("links"), s("Several ed2k file links.")).range(1, 100),
            bodyInt(s("category"), s("Category index from GET /categories; 0 = none.")).atLeast(0).def(0),
            bodyBool(s("paused"), s("Add in the paused state.")).def(false),
        };
        op.handler = [](const Call& c) {
            QStringList links;
            if (c.has(L("link")))
                links.append(c.str(L("link")));
            for (const QJsonValue& v : c.args.value(L("links")).toArray())
                links.append(v.toString());
            if (links.isEmpty())
                return error(400, s("Send 'link' or 'links'"));

            QJsonArray items;
            int added = 0;
            ops::Status last;
            for (const QString& link : std::as_const(links)) {
                const ops::AddOutcome out =
                    ops::addDownloadFromLink(link, c.integer(L("category")), c.flag(L("paused")));
                last = out.status;
                QJsonObject row{{s("link"), link}, {s("hash"), out.hash}, {s("added"), out.added}};
                if (!out.status.ok())
                    row.insert(s("error"), out.status.message);
                else if (!out.added)
                    row.insert(s("alreadyQueued"), true);
                added += out.added ? 1 : 0;
                items.append(row);
            }
            // One link: its failure is the call's failure.
            if (links.size() == 1 && !last.ok())
                return error(last.code, last.message);
            return ok(QJsonObject{{s("added"), added}, {s("items"), items}});
        };
        add(op);
    }
    {
        Operation op = make("downloads.clearCompleted", Method::Post,
                            "/api/v1/downloads/clear-completed", "Downloads",
                            "Remove completed downloads from the list",
                            "Only the list entries go; the finished files stay where they are.");
        op.mcpTool = s("downloads_clear_completed");
        op.params = {bodyArray(s("hashes"), s("Only these; default: every completed download."))};
        op.handler = [](const Call& c) {
            QSet<QString> only;
            for (const QJsonValue& v : c.args.value(L("hashes")).toArray())
                only.insert(v.toString());
            const int removed = ops::clearCompletedDownloads(only);
            if (removed < 0)
                return unavailable("Download queue");
            return ok(QJsonObject{{s("removed"), removed}});
        };
        add(op);
    }
    {
        Operation op = make("downloads.get", Method::Get, "/api/v1/downloads/{hash}", "Downloads",
                            "One download");
        op.flags = ReadOnly;
        op.mcpTool = s("download_get");
        op.params = {hashParam()};
        op.resultSchema = schemaRef(s("Download"));
        op.handler = [](const Call& c) {
            ops::Status st;
            const PartFile* file = ops::findDownload(c.str(L("hash")), st);
            return file ? ok(toJson(*file)) : error(st.code, st.message);
        };
        add(op);
    }
    {
        Operation op = make("downloads.edit", Method::Patch, "/api/v1/downloads/{hash}",
                            "Downloads", "Rename, reprioritise or recategorise a download",
                            "Send only the fields to change. All are checked before any is "
                            "applied.");
        op.mcpTool = s("download_edit");
        op.params = {
            hashParam(),
            bodyStr(s("name"), s("New file name. Not once the download is completing.")).range(1, 255),
            bodyStr(s("priority"), s("Download priority; \"auto\" lets the core choose."))
                .oneOf({s("low"), s("normal"), s("high"), s("auto")}),
            bodyInt(s("category"), s("Category index from GET /categories; 0 = none.")).atLeast(0),
        };
        op.resultSchema = schemaRef(s("Download"));
        op.handler = [](const Call& c) {
            const QString hash = c.str(L("hash"));
            ops::Status st;
            PartFile* file = ops::findDownload(hash, st);
            if (!file)
                return error(st.code, st.message);
            if (!c.has(L("name")) && !c.has(L("priority")) && !c.has(L("category")))
                return error(400, s("Nothing to change: send name, priority or category"));

            // Check what can be checked up front, so a bad field changes nothing.
            if (c.has(L("category"))
                && c.integer(L("category")) >= static_cast<qint64>(thePrefs.categoryCount()))
                return error(400, s("Unknown category"));
            if (c.has(L("name"))) {
                const PartFileStatus state = file->status();
                if (state == PartFileStatus::Complete || state == PartFileStatus::Completing)
                    return error(409, s("Download already completed"));
                if (c.str(L("name")).contains(u'|'))
                    return error(400, s("Invalid file name"));
            }

            if (c.has(L("name")))
                st = ops::renameDownload(hash, c.str(L("name")));
            if (st.ok() && c.has(L("priority"))) {
                const QString name = c.str(L("priority"));
                const bool isAuto = name == L("auto");
                st = ops::setDownloadPriority(
                    hash, static_cast<uint8>(isAuto ? 1 : downPriorityFromName(name)), isAuto);
            }
            if (st.ok() && c.has(L("category")))
                st = ops::setDownloadCategory(hash, static_cast<uint32>(c.integer(L("category"))));
            return st.ok() ? ok(toJson(*file)) : error(st.code, st.message);
        };
        add(op);
    }
    {
        Operation op = make("downloads.sources", Method::Get, "/api/v1/downloads/{hash}/sources",
                            "Downloads", "Sources of a download",
                            "The peers known to have the file, with their state and rates.");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("download_sources");
        op.params = {hashParam()};
        op.resultSchema = schemaPage(s("Client"));
        op.handler = [](const Call& c) {
            ops::Status st;
            const PartFile* file = ops::findDownload(c.str(L("hash")), st);
            if (!file)
                return error(st.code, st.message);
            std::vector<const UpDownClient*> sources;
            for (const auto* client : file->srcList())
                sources.push_back(client);
            return page(c, static_cast<qsizetype>(sources.size()), [&sources](qsizetype i) {
                return QJsonValue(toJson(*sources[static_cast<size_t>(i)]));
            });
        };
        add(op);
    }
    const auto downloadAction = [&add](const char* id, const char* verb, const char* tool,
                                       const char* summary, const char* description,
                                       ops::Status (*action)(const QString&), int flags) {
        Operation op = make(id, Method::Post, "", "Downloads", summary, description);
        op.path = QStringLiteral("/api/v1/downloads/{hash}/%1").arg(s(verb));
        op.flags = flags;
        op.mcpTool = s(tool);
        op.params = {hashParam()};
        if (!(flags & Destructive))
            op.resultSchema = schemaRef(s("Download"));
        const bool gone = (flags & Destructive) != 0;
        op.handler = [action, gone](const Call& c) {
            const QString hash = c.str(L("hash"));
            const ops::Status st = action(hash);
            if (!st.ok())
                return error(st.code, st.message);
            if (gone)
                return ok(QJsonObject{{s("cancelled"), true}});
            ops::Status lookup;
            const PartFile* file = ops::findDownload(hash, lookup);
            return file ? ok(toJson(*file)) : ok(QJsonObject{{s("ok"), true}});
        };
        add(op);
    };
    downloadAction("downloads.pause", "pause", "download_pause", "Pause a download",
                   "Sources are kept; nothing is requested until it is resumed.",
                   &ops::pauseDownload, 0);
    downloadAction("downloads.resume", "resume", "download_resume",
                   "Resume a paused or stopped download", "", &ops::resumeDownload, 0);
    downloadAction("downloads.stop", "stop", "download_stop", "Stop a download",
                   "Like pause, but the sources are dropped too. The data is kept.",
                   &ops::stopDownload, 0);
    downloadAction("downloads.cancel", "cancel", "download_cancel", "Cancel a download",
                   "Removes the download and deletes what was downloaded of it.",
                   &ops::cancelDownload, Destructive);

    // --- Search ------------------------------------------------------------------
    {
        Operation op = make("search.list", Method::Get, "/api/v1/search", "Search",
                            "Searches that still hold results");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("search_list");
        op.handler = [this](const Call& c) { return apiSearchList(c); };
        add(op);
    }
    {
        Operation op = make("search.start", Method::Post, "/api/v1/search", "Search",
                            "Start a file search",
                            "Returns a searchID at once; results arrive over the next seconds — "
                            "fetch them with GET /search/{id}/results until state is "
                            "\"finished\". 202 means the search is queued and goes out by itself "
                            "once a network is connected. \"automatic\" picks the best network "
                            "available and is the right choice unless one is wanted. "
                            "\"usenetServer\" and \"torrentServer\" ask an eD2K server's own "
                            "catalogue for Usenet or torrent releases (keywords and NOT only); "
                            "their results have metaKind set and are not eD2K downloads.");
        op.mcpTool = s("search_start");
        op.params = {
            bodyStr(s("expression"), s("Keywords. AND, OR and NOT are understood.")).req().range(1, 512),
            bodyStr(s("type"), s("Network to search."))
                .oneOf({s("automatic"), s("ed2kServer"), s("ed2kGlobal"), s("kad"),
                        s("usenetServer"), s("torrentServer")})
                .def(s("automatic")),
            bodyStr(s("fileType"), s("Restrict to a type: Audio, Video, Image, Pro (programs), "
                                     "Doc, Arc (archives), Iso (CD images)."))
                .range(0, 32),
            bodyStr(s("extension"), s("File extension without the dot.")).range(0, 16),
            bodyNum(s("minSize"), s("Smallest file size in bytes.")).atLeast(0),
            bodyNum(s("maxSize"), s("Largest file size in bytes.")).atLeast(0),
            bodyInt(s("availability"), s("Minimum number of sources.")).atLeast(0),
            bodyInt(s("completeSources"), s("Minimum number of complete sources.")).atLeast(0),
            bodyStr(s("codec"), s("Media codec.")).range(0, 64),
            bodyStr(s("title"), s("Media title.")).range(0, 255),
            bodyStr(s("album"), s("Media album.")).range(0, 255),
            bodyStr(s("artist"), s("Media artist.")).range(0, 255),
            bodyInt(s("minBitrate"), s("Minimum bitrate in kbit/s.")).atLeast(0),
            bodyInt(s("minLength"), s("Minimum media length in seconds.")).atLeast(0),
        };
        op.handler = [this](const Call& c) { return apiSearchStart(c); };
        add(op);
    }
    {
        Operation op = make("search.results", Method::Get, "/api/v1/search/{searchID}/results",
                            "Search", "Results of a search",
                            "Sorted by source count, best first. state says whether more may "
                            "come: queued, running, finished or failed. A result with many "
                            "sources and a plausible size is the safer pick; confidence and "
                            "fakeReasons say how far a result can be trusted.");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("search_results");
        op.params = {pathInt(s("searchID"), s("Id returned by POST /search.")).atLeast(1)};
        op.resultSchema = schemaPage(s("SearchResult"));
        op.handler = [this](const Call& c) { return apiSearchResults(c); };
        add(op);
    }
    {
        Operation op = make("search.download", Method::Post,
                            "/api/v1/search/{searchID}/download", "Search",
                            "Download a search result",
                            "Queues the result and carries over the sources the search found.");
        op.mcpTool = s("download_from_search");
        op.params = {
            pathInt(s("searchID"), s("Id returned by POST /search.")).atLeast(1),
            bodyStr(s("hash"), s("Hash of the result to download.")).req().range(32, 32),
            bodyInt(s("category"), s("Category index from GET /categories; 0 = none.")).atLeast(0).def(0),
        };
        op.handler = [this](const Call& c) {
            if (!m_searchList)
                return unavailable("Search list");
            const QString hash = c.str(L("hash"));
            const auto searchID = static_cast<uint32>(c.integer(L("searchID")));
            std::array<uint8, 16> raw{};
            if (decodeBase16(hash, raw.data(), 16) != 16)
                return error(400, s("Invalid hash"));
            const SearchFile* result = m_searchList->searchFileByHash(raw.data(), searchID);
            if (!result)
                return error(404, s("No such result in this search"));
            const ops::AddOutcome out = ops::addDownloadFromSearch(
                hash, result->fileName(), static_cast<uint64>(result->fileSize()), {},
                c.integer(L("category")), searchID);
            if (!out.status.ok())
                return error(out.status.code, out.status.message);
            return ok(QJsonObject{{s("hash"), hash},
                                  {s("added"), out.added},
                                  {s("alreadyQueued"), !out.added}});
        };
        add(op);
    }
    {
        Operation op = make("search.more", Method::Post, "/api/v1/search/{searchID}/more",
                            "Search", "Fetch the next page of a search",
                            "A usenetServer / torrentServer search asks its server for one page "
                            "and finishes with hasMore true while the server has a further one; "
                            "so does an eD2K server search whose server holds back matches (up "
                            "to 5 further pages, and only until the next server search). "
                            "This fetches it: the search runs again and its new results join the "
                            "old ones. 409 when the search has no further page.");
        op.mcpTool = s("search_more");
        op.params = {pathInt(s("searchID"), s("Id returned by POST /search.")).atLeast(1)};
        op.handler = [this](const Call& c) {
            if (!m_searchList)
                return unavailable("Search list");
            const auto searchID = static_cast<uint32>(c.integer(L("searchID")));
            if (!m_searchList->hasSearch(searchID))
                return error(404, s("Search not found"));
            if (!searchMore(*m_searchList, searchID))
                return error(409, s("The search has no further page"));
            return ok(QJsonObject{{s("searchID"), static_cast<qint64>(searchID)},
                                  {s("state"), searchRunStateName(SearchRunState::Running)}});
        };
        add(op);
    }
    {
        Operation op = make("search.remove", Method::Delete, "/api/v1/search/{searchID}",
                            "Search", "Stop a search and drop its results");
        op.mcpTool = s("search_remove");
        op.params = {pathInt(s("searchID"), s("Id returned by POST /search.")).atLeast(1)};
        op.handler = [this](const Call& c) {
            if (!m_searchList)
                return unavailable("Search list");
            if (!removeSearch(*m_searchList, static_cast<uint32>(c.integer(L("searchID")))))
                return error(404, s("Search not found"));
            return ok(QJsonObject{{s("removed"), true}});
        };
        add(op);
    }

    // --- Servers -----------------------------------------------------------------
    {
        Operation op = make("servers.list", Method::Get, "/api/v1/servers", "Servers",
                            "List eD2K servers",
                            "A disabled server failed too often and is skipped when connecting.");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("servers_list");
        op.resultSchema = schemaPage(s("Server"));
        op.handler = [this](const Call& c) {
            if (!m_serverList)
                return unavailable("Server list");
            const auto& servers = m_serverList->servers();
            return page(c, static_cast<qsizetype>(servers.size()), [&servers](qsizetype i) {
                return QJsonValue(toJson(*servers[static_cast<size_t>(i)]));
            });
        };
        add(op);
    }
    {
        Operation op = make("servers.add", Method::Post, "/api/v1/servers", "Servers",
                            "Add a server",
                            "The address is an IPv4 or IPv6 literal or a host name; a host name "
                            "is resolved on every connect.");
        op.mcpTool = s("server_add");
        op.params = {
            bodyStr(s("address"), s("IP address or host name.")).req().range(1, 255),
            bodyInt(s("port"), s("TCP port.")).req().range(1, 65535),
            bodyStr(s("name"), s("Display name.")).range(0, 255),
        };
        op.handler = [](const Call& c) {
            const ops::AddServerOutcome out = ops::addServer(
                c.str(L("address")), static_cast<uint16>(c.integer(L("port"))), c.str(L("name")));
            if (!out.status.ok())
                return error(out.status.code, out.status.message);
            return ok(QJsonObject{{s("added"), out.added}, {s("alreadyListed"), !out.added}});
        };
        add(op);
    }
    {
        Operation op = make("servers.import", Method::Post, "/api/v1/servers/import", "Servers",
                            "Merge a server.met from a URL",
                            "Downloads the file (gzip and zip are unpacked) and adds the servers "
                            "that are not listed yet. Held while the bound interface is missing.");
        op.mcpTool = s("servers_import");
        op.params = {bodyStr(s("url"), s("http(s) URL of a server.met file.")).req().range(8, 2048)};
        op.asyncHandler = [](const Call& c) {
            auto promise = std::make_shared<QPromise<Result>>();
            promise->start();
            QFuture<Result> future = promise->future();
            ops::importServerMetFromUrl(c.str(L("url")),
                [promise](const ops::Status& st, int added) {
                    promise->addResult(fromStatus(st, {{s("added"), added}}));
                    promise->finish();
                });
            return future;
        };
        add(op);
    }
    {
        Operation op = make("servers.remove", Method::Delete, "/api/v1/servers/{address}/{port}",
                            "Servers", "Remove a server from the list");
        op.mcpTool = s("server_remove");
        op.params = {
            pathStr(s("address"), s("IP address or host name, as listed.")),
            pathInt(s("port"), s("TCP port.")).range(1, 65535),
        };
        op.handler = [](const Call& c) {
            return fromStatus(ops::removeServer(c.str(L("address")),
                                                static_cast<uint16>(c.integer(L("port")))),
                              {{s("removed"), true}});
        };
        add(op);
    }
    {
        Operation op = make("servers.connect", Method::Post,
                            "/api/v1/servers/{address}/{port}/connect", "Servers",
                            "Connect to this server",
                            "Leaves the current server first when connected to another one.");
        op.mcpTool = s("server_connect");
        op.params = {
            pathStr(s("address"), s("IP address or host name, as listed.")),
            pathInt(s("port"), s("TCP port.")).range(1, 65535),
        };
        op.handler = [](const Call& c) {
            Server* server = ops::findServer(c.str(L("address")),
                                             static_cast<uint16>(c.integer(L("port"))));
            if (!server)
                return error(404, s("Server not found"));
            return fromStatus(ops::connectToServer(server), {{s("connecting"), true}});
        };
        add(op);
    }

    // --- Kad ---------------------------------------------------------------------
    {
        Operation op = make("kad.status", Method::Get, "/api/v1/kad", "Kad", "Kad network status",
                            "running: started; connected: has contacts and can search; "
                            "firewalled: the UDP port is not reachable from outside.");
        op.flags = ReadOnly;
        op.mcpTool = s("kad_status");
        op.handler = [](const Call&) { return ok(ops::kadStatus().toJsonObject()); };
        add(op);
    }
    {
        Operation op = make("kad.nodes", Method::Get, "/api/v1/kad/nodes", "Kad",
                            "Kad routing table contacts");
        op.flags = ReadOnly | Paged;
        op.handler = [](const Call& c) { return pageOf(c, ops::kadContacts().toJsonArray()); };
        add(op);
    }
    {
        Operation op = make("kad.stats", Method::Get, "/api/v1/kad/stats", "Kad", "Kad statistics",
                            "session and cumulative activity counters, the routing table by "
                            "contact type and version, and how many distinct nodes were seen, "
                            "by country. The seen counts are estimates (about 2 %): contacted "
                            "nodes sent us a HELLO or answered our lookup, listed ones were "
                            "only named by other nodes.");
        op.flags = ReadOnly;
        op.mcpTool = s("kad_stats");
        op.handler = [](const Call&) { return ok(ops::kadStats().toJsonObject()); };
        add(op);
    }
    {
        Operation op = make("clients.stats", Method::Get, "/api/v1/clients/stats", "Diagnostics",
                            "Distinct clients seen",
                            "How many different eD2K clients said hello, by user hash, this "
                            "session and in total, split by country. identified is the part "
                            "that proved its hash with SecureIdent. Estimates (about 2 %).");
        op.flags = ReadOnly;
        op.mcpTool = s("client_stats");
        op.handler = [](const Call&) { return ok(ops::clientStats().toJsonObject()); };
        add(op);
    }
    {
        Operation op = make("kad.start", Method::Post, "/api/v1/kad/start", "Kad", "Start Kad",
                            "Uses the stored contacts; no effect when it is already running.");
        op.mcpTool = s("kad_start");
        op.handler = [](const Call&) { return fromStatus(ops::startKad(), {{s("started"), true}}); };
        add(op);
    }
    {
        Operation op = make("kad.stop", Method::Post, "/api/v1/kad/stop", "Kad", "Stop Kad",
                            "The contacts are saved; searches and source lookups over Kad end.");
        op.mcpTool = s("kad_stop");
        op.handler = [](const Call&) { return fromStatus(ops::stopKad(), {{s("stopped"), true}}); };
        add(op);
    }
    {
        Operation op = make("kad.bootstrap", Method::Post, "/api/v1/kad/bootstrap", "Kad",
                            "Bootstrap Kad from one node",
                            "Starts Kad if needed and asks the given node for contacts.");
        op.params = {
            bodyStr(s("host"), s("IP address or host name of a Kad node.")).req().range(1, 255),
            bodyInt(s("port"), s("Its UDP port.")).req().range(1, 65535),
        };
        op.handler = [](const Call& c) {
            return fromStatus(ops::startKad(c.str(L("host")),
                                            static_cast<uint16>(c.integer(L("port")))),
                              {{s("bootstrapping"), true}});
        };
        add(op);
    }
    {
        Operation op = make("kad.importNodes", Method::Post, "/api/v1/kad/import-nodes", "Kad",
                            "Load a nodes.dat from a URL",
                            "For a Kad that has no contacts left. Downloads the file and starts "
                            "Kad from it.");
        op.mcpTool = s("kad_import_nodes");
        op.params = {bodyStr(s("url"), s("http(s) URL of a nodes.dat file.")).req().range(8, 2048)};
        op.asyncHandler = [](const Call& c) {
            auto promise = std::make_shared<QPromise<Result>>();
            promise->start();
            QFuture<Result> future = promise->future();
            ops::importKadNodesFromUrl(c.str(L("url")), [promise](const ops::Status& st) {
                promise->addResult(fromStatus(st, {{s("imported"), true}}));
                promise->finish();
            });
            return future;
        };
        add(op);
    }

    // --- Uploads -------------------------------------------------------------------
    const auto clientList = [&add, this](const char* id, const char* path, const char* tool,
                                         const char* summary, const char* description,
                                         bool waiting) {
        Operation op = make(id, Method::Get, path, "Uploads", summary, description);
        op.flags = ReadOnly | Paged;
        op.mcpTool = s(tool);
        op.resultSchema = schemaPage(s("Client"));
        op.handler = [this, waiting](const Call& c) {
            if (!m_uploadQueue)
                return unavailable("Upload queue");
            std::vector<const UpDownClient*> clients;
            const auto collect = [&clients](UpDownClient* client) { clients.push_back(client); };
            if (waiting)
                m_uploadQueue->forEachWaiting(collect);
            else
                m_uploadQueue->forEachUploading(collect);
            Result result = page(c, static_cast<qsizetype>(clients.size()),
                                 [&clients](qsizetype i) {
                                     return QJsonValue(toJson(*clients[static_cast<size_t>(i)]));
                                 });
            QJsonObject body = result.body.toObject();
            body.insert(s("summary"), QJsonObject{
                {s("datarate"), static_cast<qint64>(m_uploadQueue->datarate())},
                {s("uploadQueueLength"), m_uploadQueue->uploadQueueLength()},
                {s("waitingUserCount"), m_uploadQueue->waitingUserCount()},
                {s("successfulUploads"), static_cast<qint64>(m_uploadQueue->successfulUploadCount())},
                {s("failedUploads"), static_cast<qint64>(m_uploadQueue->failedUploadCount())},
            });
            result.body = body;
            return result;
        };
        add(op);
    };
    clientList("uploads.list", "/api/v1/uploads", "uploads_list", "Clients being uploaded to",
               "One row per upload slot. summary carries the queue's counters.", false);
    clientList("uploads.queue", "/api/v1/upload-queue", "upload_queue_list",
               "Clients waiting for an upload slot",
               "queueScore decides who is served next; queueRating is the same without the "
               "waiting-time part.", true);

    // --- Shared files -------------------------------------------------------------
    {
        Operation op = make("shared.list", Method::Get, "/api/v1/shared", "Shared files",
                            "List shared files");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("shared_list");
        op.handler = [this](const Call& c) {
            if (!m_sharedFiles)
                return unavailable("Shared file list");
            const qsizetype total = m_sharedFiles->getCount();
            const qsizetype first = std::clamp<qsizetype>(c.offset(), 0, total);
            const qsizetype last = first + std::clamp<qsizetype>(c.limit(), 1, kMaxPageSize);
            QJsonArray items;
            qsizetype index = 0;
            // Only the rows of the window are built: a library can hold 100,000 files.
            m_sharedFiles->forEachFile([&](KnownFile* file) {
                if (index >= first && index < last) {
                    items.append(QJsonObject{
                        {s("hash"), md4str(file->fileHash())},
                        {s("fileName"), file->fileName()},
                        {s("fileSize"), static_cast<qint64>(file->fileSize())},
                    });
                }
                ++index;
            });
            return ok(QJsonObject{{s("items"), items},
                                  {s("total"), static_cast<qint64>(total)},
                                  {s("offset"), static_cast<qint64>(first)},
                                  {s("limit"), static_cast<qint64>(last - first)}});
        };
        add(op);
    }
    {
        Operation op = make("shared.directories", Method::Get, "/api/v1/shared/directories",
                            "Shared files", "Shared directories",
                            "The directories whose files are shared, plus the incoming and "
                            "temporary directories, which are always shared.");
        op.flags = ReadOnly;
        op.mcpTool = s("shared_directories_get");
        op.handler = [this](const Call&) {
            if (!m_preferences)
                return unavailable("Preferences");
            return ok(QJsonObject{
                {s("directories"), QJsonArray::fromStringList(m_preferences->sharedDirs())},
                {s("incomingDir"), m_preferences->incomingDir()},
                {s("tempDirs"), QJsonArray::fromStringList(m_preferences->tempDirs())},
            });
        };
        add(op);
    }
    {
        Operation op = make("shared.setDirectories", Method::Put, "/api/v1/shared/directories",
                            "Shared files", "Replace the shared directory list",
                            "Paths are on the machine the core runs on and must exist. The "
                            "share is rescanned; hashing new files takes a while.");
        op.mcpTool = s("shared_directories_set");
        op.params = {bodyArray(s("directories"), s("Absolute directory paths.")).req().range(0, 1000)};
        op.handler = [](const Call& c) {
            QStringList dirs;
            for (const QJsonValue& v : c.args.value(L("directories")).toArray())
                dirs.append(v.toString());
            return fromStatus(ops::setSharedDirectories(dirs),
                              {{s("directories"), QJsonArray::fromStringList(thePrefs.sharedDirs())}});
        };
        add(op);
    }
    {
        Operation op = make("shared.reload", Method::Post, "/api/v1/shared/reload",
                            "Shared files", "Rescan the shared directories");
        op.mcpTool = s("shared_reload");
        op.handler = [](const Call&) {
            return fromStatus(ops::reloadSharedFiles(), {{s("reloading"), true}});
        };
        add(op);
    }

    // --- Categories ---------------------------------------------------------------
    {
        Operation op = make("categories.list", Method::Get, "/api/v1/categories", "Categories",
                            "Download categories",
                            "A category is an index. Entry 0 is the default; a category can "
                            "have an incoming directory of its own.");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("categories_list");
        op.handler = [this](const Call& c) {
            if (!m_apiBackend)
                return unavailable("Categories");
            return pageOf(c, m_apiBackend->categories());
        };
        add(op);
    }
    const QList<Param> categoryFields{
        bodyStr(s("title"), s("Name.")).range(1, 100),
        bodyStr(s("incoming"), s("Incoming directory; empty = the global one.")).range(0, 4096),
        bodyStr(s("comment"), s("Free text.")).range(0, 1000),
        bodyStr(s("autocat"), s("File name patterns that sort new downloads into this "
                                "category, separated by |.")).range(0, 1000),
        bodyInt(s("prio"), s("Priority of new downloads in this category.")).range(0, 5),
    };
    {
        Operation op = make("categories.add", Method::Post, "/api/v1/categories", "Categories",
                            "Add a category");
        op.mcpTool = s("category_add");
        op.params = categoryFields;
        op.params[0].required = true;
        op.handler = [this](const Call& c) { return apiCategoryEdit(c, 0); };
        add(op);
    }
    {
        Operation op = make("categories.edit", Method::Patch, "/api/v1/categories/{index}",
                            "Categories", "Change a category");
        op.params = categoryFields;
        op.params.prepend(pathInt(s("index"), s("Category index; 1 or higher.")).atLeast(1));
        op.handler = [this](const Call& c) { return apiCategoryEdit(c, 1); };
        add(op);
    }
    {
        Operation op = make("categories.remove", Method::Delete, "/api/v1/categories/{index}",
                            "Categories", "Remove a category",
                            "Its downloads fall back to the default category; the higher "
                            "categories move down by one index.");
        op.flags = Destructive;
        op.params = {pathInt(s("index"), s("Category index; 1 or higher.")).atLeast(1)};
        op.handler = [this](const Call& c) { return apiCategoryEdit(c, 2); };
        add(op);
    }

    // --- Friends -------------------------------------------------------------------
    {
        Operation op = make("friends.list", Method::Get, "/api/v1/friends", "Friends",
                            "List friends");
        op.flags = ReadOnly | Paged;
        op.resultSchema = schemaPage(s("Friend"));
        op.handler = [this](const Call& c) {
            if (!m_friendList)
                return unavailable("Friend list");
            const auto& friends = m_friendList->friends();
            return page(c, static_cast<qsizetype>(friends.size()), [&friends](qsizetype i) {
                return QJsonValue(toJson(*friends[static_cast<size_t>(i)]));
            });
        };
        add(op);
    }
    {
        Operation op = make("friends.add", Method::Post, "/api/v1/friends", "Friends",
                            "Add a friend", "By user hash, by address and port, or both.");
        op.params = {
            bodyStr(s("hash"), s("User hash: 32 hexadecimal digits.")).range(0, 32),
            bodyStr(s("name"), s("Display name.")).range(0, 255),
            bodyStr(s("addr"), s("IPv4 or IPv6 address.")).range(0, 64),
            bodyNum(s("ip"), s("IPv4 address as a number; \"addr\" is preferred.")).atLeast(0),
            bodyInt(s("port"), s("TCP port.")).range(0, 65535),
        };
        op.resultSchema = schemaRef(s("Friend"));
        op.handler = [this](const Call& c) {
            if (!m_friendList)
                return unavailable("Friend list");
            const QString hashStr = c.str(L("hash"));
            // "addr" is the IPv6-capable form the list emits; "ip" stays for IPv4 callers.
            Address friendAddr = Address::fromString(c.str(L("addr")));
            if (friendAddr.isNull())
                friendAddr = Address::fromNetworkOrder(
                    static_cast<uint32>(c.args.value(L("ip")).toDouble()));

            std::array<uint8, 16> hashBytes{};
            const bool hasHash = hashStr.size() == 32
                && decodeBase16(hashStr, hashBytes.data(), 16) > 0;
            auto* f = m_friendList->addFriend(hasHash ? hashBytes.data() : nullptr, friendAddr,
                                              static_cast<uint16>(c.integer(L("port"))),
                                              c.str(L("name")), hasHash);
            if (f)
                saveFriends();
            return f ? ok(toJson(*f)) : error(400, s("Failed to add friend"));
        };
        add(op);
    }
    {
        Operation op = make("friends.remove", Method::Delete, "/api/v1/friends/{hash}", "Friends",
                            "Remove a friend");
        op.params = {pathStr(s("hash"), s("User hash: 32 hexadecimal digits.")).range(32, 32)};
        op.handler = [this](const Call& c) {
            if (!m_friendList)
                return unavailable("Friend list");
            std::array<uint8, 16> hashBytes{};
            if (decodeBase16(c.str(L("hash")), hashBytes.data(), 16) != 16)
                return error(400, s("Invalid hash format"));
            auto* f = m_friendList->searchFriend(hashBytes.data());
            if (!f)
                return error(404, s("Friend not found"));
            m_friendList->removeFriend(f);
            saveFriends();
            return ok(QJsonObject{{s("deleted"), true}});
        };
        add(op);
    }

    // --- Statistics and diagnostics ----------------------------------------------
    {
        Operation op = make("stats.get", Method::Get, "/api/v1/stats", "Diagnostics",
                            "Transfer statistics",
                            "Rates are KiB per second; byte counters cover this session.");
        op.flags = ReadOnly;
        op.mcpTool = s("stats_get");
        op.handler = [this](const Call&) {
            if (!m_statistics)
                return unavailable("Statistics");
            return ok(QJsonObject{
                {s("rateDown"), static_cast<double>(m_statistics->rateDown())},
                {s("rateUp"), static_cast<double>(m_statistics->rateUp())},
                {s("maxDown"), static_cast<double>(m_statistics->maxDown())},
                {s("maxUp"), static_cast<double>(m_statistics->maxUp())},
                {s("sessionReceivedBytes"), static_cast<qint64>(m_statistics->sessionReceivedBytes())},
                {s("sessionSentBytes"), static_cast<qint64>(m_statistics->sessionSentBytes())},
                {s("reconnects"), m_statistics->reconnects()},
                {s("uptimeSeconds"), static_cast<qint64>(m_statistics->uptimeSecs())},
            });
        };
        add(op);
    }
    {
        Operation op = make("logs.list", Method::Get, "/api/v1/logs", "Diagnostics",
                            "Recent log records",
                            "The newest records of the daemon's log ring, oldest first. Pass the "
                            "returned lastId as since to read only what is new.");
        op.flags = ReadOnly;
        op.mcpTool = s("logs_get");
        op.params = {
            queryInt(s("limit"), s("Newest records to return.")).range(1, kMaxPageSize).def(100),
            queryInt(s("since"), s("Only records with an id above this.")).atLeast(0).def(0),
            queryStr(s("level"), s("Lowest severity to include."))
                .oneOf({s("debug"), s("info"), s("warning"), s("error")}).def(s("debug")),
        };
        op.handler = [this](const Call& c) { return apiLogs(c); };
        add(op);
    }
    {
        Operation op = make("network.get", Method::Get, "/api/v1/network", "Diagnostics",
                            "Network information",
                            "Ports, public addresses, the bound interface and whether it is "
                            "present, both networks' state, port mapping and the web server.");
        op.flags = ReadOnly;
        op.mcpTool = s("network_get");
        op.handler = [this](const Call&) { return apiNetwork(); };
        add(op);
    }
    {
        Operation op = make("nat.get", Method::Get, "/api/v1/nat", "Diagnostics",
                            "Port mapping state",
                            "What the router granted: method (UPnP, NAT-PMP, PCP), external "
                            "address and one entry per mapped port.");
        op.flags = ReadOnly;
        op.handler = [](const Call&) {
            return ok(ops::networkInfo().value(QStringLiteral("portmap")).toMap().toJsonObject());
        };
        add(op);
    }
    {
        Operation op = make("nat.refresh", Method::Post, "/api/v1/nat/refresh", "Diagnostics",
                            "Redo port mapping and the firewall check",
                            "Asks the router again and, when Kad runs, re-tests whether the "
                            "ports are reachable. The result shows up in GET /network.");
        op.mcpTool = s("firewall_recheck");
        op.handler = [](const Call&) {
            const ops::Status st = ops::recheckFirewall();
            return ok(QJsonObject{{s("portMapping"), s("reprobing")},
                                  {s("kadFirewallCheck"), st.ok() ? s("started") : st.message}});
        };
        add(op);
    }
    {
        Operation op = make("diagnostics.get", Method::Get, "/api/v1/diagnostics", "Diagnostics",
                            "Diagnostic bundle",
                            "Version, network information and the latest warnings and errors "
                            "in one answer. For a bug report or a first look at a problem.");
        op.flags = ReadOnly;
        op.handler = [this](const Call&) { return apiDiagnostics(); };
        add(op);
    }

    // --- Preferences ---------------------------------------------------------------
    {
        Operation op = make("preferences.schema", Method::Get, "/api/v1/preferences/schema",
                            "Preferences", "Preference metadata",
                            "Every preference reachable here: type, range, unit, default, "
                            "whether a change needs a restart and whether it can be changed "
                            "remotely. Credentials and program paths are not offered.");
        op.flags = ReadOnly;
        op.mcpTool = s("preferences_schema");
        op.handler = [](const Call&) {
            return ok(QJsonObject{{s("items"), preferenceSchemaJson()}});
        };
        add(op);
    }
    {
        Operation op = make("preferences.get", Method::Get, "/api/v1/preferences", "Preferences",
                            "Current preference values", "One value per key of the schema.");
        op.flags = ReadOnly;
        op.mcpTool = s("preferences_get");
        op.handler = [this](const Call&) {
            return m_preferences ? ok(preferenceValues(*m_preferences))
                                 : unavailable("Preferences");
        };
        add(op);
    }
    {
        Operation op = make("preferences.patch", Method::Patch, "/api/v1/preferences",
                            "Preferences", "Change preferences",
                            "A JSON object of key and new value. Every key is checked against "
                            "the schema first; one bad value and nothing is changed. Applied at "
                            "once unless the schema says restartRequired.");
        op.flags = FreeBody;
        op.mcpTool = s("preferences_set");
        op.handler = [this](const Call& c) {
            if (!m_preferences || !m_apiBackend)
                return unavailable("Preferences");
            PrefChanges changes;
            if (const QString problem = checkPreferencePatch(c.args, changes); !problem.isEmpty())
                return error(400, problem);
            if (!changes.isEmpty()) {
                if (const ops::Status st = m_apiBackend->applyPreferences(changes); !st.ok())
                    return error(st.code, st.message);
            }
            return ok(preferenceValues(*m_preferences));
        };
        add(op);
    }

    addUsenetOperations();

    // --- Streams (token in the query, not the API key; routed outside the table) ---
    {
        Operation op = make("downloads.preview", Method::Get, "/api/v1/downloads/{hash}/preview",
                            "Streams", "Byte-range stream of a download or shared file",
                            "For media players. Authenticated by ?token=, the per-process "
                            "stream token, not by the API key.");
        op.flags = ReadOnly | RawStream;
        op.params = {hashParam(), queryStr(s("token"), s("Stream token.")).req()};
        add(op);
    }
    {
        Operation op = make("incoming.list", Method::Get, "/api/v1/incoming", "Streams",
                            "Browse the incoming folder (HTML)",
                            "Authenticated by ?token=, the stream token.");
        op.flags = ReadOnly | RawStream;
        op.params = {queryStr(s("token"), s("Stream token.")).req(),
                     queryStr(s("path"), s("Folder relative to the incoming directory."))};
        add(op);
    }
}

// ---------------------------------------------------------------------------
// Usenet
// ---------------------------------------------------------------------------

void WebServer::addUsenetOperations()
{
    const auto add = [this](Operation op) { m_api.add(std::move(op)); };
    const auto idParam = [] { return pathStr(s("id"), s("Queue item id.")); };

    {
        Operation op = make("usenet.stats", Method::Get, "/api/v1/usenet/stats", "Usenet",
                            "Usenet queue totals");
        op.flags = ReadOnly;
        op.handler = [this](const Call&) { return toResult(handleRestUsenetStats()); };
        add(op);
    }
    {
        Operation op = make("usenet.pauseAll", Method::Post, "/api/v1/usenet/pause", "Usenet",
                            "Pause the Usenet engine");
        op.handler = [this](const Call&) { return toResult(handleUsenetEngineOp(true)); };
        add(op);
    }
    {
        Operation op = make("usenet.resumeAll", Method::Post, "/api/v1/usenet/resume", "Usenet",
                            "Resume the Usenet engine");
        op.handler = [this](const Call&) { return toResult(handleUsenetEngineOp(false)); };
        add(op);
    }
    {
        Operation op = make("usenet.categoryAction", Method::Post,
                            "/api/v1/usenet/categories/{category}/{action}", "Usenet",
                            "Pause, resume or cancel every release of a category");
        op.params = {
            pathInt(s("category"), s("Category index.")).atLeast(0),
            pathStr(s("action"), s("What to do.")).oneOf({s("pause"), s("resume"), s("cancel")}),
        };
        op.handler = [this](const Call& c) {
            return toResult(handleRestUsenetCategoryOp(QString::number(c.integer(L("category"))),
                                                       c.str(L("action"))));
        };
        add(op);
    }
    {
        Operation op = make("usenet.list", Method::Get, "/api/v1/usenet", "Usenet",
                            "List Usenet downloads",
                            "One row per release, with progress, speed and post-processing "
                            "state.");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("usenet_list");
        op.params = {queryInt(s("category"), s("Only this category; 0 = all.")).atLeast(0).def(0)};
        op.handler = [this](const Call& c) {
            const Result rows = toResult(handleRestUsenetList(static_cast<int>(c.integer(L("category")))));
            return rows.ok() ? pageOf(c, rows.body.toArray()) : rows;
        };
        add(op);
    }
    {
        Operation op = make("usenet.add", Method::Post, "/api/v1/usenet", "Usenet",
                            "Add a Usenet download",
                            "Either post the .nzb file itself as the request body (options in "
                            "the query string), or a JSON object with the URL of one. 409 asks "
                            "a question — already downloaded — that \"force\" answers.");
        op.flags = RawBody;
        op.mcpTool = s("usenet_add_url");
        op.params = {
            bodyStr(s("url"), s("http(s) URL of an .nzb file.")).range(0, 4096),
            bodyStr(s("name"), s("Release name; default: taken from the NZB.")).range(0, 255),
            bodyBool(s("force"), s("Add even though it was downloaded before.")),
            bodyStr(s("password"), s("Archive password.")).range(0, 255),
            bodyBool(s("paused"), s("Add paused.")),
            bodyInt(s("category"), s("Category index.")).atLeast(0),
            bodyInt(s("priority"), s("-2 (lowest) to 2 (highest).")).range(-2, 2),
        };
        op.asyncHandler = [this](const Call& c) -> QFuture<Result> {
            if (c.request)
                return toResult(handleUsenetAdd(*c.request, /*restApi*/ true));
            // An MCP tool call: a URL and nothing else.
            if (c.str(L("url")).isEmpty())
                return Registry::finished(error(400, s("Missing 'url'")));
            UsenetWebAddOptions options;
            options.force = c.flag(L("force"));
            options.password = c.str(L("password"));
            options.paused = c.flag(L("paused"));
            return toResult(usenetAddResolved(
                c.str(L("url")), c.str(L("name")), options,
                c.has(L("category")) ? QString::number(c.integer(L("category"))) : QString(),
                c.has(L("priority")) ? QString::number(c.integer(L("priority"))) : QString(), {}));
        };
        add(op);
    }
    {
        Operation op = make("usenet.get", Method::Get, "/api/v1/usenet/{id}", "Usenet",
                            "One Usenet download in full");
        op.flags = ReadOnly;
        op.mcpTool = s("usenet_get");
        op.params = {idParam()};
        op.handler = [this](const Call& c) { return toResult(handleRestUsenetItem(c.str(L("id")))); };
        add(op);
    }
    {
        Operation op = make("usenet.edit", Method::Patch, "/api/v1/usenet/{id}", "Usenet",
                            "Change a Usenet download",
                            "All fields are checked before any is applied.");
        op.params = {
            idParam(),
            bodyInt(s("priority"), s("-2 (lowest) to 2 (highest).")).range(-2, 2),
            bodyInt(s("category"), s("Category index.")).atLeast(0),
            bodyStr(s("password"), s("Archive password.")).range(0, 255),
            bodyArray(s("skipFiles"), s("File indices not to download.")).of(ParamType::Integer),
            bodyArray(s("unskipFiles"), s("File indices to download after all.")).of(ParamType::Integer),
        };
        op.handler = [this](const Call& c) {
            QJsonObject body = c.args;
            body.remove(L("id"));
            return toResult(handleRestUsenetPatch(c.str(L("id")), body));
        };
        add(op);
    }
    {
        Operation op = make("usenet.remove", Method::Delete, "/api/v1/usenet/{id}", "Usenet",
                            "Remove a Usenet download",
                            "With deleteFiles the downloaded data goes too.");
        op.flags = Destructive;
        op.mcpTool = s("usenet_remove");
        op.params = {idParam(),
                     queryBool(s("deleteFiles"), s("Also delete what was downloaded.")).def(false)};
        op.handler = [this](const Call& c) {
            return toResult(handleRestUsenetDelete(c.str(L("id")), c.flag(L("deleteFiles"))));
        };
        add(op);
    }
    {
        Operation op = make("usenet.entries", Method::Get, "/api/v1/usenet/{id}/{fileIndex}/entries",
                            "Usenet", "Files inside an archive of a release");
        op.flags = ReadOnly;
        op.params = {idParam(), pathInt(s("fileIndex"), s("Index of the file in the NZB.")).atLeast(0)};
        op.handler = [this](const Call& c) {
            return toResult(handleRestUsenetEntries(c.str(L("id")),
                                                    QString::number(c.integer(L("fileIndex")))));
        };
        add(op);
    }
    {
        Operation op = make("usenet.itemAction", Method::Post, "/api/v1/usenet/{id}/{action}",
                            "Usenet", "Pause, resume or health-check a Usenet download");
        op.mcpTool = s("usenet_item_action");
        op.params = {idParam(),
                     pathStr(s("action"), s("What to do."))
                         .oneOf({s("pause"), s("resume"), s("check")})};
        op.handler = [this](const Call& c) {
            return toResult(handleRestUsenetItemOp(c.str(L("id")), c.str(L("action"))));
        };
        add(op);
    }
    {
        Operation op = make("usenet.preview", Method::Get, "/api/v1/usenet/{id}/{fileIndex}/preview",
                            "Streams", "Byte-range stream of a Usenet file",
                            "Authenticated by ?token=, the stream token. Waits up to 15 seconds "
                            "for bytes that are still downloading.");
        op.flags = ReadOnly | RawStream;
        op.params = {idParam(), pathInt(s("fileIndex"), s("Index of the file in the NZB.")).atLeast(0),
                     queryStr(s("token"), s("Stream token.")).req()};
        add(op);
    }
}

// ---------------------------------------------------------------------------
// HTTP plumbing
// ---------------------------------------------------------------------------

void WebServer::registerApiRoutes()
{
    // Literal paths before the ones with parameters, so "stats" is never taken for an id.
    QList<const Operation*> ordered;
    for (const Operation& op : m_api.operations()) {
        if (!op.is(RawStream))
            ordered.append(&op);
    }
    std::ranges::stable_sort(ordered, {}, [](const Operation* op) {
        return Registry::pathParamNames(op->path).size();
    });

    static const QRegularExpression placeholder(QStringLiteral("\\{[^}]+\\}"));
    for (const Operation* op : std::as_const(ordered)) {
        QString route = op->path;
        route.replace(placeholder, QStringLiteral("<arg>"));
        const auto method = op->method == Method::Get ? QHttpServerRequest::Method::Get
                          : op->method == Method::Post ? QHttpServerRequest::Method::Post
                          : op->method == Method::Put ? QHttpServerRequest::Method::Put
                          : op->method == Method::Patch ? QHttpServerRequest::Method::Patch
                                                        : QHttpServerRequest::Method::Delete;

        switch (Registry::pathParamNames(op->path).size()) {
        case 0:
            m_server->route(route, method, [this, op](const QHttpServerRequest& req) {
                return runOperation(*op, {}, req);
            });
            break;
        case 1:
            m_server->route(route, method,
                [this, op](const QString& a, const QHttpServerRequest& req) {
                    return runOperation(*op, {a}, req);
                });
            break;
        case 2:
            m_server->route(route, method,
                [this, op](const QString& a, const QString& b, const QHttpServerRequest& req) {
                    return runOperation(*op, {a, b}, req);
                });
            break;
        default:
            logError(QStringLiteral("WebServer: route %1 has too many path parameters").arg(op->path));
            break;
        }
    }

    // Anything else under /api/v1: the same error envelope, not Qt's bare 404.
    m_server->setMissingHandler(this, [](const QHttpServerRequest& req,
                                         QHttpServerResponder& responder) {
        if (req.url().path().startsWith(QLatin1StringView("/api/v1/"))) {
            responder.sendResponse(toResponse(error(404,
                QStringLiteral("No such route: %1. GET /api/v1/openapi.json lists them.")
                    .arg(req.url().path()))));
            return;
        }
        responder.sendResponse(QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound));
    });
}

QFuture<QHttpServerResponse> WebServer::runOperation(const Operation& op,
                                                     const QStringList& pathValues,
                                                     const QHttpServerRequest& req)
{
    const auto finished = [](QHttpServerResponse&& response) {
        QPromise<QHttpServerResponse> promise;
        QFuture<QHttpServerResponse> future = promise.future();
        promise.start();
        promise.addResult(std::move(response));
        promise.finish();
        return future;
    };

    if (!op.is(NoAuth)) {
        if (auto auth = checkAuth(req.headers()); !auth.ok)
            return finished(std::move(auth.response));
    }

    // Path values arrive percent-decoded already.
    const Registry::Parsed parsed = m_api.parseHttp(op, pathValues, QUrlQuery(req.query()),
                                                    op.is(RawBody) ? QByteArray() : req.body());
    if (!parsed.ok)
        return finished(toResponse(parsed.failure));

    Call call;
    call.args = parsed.args;
    call.request = &req;

    if (!op.asyncHandler)
        return finished(toResponse(op.handler ? op.handler(call)
                                              : error(501, QStringLiteral("Not implemented"))));

    return op.asyncHandler(call).then(this, [](const Result& result) {
        return toResponse(result);
    });
}

QHttpServerResponse WebServer::toResponse(const Result& result)
{
    const auto status = static_cast<QHttpServerResponse::StatusCode>(result.status);
    QHttpServerResponse response = result.body.isArray()
        ? QHttpServerResponse(result.body.toArray(), status)
        : QHttpServerResponse(result.body.toObject(), status);
    QHttpHeaders headers = response.headers();
    headers.append(QByteArrayLiteral("X-Contract-Version"), QByteArray::number(kContractVersion));
    response.setHeaders(std::move(headers));
    return response;
}

Result WebServer::toResult(const QHttpServerResponse& response)
{
    const QJsonDocument doc = QJsonDocument::fromJson(response.data());
    Result result;
    result.status = static_cast<int>(response.statusCode());
    result.body = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
    return result;
}

QFuture<Result> WebServer::toResult(QFuture<QHttpServerResponse> response)
{
    return response.then(this, [](const QHttpServerResponse& r) { return toResult(r); });
}

// ---------------------------------------------------------------------------
// Handlers too long for the table
// ---------------------------------------------------------------------------

Result WebServer::apiSnapshot()
{
    QJsonObject out{
        {s("version"), QString(kAppVersion)},
        {s("uptimeSeconds"), m_statistics ? static_cast<qint64>(m_statistics->uptimeSecs()) : qint64(0)},
        {s("netBlocked"), !BindAddress::outboundAllowed()},
        {s("netBlockReason"), BindAddress::current().reason},
    };

    QJsonObject ed2k{{s("connected"), false}, {s("connecting"), false}};
    if (m_serverConnect) {
        ed2k[s("connected")] = m_serverConnect->isConnected();
        ed2k[s("connecting")] = m_serverConnect->isConnecting();
        ed2k[s("lowID")] = m_serverConnect->isConnected() && m_serverConnect->isLowID();
        if (const auto* server = m_serverConnect->currentServer())
            ed2k[s("server")] = QStringLiteral("%1:%2").arg(server->address()).arg(server->port());
    }
    out[s("ed2k")] = ed2k;

    auto* kad = kad::Kademlia::instance();
    out[s("kad")] = QJsonObject{
        {s("running"), kad && kad->isRunning()},
        {s("connected"), kad && kad->isConnected()},
        {s("firewalled"), kad && kad->isFirewalled()},
    };

    if (m_statistics) {
        out[s("rates")] = QJsonObject{
            {s("downKiBps"), static_cast<double>(m_statistics->rateDown())},
            {s("upKiBps"), static_cast<double>(m_statistics->rateUp())},
        };
    }

    if (m_downloadQueue) {
        int transferring = 0, paused = 0, completed = 0;
        for (const PartFile* file : m_downloadQueue->files()) {
            if (file->status() == PartFileStatus::Complete)
                ++completed;
            else if (file->isPaused() || file->isStopped())
                ++paused;
            else if (file->transferringSrcCount() > 0)
                ++transferring;
        }
        out[s("downloads")] = QJsonObject{
            {s("total"), static_cast<qint64>(m_downloadQueue->files().size())},
            {s("transferring"), transferring},
            {s("paused"), paused},
            {s("completed"), completed},
        };
    }
    if (m_uploadQueue) {
        out[s("uploads")] = QJsonObject{
            {s("active"), m_uploadQueue->uploadQueueLength()},
            {s("waiting"), m_uploadQueue->waitingUserCount()},
        };
    }
    if (m_sharedFiles)
        out[s("sharedFiles")] = m_sharedFiles->getCount();
    if (m_serverList)
        out[s("servers")] = static_cast<qint64>(m_serverList->servers().size());
    if (usenetAvailable())
        out[s("usenet")] = usenetStatsJson(usenetRows());
    return ok(out);
}

Result WebServer::apiNetwork() const
{
    QJsonObject info = ops::networkInfo().toJsonObject();
    info.insert(s("configuredBind"), m_preferences ? m_preferences->bindAddress() : QString());
    info.insert(s("web"), QJsonObject{
        {s("port"), port()},
        {s("https"), isHttps()},
        {s("webUi"), m_config.webUiEnabled},
        {s("restApi"), m_config.restApiEnabled},
        {s("mcp"), m_config.mcpEnabled},
        {s("sessions"), sessionCount()},
    });
    return ok(info);
}

Result WebServer::apiLogs(const Call& call) const
{
    if (!m_apiBackend)
        return unavailable("Log");

    const int minRank = severityRank(call.str(L("level")));
    const QList<ApiBackend::LogRecord> records = m_apiBackend->logs(call.integer(L("since")));

    QJsonArray items;
    qint64 lastId = call.integer(L("since"));
    for (const ApiBackend::LogRecord& record : records) {
        lastId = std::max(lastId, record.id);
        const QString severity = severityName(record.severity);
        if (severityRank(severity) < minRank)
            continue;
        items.append(QJsonObject{
            {s("id"), record.id},
            {s("timestamp"), record.timestamp},
            {s("severity"), severity},
            {s("category"), record.category},
            {s("message"), record.message},
        });
    }
    // The newest ones, still oldest first.
    const qsizetype limit = call.limit();
    while (items.size() > limit)
        items.removeFirst();
    return ok(QJsonObject{{s("items"), items}, {s("lastId"), lastId}});
}

Result WebServer::apiDiagnostics()
{
    QJsonObject out{
        {s("version"), QString(kAppVersion)},
        {s("contractVersion"), kContractVersion},
        {s("qtVersion"), QString::fromLatin1(qVersion())},
        {s("os"), QSysInfo::prettyProductName()},
        {s("network"), apiNetwork().body},
        {s("snapshot"), apiSnapshot().body},
    };
    if (m_apiBackend) {
        QJsonArray problems;
        for (const ApiBackend::LogRecord& record : m_apiBackend->logs(0)) {
            if (record.severity != QtWarningMsg && record.severity != QtCriticalMsg
                && record.severity != QtFatalMsg)
                continue;
            problems.append(QJsonObject{
                {s("timestamp"), record.timestamp},
                {s("severity"), severityName(record.severity)},
                {s("message"), record.message},
            });
        }
        while (problems.size() > 50)
            problems.removeFirst();
        out[s("recentProblems")] = problems;
    }
    return ok(out);
}

Result WebServer::apiSearchStart(const Call& call)
{
    if (!m_searchList)
        return unavailable("Search list");

    const QString expression = call.str(L("expression"));
    SearchParams params;
    params.expression = expression;
    params.keyword = expression;
    params.searchTitle = expression;

    const auto number = [&call](L key) {
        const double v = call.args.value(key).toDouble();
        return v > 0 ? static_cast<uint64>(v) : uint64{0};
    };
    params.fileType        = call.str(L("fileType"));
    params.extension       = call.str(L("extension"));
    params.codec           = call.str(L("codec"));
    params.title           = call.str(L("title"));
    params.album           = call.str(L("album"));
    params.artist          = call.str(L("artist"));
    params.minSize         = number(L("minSize"));
    params.maxSize         = number(L("maxSize"));
    params.availability    = static_cast<uint32>(number(L("availability")));
    params.completeSources = static_cast<uint32>(number(L("completeSources")));
    params.minBitrate      = static_cast<uint32>(number(L("minBitrate")));
    params.minLength       = static_cast<uint32>(number(L("minLength")));

    const QString type = call.str(L("type"));
    params.type = type == L("kad") ? SearchType::Kademlia
                : type == L("usenetServer") ? SearchType::MetaUsenet
                : type == L("torrentServer") ? SearchType::MetaTorrent
                : type == L("ed2kGlobal") ? SearchType::Ed2kGlobal
                : type == L("ed2kServer") ? SearchType::Ed2kServer
                                          : SearchType::Automatic;

    // The same entry point the GUI uses, so a search here really goes out.
    const SearchStartResult outcome = startSearch(*m_searchList, params);
    if (!outcome.ok)
        return error(409, outcome.error);

    // "automatic" is resolved once the search is sent; until then it is what was asked.
    QJsonObject result{
        {s("searchID"), static_cast<qint64>(outcome.searchID)},
        {s("type"), searchTypeName(outcome.type)},
        {s("state"), searchRunStateName(outcome.state)},
    };
    if (!outcome.started) {
        // Not sent yet: it goes out by itself once the network is there and the
        // search before it is done. 202 — taken, not done.
        result.insert(s("reason"), outcome.reason);
        return accepted(result);
    }
    return ok(result);
}

Result WebServer::apiSearchResults(const Call& call) const
{
    if (!m_searchList)
        return unavailable("Search list");

    const auto searchID = static_cast<uint32>(call.integer(L("searchID")));
    std::vector<const SearchFile*> files;
    if (!m_searchList->forEachResult(searchID, [&files](const SearchFile* file) {
            files.push_back(file);
        }))
        return error(404, s("Search not found"));

    std::ranges::stable_sort(files, std::greater{}, [](const SearchFile* file) {
        return file->sourceCount();
    });

    Result result = page(call, static_cast<qsizetype>(files.size()), [&files](qsizetype i) {
        return QJsonValue(toJson(*files[static_cast<size_t>(i)]));
    });
    QJsonObject body = result.body.toObject();
    body.insert(s("searchID"), static_cast<qint64>(searchID));
    body.insert(s("foundFiles"), static_cast<qint64>(m_searchList->foundFiles(searchID)));
    body.insert(s("foundSources"), static_cast<qint64>(m_searchList->foundSources(searchID)));
    // Whether there is more to wait for: queued / running / finished / failed.
    if (const auto status = m_searchList->queue().status(searchID)) {
        body.insert(s("state"), searchRunStateName(status->state));
        if (!status->reason.isEmpty())
            body.insert(s("reason"), status->reason);
        if (!status->error.isEmpty())
            body.insert(s("error"), status->error);
        // a further page the server holds: POST /search/{id}/more
        body.insert(s("hasMore"), status->hasMore);
    }
    result.body = body;
    return result;
}

Result WebServer::apiSearchList(const Call& call) const
{
    if (!m_searchList)
        return unavailable("Search list");

    const std::vector<uint32> ids = m_searchList->searchIDs();
    return page(call, static_cast<qsizetype>(ids.size()), [this, &ids](qsizetype i) {
        const uint32 id = ids[static_cast<size_t>(i)];
        QJsonObject row{
            {s("searchID"), static_cast<qint64>(id)},
            {s("resultCount"), static_cast<qint64>(m_searchList->resultCount(id))},
        };
        if (const auto status = m_searchList->queue().status(id)) {
            row.insert(s("state"), searchRunStateName(status->state));
            row.insert(s("type"), searchTypeName(status->type));
            row.insert(s("hasMore"), status->hasMore);
            if (!status->primaryKeyword.isEmpty())
                row.insert(s("keyword"), status->primaryKeyword);
        }
        return QJsonValue(row);
    });
}

Result WebServer::apiCategoryEdit(const Call& call, int mode)
{
    if (!m_apiBackend)
        return unavailable("Categories");

    // The store takes the whole list; `oldIndex` tells it which entry is which, so
    // the downloads of a moved or removed category are renumbered with it.
    QJsonArray list;
    const QJsonArray current = m_apiBackend->categories();
    for (qsizetype i = 0; i < current.size(); ++i) {
        QJsonObject entry = current.at(i).toObject();
        entry.insert(s("oldIndex"), i);
        entry.remove(L("resolvedIncoming"));
        list.append(entry);
    }

    const auto applyFields = [&call](QJsonObject& entry) {
        for (const L key : {L("title"), L("incoming"), L("comment"), L("autocat"), L("prio")}) {
            if (call.has(key))
                entry.insert(key, call.args.value(key));
        }
    };

    const qsizetype index = call.integer(L("index"), -1);
    if (mode != 0 && (index < 1 || index >= list.size()))
        return error(404, s("Category not found"));

    if (mode == 0) {
        QJsonObject entry;
        applyFields(entry);
        list.append(entry);
    } else if (mode == 1) {
        QJsonObject entry = list.at(index).toObject();
        const QJsonObject untouched = entry;
        applyFields(entry);
        if (entry == untouched)
            return error(400, s("Nothing to change"));
        list[index] = entry;
    } else {
        list.removeAt(index);
    }

    if (const ops::Status st = m_apiBackend->setCategories(list); !st.ok())
        return error(st.code, st.message);
    return ok(QJsonObject{{s("items"), m_apiBackend->categories()}});
}

void WebServer::saveFriends() const
{
    if (!m_friendList || !m_preferences)
        return;
    if (const QString dir = m_preferences->configDir(); !dir.isEmpty())
        m_friendList->save(dir);
}

} // namespace eMule
