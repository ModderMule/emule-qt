#include "pch.h"

/// @file ClickNLoadServer.cpp
/// @brief Click'n'Load 2 HTTP endpoints, as JDownloader's ExternInterfaceImpl answers them.

#include "app/ClickNLoadServer.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace eMule {

namespace {

constexpr qsizetype kMaxHeaderBytes = 16 * 1024;
/// a DLC of 4 MiB as base64, form-encoded
constexpr qsizetype kMaxBodyBytes = 6 * 1024 * 1024;
constexpr int kRetryMs = 60 * 1000;
constexpr int kIdleMs = 30 * 1000;

QByteArray reasonOf(int code)
{
    switch (code) {
    case 200: return QByteArrayLiteral("OK");
    case 204: return QByteArrayLiteral("No Content");
    case 400: return QByteArrayLiteral("Bad Request");
    case 404: return QByteArrayLiteral("Not Found");
    case 405: return QByteArrayLiteral("Method Not Allowed");
    case 411: return QByteArrayLiteral("Length Required");
    case 413: return QByteArrayLiteral("Payload Too Large");
    case 431: return QByteArrayLiteral("Request Header Fields Too Large");
    default:  return QByteArrayLiteral("Error");
    }
}

QString firstOf(const QList<std::pair<QString, QString>>& form, QStringView key)
{
    for (const auto& [k, v] : form)
        if (k == key)
            return v;
    return {};
}

QStringList allOf(const QList<std::pair<QString, QString>>& form, QStringView key)
{
    QStringList out;
    for (const auto& [k, v] : form)
        if (k == key)
            out << v;
    return out;
}

/// Archive passwords: passwords[] once each, or passwords with one per line
QStringList passwordsOf(const QList<std::pair<QString, QString>>& form)
{
    QStringList raw = allOf(form, u"passwords[]");
    if (raw.isEmpty())
        raw = allOf(form, u"passwords");
    QStringList out;
    for (const QString& r : std::as_const(raw))
        for (const QString& line : r.split(QRegularExpression(QStringLiteral("[\r\n]+"))))
            if (const QString t = line.trimmed(); !t.isEmpty() && !out.contains(t))
                out << t;
    return out;
}

/// keys JD's RemoteAPI does not take as method parameters
bool isApiControlKey(const QString& key)
{
    return key == u"callback" || key == u"signature" || key == u"rid" || key == u"_";
}

/// A JSON string, or a list of strings one per line.
QString stringOrLines(const QJsonValue& v)
{
    if (v.isArray()) {
        QStringList parts;
        for (const QJsonValue& e : v.toArray())
            if (e.isString())
                parts << e.toString();
        return parts.join(u'\n');
    }
    return v.toString();
}

/// The CnlQuery object of an application/json body: the object, [obj, ...] or {"params":[obj]}.
QJsonObject cnlOfJsonBody(const QByteArray& body, bool* ok)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    *ok = err.error == QJsonParseError::NoError;
    if (doc.isArray()) {
        *ok = *ok && doc.array().at(0).isObject();
        return doc.array().at(0).toObject();
    }
    const QJsonObject o = doc.object();
    if (const QJsonValue params = o.value(u"params"); params.isArray()) {
        *ok = *ok && params.toArray().at(0).isObject();
        return params.toArray().at(0).toObject();
    }
    *ok = *ok && doc.isObject();
    return o;
}

} // namespace

ClickNLoadServer::ClickNLoadServer(Handler handler, QObject* parent)
    : QObject(parent)
    , m_handler(std::move(handler))
    , m_retry(new QTimer(this))
{
    m_retry->setInterval(kRetryMs);
    connect(m_retry, &QTimer::timeout, this, &ClickNLoadServer::tryListen);
}

ClickNLoadServer::~ClickNLoadServer() = default;

void ClickNLoadServer::setEnabled(bool on, quint16 port)
{
    if (on == m_enabled && port == m_port)
        return;
    stopListening();
    m_enabled = on;
    m_port = port;
    m_warned = false;
    if (on)
        tryListen();
}

bool ClickNLoadServer::isListening() const
{
    return m_v4 && m_v4->isListening();
}

quint16 ClickNLoadServer::port() const
{
    return isListening() ? m_v4->serverPort() : m_port;
}

QList<std::pair<QString, QString>> ClickNLoadServer::parseForm(const QByteArray& form)
{
    QList<std::pair<QString, QString>> out;
    for (const QByteArray& part : form.split('&')) {
        if (part.isEmpty())
            continue;
        const qsizetype eq = part.indexOf('=');
        QByteArray key = eq < 0 ? part : part.left(eq);
        QByteArray value = eq < 0 ? QByteArray() : part.mid(eq + 1);
        key.replace('+', ' ');
        value.replace('+', ' ');
        out.append({QString::fromUtf8(QByteArray::fromPercentEncoding(key)),
                    QString::fromUtf8(QByteArray::fromPercentEncoding(value))});
    }
    return out;
}

QString ClickNLoadServer::cleanPackageName(const QString& name)
{
    static const QRegularExpression site(QStringLiteral(R"(\s*-\s*[a-z0-9\-]+\.[a-z]{2,3}\s*$)"));
    return QString(name).remove(site).trimmed();
}

void ClickNLoadServer::tryListen()
{
    if (!m_enabled || isListening())
        return;
    if (!m_v4) {
        m_v4 = new QTcpServer(this);
        connect(m_v4, &QTcpServer::newConnection, this, [this] { onConnection(m_v4); });
    }
    if (!m_v4->listen(QHostAddress::LocalHost, m_port)) {
        // JDownloader (or another copy) has the port: no Click'n'Load here until it is free
        if (!m_warned)
            logWarning(QStringLiteral("Click'n'Load: port %1 is in use (%2), trying again every minute")
                           .arg(m_port)
                           .arg(m_v4->errorString()));
        m_warned = true;
        m_retry->start();
        return;
    }
    m_retry->stop();
    m_port = m_v4->serverPort();
    // "localhost" may name ::1 first; best effort
    if (!m_v6) {
        m_v6 = new QTcpServer(this);
        connect(m_v6, &QTcpServer::newConnection, this, [this] { onConnection(m_v6); });
    }
    m_v6->listen(QHostAddress::LocalHostIPv6, m_port);
    logInfo(QStringLiteral("Click'n'Load: listening on 127.0.0.1:%1").arg(m_port));
}

void ClickNLoadServer::stopListening()
{
    m_retry->stop();
    if (m_v4)
        m_v4->close();
    if (m_v6)
        m_v6->close();
}

void ClickNLoadServer::onConnection(QTcpServer* server)
{
    while (QTcpSocket* s = server->nextPendingConnection()) {
        // a socket of the server dies with it; keep it ours
        s->setParent(this);
        m_requests.insert(s, {});
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            m_requests.remove(s);
            s->deleteLater();
        });
        QTimer::singleShot(kIdleMs, s, [s] { s->abort(); });
    }
}

void ClickNLoadServer::onReadyRead(QTcpSocket* s)
{
    auto it = m_requests.find(s);
    if (it == m_requests.end() || it->answered) {
        s->readAll();
        return;
    }
    it->buffer += s->readAll();
    const qsizetype headerEnd = it->buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (it->buffer.size() > kMaxHeaderBytes)
            respond(s, 431, {});
        return;
    }
    const QList<QByteArray> lines = it->buffer.left(headerEnd).split('\n');
    const QList<QByteArray> start = lines.value(0).trimmed().split(' ');
    if (start.size() < 2) {
        respond(s, 400, {});
        return;
    }
    QHash<QByteArray, QByteArray> headers;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const qsizetype colon = lines[i].indexOf(':');
        if (colon > 0)
            headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
    }
    if (headers.value("transfer-encoding").toLower().contains("chunked")) {
        respond(s, 411, {});
        return;
    }
    bool okLength = true;
    const qint64 length = headers.contains("content-length") ? headers.value("content-length").toLongLong(&okLength) : 0;
    if (!okLength || length < 0 || length > kMaxBodyBytes) {
        respond(s, 413, {});
        return;
    }
    const qsizetype bodyStart = headerEnd + 4;
    if (it->buffer.size() - bodyStart < length)
        return;   // more to come
    const QByteArray body = it->buffer.mid(bodyStart, length);
    it->answered = true;
    it->buffer.clear();
    handle(s, start[0].toUpper(), start[1], headers, body);
}

void ClickNLoadServer::handle(QTcpSocket* s, const QByteArray& method, const QByteArray& target,
                              const QHash<QByteArray, QByteArray>& headers, const QByteArray& body)
{
    if (method == "OPTIONS") {
        // CORS preflight, Private Network Access included: any page may post, as with JD
        QList<std::pair<QByteArray, QByteArray>> extra{
            {QByteArrayLiteral("Access-Control-Allow-Methods"), QByteArrayLiteral("GET, POST, OPTIONS")},
            {QByteArrayLiteral("Access-Control-Max-Age"), QByteArrayLiteral("1800")},
            {QByteArrayLiteral("Access-Control-Allow-Private-Network"), QByteArrayLiteral("true")}};
        if (const QByteArray h = headers.value("access-control-request-headers"); !h.isEmpty())
            extra.append({QByteArrayLiteral("Access-Control-Allow-Headers"), h});
        respond(s, 204, {}, {}, extra);
        return;
    }
    if (method != "GET" && method != "POST") {
        respond(s, 405, {});
        return;
    }
    const qsizetype q = target.indexOf('?');
    QByteArray path = q < 0 ? target : target.left(q);
    while (path.size() > 1 && path.endsWith('/'))
        path.chop(1);
    QList<std::pair<QString, QString>> form = parseForm(q < 0 ? QByteArray() : target.mid(q + 1));
    if (method == "POST" && !headers.value("content-type").toLower().startsWith("multipart/"))
        form += parseForm(body);

    if (path == "/jdcheck.js") {
        // pages look for the variable to offer their Click'n'Load button
        respond(s, 200, "jdownloader=true;\r\nvar version='" + QCoreApplication::applicationVersion().toUtf8() + "';\r\n",
                QByteArrayLiteral("text/javascript; charset=utf-8"));
        return;
    }
    if (path == "/jdcheckjson") {
        respond(s, 200, "{\"version\":\"" + QCoreApplication::applicationVersion().toUtf8() + "\",\"name\":\"eMule Qt\"}",
                QByteArrayLiteral("application/json"));
        return;
    }
    if (path == "/crossdomain.xml") {
        respond(s, 200,
                "<?xml version=\"1.0\"?>\r\n"
                "<!DOCTYPE cross-domain-policy SYSTEM \"http://www.macromedia.com/xml/dtds/cross-domain-policy.dtd\">\r\n"
                "<cross-domain-policy>\r\n<allow-access-from domain=\"*\" />\r\n</cross-domain-policy>\r\n",
                QByteArrayLiteral("text/xml"));
        return;
    }
    if (path == "/flash") {
        respond(s, 200, "JDownloader\r\n");
        return;
    }
    if (path == "/flash/addcnl") {
        handleAddCnl(s, q < 0 ? QByteArray() : target.mid(q + 1), headers, body);
        return;
    }

    Post post;
    post.source = firstOf(form, u"source");
    if (post.source.isEmpty())
        post.source = QString::fromUtf8(headers.value("referer"));
    if (post.source.isEmpty())
        post.source = QString::fromUtf8(headers.value("origin"));
    post.package = cleanPackageName(firstOf(form, u"package"));
    post.passwords = passwordsOf(form);
    if (path == "/flash/add") {
        post.urls = firstOf(form, u"urls");
        if (post.urls.trimmed().isEmpty()) {
            respond(s, 200, "success\r\n");   // JD: nothing to add is no error
            return;
        }
        handlePost(s, std::move(post));
        return;
    }
    if (path == "/flash/addcrypted2") {
        // base64 has no spaces: a '+' the page did not encode came in as one
        post.crypted = firstOf(form, u"crypted").trimmed().replace(u' ', u'+');
        post.jk = firstOf(form, u"jk");
        post.k = firstOf(form, u"k").trimmed();
        if (post.crypted.isEmpty() || (post.jk.trimmed().isEmpty() && post.k.isEmpty())) {
            respond(s, 200, "failed\r\n");
            return;
        }
        handlePost(s, std::move(post));
        return;
    }
    if (path == "/flash/addcrypted") {
        post.kind = Post::Kind::Dlc;
        post.crypted = firstOf(form, u"crypted").trimmed().replace(u' ', u'+');
        if (post.crypted.isEmpty()) {
            respond(s, 200, "failed no DLC Content available\r\n");
            return;
        }
        handlePost(s, std::move(post));
        return;
    }
    respond(s, 404, {});
}

void ClickNLoadServer::handlePost(QTcpSocket* s, Post post, bool jsonp)
{
    logInfo(QStringLiteral("Click'n'Load: a post from %1").arg(post.source.isEmpty() ? QStringLiteral("?") : post.source.left(200)));
    if (!m_handler) {
        respond(s, 200, jsonp ? QByteArray() : QByteArrayLiteral("failed\r\n"));
        return;
    }
    // the handler answers after the core did; the page may have gone by then
    QPointer<QTcpSocket> socket(s);
    m_handler(post, [this, socket, jsonp](bool ok, const QString& error) {
        if (!socket)
            return;
        if (jsonp)
            respond(socket, 200, {}, QByteArrayLiteral("text/javascript; charset=utf-8"));
        else
            respond(socket, 200, ok ? QByteArrayLiteral("success\r\n") : "failed " + error.toUtf8() + "\r\n");
    });
}

void ClickNLoadServer::respond(QTcpSocket* s, int code, const QByteArray& body, const QByteArray& type,
                               const QList<std::pair<QByteArray, QByteArray>>& extra)
{
    if (const auto it = m_requests.find(s); it != m_requests.end())
        it->answered = true;
    QByteArray out = "HTTP/1.1 " + QByteArray::number(code) + ' ' + reasonOf(code) + "\r\n";
    if (!type.isEmpty() && code != 204)
        out += "Content-Type: " + type + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Access-Control-Allow-Origin: *\r\n";
    out += "Cache-Control: no-store\r\n";
    for (const auto& [k, v] : extra)
        out += k + ": " + v + "\r\n";
    out += "Connection: close\r\n\r\n";
    out += body;
    s->write(out);
    s->disconnectFromHost();
}

void ClickNLoadServer::handleAddCnl(QTcpSocket* s, const QByteArray& query,
                                    const QHash<QByteArray, QByteArray>& headers, const QByteArray& body)
{
    // JD's RemoteAPI: the object is the first parameter value, from the query, then the body
    const QList<std::pair<QString, QString>> queryForm = parseForm(query);
    const bool jsonp = !firstOf(queryForm, u"callback").isEmpty();
    const bool jsonBody = headers.value("content-type").toLower().contains("application/json");
    QList<std::pair<QString, QString>> params = queryForm;
    if (!jsonBody && !headers.value("content-type").toLower().startsWith("multipart/"))
        params += parseForm(body);
    QJsonObject cnl;
    bool ok = false;
    const auto value = std::ranges::find_if(params, [](const auto& p) { return !isApiControlKey(p.first); });
    if (value != params.end()) {
        QJsonParseError err;
        // "?{json}" has no '=': JD takes the lone part as the value
        const QString text = value->second.isEmpty() ? value->first : value->second;
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
        ok = err.error == QJsonParseError::NoError && doc.isObject();
        cnl = doc.object();
    } else if (jsonBody) {
        cnl = cnlOfJsonBody(body, &ok);
    }
    if (!ok) {
        respond(s, 400, jsonp ? QByteArray() : QByteArrayLiteral("failed bad parameter\r\n"));
        return;
    }

    Post post;
    post.urls = stringOrLines(cnl.value(u"urls"));
    // base64 has no spaces: a '+' decoded from a query or form came in as one
    post.crypted = cnl.value(u"crypted").toString().trimmed().replace(u' ', u'+');
    post.jk = cnl.value(u"jk").toString();
    post.k = (cnl.contains(u"key") ? cnl.value(u"key") : cnl.value(u"k")).toString().trimmed();
    if (post.jk.trimmed().isEmpty() && post.k.isEmpty())
        post.crypted.clear();   // JD drops a list it can't decrypt
    for (const QString& line : stringOrLines(cnl.value(u"passwords")).split(QRegularExpression(QStringLiteral("[\r\n]+"))))
        if (const QString t = line.trimmed(); !t.isEmpty() && !post.passwords.contains(t))
            post.passwords << t;
    post.package = cleanPackageName(
        (cnl.contains(u"packageName") ? cnl.value(u"packageName") : cnl.value(u"package")).toString());
    post.source = cnl.value(u"source").toString();
    if (post.source.isEmpty())
        post.source = cnl.value(u"referrer").toString();
    if (post.source.isEmpty())
        post.source = QString::fromUtf8(headers.value("referer"));
    if (post.source.isEmpty())
        post.source = QString::fromUtf8(headers.value("origin"));
    // comment, dir, autostart, permission: the "Add which?" dialog asks anyway

    if (post.urls.trimmed().isEmpty() && post.crypted.isEmpty()) {
        // JD: nothing to add is no error
        respond(s, 200, jsonp ? QByteArray() : QByteArrayLiteral("success\r\n"),
                jsonp ? QByteArrayLiteral("text/javascript; charset=utf-8") : QByteArrayLiteral("text/html; charset=utf-8"));
        return;
    }
    handlePost(s, std::move(post), jsonp);
}

} // namespace eMule
