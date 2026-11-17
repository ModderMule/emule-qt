#include "pch.h"
/// @file WebServerEd3k.cpp
/// @brief The /api/v1/ed3k and /api/v1/hoster operations: the two joins of the ed3k
///        module. Core never links it; both talk to a backend DaemonApp installs.

#include "webserver/WebServer.h"

#include <QCborMap>
#include <QCborValue>
#include <QJsonArray>
#include <QPromise>
#include <QRegularExpression>

#include <memory>

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

// --- ed3k ------------------------------------------------------------------

/// Ed3kDownloadInfo::State. Index = wire value.
const QStringList& ed3kStateNames()
{
    static const QStringList names{s("downloading"), s("paused"), s("verifying"), s("complete"), s("failed")};
    return names;
}

/// eMule's kPr* as the ed3k downloads take them. Index = wire value.
const QStringList& ed3kPriorityNames()
{
    static const QStringList names{s("low"), s("normal"), s("high"), s("veryHigh"), s("veryLow")};
    return names;
}

[[nodiscard]] Param rootParam()
{
    return pathStr(s("root"), s("ed3k root: 64 hexadecimal digits.")).range(64, 64);
}

[[nodiscard]] bool isHexRoot(const QString& root)
{
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return hex.match(root).hasMatch();
}

/// A GetEd3kDownloads row without the progress-bar bytes, plus state and priority as words.
[[nodiscard]] QJsonObject ed3kRowJson(const QCborMap& row)
{
    QCborMap m = row;
    for (const char* bar : {"blocks", "pending", "avail", "unverified", "blockSize", "pieceSize"})
        m.remove(QString::fromLatin1(bar));
    QJsonObject o = m.toJsonObject();
    const auto state = row.value(L("state")).toInteger();
    o.insert(L("state"), ed3kStateNames().value(int(state), s("unknown")));
    const auto priority = row.value(L("priority")).toInteger();
    o.insert(L("priority"), ed3kPriorityNames().value(int(priority), s("normal")));
    return o;
}

[[nodiscard]] Result ed3kUnavailable()
{
    return error(503, s("ed3k is not available: switched off, or not built"));
}

// --- hoster ----------------------------------------------------------------

/// Ed3kHosterJoin::State. Index = wire value.
const QStringList& hosterStateNames()
{
    static const QStringList names{s("matching"), s("lookingUp"), s("checking"), s("queued"), s("waiting"),
                                   s("downloading"), s("puzzle"), s("paused"), s("complete"), s("gone"),
                                   s("needsUser"), s("noResolver"), s("failed")};
    return names;
}

[[nodiscard]] QJsonObject hosterRowJson(const QCborMap& row)
{
    QJsonObject o = row.toJsonObject();
    const auto state = row.value(L("state")).toInteger();
    o.insert(L("state"), hosterStateNames().value(int(state), s("unknown")));
    return o;
}

[[nodiscard]] Result hosterUnavailable()
{
    return error(503, s("Hoster downloads not available: ed3k off, or no hoster resolver"));
}

/// The links of a request: "link" and/or "links", trimmed, empty ones dropped.
[[nodiscard]] QStringList linksOf(const Call& c)
{
    QStringList links;
    if (c.has(L("link")))
        links.append(c.str(L("link")).trimmed());
    for (const QJsonValue& v : c.args.value(L("links")).toArray())
        links.append(v.toString().trimmed());
    links.removeAll(QString());
    return links;
}

} // namespace

// ---------------------------------------------------------------------------
// ed3k downloads
// ---------------------------------------------------------------------------

void WebServer::addEd3kOperations()
{
    const auto add = [this](Operation op) { m_api.add(std::move(op)); };

    // The row of @p root, else an error; the backend's state is checked first.
    const auto withDownload = [this](const Call& c, auto&& then) -> Result {
        if (!m_ed3kBackend || !m_ed3kBackend->available())
            return ed3kUnavailable();
        const QString root = c.str(L("root")).toLower();
        if (!isHexRoot(root))
            return error(400, s("Invalid root: 64 hexadecimal digits"));
        for (const QCborValue& v : m_ed3kBackend->downloads()) {
            const QCborMap row = v.toMap();
            if (row.value(L("root")).toString().compare(root, Qt::CaseInsensitive) == 0)
                return then(root, row);
        }
        return error(404, s("No such ed3k download"));
    };
    const auto rowOf = [this](const QString& root) -> QJsonObject {
        for (const QCborValue& v : m_ed3kBackend->downloads())
            if (v.toMap().value(L("root")).toString().compare(root, Qt::CaseInsensitive) == 0)
                return ed3kRowJson(v.toMap());
        return {};
    };

    {
        Operation op = make("ed3k.downloads", Method::Get, "/api/v1/ed3k/downloads", "ed3k",
                            "List ed3k downloads",
                            "The ed3k download queue (files named by a 64-hex BLAKE3 root). Sizes "
                            "are bytes, rate is bytes per second. A completed download stays "
                            "listed until cleared.");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("ed3k_downloads_list");
        op.handler = [this](const Call& c) {
            if (!m_ed3kBackend || !m_ed3kBackend->available())
                return ed3kUnavailable();
            QJsonArray rows;
            for (const QCborValue& v : m_ed3kBackend->downloads()) {
                const QCborMap row = v.toMap();
                // single transfer mode lists torrent/Usenet/hoster rows here too: not ours
                if (isHexRoot(row.value(L("root")).toString()))
                    rows.append(ed3kRowJson(row));
            }
            return pageOf(c, rows);
        };
        add(op);
    }
    {
        Operation op = make("ed3k.add", Method::Post, "/api/v1/ed3k/downloads", "ed3k",
                            "Add ed3k downloads by link",
                            "Takes one ed3k file link or several. Answers per link whether it was "
                            "added, with the download's root.");
        op.mcpTool = s("ed3k_download_add");
        op.params = {
            bodyStr(s("link"), s("One ed3k file link.")).range(1, 8192),
            bodyArray(s("links"), s("Several ed3k file links.")).range(1, 100),
            bodyInt(s("category"), s("Category index from GET /categories; 0 = none.")).atLeast(0).def(0),
            bodyBool(s("paused"), s("Add in the paused state.")).def(false),
        };
        op.handler = [this](const Call& c) {
            if (!m_ed3kBackend || !m_ed3kBackend->available())
                return ed3kUnavailable();
            const QStringList links = linksOf(c);
            if (links.isEmpty())
                return error(400, s("Send 'link' or 'links'"));
            QJsonArray items;
            int added = 0;
            QString lastError;
            for (const QString& link : links) {
                const Ed3kWebBackend::AddResult r =
                    m_ed3kBackend->addLink(link, int(c.integer(L("category"))), c.flag(L("paused")));
                QJsonObject row{{s("link"), link}, {s("added"), !r.root.isEmpty()}};
                if (!r.root.isEmpty()) {
                    row.insert(s("root"), r.root);
                    ++added;
                } else {
                    row.insert(s("error"), r.error);
                    lastError = r.error;
                }
                items.append(row);
            }
            // one link that failed: its error, not a 200 with a row saying so
            if (links.size() == 1 && added == 0)
                return error(400, lastError.isEmpty() ? s("Not added") : lastError);
            return ok(QJsonObject{{s("added"), added}, {s("items"), items}});
        };
        add(op);
    }
    {
        Operation op = make("ed3k.clearCompleted", Method::Post, "/api/v1/ed3k/downloads/clear-completed",
                            "ed3k", "Remove completed ed3k downloads from the list",
                            "The files stay on disk and shared.");
        op.mcpTool = s("ed3k_downloads_clear_completed");
        op.handler = [this](const Call&) {
            if (!m_ed3kBackend || !m_ed3kBackend->available())
                return ed3kUnavailable();
            m_ed3kBackend->control({}, Ed3kWebBackend::Control::ClearCompleted, 0);
            return ok(QJsonObject{{s("ok"), true}});
        };
        add(op);
    }
    {
        Operation op = make("ed3k.get", Method::Get, "/api/v1/ed3k/downloads/{root}", "ed3k",
                            "One ed3k download");
        op.flags = ReadOnly;
        op.mcpTool = s("ed3k_download_get");
        op.params = {rootParam()};
        op.handler = [withDownload](const Call& c) {
            return withDownload(c, [](const QString&, const QCborMap& row) { return ok(ed3kRowJson(row)); });
        };
        add(op);
    }
    {
        Operation op = make("ed3k.edit", Method::Patch, "/api/v1/ed3k/downloads/{root}", "ed3k",
                            "Change an ed3k download",
                            "Rename it, set its priority or category, or have the first and last "
                            "parts fetched first for a preview.");
        op.mcpTool = s("ed3k_download_edit");
        op.params = {
            rootParam(),
            bodyStr(s("name"), s("New file name.")).range(1, 255),
            bodyStr(s("priority"), s("Download priority.")).oneOf(ed3kPriorityNames()),
            bodyInt(s("category"), s("Category index from GET /categories; 0 = none.")).atLeast(0),
            bodyBool(s("previewFirst"), s("Fetch the parts a preview needs first.")),
        };
        op.handler = [this, withDownload, rowOf](const Call& c) {
            return withDownload(c, [&](const QString& root, const QCborMap&) -> Result {
                if (!c.has(L("name")) && !c.has(L("priority")) && !c.has(L("category"))
                    && !c.has(L("previewFirst")))
                    return error(400, s("Nothing to change: send name, priority, category or previewFirst"));
                using C = Ed3kWebBackend::Control;
                if (c.has(L("name"))) {
                    if (const QString why = m_ed3kBackend->rename(root, c.str(L("name"))); !why.isEmpty())
                        return error(409, why);
                }
                if (c.has(L("priority"))
                    && !m_ed3kBackend->control(root, C::Priority, ed3kPriorityNames().indexOf(c.str(L("priority")))))
                    return error(409, s("The priority was not taken"));
                if (c.has(L("category")) && !m_ed3kBackend->control(root, C::Category, c.integer(L("category"))))
                    return error(409, s("The category was not taken"));
                if (c.has(L("previewFirst"))
                    && !m_ed3kBackend->control(root, C::PreviewFirst, c.flag(L("previewFirst")) ? 1 : 0))
                    return error(409, s("Preview parts first was not taken"));
                return ok(rowOf(root));
            });
        };
        add(op);
    }
    {
        Operation op = make("ed3k.move", Method::Post, "/api/v1/ed3k/downloads/{root}/move", "ed3k",
                            "Move an unfinished ed3k download to another temp folder",
                            "Copies the part file over in the background; the download goes on.");
        op.mcpTool = s("ed3k_download_move");
        op.params = {rootParam(), bodyStr(s("folder"), s("Existing, writable folder.")).req().range(1, 4096)};
        op.handler = [this, withDownload](const Call& c) {
            return withDownload(c, [&](const QString& root, const QCborMap&) -> Result {
                if (const QString why = m_ed3kBackend->moveTo(root, c.str(L("folder"))); !why.isEmpty())
                    return error(409, why);
                return ok(QJsonObject{{s("ok"), true}, {s("root"), root}});
            });
        };
        add(op);
    }
    {
        Operation op = make("ed3k.cancel", Method::Post, "/api/v1/ed3k/downloads/{root}/cancel", "ed3k",
                            "Cancel an ed3k download",
                            "Removes it and its part file. A completed one leaves the list; with "
                            "deleteFile its file is deleted from disk too.");
        op.flags = Destructive;
        op.mcpTool = s("ed3k_download_cancel");
        op.params = {rootParam(), bodyBool(s("deleteFile"), s("A completed download: delete its file.")).def(false)};
        op.handler = [this, withDownload](const Call& c) {
            return withDownload(c, [&](const QString& root, const QCborMap&) -> Result {
                const auto action = c.flag(L("deleteFile")) ? Ed3kWebBackend::Control::CancelDelete
                                                            : Ed3kWebBackend::Control::Cancel;
                if (!m_ed3kBackend->control(root, action, 0))
                    return error(409, s("Not cancelled"));
                return ok(QJsonObject{{s("cancelled"), true}, {s("root"), root}});
            });
        };
        add(op);
    }
    {
        Operation op = make("ed3k.action", Method::Post, "/api/v1/ed3k/downloads/{root}/{action}", "ed3k",
                            "Pause or resume an ed3k download");
        op.mcpTool = s("ed3k_download_action");
        op.params = {rootParam(), pathStr(s("action"), s("What to do.")).oneOf({s("pause"), s("resume")})};
        op.handler = [this, withDownload, rowOf](const Call& c) {
            return withDownload(c, [&](const QString& root, const QCborMap&) -> Result {
                const bool pause = c.str(L("action")) == L("pause");
                m_ed3kBackend->control(root, pause ? Ed3kWebBackend::Control::Pause
                                                   : Ed3kWebBackend::Control::Resume, 0);
                return ok(rowOf(root));
            });
        };
        add(op);
    }
}

// ---------------------------------------------------------------------------
// Hoster downloads
// ---------------------------------------------------------------------------

void WebServer::addHosterOperations()
{
    const auto add = [this](Operation op) { m_api.add(std::move(op)); };
    const auto idParam = [] { return pathInt(s("id"), s("Hoster item id from GET /hoster.")).atLeast(0); };

    {
        Operation op = make("hoster.list", Method::Get, "/api/v1/hoster", "Hoster",
                            "List hoster downloads",
                            "Downloads from one-click hosters and web links, resolved by the "
                            "hoster plugins: state, size and done in bytes, speed in bytes/s, "
                            "package. \"needsUser\" waits for a captcha or password in the app.");
        op.flags = ReadOnly | Paged;
        op.mcpTool = s("hoster_list");
        op.handler = [this](const Call& c) {
            if (!m_hosterBackend || !m_hosterBackend->available())
                return hosterUnavailable();
            QJsonArray rows;
            for (const QCborValue& v : m_hosterBackend->items())
                rows.append(hosterRowJson(v.toMap()));
            return pageOf(c, rows);
        };
        add(op);
    }
    {
        Operation op = make("hoster.add", Method::Post, "/api/v1/hoster", "Hoster",
                            "Add hoster links",
                            "Links of file hosters, folders or web pages; each is resolved by a "
                            "hoster plugin. With \"package\" they form one new package.");
        op.mcpTool = s("hoster_add");
        op.params = {
            bodyStr(s("link"), s("One http(s) link.")).range(1, 8192),
            bodyArray(s("links"), s("Several links.")).range(1, 500),
            bodyStr(s("package"), s("Name of a new package for them.")).range(0, 255),
        };
        op.handler = [this](const Call& c) {
            if (!m_hosterBackend || !m_hosterBackend->available())
                return hosterUnavailable();
            const QStringList links = linksOf(c);
            if (links.isEmpty())
                return error(400, s("Send 'link' or 'links'"));
            const int added = m_hosterBackend->addLinks(links.join(QLatin1Char('\n')), c.str(L("package")).trimmed());
            if (added == 0)
                return error(400, s("No link was taken"));
            return ok(QJsonObject{{s("added"), added}});
        };
        add(op);
    }
    {
        Operation op = make("hoster.container", Method::Post, "/api/v1/hoster/container", "Hoster",
                            "Open a link container",
                            "A DLC, RSDF or CCF file, base64 in \"data\": all its links are "
                            "added. Answers once the container is decrypted.");
        op.mcpTool = s("hoster_open_container");
        op.params = {
            bodyStr(s("data"), s("The container file, base64.")).req().range(4, 16 * 1024 * 1024),
            bodyStr(s("fileName"), s("Its file name; the extension says the format.")).req().range(5, 255),
        };
        op.asyncHandler = [this](const Call& c) -> QFuture<Result> {
            if (!m_hosterBackend || !m_hosterBackend->available())
                return Registry::finished(hosterUnavailable());
            const auto decoded = QByteArray::fromBase64Encoding(c.str(L("data")).toLatin1(),
                                                                QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded || decoded.decoded.isEmpty())
                return Registry::finished(error(400, s("'data' is not base64")));
            auto promise = std::make_shared<QPromise<Result>>();
            promise->start();
            QFuture<Result> future = promise->future();
            m_hosterBackend->openContainer(decoded.decoded, c.str(L("fileName")),
                [promise](int added, const QString& why) {
                    promise->addResult(added > 0 ? ok(QJsonObject{{s("added"), added}})
                                                 : error(422, why.isEmpty() ? s("No links in it") : why));
                    promise->finish();
                });
            return future;
        };
        add(op);
    }
    {
        Operation op = make("hoster.remove", Method::Post, "/api/v1/hoster/{id}/remove", "Hoster",
                            "Remove a hoster download",
                            "With deleteFile its file is deleted from disk too.");
        op.flags = Destructive;
        op.mcpTool = s("hoster_remove");
        op.params = {idParam(), bodyBool(s("deleteFile"), s("Delete its file too.")).def(false)};
        op.handler = [this](const Call& c) {
            if (!m_hosterBackend || !m_hosterBackend->available())
                return hosterUnavailable();
            const auto id = quint32(c.integer(L("id")));
            if (!m_hosterBackend->action(id, c.flag(L("deleteFile")) ? s("delete") : s("remove")))
                return error(404, s("No such hoster download"));
            return ok(QJsonObject{{s("removed"), true}, {s("id"), qint64(id)}});
        };
        add(op);
    }
    {
        Operation op = make("hoster.action", Method::Post, "/api/v1/hoster/{id}/{action}", "Hoster",
                            "Pause, resume or retry a hoster download");
        op.mcpTool = s("hoster_action");
        op.params = {idParam(),
                     pathStr(s("action"), s("What to do.")).oneOf({s("pause"), s("resume"), s("retry")})};
        op.handler = [this](const Call& c) {
            if (!m_hosterBackend || !m_hosterBackend->available())
                return hosterUnavailable();
            const auto id = quint32(c.integer(L("id")));
            if (!m_hosterBackend->action(id, c.str(L("action"))))
                return error(404, s("No such hoster download"));
            return ok(QJsonObject{{s("ok"), true}, {s("id"), qint64(id)}});
        };
        add(op);
    }
}

} // namespace eMule
