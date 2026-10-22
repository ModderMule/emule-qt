#include "pch.h"
/// @file WebServerTorrents.cpp
/// @brief The /api/v1/torrents operations. Only a BitTorrent build has them: without
///        EMULE_HAVE_BITTORRENT the table gains nothing and the routes do not exist.

#include "webserver/WebServer.h"

#ifdef EMULE_HAVE_BITTORRENT

#include <QHttpServerRequest>
#include <QJsonDocument>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>

#endif

namespace eMule {

#ifndef EMULE_HAVE_BITTORRENT

void WebServer::addTorrentOperations()
{
}

#else

using namespace api;

namespace {

using L = QLatin1StringView;

[[nodiscard]] QString s(const char* text) { return QString::fromUtf8(text); }

[[nodiscard]] Operation make(const char* id, Method method, const char* path,
                             const char* summary, const char* description = "")
{
    Operation op;
    op.id = s(id);
    op.method = method;
    op.path = s(path);
    op.tag = s("Torrents");
    op.summary = s(summary);
    op.description = s(description);
    return op;
}

/// bt::BtState, which core cannot name. Index = wire value.
const QStringList& stateNames()
{
    static const QStringList names{
        s("queued"), s("checkingFiles"), s("downloadingMetadata"), s("downloading"),
        s("finished"), s("seeding"), s("checkingResume"), s("paused"), s("error"), s("moving"),
    };
    return names;
}

enum WireState { Queued = 0, CheckingFiles = 1, DownloadingMetadata = 2, Downloading = 3,
                 Finished = 4, Seeding = 5, CheckingResume = 6, ErrorState = 8 };

/// bt::BtLimitMode. Index = wire value.
const QStringList& limitModeNames()
{
    static const QStringList names{s("global"), s("unlimited"), s("custom")};
    return names;
}

const QStringList& filterNames()
{
    static const QStringList names{s("all"), s("downloading"), s("seeding"), s("paused"),
                                   s("active"), s("error")};
    return names;
}

const QStringList& actionNames()
{
    static const QStringList names{s("pause"), s("resume"), s("recheck"), s("reannounce")};
    return names;
}

[[nodiscard]] Param idParam()
{
    return pathStr(s("id"), s("Torrent id: bt:v1:<40 hex> or bt:v2:<64 hex>.")).range(46, 70);
}

[[nodiscard]] Result notRunning()
{
    return error(503, s("BitTorrent is switched off"));
}

[[nodiscard]] Result notFound()
{
    return error(404, s("Torrent not found"));
}

[[nodiscard]] bool isComplete(const QCborMap& row)
{
    const qint64 state = row.value(L("state")).toInteger();
    if (state == Finished || state == Seeding)
        return true;
    const qint64 size = row.value(L("size")).toInteger();
    return row.value(L("hasMetadata")).toBool() && size > 0 && row.value(L("done")).toInteger() >= size;
}

/// The GUI's filter buttons (TorrentFilterProxy::matches), on a wire row.
[[nodiscard]] bool matchesFilter(const QCborMap& row, const QString& filter)
{
    const qint64 state = row.value(L("state")).toInteger();
    const bool paused = row.value(L("paused")).toBool();
    if (filter == L("downloading")) {
        switch (state) {
        case Downloading:
        case DownloadingMetadata:
        case CheckingFiles:
        case CheckingResume:
            return true;
        case Queued:
            return !isComplete(row);
        default:
            return false;
        }
    }
    if (filter == L("seeding"))
        return isComplete(row) && !paused;
    if (filter == L("paused"))
        return paused;
    if (filter == L("active"))
        return row.value(L("rateDown")).toInteger() > 0 || row.value(L("rateUp")).toInteger() > 0;
    if (filter == L("error"))
        return state == ErrorState || !row.value(L("error")).toString().isEmpty();
    return true;
}

/// A wire row as JSON: state by name, the piece map left to GET {id}.
[[nodiscard]] QJsonObject rowJson(const QCborMap& row, bool withMaps)
{
    QJsonObject o = row.toJsonObject();
    const auto state = row.value(L("state")).toInteger();
    o.insert(L("state"), stateNames().value(static_cast<qsizetype>(state), s("unknown")));
    o.insert(L("complete"), isComplete(row));
    if (!withMaps)
        o.remove(L("pieceMap"));
    return o;
}

[[nodiscard]] QJsonObject optionsJson(const QCborMap& m)
{
    const auto mode = [&m](const char* key) {
        return limitModeNames().value(static_cast<qsizetype>(m.value(L(key)).toInteger()), s("global"));
    };
    return QJsonObject{
        {s("downloadLimitKb"), m.value(L("downloadLimitKb")).toInteger()},
        {s("uploadLimitKb"), m.value(L("uploadLimitKb")).toInteger()},
        {s("maxPeers"), m.value(L("maxPeers")).toInteger()},
        {s("ratioMode"), mode("ratioMode")},
        {s("ratio"), static_cast<double>(m.value(L("ratioPercent")).toInteger(100)) / 100.0},
        {s("seedTimeMode"), mode("seedTimeMode")},
        {s("seedTimeMinutes"), m.value(L("seedTimeMinutes")).toInteger()},
    };
}

[[nodiscard]] QJsonArray listJson(const QCborMap& details, const char* key)
{
    return details.value(L(key)).toArray().toJsonArray();
}

[[nodiscard]] QString addOutcomeName(TorrentWebAddOutcome outcome)
{
    switch (outcome) {
    case TorrentWebAddOutcome::Added:      return s("added");
    case TorrentWebAddOutcome::Duplicate:  return s("duplicate");
    case TorrentWebAddOutcome::Invalid:    return s("invalid");
    case TorrentWebAddOutcome::NotRunning: return s("notRunning");
    case TorrentWebAddOutcome::Failed:     break;
    }
    return s("failed");
}

[[nodiscard]] Result addReply(const TorrentWebAddResult& r)
{
    const QJsonObject body{{s("id"), r.id}, {s("outcome"), addOutcomeName(r.outcome)},
                           {s("message"), r.message}};
    switch (r.outcome) {
    case TorrentWebAddOutcome::Added:      return ok(body);
    case TorrentWebAddOutcome::Duplicate:  return {409, body};
    case TorrentWebAddOutcome::Invalid:    return {400, body};
    case TorrentWebAddOutcome::NotRunning: return {503, body};
    case TorrentWebAddOutcome::Failed:     break;
    }
    return {500, body};
}

[[nodiscard]] QStringList stringList(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& item : v.toArray()) {
        if (const QString url = item.toString().trimmed(); !url.isEmpty())
            out.append(url);
    }
    return out;
}

} // namespace

void WebServer::addTorrentOperations()
{
    const auto add = [this](Operation op) { m_api.add(std::move(op)); };

    // Every per-torrent route: 503 without the engine, 404 for an unknown id.
    const auto withTorrent = [this](const Call& c, auto&& body) -> Result {
        if (!m_torrentBackend)
            return error(503, s("BitTorrent engine not available"));
        if (!m_torrentBackend->running())
            return notRunning();
        const QString id = c.str(L("id"));
        const QCborMap row = m_torrentBackend->torrent(id);
        if (row.isEmpty())
            return notFound();
        return body(id, row);
    };

    {
        Operation op = make("torrents.status", Method::Get, "/api/v1/torrents/status",
                            "BitTorrent engine state",
                            "Whether the engine runs (the bittorrent.enabled preference), its "
                            "listen port, DHT nodes, torrent count and total rates in bytes/s.");
        op.flags = ReadOnly;
        op.handler = [this](const Call&) {
            if (!m_torrentBackend)
                return error(503, s("BitTorrent engine not available"));
            return ok(m_torrentBackend->status().toJsonObject());
        };
        add(op);
    }
    {
        Operation op = make("torrents.list", Method::Get, "/api/v1/torrents", "List torrents",
                            "One row per torrent: state, progress, rates in bytes/s, peers, "
                            "seeds, ratio totals and dates (unix seconds). \"filter\" picks the "
                            "same groups as the filter bar of the Torrents tab. Empty while "
                            "BitTorrent is switched off.");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("torrents_list");
        op.params = {queryStr(s("filter"), s("Which torrents.")).oneOf(filterNames()).def(s("all"))};
        op.handler = [this](const Call& c) {
            if (!m_torrentBackend)
                return error(503, s("BitTorrent engine not available"));
            const QString filter = c.str(L("filter"));
            QJsonArray rows;
            if (m_torrentBackend->running()) {
                for (const QCborValue& v : m_torrentBackend->torrents()) {
                    const QCborMap row = v.toMap();
                    if (matchesFilter(row, filter))
                        rows.append(rowJson(row, false));
                }
            }
            return pageOf(c, rows);
        };
        add(op);
    }
    {
        Operation op = make("torrents.add", Method::Post, "/api/v1/torrents", "Add a torrent",
                            "Either post the .torrent file itself as the request body (\"paused\" "
                            "in the query string), or a JSON object with a magnet link. Saved to "
                            "the BitTorrent download folder. 409: already there (the reply "
                            "carries its id).");
        op.flags = RawBody;
        op.mcpTool = s("torrents_add_magnet");
        op.params = {
            bodyStr(s("magnet"), s("magnet: link.")).range(8, 65536),
            bodyBool(s("paused"), s("Add paused.")),
        };
        op.handler = [this](const Call& c) -> Result {
            if (!m_torrentBackend)
                return error(503, s("BitTorrent engine not available"));
            if (!m_torrentBackend->running())
                return notRunning();

            QString magnet = c.str(L("magnet"));
            bool paused = c.flag(L("paused"));
            QByteArray torrentFile;
            if (c.request) {
                const QByteArray contentType =
                    c.request->headers().combinedValue(QByteArrayLiteral("Content-Type")).toLower();
                const QByteArray body = c.request->body();
                if (contentType.startsWith("application/json")) {
                    const QJsonDocument doc = QJsonDocument::fromJson(body);
                    if (!doc.isObject())
                        return error(400, s("Invalid JSON body"));
                    magnet = doc.object().value(L("magnet")).toString();
                    paused = doc.object().value(L("paused")).toBool();
                } else {
                    torrentFile = body;
                    const QString p = QUrlQuery(c.request->query()).queryItemValue(s("paused"));
                    paused = p == L("1") || p.compare(L("true"), Qt::CaseInsensitive) == 0;
                }
            }
            if (!torrentFile.isEmpty())
                return addReply(m_torrentBackend->addTorrentFile(torrentFile, {}, paused));
            if (!magnet.startsWith(L("magnet:"), Qt::CaseInsensitive))
                return error(400, s("Post the .torrent as the request body, or a JSON "
                                    "object with a \"magnet\" link"));
            return addReply(m_torrentBackend->addMagnet(magnet, {}, paused));
        };
        add(op);
    }
    {
        Operation op = make("torrents.get", Method::Get, "/api/v1/torrents/{id}", "One torrent",
                            "The list row plus its options, \"pieceMap\" (download progress) and "
                            "\"availabilityMap\" (peers having each piece): base64url, one byte "
                            "per bucket of pieces, 255 = all. The availability map is empty while "
                            "seeding.");
        op.flags = ReadOnly;
        op.mcpTool = s("torrents_get");
        op.params = {idParam()};
        op.handler = [withTorrent, this](const Call& c) {
            return withTorrent(c, [this](const QString& id, const QCborMap& row) {
                const QCborMap details = m_torrentBackend->details(id);
                QJsonObject o = rowJson(row, true);
                o.insert(L("availabilityMap"), details.value(L("availabilityMap")).toJsonValue());
                o.insert(L("options"), optionsJson(details.value(L("options")).toMap()));
                return ok(o);
            });
        };
        add(op);
    }
    {
        Operation op = make("torrents.edit", Method::Patch, "/api/v1/torrents/{id}",
                            "Change a torrent's options",
                            "Only the fields sent change. Speed limits in KiB/s, 0 = the global "
                            "limit; maxPeers 0 = the default. ratioMode / seedTimeMode: global "
                            "(the preferences), unlimited (seed forever) or custom (stop at "
                            "\"ratio\" / \"seedTimeMinutes\"). The reply holds the options now.");
        op.mcpTool = s("torrents_set_options");
        op.params = {
            idParam(),
            bodyInt(s("downloadLimitKb"), s("KiB/s; 0 = global limit.")).range(0, 1048576),
            bodyInt(s("uploadLimitKb"), s("KiB/s; 0 = global limit.")).range(0, 1048576),
            bodyInt(s("maxPeers"), s("Connections; 0 = default.")).range(0, 65535),
            bodyStr(s("ratioMode"), s("Seed ratio limit.")).oneOf(limitModeNames()),
            bodyNum(s("ratio"), s("Stop seeding at this ratio (custom).")).range(0.01, 1000),
            bodyStr(s("seedTimeMode"), s("Seed time limit.")).oneOf(limitModeNames()),
            bodyInt(s("seedTimeMinutes"), s("Stop seeding after (custom).")).range(1, 5256000),
        };
        op.handler = [withTorrent, this](const Call& c) {
            return withTorrent(c, [&c, this](const QString& id, const QCborMap&) -> Result {
                QCborMap options = m_torrentBackend->details(id).value(L("options")).toMap();
                bool any = false;
                const auto setInt = [&](const char* key) {
                    if (c.has(L(key))) {
                        options.insert(QString::fromLatin1(key), c.integer(L(key)));
                        any = true;
                    }
                };
                const auto setMode = [&](const char* key) {
                    if (c.has(L(key))) {
                        options.insert(QString::fromLatin1(key), limitModeNames().indexOf(c.str(L(key))));
                        any = true;
                    }
                };
                setInt("downloadLimitKb");
                setInt("uploadLimitKb");
                setInt("maxPeers");
                setMode("ratioMode");
                setMode("seedTimeMode");
                setInt("seedTimeMinutes");
                if (c.has(L("ratio"))) {
                    options.insert(s("ratioPercent"),
                                   static_cast<qint64>(std::lround(c.args.value(L("ratio")).toDouble() * 100)));
                    any = true;
                }
                if (!any)
                    return error(400, s("Nothing to change"));
                if (!m_torrentBackend->setOptions(id, options))
                    return error(409, s("The engine refused the options"));
                return ok(optionsJson(m_torrentBackend->details(id).value(L("options")).toMap()));
            });
        };
        add(op);
    }
    {
        Operation op = make("torrents.remove", Method::Delete, "/api/v1/torrents/{id}",
                            "Remove a torrent", "With deleteFiles the downloaded data goes too.");
        op.flags = Destructive;
        op.mcpTool = s("torrents_remove");
        op.params = {idParam(),
                     queryBool(s("deleteFiles"), s("Also delete what was downloaded.")).def(false)};
        op.handler = [withTorrent, this](const Call& c) {
            return withTorrent(c, [&c, this](const QString& id, const QCborMap&) -> Result {
                if (!m_torrentBackend->remove(id, c.flag(L("deleteFiles"))))
                    return error(409, s("The engine refused to remove it"));
                return ok(QJsonObject{{s("ok"), true}, {s("id"), id}});
            });
        };
        add(op);
    }
    {
        Operation op = make("torrents.files", Method::Get, "/api/v1/torrents/{id}/files",
                            "Files of a torrent",
                            "index, path, size, done (bytes) and priority: 0 = not downloaded, "
                            "1 = low, 4 = normal, 7 = high. \"pad\" marks alignment padding.");
        op.flags = ReadOnly;
        op.mcpTool = s("torrents_files");
        op.params = {idParam()};
        op.handler = [withTorrent, this](const Call& c) {
            return withTorrent(c, [this](const QString& id, const QCborMap&) {
                return ok(QJsonObject{{s("items"), listJson(m_torrentBackend->details(id), "files")}});
            });
        };
        add(op);
    }
    {
        Operation op = make("torrents.setFiles", Method::Patch, "/api/v1/torrents/{id}/files",
                            "Choose which files download",
                            "Gives the files listed one priority: 0 skips them, 1-7 downloads "
                            "them (4 = normal). The reply holds every file.");
        op.mcpTool = s("torrents_set_file_priority");
        op.params = {
            idParam(),
            bodyArray(s("files"), s("File indices.")).of(ParamType::Integer).atLeast(1).req(),
            bodyInt(s("priority"), s("0 = skip, 1 = low, 4 = normal, 7 = high.")).range(0, 7).req(),
        };
        op.handler = [withTorrent, this](const Call& c) {
            return withTorrent(c, [&c, this](const QString& id, const QCborMap&) -> Result {
                const QCborArray files = m_torrentBackend->details(id).value(L("files")).toArray();
                QList<int> priorities;
                priorities.reserve(files.size());
                for (const QCborValue& f : files)
                    priorities.append(static_cast<int>(f.toMap().value(L("priority")).toInteger(4)));
                const int priority = static_cast<int>(c.integer(L("priority")));
                for (const QJsonValue& v : c.args.value(L("files")).toArray()) {
                    const qint64 index = static_cast<qint64>(v.toDouble(-1));
                    if (index < 0 || index >= priorities.size())
                        return error(400, s("No file %1").arg(index));
                    priorities[static_cast<qsizetype>(index)] = priority;
                }
                if (!m_torrentBackend->setFilePriorities(id, priorities))
                    return error(409, s("The engine refused the priorities"));
                return ok(QJsonObject{{s("items"), listJson(m_torrentBackend->details(id), "files")}});
            });
        };
        add(op);
    }
    {
        Operation op = make("torrents.peers", Method::Get, "/api/v1/torrents/{id}/peers",
                            "Connected peers of a torrent",
                            "Rates in bytes/s. \"flags\" uses Transmission's letters: T uTP, O "
                            "optimistic unchoke, D/d downloading / interested but choked, U/u "
                            "uploading / they are interested but choked, K unchoked us, not "
                            "interested, ? we unchoked them, they are not interested, E encrypted, "
                            "h holepunched, H from DHT, X from PEX, I incoming.");
        op.flags = ReadOnly;
        op.mcpTool = s("torrents_peers");
        op.params = {idParam()};
        op.handler = [withTorrent, this](const Call& c) {
            return withTorrent(c, [this](const QString& id, const QCborMap&) {
                return ok(QJsonObject{{s("items"), listJson(m_torrentBackend->details(id), "peers")}});
            });
        };
        add(op);
    }
    {
        Operation op = make("torrents.trackers", Method::Get, "/api/v1/torrents/{id}/trackers",
                            "Trackers of a torrent",
                            "url, tier, working, message, the last scrape (seeds, peers, "
                            "downloaded; -1 = unknown) and nextAnnounce (unix seconds, -1 = none).");
        op.flags = ReadOnly;
        op.mcpTool = s("torrents_trackers");
        op.params = {idParam()};
        op.handler = [withTorrent, this](const Call& c) {
            return withTorrent(c, [this](const QString& id, const QCborMap&) {
                return ok(QJsonObject{{s("items"), listJson(m_torrentBackend->details(id), "trackers")}});
            });
        };
        add(op);
    }
    {
        Operation op = make("torrents.editTrackers", Method::Patch, "/api/v1/torrents/{id}/trackers",
                            "Add or remove trackers",
                            "Announce URLs; a new one joins a tier of its own. The reply holds "
                            "every tracker.");
        op.mcpTool = s("torrents_edit_trackers");
        op.params = {
            idParam(),
            bodyArray(s("add"), s("Announce URLs to add.")).of(ParamType::String),
            bodyArray(s("remove"), s("Announce URLs to remove.")).of(ParamType::String),
        };
        op.handler = [withTorrent, this](const Call& c) {
            return withTorrent(c, [&c, this](const QString& id, const QCborMap&) -> Result {
                const QStringList added = stringList(c.args.value(L("add")));
                const QStringList removed = stringList(c.args.value(L("remove")));
                if (added.isEmpty() && removed.isEmpty())
                    return error(400, s("Nothing to change"));
                for (const QString& url : added) {
                    const QUrl u(url);
                    if (!u.isValid() || u.host().isEmpty()
                        || !QStringList{s("http"), s("https"), s("udp")}.contains(u.scheme()))
                        return error(400, s("Not an announce URL: %1").arg(url));
                }
                if (!added.isEmpty() && !m_torrentBackend->addTrackers(id, added))
                    return error(409, s("The engine refused the trackers"));
                if (!removed.isEmpty() && !m_torrentBackend->removeTrackers(id, removed))
                    return error(409, s("The engine refused to remove the trackers"));
                return ok(QJsonObject{{s("items"), listJson(m_torrentBackend->details(id), "trackers")}});
            });
        };
        add(op);
    }
    {
        Operation op = make("torrents.action", Method::Post, "/api/v1/torrents/{id}/{action}",
                            "Pause, resume, recheck or reannounce a torrent",
                            "The new state shows in the list within about a second.");
        op.mcpTool = s("torrents_action");
        op.params = {idParam(), pathStr(s("action"), s("What to do.")).oneOf(actionNames())};
        op.handler = [withTorrent, this](const Call& c) {
            return withTorrent(c, [&c, this](const QString& id, const QCborMap&) -> Result {
                const QString action = c.str(L("action"));
                const bool done = action == L("pause")   ? m_torrentBackend->pause(id)
                                : action == L("resume")  ? m_torrentBackend->resume(id)
                                : action == L("recheck") ? m_torrentBackend->recheck(id)
                                                         : m_torrentBackend->reannounce(id);
                if (!done)
                    return error(409, s("The engine refused to %1 it").arg(action));
                return ok(QJsonObject{{s("ok"), true}, {s("id"), id}});
            });
        };
        add(op);
    }
}

#endif // EMULE_HAVE_BITTORRENT

} // namespace eMule
