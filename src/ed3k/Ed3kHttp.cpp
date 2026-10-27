/// @file Ed3kHttp.cpp
/// @brief Host IO HTTP client of the ed3k node — implementation.

#include "Ed3kHttp.h"

#include "utils/Log.h"

#include <QHostInfo>
#include <QPointer>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <limits>

namespace eMule::ed3k {

namespace {

// headers at most 64 of 8 KiB (host-io.md "Limits"); a bit of room for the status line
constexpr qsizetype kMaxHeaderBytes = 64 * 8 * 1024 + 1024;

QByteArray methodName(std::uint32_t m)
{
    switch (m) {
    case ED3K_HTTP_GET:    return "GET";
    case ED3K_HTTP_POST:   return "POST";
    case ED3K_HTTP_DELETE: return "DELETE";
    case ED3K_HTTP_HEAD:   return "HEAD";
    default:               return {};
    }
}

QHostAddress toQt(const ::ed3k::Address& a)
{
    if (a.isV4())
        return QHostAddress((quint32(a.ip[12]) << 24) | (quint32(a.ip[13]) << 16) | (quint32(a.ip[14]) << 8)
                            | quint32(a.ip[15]));
    Q_IPV6ADDR v6;
    std::copy(a.ip.begin(), a.ip.end(), v6.c);
    return QHostAddress(v6);
}

::ed3k::Address fromQt(const QHostAddress& ip, std::uint16_t port)
{
    ::ed3k::Address a;
    bool v4 = false;
    const quint32 n = ip.toIPv4Address(&v4);
    if (v4)
        return ::ed3k::Address::v4(std::uint8_t(n >> 24), std::uint8_t(n >> 16), std::uint8_t(n >> 8), std::uint8_t(n),
                                   port);
    const Q_IPV6ADDR v6 = ip.toIPv6Address();
    std::copy(std::begin(v6.c), std::end(v6.c), a.ip.begin());
    a.port = port;
    return a;
}

} // namespace

struct Ed3kHttp::Request {
    ::ed3k::event::HttpRequest ev;
    QUrl url;
    bool tls = false;
    QList<QHostAddress> addrs;   ///< of the name, offered one by one
    qsizetype nextAddr = 0;
    bool literal = false;        ///< the event had addr: no other address
    QTcpSocket* socket = nullptr;
    QTimer timer;
    bool tcpUp = false;      ///< TCP connected (TLS may still fail)
    bool connected = false;  ///< ready for the request (TLS done)
    // response
    QByteArray in;
    bool headersDone = false;
    std::uint16_t status = 0;
    QByteArray headers;
    QByteArray body;
    qint64 contentLength = -1;
    bool chunked = false;
    bool noBody = false;
    bool chunkTrailer = false;

    ~Request()
    {
        if (socket) {
            QObject::disconnect(socket, nullptr, nullptr, nullptr);
            socket->abort();
            socket->deleteLater();
        }
    }
};

Ed3kHttp::Ed3kHttp(Answers answers, QString userAgent, QObject* parent)
    : QObject(parent)
    , m_answers(std::move(answers))
    , m_userAgent(std::move(userAgent))
{
}

Ed3kHttp::~Ed3kHttp() = default;

void Ed3kHttp::start(const ::ed3k::event::HttpRequest& req)
{
    const std::uint64_t id = req.id;
    auto r = std::make_unique<Request>();
    r->ev = req;
    r->url = QUrl(QString::fromStdString(req.url), QUrl::StrictMode);
    const QString scheme = r->url.scheme().toLower();
    r->tls = scheme == QLatin1String("https");
    r->noBody = req.method == ED3K_HTTP_HEAD;
    if (!r->url.isValid() || r->url.host().isEmpty() || (!r->tls && scheme != QLatin1String("http"))
        || methodName(req.method).isEmpty()) {
        logEd3kDebug(QStringLiteral("http %1: unusable request").arg(id));
        m_answers.failed(id);
        return;
    }
    r->timer.setSingleShot(true);
    connect(&r->timer, &QTimer::timeout, this, [this, id] {
        logEd3kDebug(QStringLiteral("http %1: timed out").arg(id));
        // queued: the timer belongs to the request fail() destroys
        QMetaObject::invokeMethod(this, [this, id] { fail(id); }, Qt::QueuedConnection);
    });
    r->timer.start(std::chrono::seconds(std::clamp<std::uint64_t>(req.timeoutS, 1, 600)));

    Request& ref = *r;
    m_requests[id] = std::move(r);
    if (req.addr) {
        ref.literal = true;
        connectTo(ref, toQt(*req.addr), req.addr->port ? req.addr->port : req.port);
        return;
    }
    // a name: resolved here, every address offered to the core first (host-io.md)
    QHostInfo::lookupHost(ref.url.host(), this, [this, id](const QHostInfo& info) {
        const auto it = m_requests.find(id);
        if (it == m_requests.end())
            return;
        it->second->addrs = info.addresses();
        resolveNext(*it->second);
    });
}

void Ed3kHttp::cancel(std::uint64_t id)
{
    drop(id);
}

void Ed3kHttp::clear()
{
    m_requests.clear();
}

void Ed3kHttp::resolveNext(Request& r)
{
    const std::uint64_t id = r.ev.id;
    if (r.nextAddr >= r.addrs.size()) {
        fail(id);
        return;
    }
    const QHostAddress ip = r.addrs.at(r.nextAddr++);
    const bool ok = m_answers.resolved(id, fromQt(ip, r.ev.port));
    // the answer may have pumped the engine: look the request up again
    const auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    if (!ok) {
        // Denied: the request is over, no other address of the name (IoCancel follows)
        drop(id);
        return;
    }
    connectTo(*it->second, ip, r.ev.port);
}

void Ed3kHttp::connectTo(Request& r, const QHostAddress& ip, std::uint16_t port)
{
    const std::uint64_t id = r.ev.id;
    if (r.socket) {
        QObject::disconnect(r.socket, nullptr, nullptr, nullptr);
        r.socket->abort();
        r.socket->deleteLater();
        r.socket = nullptr;
    }
    r.connected = false;
    r.tcpUp = false;
    QTcpSocket* s = nullptr;
    if (r.tls) {
        auto* ssl = new QSslSocket;
        // Host/SNI/certificate: the name of the URL; the TCP connection: the checked address
        ssl->setPeerVerifyName(r.url.host());
        connect(ssl, &QSslSocket::encrypted, this, [this, id] { onConnected(id); });
        connect(ssl, &QSslSocket::connected, this, [this, id] {
            if (const auto it = m_requests.find(id); it != m_requests.end())
                it->second->tcpUp = true;
        });
        s = ssl;
    } else {
        s = new QTcpSocket;
        connect(s, &QTcpSocket::connected, this, [this, id] { onConnected(id); });
    }
    r.socket = s;
    // the literal address goes to the proxy, never the name
    s->setProxy(m_proxy);
    connect(s, &QTcpSocket::readyRead, this, [this, id] { onReadyRead(id); });
    connect(s, &QTcpSocket::errorOccurred, this, [this, id](QAbstractSocket::SocketError) { onSocketError(id); });
    connect(s, &QTcpSocket::disconnected, this, [this, id] { onDisconnected(id); });
    if (auto* ssl = qobject_cast<QSslSocket*>(s))
        ssl->connectToHostEncrypted(ip.toString(), port, r.url.host());
    else
        s->connectToHost(ip, port);
}

void Ed3kHttp::onConnected(std::uint64_t id)
{
    const auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    Request& r = *it->second;
    r.tcpUp = true;
    r.connected = true;
    r.socket->write(requestBytes(r));
}

void Ed3kHttp::onReadyRead(std::uint64_t id)
{
    const auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    Request& r = *it->second;
    r.in.append(r.socket->readAll());
    parse(r, false);
}

void Ed3kHttp::onSocketError(std::uint64_t id)
{
    const auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    Request& r = *it->second;
    if (r.connected) {
        if (r.socket->error() == QAbstractSocket::RemoteHostClosedError) {
            onDisconnected(id);
            return;
        }
        logEd3kDebug(QStringLiteral("http %1: %2").arg(id).arg(r.socket->errorString()));
        fail(id);   // broken transfer
        return;
    }
    if (r.tcpUp || r.literal) {
        // TLS failure, or the one address of a literal URL: no other address (host-io.md)
        logEd3kDebug(QStringLiteral("http %1: %2").arg(id).arg(r.socket->errorString()));
        fail(id);
        return;
    }
    // no TCP connection to this address: offer the next one of the name
    m_answers.unreachable(id);
    const auto again = m_requests.find(id);
    if (again != m_requests.end())
        resolveNext(*again->second);
}

void Ed3kHttp::onDisconnected(std::uint64_t id)
{
    const auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    Request& r = *it->second;
    if (!r.connected)
        return;   // errorOccurred handles it
    r.in.append(r.socket->readAll());
    if (parse(r, true))
        fail(id);   // not complete at the end: broken transfer
}

bool Ed3kHttp::parse(Request& r, bool atEnd)
{
    const std::uint64_t id = r.ev.id;
    while (!r.headersDone) {
        const qsizetype end = r.in.indexOf("\r\n\r\n");
        if (end < 0) {
            if (r.in.size() > kMaxHeaderBytes) {
                fail(id);
                return false;
            }
            return true;
        }
        const QByteArray block = r.in.left(end);
        r.in.remove(0, end + 4);
        const qsizetype eol = block.indexOf("\r\n");
        const QByteArray statusLine = eol < 0 ? block : block.left(eol);
        const QList<QByteArray> parts = statusLine.split(' ');
        bool ok = false;
        const int status = parts.size() >= 2 && parts.at(0).startsWith("HTTP/1.") ? parts.at(1).toInt(&ok) : 0;
        if (!ok || status < 100 || status > 999) {
            fail(id);
            return false;
        }
        if (status >= 100 && status < 200)
            continue;   // interim answer: the real one follows
        r.status = std::uint16_t(status);
        r.headers = eol < 0 ? QByteArray() : block.mid(eol + 2) + "\r\n";
        for (const QByteArray& line : r.headers.split('\n')) {
            const qsizetype colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            const QByteArray name = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (name == "content-length")
                r.contentLength = value.toLongLong(&ok);
            else if (name == "transfer-encoding" && value.toLower().contains("chunked"))
                r.chunked = true;
        }
        if (r.status == 204 || r.status == 304)
            r.noBody = true;
        r.headersDone = true;
    }

    const auto tooLong = [&r](qint64 more) {
        return qint64(r.body.size()) + more > qint64(std::min<std::uint64_t>(r.ev.maxLen, std::numeric_limits<qint32>::max()));
    };
    if (r.noBody) {
        finish(id);
        return false;
    }
    if (r.chunked) {
        for (;;) {
            if (r.chunkTrailer) {
                // trailers end with an empty line
                const qsizetype end = r.in.indexOf("\r\n");
                if (end < 0)
                    return true;
                const bool last = end == 0;
                r.in.remove(0, end + 2);
                if (last) {
                    finish(id);
                    return false;
                }
                continue;
            }
            const qsizetype eol = r.in.indexOf("\r\n");
            if (eol < 0)
                return true;
            QByteArray sizeText = r.in.left(eol);
            if (const qsizetype semi = sizeText.indexOf(';'); semi >= 0)
                sizeText.truncate(semi);
            bool ok = false;
            const qint64 size = sizeText.trimmed().toLongLong(&ok, 16);
            if (!ok || size < 0) {
                fail(id);
                return false;
            }
            if (size == 0) {
                r.in.remove(0, eol + 2);
                r.chunkTrailer = true;
                continue;
            }
            if (tooLong(size)) {
                fail(id);
                return false;
            }
            if (r.in.size() < eol + 2 + size + 2)
                return true;
            r.body.append(r.in.mid(eol + 2, size));
            r.in.remove(0, eol + 2 + size + 2);
        }
    }
    if (r.contentLength >= 0) {
        const qint64 want = r.contentLength - r.body.size();
        if (tooLong(std::min<qint64>(want, r.in.size()))) {
            fail(id);
            return false;
        }
        r.body.append(r.in.left(want));
        r.in.clear();
        if (r.body.size() >= r.contentLength) {
            finish(id);
            return false;
        }
        return true;
    }
    // until the connection closes
    if (tooLong(r.in.size())) {
        fail(id);
        return false;
    }
    r.body.append(r.in);
    r.in.clear();
    if (atEnd) {
        finish(id);
        return false;
    }
    return true;
}

void Ed3kHttp::finish(std::uint64_t id)
{
    const auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    std::unique_ptr<Request> r = std::move(it->second);
    m_requests.erase(it);
    m_answers.answer(id, r->status, r->headers, r->body);
}

void Ed3kHttp::fail(std::uint64_t id)
{
    const auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    std::unique_ptr<Request> r = std::move(it->second);
    m_requests.erase(it);
    m_answers.failed(id);
}

void Ed3kHttp::drop(std::uint64_t id)
{
    m_requests.erase(id);
}

QByteArray Ed3kHttp::requestBytes(const Request& r) const
{
    QByteArray target = r.url.path(QUrl::FullyEncoded).toLatin1();
    if (target.isEmpty())
        target = "/";
    if (r.url.hasQuery())
        target += '?' + r.url.query(QUrl::FullyEncoded).toLatin1();
    QByteArray host = r.url.host(QUrl::FullyEncoded).toLatin1();
    if (host.contains(':'))
        host = '[' + host + ']';
    const int defaultPort = r.tls ? 443 : 80;
    if (r.url.port(defaultPort) != defaultPort)
        host += ':' + QByteArray::number(r.url.port());

    QByteArray out = methodName(r.ev.method) + ' ' + target + " HTTP/1.1\r\n";
    out += "Host: " + host + "\r\n";
    // the core's headers exactly as given (Accept-Encoding, Range, Content-Type ...)
    out += QByteArray::fromStdString(r.ev.headers);
    if (!out.endsWith("\r\n"))
        out += "\r\n";
    out += "User-Agent: " + m_userAgent.toUtf8() + "\r\n";
    out += "Connection: close\r\n";
    if (!r.ev.body.empty() || r.ev.method == ED3K_HTTP_POST)
        out += "Content-Length: " + QByteArray::number(qint64(r.ev.body.size())) + "\r\n";
    out += "\r\n";
    out.append(reinterpret_cast<const char*>(r.ev.body.data()), qsizetype(r.ev.body.size()));
    return out;
}

} // namespace eMule::ed3k
