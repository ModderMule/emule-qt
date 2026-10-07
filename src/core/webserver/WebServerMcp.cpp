#include "pch.h"
/// @file WebServerMcp.cpp
/// @brief The parts of the API that are not plain request/response: the event
/// stream, the playground page and the MCP endpoint.

#include "webserver/WebServer.h"

#include "app/AppConfig.h"
#include "webserver/ApiEventHub.h"
#include "webserver/OpenApiWriter.h"

#include <QFile>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPromise>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace eMule {

using namespace api;

namespace {

using L = QLatin1StringView;

constexpr int kEventHeartbeatMs = 15000;
constexpr int kMaxEventStreams = 16;
/// Rows an MCP tool returns when the model did not ask for a number: a page a
/// model can read without spending its context on it.
constexpr int kMcpDefaultPageSize = 50;

// Two eras of the protocol on one endpoint. The handshake revisions open with
// "initialize"; from 2026-07-28 on there is no handshake and every request names
// its revision in params._meta. Newest first.
const QStringList kMcpHandshakeVersions{QStringLiteral("2025-11-25"), QStringLiteral("2025-06-18"),
                                        QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05")};
const QStringList kMcpPerRequestVersions{QStringLiteral("2026-07-28")};

constexpr L kMetaVersionKey{"io.modelcontextprotocol/protocolVersion"};
constexpr int kRpcHeaderMismatch = -32020;
constexpr int kRpcUnsupportedVersion = -32022;
constexpr int kRpcMethodNotFound = -32601;

[[nodiscard]] QStringList allMcpVersions()
{
    return kMcpPerRequestVersions + kMcpHandshakeVersions;
}

/// The revision a request names for itself; empty for a handshake-era request.
[[nodiscard]] QString requestedVersion(const QJsonObject& request)
{
    return request.value(L("params")).toObject().value(L("_meta")).toObject()
        .value(kMetaVersionKey).toString();
}

/// "=?base64?...?=" is how a header carries a value that is not plain ASCII.
[[nodiscard]] QString decodeHeaderValue(const QByteArray& raw)
{
    if (raw.startsWith("=?base64?") && raw.endsWith("?="))
        return QString::fromUtf8(QByteArray::fromBase64(raw.mid(9, raw.size() - 11)));
    return QString::fromUtf8(raw);
}

const char* const kMcpInstructions =
    "Controls an eMuleQt file-sharing core (eD2K servers, Kad, Usenet). Call get_status first: "
    "it shows whether the networks are connected. Searching needs a connected network; "
    "connection_connect and kad_start bring them up. To fetch a file: search_start, then "
    "search_results until state is \"finished\" (results arrive over several seconds), then "
    "download_from_search with the hash of the result. Prefer results with many sources and "
    "spamRating 0. A file is identified by its 32-digit hex hash; sizes are bytes. Lists are "
    "paged: pass limit and offset. Calls that delete data need confirm: true — ask the user "
    "before using them.";

const char* const kDocsPage = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>eMuleQt REST API</title>
<link rel="stylesheet" href="docs/swagger-ui.css">
<style>
body { margin: 0; background: #fafafa; }
#missing { font: 15px/1.5 system-ui, sans-serif; max-width: 40em; margin: 4em auto; padding: 0 1em; }
</style>
</head>
<body>
<div id="swagger-ui"></div>
<div id="missing" hidden>
<h1>eMuleQt REST API</h1>
<p>The playground files are not installed (<code>swagger-ui-bundle.js</code> and
<code>swagger-ui.css</code> in the <code>webserver</code> folder of the configuration directory).</p>
<p>The API description itself is at <a href="openapi.json">openapi.json</a>.</p>
</div>
<script src="docs/swagger-ui-bundle.js"></script>
<script>
if (window.SwaggerUIBundle) {
  window.ui = SwaggerUIBundle({
    url: "openapi.json",
    dom_id: "#swagger-ui",
    deepLinking: true,
    persistAuthorization: true,
    tryItOutEnabled: true,
    docExpansion: "list",
    validatorUrl: null
  });
} else {
  document.getElementById("missing").hidden = false;
}
</script>
</body>
</html>
)html";

[[nodiscard]] QHttpServerResponse textResponse(const QByteArray& mime, const QByteArray& body)
{
    return QHttpServerResponse(mime, body, QHttpServerResponse::StatusCode::Ok);
}

[[nodiscard]] QJsonObject rpcError(const QJsonValue& id, int code, const QString& message)
{
    return QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
                                              {QStringLiteral("message"), message}}},
    };
}

[[nodiscard]] QJsonObject rpcResult(const QJsonValue& id, const QJsonObject& result)
{
    return QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("result"), result},
    };
}

[[nodiscard]] QFuture<QJsonValue> ready(const QJsonValue& value)
{
    QPromise<QJsonValue> promise;
    QFuture<QJsonValue> future = promise.future();
    promise.start();
    promise.addResult(value);
    promise.finish();
    return future;
}

/// What a tool call hands back: the JSON as text for the model, and as data.
[[nodiscard]] QJsonObject toolResult(const Result& result)
{
    // structuredContent has to be an object
    const QJsonObject structured = result.body.isArray()
        ? QJsonObject{{QStringLiteral("items"), result.body.toArray()}}
        : result.body.toObject();
    QJsonObject out{
        {QStringLiteral("content"), QJsonArray{QJsonObject{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"), QString::fromUtf8(
                QJsonDocument(structured).toJson(QJsonDocument::Compact))},
        }}},
        {QStringLiteral("isError"), !result.ok()},
    };
    if (result.ok())
        out.insert(QStringLiteral("structuredContent"), structured);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// OpenAPI document and playground
// ---------------------------------------------------------------------------

QJsonObject WebServer::openApiDocument() const
{
    return buildOpenApi(m_api, QString(kAppVersion));
}

void WebServer::registerDocsRoutes()
{
    // No key: a browser cannot send the header for a page load, and neither the
    // page nor the script holds anything secret. Every call it makes needs the key.
    m_server->route(QStringLiteral("/api/v1/docs"), QHttpServerRequest::Method::Get, [] {
        return textResponse(QByteArrayLiteral("text/html; charset=utf-8"), QByteArray(kDocsPage));
    });

    m_server->route(QStringLiteral("/api/v1/docs/<arg>"), QHttpServerRequest::Method::Get,
        [this](const QString& name) {
            // Two fixed names; nothing else is served from here.
            const bool script = name == L("swagger-ui-bundle.js");
            if (!script && name != L("swagger-ui.css"))
                return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
            QFile file(m_webDataDir + u'/' + name);
            if (m_webDataDir.isEmpty() || !file.open(QIODevice::ReadOnly))
                return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
            QHttpServerResponse response = textResponse(
                script ? QByteArrayLiteral("application/javascript; charset=utf-8")
                       : QByteArrayLiteral("text/css; charset=utf-8"),
                file.readAll());
            QHttpHeaders headers = response.headers();
            headers.append(QHttpHeaders::WellKnownHeader::CacheControl, QByteArrayLiteral("max-age=86400"));
            response.setHeaders(std::move(headers));
            return response;
        });
}

// ---------------------------------------------------------------------------
// Event stream
// ---------------------------------------------------------------------------

int WebServer::eventStreamCount() const
{
    return static_cast<int>(m_eventClients.size());
}

void WebServer::registerEventRoute()
{
    m_server->route(QStringLiteral("/api/v1/events"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req, QHttpServerResponder& responder) {
            handleEventStream(req, responder);
        });

    if (!m_eventHub)
        return;
    // Context is the server object: the connections go when it is rebuilt.
    connect(m_eventHub, &ApiEventHub::published, m_server.get(),
            [this](const ApiEventHub::Event& event) {
                sendEvent(event.type, event.id, event.data, event.topic());
            });

    m_eventHeartbeat = new QTimer(this);
    m_eventHeartbeat->setInterval(kEventHeartbeatMs);
    connect(m_eventHeartbeat, &QTimer::timeout, this, [this] {
        for (EventClient& client : m_eventClients)
            client.responder->writeChunk(QByteArrayLiteral(": ping\n\n"));
    });
    m_eventHeartbeat->start();
}

void WebServer::handleEventStream(const QHttpServerRequest& req, QHttpServerResponder& responder)
{
    if (auto auth = checkAuth(req.headers()); !auth.ok) {
        responder.sendResponse(std::move(auth.response));
        return;
    }
    if (!m_eventHub) {
        responder.sendResponse(toResponse(error(503, QStringLiteral("Events not available"))));
        return;
    }
    if (static_cast<int>(m_eventClients.size()) >= kMaxEventStreams) {
        responder.sendResponse(toResponse(error(429, QStringLiteral("Too many event streams"))));
        return;
    }

    const QUrlQuery query(req.query());
    for (const auto& [key, value] : query.queryItems()) {
        Q_UNUSED(value);
        if (key != L("topics") && key != L("lastEventId")) {
            responder.sendResponse(toResponse(
                error(400, QStringLiteral("Unknown query parameter '%1'").arg(key))));
            return;
        }
    }

    // The connection's socket: the only thing that says when the client is gone.
    QTcpSocket* socket = nullptr;
    QList<QTcpSocket*> sockets = m_server->findChildren<QTcpSocket*>();
    if (m_tcpServer)
        sockets += m_tcpServer->findChildren<QTcpSocket*>();
    for (QTcpSocket* candidate : std::as_const(sockets)) {
        // A dual-stack listener reports an IPv4 peer as ::ffff:a.b.c.d
        if (candidate->peerPort() == req.remotePort()
            && candidate->peerAddress().isEqual(req.remoteAddress(),
                                                QHostAddress::TolerantConversion)) {
            socket = candidate;
            break;
        }
    }
    if (!socket) {
        responder.sendResponse(toResponse(error(500, QStringLiteral("Connection not found"))));
        return;
    }

    EventClient client;
    client.key = reinterpret_cast<quintptr>(socket);
    const QStringList topics = query.queryItemValue(QStringLiteral("topics"), QUrl::FullyDecoded)
                                   .split(u',', Qt::SkipEmptyParts);
    for (const QString& topic : topics)
        client.topics.insert(topic.trimmed());

    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::ContentType, QByteArrayLiteral("text/event-stream"));
    headers.append(QHttpHeaders::WellKnownHeader::CacheControl, QByteArrayLiteral("no-cache"));
    headers.append(QByteArrayLiteral("X-Accel-Buffering"), QByteArrayLiteral("no"));
    client.responder = std::make_unique<QHttpServerResponder>(std::move(responder));
    client.responder->writeBeginChunked(headers);
    client.responder->writeChunk(QByteArrayLiteral("retry: 3000\n\n"));

    // Resume: the header an EventSource sends by itself, or the query for other clients.
    QByteArray lastText = req.headers().combinedValue(QByteArrayLiteral("Last-Event-ID"));
    if (lastText.isEmpty())
        lastText = query.queryItemValue(QStringLiteral("lastEventId")).toLatin1();
    bool resumed = false;
    const qint64 lastId = lastText.toLongLong(&resumed);

    const quintptr key = client.key;
    m_eventClients.push_back(std::move(client));
    m_eventHub->addListener();
    connect(socket, &QTcpSocket::disconnected, this, [this, key] { dropEventClient(key); });
    connect(socket, &QObject::destroyed, this, [this, key] { dropEventClient(key); });

    EventClient& stored = m_eventClients.back();
    if (resumed) {
        bool reset = false;
        const QList<ApiEventHub::Event> missed = m_eventHub->since(lastId, reset);
        if (reset) {
            stored.responder->writeChunk(
                QByteArrayLiteral("id: ") + QByteArray::number(m_eventHub->lastId())
                + QByteArrayLiteral("\nevent: sync.reset\ndata: {}\n\n"));
        }
        for (const ApiEventHub::Event& event : missed) {
            if (!stored.topics.isEmpty() && !stored.topics.contains(event.topic()))
                continue;
            stored.responder->writeChunk(QByteArrayLiteral("id: ") + QByteArray::number(event.id)
                                         + QByteArrayLiteral("\nevent: ") + event.type.toUtf8()
                                         + QByteArrayLiteral("\ndata: ") + event.data
                                         + QByteArrayLiteral("\n\n"));
        }
    }
}

void WebServer::sendEvent(const QString& type, qint64 id, const QByteArray& data,
                          const QString& topic)
{
    if (m_eventClients.empty())
        return;
    const QByteArray frame = QByteArrayLiteral("id: ") + QByteArray::number(id)
                           + QByteArrayLiteral("\nevent: ") + type.toUtf8()
                           + QByteArrayLiteral("\ndata: ") + data + QByteArrayLiteral("\n\n");
    for (EventClient& client : m_eventClients) {
        if (client.topics.isEmpty() || client.topics.contains(topic))
            client.responder->writeChunk(frame);
    }
}

void WebServer::dropEventClient(quintptr key)
{
    const auto before = m_eventClients.size();
    std::erase_if(m_eventClients, [key](const EventClient& client) { return client.key == key; });
    if (m_eventHub) {
        for (auto n = m_eventClients.size(); n < before; ++n)
            m_eventHub->removeListener();
    }
}

// ---------------------------------------------------------------------------
// MCP (Model Context Protocol), streamable HTTP, stateless
// ---------------------------------------------------------------------------

void WebServer::registerMcpRoute()
{
    m_server->route(QStringLiteral("/mcp"), QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) -> QFuture<QHttpServerResponse> {
            const auto finished = [](QHttpServerResponse&& response) {
                QPromise<QHttpServerResponse> promise;
                QFuture<QHttpServerResponse> future = promise.future();
                promise.start();
                promise.addResult(std::move(response));
                promise.finish();
                return future;
            };

            // A page of another site must not be able to drive this (DNS rebinding).
            if (!originAllowed(req))
                return finished(toResponse(error(403, QStringLiteral("Origin not allowed"))));
            if (auto auth = checkAuth(req.headers()); !auth.ok)
                return finished(std::move(auth.response));

            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(req.body(), &parseError);
            if (parseError.error != QJsonParseError::NoError || (!doc.isObject() && !doc.isArray())) {
                return finished(QHttpServerResponse(
                    rpcError(QJsonValue(), -32700, QStringLiteral("Parse error")),
                    QHttpServerResponse::StatusCode::BadRequest));
            }

            const QJsonValue message = doc.isArray() ? QJsonValue(doc.array())
                                                     : QJsonValue(doc.object());

            // A per-request-era call mirrors version, method and tool name into
            // headers; a proxy may route on them, so they have to say what the body says.
            const QJsonObject request = doc.object();
            const QString version = requestedVersion(request);
            const bool perRequestEra = doc.isObject() && !version.isEmpty();
            if (perRequestEra) {
                const QHttpHeaders headers = req.headers();
                const QString method = request.value(L("method")).toString();
                QString problem;
                const auto check = [&](const char* name, const QString& expected) {
                    if (!problem.isEmpty())
                        return;
                    const QByteArray raw = headers.combinedValue(QByteArray(name));
                    if (raw.isEmpty())
                        problem = QStringLiteral("Header mismatch: %1 header is missing")
                                      .arg(QLatin1StringView(name));
                    else if (decodeHeaderValue(raw) != expected)
                        problem = QStringLiteral("Header mismatch: %1 header value '%2' does not "
                                                 "match body value '%3'")
                                      .arg(QLatin1StringView(name), decodeHeaderValue(raw), expected);
                };
                check("MCP-Protocol-Version", version);
                check("Mcp-Method", method);
                if (method == L("tools/call"))
                    check("Mcp-Name", request.value(L("params")).toObject().value(L("name")).toString());
                if (!problem.isEmpty()) {
                    return finished(QHttpServerResponse(
                        rpcError(request.value(L("id")), kRpcHeaderMismatch, problem),
                        QHttpServerResponse::StatusCode::BadRequest));
                }
            }

            return handleMcpMessage(message).then(this, [perRequestEra](const QJsonValue& reply) {
                // Nothing to answer (notifications only): 202, no body.
                if (reply.isUndefined())
                    return QHttpServerResponse(QHttpServerResponse::StatusCode::Accepted);
                if (reply.isArray())
                    return QHttpServerResponse(reply.toArray());
                // The newer revisions put these two failures in the HTTP status too.
                auto status = QHttpServerResponse::StatusCode::Ok;
                const int code = reply.toObject().value(L("error")).toObject().value(L("code")).toInt();
                if (code == kRpcUnsupportedVersion)
                    status = QHttpServerResponse::StatusCode::BadRequest;
                else if (perRequestEra && code == kRpcMethodNotFound)
                    status = QHttpServerResponse::StatusCode::NotFound;
                return QHttpServerResponse(reply.toObject(), status);
            });
        });

    // No server-initiated stream: the spec lets a server refuse the GET.
    m_server->route(QStringLiteral("/mcp"), QHttpServerRequest::Method::Get, [] {
        QHttpServerResponse response(QHttpServerResponse::StatusCode::MethodNotAllowed);
        QHttpHeaders headers = response.headers();
        headers.append(QHttpHeaders::WellKnownHeader::Allow, QByteArrayLiteral("POST"));
        response.setHeaders(std::move(headers));
        return response;
    });
}

bool WebServer::originAllowed(const QHttpServerRequest& req) const
{
    const QByteArray origin = req.headers().combinedValue(QByteArrayLiteral("Origin"));
    if (origin.isEmpty())
        return true;   // not a browser
    const QString text = QString::fromLatin1(origin);
    if (m_config.corsAllowedOrigins.contains(QStringLiteral("*"))
        || m_config.corsAllowedOrigins.contains(text))
        return true;
    // The page was served by this server.
    const QUrl url(text);
    const QByteArray host = req.headers().combinedValue(QByteArrayLiteral("Host"));
    const QString self = url.port() > 0 ? QStringLiteral("%1:%2").arg(url.host()).arg(url.port())
                                        : url.host();
    return !host.isEmpty() && QString::fromLatin1(host) == self;
}

QFuture<QJsonValue> WebServer::handleMcpMessage(const QJsonValue& message)
{
    // A batch (older protocol revisions): answer each in turn.
    if (message.isArray()) {
        const QJsonArray batch = message.toArray();
        auto replies = std::make_shared<QJsonArray>();
        auto promise = std::make_shared<QPromise<QJsonValue>>();
        promise->start();
        QFuture<QJsonValue> future = promise->future();
        auto remaining = std::make_shared<qsizetype>(batch.size());
        if (batch.isEmpty()) {
            promise->addResult(rpcError(QJsonValue(), -32600, QStringLiteral("Empty batch")));
            promise->finish();
            return future;
        }
        for (const QJsonValue& one : batch) {
            handleMcpMessage(one).then(this, [replies, promise, remaining](const QJsonValue& reply) {
                if (!reply.isUndefined())
                    replies->append(reply);
                if (--*remaining == 0) {
                    promise->addResult(replies->isEmpty() ? QJsonValue(QJsonValue::Undefined)
                                                          : QJsonValue(*replies));
                    promise->finish();
                }
            });
        }
        return future;
    }

    const QJsonObject request = message.toObject();
    const QJsonValue id = request.value(L("id"));
    const QString method = request.value(L("method")).toString();
    const bool isNotification = !request.contains(L("id"));

    if (request.value(L("jsonrpc")).toString() != L("2.0") || method.isEmpty()) {
        // A response to something we never asked, or garbage.
        if (request.contains(L("result")) || request.contains(L("error")))
            return ready(QJsonValue(QJsonValue::Undefined));
        return ready(rpcError(id, -32600, QStringLiteral("Invalid request")));
    }
    if (isNotification)
        return ready(QJsonValue(QJsonValue::Undefined));   // notifications/initialized etc.

    const QJsonObject params = request.value(L("params")).toObject();
    const QJsonObject capabilities{
        {QStringLiteral("tools"), QJsonObject{{QStringLiteral("listChanged"), false}}}};
    const QJsonObject serverInfo{{QStringLiteral("name"), QStringLiteral("emuleqt")},
                                 {QStringLiteral("title"), QStringLiteral("eMuleQt")},
                                 {QStringLiteral("version"), QString(kAppVersion)}};

    // Per-request era: the request names its revision; no handshake came before it.
    const QString version = requestedVersion(request);
    const bool perRequestEra = !version.isEmpty();
    if (perRequestEra && !kMcpPerRequestVersions.contains(version)) {
        QJsonObject failure = rpcError(id, kRpcUnsupportedVersion,
                                       QStringLiteral("Unsupported protocol version"));
        QJsonObject detail = failure.value(L("error")).toObject();
        detail.insert(QStringLiteral("data"), QJsonObject{
            {QStringLiteral("supported"), QJsonArray::fromStringList(allMcpVersions())},
            {QStringLiteral("requested"), version}});
        failure.insert(QStringLiteral("error"), detail);
        return ready(failure);
    }
    // Results of that era say whether they are final; ours always are.
    const auto complete = [perRequestEra](QJsonObject result) {
        if (perRequestEra)
            result.insert(QStringLiteral("resultType"), QStringLiteral("complete"));
        return result;
    };

    if (method == L("server/discover")) {
        return ready(rpcResult(id, QJsonObject{
            {QStringLiteral("resultType"), QStringLiteral("complete")},
            {QStringLiteral("supportedVersions"), QJsonArray::fromStringList(allMcpVersions())},
            {QStringLiteral("capabilities"), capabilities},
            {QStringLiteral("_meta"), QJsonObject{
                {QStringLiteral("io.modelcontextprotocol/serverInfo"), serverInfo}}},
            {QStringLiteral("instructions"), QString::fromUtf8(kMcpInstructions)},
        }));
    }
    if (method == L("initialize") && !perRequestEra) {
        const QString asked = params.value(L("protocolVersion")).toString();
        return ready(rpcResult(id, QJsonObject{
            {QStringLiteral("protocolVersion"),
             kMcpHandshakeVersions.contains(asked) ? asked : kMcpHandshakeVersions.first()},
            {QStringLiteral("capabilities"), capabilities},
            {QStringLiteral("serverInfo"), serverInfo},
            {QStringLiteral("instructions"), QString::fromUtf8(kMcpInstructions)},
        }));
    }
    if (method == L("ping"))
        return ready(rpcResult(id, complete({})));
    if (method == L("tools/list"))
        return ready(rpcResult(id, complete(mcpToolList())));
    if (method == L("tools/call")) {
        return mcpToolCall(id, params).then(this, [complete](const QJsonValue& reply) {
            QJsonObject out = reply.toObject();
            if (out.contains(L("result")))
                out.insert(QStringLiteral("result"), complete(out.value(L("result")).toObject()));
            return QJsonValue(out);
        });
    }

    return ready(rpcError(id, kRpcMethodNotFound,
                          QStringLiteral("Method not found: %1").arg(method)));
}

QJsonObject WebServer::mcpToolList() const
{
    QJsonArray tools;
    for (const Operation& op : m_api.operations()) {
        if (op.mcpTool.isEmpty() || op.is(NoMcpWrite) || op.is(RawStream))
            continue;
        if (m_config.mcpReadOnly && !op.is(ReadOnly))
            continue;

        QString description = op.summary + u'.';
        if (!op.description.isEmpty())
            description += u' ' + op.description;
        if (op.is(Destructive))
            description += QStringLiteral(" Destructive: needs confirm=true; ask the user first.");

        tools.append(QJsonObject{
            {QStringLiteral("name"), op.mcpTool},
            {QStringLiteral("title"), op.summary},
            {QStringLiteral("description"), description},
            {QStringLiteral("inputSchema"), inputSchema(op)},
            {QStringLiteral("annotations"), QJsonObject{
                {QStringLiteral("readOnlyHint"), op.is(ReadOnly)},
                {QStringLiteral("destructiveHint"), op.is(Destructive)},
                {QStringLiteral("openWorldHint"), true},
            }},
        });
    }
    return QJsonObject{{QStringLiteral("tools"), tools}};
}

QFuture<QJsonValue> WebServer::mcpToolCall(const QJsonValue& id, const QJsonObject& params)
{
    const QString name = params.value(L("name")).toString();
    const Operation* op = m_api.findTool(name);
    if (!op || op->is(NoMcpWrite) || op->is(RawStream))
        return ready(rpcError(id, -32602, QStringLiteral("Unknown tool: %1").arg(name)));
    if (m_config.mcpReadOnly && !op->is(ReadOnly)) {
        return ready(rpcResult(id, toolResult(
            error(403, QStringLiteral("This server offers read-only tools only")))));
    }

    QJsonObject args = params.value(L("arguments")).toObject();
    if (op->is(Paged) && !args.contains(L("limit")))
        args.insert(QStringLiteral("limit"), kMcpDefaultPageSize);

    // A bad argument is a tool error, not a protocol error: the model can read it and retry.
    const Registry::Parsed parsed = m_api.parseArgs(*op, args);
    if (!parsed.ok)
        return ready(rpcResult(id, toolResult(parsed.failure)));

    Call call;
    call.args = parsed.args;
    return m_api.invoke(*op, call).then(this, [id](const Result& result) {
        return QJsonValue(rpcResult(id, toolResult(result)));
    });
}

} // namespace eMule
