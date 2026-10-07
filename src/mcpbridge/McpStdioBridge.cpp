/// @file McpStdioBridge.cpp
/// @brief MCP stdio bridge — implementation.

#include "McpStdioBridge.h"

#include "app/AppConfig.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <yaml-cpp/yaml.h>

#include <algorithm>

namespace eMule {

namespace {

constexpr int kTimeoutMs = 120000;   // a URL import waits for a download
constexpr int kBridgeError = -32000;

} // namespace

McpStdioBridge::Target McpStdioBridge::targetFromConfig(const QString& configDir, QString& problem)
{
    Target target;
    const QString path = configDir + QStringLiteral("/preferences.yml");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        problem = QStringLiteral("Cannot read %1 — pass --url and --api-key").arg(path);
        return target;
    }

    try {
        const YAML::Node root = YAML::Load(file.readAll().toStdString());
        const YAML::Node ws = root["webserver"];
        if (!ws) {
            problem = QStringLiteral("%1 has no webserver section").arg(path);
            return target;
        }
        const bool https = ws["httpsEnabled"].as<bool>(false);
        const int port = ws["port"].as<int>(4711);
        QString host = QString::fromStdString(ws["listenAddress"].as<std::string>(std::string()));
        if (host.isEmpty() || host == QLatin1StringView("0.0.0.0") || host == QLatin1StringView("::"))
            host = QStringLiteral("127.0.0.1");
        else if (host.contains(u':'))
            host = u'[' + host + u']';

        target.apiKey = QByteArray::fromStdString(ws["apiKey"].as<std::string>(std::string()));
        target.insecureTls = https;   // a local daemon's certificate is usually self-signed
        target.url = QUrl(QStringLiteral("%1://%2:%3/mcp")
                              .arg(https ? QStringLiteral("https") : QStringLiteral("http"), host)
                              .arg(port));
        if (!ws["mcpEnabled"].as<bool>(false)) {
            problem = QStringLiteral("The MCP endpoint is switched off. Enable it in Options > "
                                     "Web Interface (or set webserver.mcpEnabled) and restart "
                                     "this bridge.");
        }
    } catch (const YAML::Exception& e) {
        problem = QStringLiteral("%1 is not readable YAML: %2").arg(path, QString::fromUtf8(e.what()));
    }
    return target;
}

McpStdioBridge::McpStdioBridge(Target target, std::function<void(const QByteArray&)> writeLine,
                               QObject* parent)
    : QObject(parent)
    , m_target(std::move(target))
    , m_writeLine(std::move(writeLine))
{
}

void McpStdioBridge::handleLine(const QByteArray& lineIn)
{
    const QByteArray line = lineIn.trimmed();
    if (line.isEmpty())
        return;

    // Only to know whether an answer is owed should the daemon not give one.
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    const QJsonObject message = doc.object();
    const bool expectsReply = doc.isArray() || message.contains(QLatin1StringView("id"));
    const QJsonValue id = message.value(QLatin1StringView("id"));

    QNetworkRequest request(m_target.url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("Authorization"), "Bearer " + m_target.apiKey);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString(kUserAgent + QStringLiteral(" mcp-bridge")));
    request.setTransferTimeout(kTimeoutMs);

    // The newer protocol revisions mirror version, method and tool name into
    // headers on HTTP; over stdio the client had no reason to send them.
    const QJsonObject params = message.value(QLatin1StringView("params")).toObject();
    const QString version = params.value(QLatin1StringView("_meta")).toObject()
        .value(QLatin1StringView("io.modelcontextprotocol/protocolVersion")).toString();
    if (!version.isEmpty()) {
        const auto headerValue = [](const QString& text) {
            const QByteArray utf8 = text.toUtf8();
            const bool plain = !utf8.isEmpty() && utf8 == utf8.trimmed()
                && !utf8.startsWith("=?base64?")
                && std::all_of(utf8.begin(), utf8.end(), [](char c) { return c >= 0x20 && c < 0x7f; });
            return plain ? utf8 : QByteArray("=?base64?") + utf8.toBase64() + QByteArray("?=");
        };
        const QString method = message.value(QLatin1StringView("method")).toString();
        request.setRawHeader(QByteArrayLiteral("MCP-Protocol-Version"), version.toLatin1());
        request.setRawHeader(QByteArrayLiteral("Mcp-Method"), headerValue(method));
        if (method == QLatin1StringView("tools/call"))
            request.setRawHeader(QByteArrayLiteral("Mcp-Name"),
                                 headerValue(params.value(QLatin1StringView("name")).toString()));
    }

    ++m_pending;
    QNetworkReply* reply = m_nam.post(request, line);
    if (m_target.insecureTls)
        connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->ignoreSslErrors(); });

    connect(reply, &QNetworkReply::finished, this, [this, reply, id, expectsReply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll().trimmed();

        if (status == 200 && !body.isEmpty()) {
            // One line per message: compact whatever came.
            const QJsonDocument out = QJsonDocument::fromJson(body);
            m_writeLine(out.isNull() ? body : out.toJson(QJsonDocument::Compact));
        } else if (status == 202 || !expectsReply) {
            // a notification: nothing to say
        } else if ((status == 400 || status == 404) && body.startsWith('{')
                   && body.contains("\"jsonrpc\"")) {
            m_writeLine(body);   // the daemon's own JSON-RPC error (parse, version, method)
        } else {
            const QString where = m_target.url.toString(QUrl::RemoveUserInfo);
            QString text;
            if (status == 401)
                text = QStringLiteral("eMuleQt refused the API key (%1)").arg(where);
            else if (status == 404 || status == 405)
                text = QStringLiteral("The MCP endpoint is switched off at %1. Enable it in "
                                      "Options > Web Interface.").arg(where);
            else if (status == 0)
                text = QStringLiteral("eMuleQt is not reachable at %1: %2. Is the daemon running?")
                           .arg(where, reply->errorString());
            else
                text = QStringLiteral("eMuleQt answered HTTP %1 at %2").arg(status).arg(where);
            this->reply(id, kBridgeError, text);
        }

        --m_pending;
        maybeFinish();
    });
}

void McpStdioBridge::inputClosed()
{
    m_inputClosed = true;
    maybeFinish();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void McpStdioBridge::reply(const QJsonValue& id, int code, const QString& message)
{
    m_writeLine(QJsonDocument(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
                                              {QStringLiteral("message"), message}}},
    }).toJson(QJsonDocument::Compact));
}

void McpStdioBridge::maybeFinish()
{
    if (m_inputClosed && m_pending == 0)
        emit finished();
}

} // namespace eMule
