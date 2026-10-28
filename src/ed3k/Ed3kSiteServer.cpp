/// @file Ed3kSiteServer.cpp
/// @brief The local website server (name.md §6.2, §6.3) — implementation.

#include "Ed3kSiteServer.h"

#include "Ed3kSession.h"
#include "Ed3kSets.h"

#include "utils/Log.h"

#include <QDateTime>
#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QTcpServer>
#include <QUrl>

#include <algorithm>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

// A request answered later: a set's manifest, a member or a name is coming.
struct Ed3kSiteServer::Pending {
    explicit Pending(QHttpServerResponder&& r) : responder(std::move(r)) {}

    QHttpServerResponder responder;
    qint64 deadline = 0;   ///< ms since the epoch
    bool head = false;
    bool user = false;
    QString path;          ///< percent-encoded, from "/"
    QByteArray ifNoneMatch;
    QByteArray range;     ///< Range of a GET (RFC 9110 §14.2), If-Range with it
    QByteArray ifRange;
    // a set's origin
    ffi::Key32 key{};
    QByteArray id;
    QByteArray manifest;   ///< once there: waiting for a member
    std::optional<ffi::Root> member;
    /// a range parked for its blocks: offset, length (0: the whole member)
    std::uint64_t rangeFirst = 0;
    std::uint64_t rangeLen = 0;
    // a readable host
    std::optional<std::uint64_t> resolve;
    // a set not held: waiting for its manifest (siteSetStatus), since parkedAt (ms)
    bool setWait = false;
    qint64 parkedAt = 0;
};

namespace {

QByteArray methodName(QHttpServerRequest::Method m)
{
    using M = QHttpServerRequest::Method;
    switch (m) {
    case M::Get: return "GET";
    case M::Head: return "HEAD";
    case M::Post: return "POST";
    case M::Put: return "PUT";
    case M::Delete: return "DELETE";
    case M::Options: return "OPTIONS";
    case M::Patch: return "PATCH";
    case M::Connect: return "CONNECT";
    case M::Trace: return "TRACE";
    default: return "UNKNOWN";
    }
}

/// The port of a Host header; 0: none or not a number.
std::uint16_t hostPort(const QByteArray& host)
{
    const qsizetype bracket = host.lastIndexOf(']');
    const qsizetype colon = host.lastIndexOf(':');
    if (colon < 0 || colon < bracket)
        return 0;
    bool ok = false;
    const uint p = host.mid(colon + 1).toUInt(&ok);
    return ok && p > 0 && p <= 65535 ? std::uint16_t(p) : 0;
}

std::string_view viewOf(const QByteArray& b)
{
    return {b.constData(), std::size_t(b.size())};
}

QByteArray etagOf(const QByteArray& headerLines)
{
    for (const QByteArray& line : headerLines.split('\n')) {
        const QByteArray l = line.trimmed();
        if (l.toLower().startsWith("etag:"))
            return l.mid(5).trimmed();
    }
    return {};
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

qint64 nowS()
{
    return QDateTime::currentSecsSinceEpoch();
}

QString fetchKey(const ::ed3k::Key32& key, const QByteArray& id)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(key.data()), qsizetype(key.size())).toHex() + '/' + id.toHex());
}

const QByteArray kPlain = "Content-Type: text/plain; charset=utf-8\r\nX-Content-Type-Options: nosniff\r\nCache-Control: no-store\r\n";

/// What a Range header asks of a member of @p size bytes (RFC 9110 §14.1.2, §14.2).
struct RangeAsk {
    enum Kind { Whole, One, Unsatisfiable } kind = Whole;
    std::uint64_t first = 0;
    std::uint64_t last = 0;   ///< inclusive
};

/// One byte range only: several are answered with the whole member (a server MAY ignore
/// Range, §14.2; no multipart/byteranges here). Not "bytes=" or not parsing: ignored too.
RangeAsk parseRange(const QByteArray& header, std::uint64_t size)
{
    const QByteArray h = header.trimmed();
    if (h.isEmpty() || !h.toLower().startsWith("bytes=") || size == 0)
        return {};
    const QByteArray set = h.mid(6).trimmed();
    if (set.contains(','))
        return {};
    const qsizetype dash = set.indexOf('-');
    if (dash < 0)
        return {};
    const QByteArray a = set.left(dash).trimmed();
    const QByteArray b = set.mid(dash + 1).trimmed();
    bool okA = false;
    bool okB = false;
    const auto digits = [](const QByteArray& t) { return !t.isEmpty() && std::ranges::all_of(t, [](char c) { return c >= '0' && c <= '9'; }); };
    if (a.isEmpty()) {
        // suffix: the last n bytes; 0 is unsatisfiable
        const std::uint64_t n = digits(b) ? b.toULongLong(&okB) : 0;
        if (!okB)
            return {};
        if (n == 0)
            return {RangeAsk::Unsatisfiable};
        return {RangeAsk::One, size - std::min(n, size), size - 1};
    }
    const std::uint64_t first = digits(a) ? a.toULongLong(&okA) : 0;
    if (!okA)
        return {};
    std::uint64_t last = size - 1;
    if (!b.isEmpty()) {
        last = digits(b) ? b.toULongLong(&okB) : 0;
        if (!okB || last < first)
            return {};   // invalid: ignored (§14.1.1)
        last = std::min(last, size - 1);
    }
    if (first >= size)
        return {RangeAsk::Unsatisfiable};
    return {RangeAsk::One, first, last};
}

} // namespace

Ed3kSiteServer::Ed3kSiteServer(Ed3kSession& session, Ed3kNames& names, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_names(names)
{
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kSiteServer::onSessionStatus);
    connect(&m_names, &Ed3kNames::siteChanged, this, &Ed3kSiteServer::onSiteChanged);
    connect(&m_names, &Ed3kNames::siteMemberReady, this, &Ed3kSiteServer::onMemberReady);
    connect(&m_names, &Ed3kNames::resolved, this, &Ed3kSiteServer::onResolved);
    connect(&m_names, &Ed3kNames::failed, this, &Ed3kSiteServer::onFailed);
    m_tick.setInterval(500);
    connect(&m_tick, &QTimer::timeout, this, &Ed3kSiteServer::onTick);
    onSessionStatus();
}

Ed3kSiteServer::~Ed3kSiteServer()
{
    close();
}

void Ed3kSiteServer::setPort(std::uint16_t port)
{
    if (port == m_wantPort)
        return;
    m_wantPort = port;
    if (m_server) {
        close();
        listen();
    }
}

std::uint16_t Ed3kSiteServer::port() const
{
    return m_server ? m_port : 0;
}

QString Ed3kSiteServer::originUrl(const ffi::Key32& key, const QByteArray& id, const QString& path) const
{
    if (!isListening() || id.isEmpty())
        return {};
    const auto host = ffi::siteOrigin(key, std::span(reinterpret_cast<const std::uint8_t*>(id.constData()), std::size_t(id.size())));
    if (!host)
        return {};
    QString p = path;
    if (!p.startsWith(QLatin1Char('/')))
        p.prepend(QLatin1Char('/'));
    return QStringLiteral("http://%1:%2%3")
        .arg(QString::fromStdString(*host))
        .arg(m_port)
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(p, "/")));
}

void Ed3kSiteServer::setFetchingSince(const ffi::Key32& key, const QByteArray& id, qint64 secs)
{
    m_fetching[fetchKey(key, id)] = secs;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kSiteServer::onSessionStatus()
{
    // the port serves websites while the node runs, and nothing else (§6.2)
    if (m_session.isRunning() && !m_server)
        listen();
    else if (!m_session.isRunning() && m_server)
        close();
}

void Ed3kSiteServer::listen()
{
    auto server = std::make_unique<QHttpServer>();
    server->setMissingHandler(this, [this](const QHttpServerRequest& req, QHttpServerResponder& resp) { onRequest(req, resp); });
    // 127.0.0.1 and ::1 only, one port (§6.2)
    auto* v4 = new QTcpServer(server.get());
    if (!v4->listen(QHostAddress::LocalHost, m_wantPort) || !server->bind(v4)) {
        logEd3kWarning(QStringLiteral("ed3k: the website server cannot listen on 127.0.0.1:%1 (%2)").arg(m_wantPort).arg(v4->errorString()));
        return;
    }
    const std::uint16_t port = v4->serverPort();
    auto* v6 = new QTcpServer(server.get());
    if (!v6->listen(QHostAddress::LocalHostIPv6, port) || !server->bind(v6)) {
        logEd3kDebug(QStringLiteral("ed3k: the website server has no [::1]:%1 (%2)").arg(port).arg(v6->errorString()));
        delete v6;
    }
    m_server = std::move(server);
    m_port = port;
    m_tick.start();
    logEd3k(QStringLiteral("ed3k: websites on http://localhost:%1 (loopback only)").arg(port));
}

void Ed3kSiteServer::close()
{
    m_tick.stop();
    for (auto& p : m_pending)
        answer(p->responder, 503, "ed3k stopped\n", kPlain, p->head);
    m_pending.clear();
    m_server.reset();
    m_port = 0;
}

void Ed3kSiteServer::onRequest(const QHttpServerRequest& req, QHttpServerResponder& resp)
{
    // hosts of the port that are no website (live streams)
    if (m_hostHandler && m_hostHandler(req, resp))
        return;
    const QByteArray hostHeader = req.value("host");
    const auto host = ffi::siteHost(viewOf(hostHeader));
    const std::uint32_t kind = host ? host->kind : ED3K_SITE_HOST_OTHER;
    // §6.2: a Host this server serves on this port, else 421 (DNS rebinding reaches nothing)
    const bool served = kind != ED3K_SITE_HOST_OTHER && hostPort(hostHeader) == m_port;
    const QByteArray method = methodName(req.method());
    const QByteArray fetchSite = req.value("sec-fetch-site");
    const auto gate = ffi::siteGate(served, kind, viewOf(method), viewOf(fetchSite), viewOf(req.value("sec-fetch-mode")),
                                    viewOf(req.value("sec-fetch-user")));
    if (!gate || gate->status != 0) {
        if (served && fetchSite.isEmpty() && !m_toldFetchSite) {
            m_toldFetchSite = true;
            logEd3kWarning(QStringLiteral("ed3k: websites need a browser that sends Sec-Fetch-Site; this one does not (name.md §6.2)"));
        }
        answer(resp, gate ? gate->status : 421, {}, kPlain);
        return;
    }
    auto p = std::make_unique<Pending>(std::move(resp));
    p->head = req.method() == QHttpServerRequest::Method::Head;
    p->user = gate->user;
    p->path = req.url().path(QUrl::FullyEncoded);
    if (p->path.isEmpty())
        p->path = QStringLiteral("/");
    p->ifNoneMatch = req.value("if-none-match").trimmed();
    // Range only on GET (RFC 9110 §14.2)
    if (req.method() == QHttpServerRequest::Method::Get) {
        p->range = req.value("range");
        p->ifRange = req.value("if-range").trimmed();
    }
    p->deadline = nowMs() + m_waitMs;

    if (kind == ED3K_SITE_HOST_NAME || kind == ED3K_SITE_HOST_WEB) {
        // resolving happens only for the user (§6.2, §5.3)
        if (!p->user) {
            answer(p->responder, 403, {}, kPlain, p->head);
            return;
        }
        const auto id = m_names.resolve(QString::fromUtf8(host->text.data(), qsizetype(host->text.size())));
        if (!id) {
            answer(p->responder, 404, "no such name\n", kPlain, p->head);
            return;
        }
        p->resolve = *id;
        park(std::move(p));
        return;
    }
    p->key = host->key;
    p->id = QByteArray(host->text.data(), qsizetype(host->text.size()));
    const QByteArray manifest = m_names.siteManifest(p->key, p->id);
    if (!manifest.isEmpty()) {
        // one kept from an earlier run: followed for the user, for a newer version (§6.2)
        if (p->user && !m_names.siteFollowed(p->key, p->id))
            (void)m_names.followSite(p->key, p->id);
        servePage(std::move(p), manifest);
        return;
    }
    // a set not held is fetched only for the user (§6.2)
    // was: followed for the user, else 404 (503 when the follow failed); now siteSetStatus
    // (§6.3, batch 11 ruling #9): wait for the manifest, 504 after 60 s, 503 when not fetched
    // if (p->user)
    //     (void)m_names.followSite(p->key, p->id);
    if (p->user && m_names.followSite(p->key, p->id))
        m_fetching.try_emplace(fetchKey(p->key, p->id), nowS());
    p->parkedAt = nowMs();
    waitForSet(std::move(p));
}

void Ed3kSiteServer::servePage(std::unique_ptr<Pending> p, const QByteArray& manifest)
{
    const QByteArray path = p->path.toLatin1();
    const auto page = ffi::sitePage(std::span(reinterpret_cast<const std::uint8_t*>(manifest.constData()), std::size_t(manifest.size())),
                                    viewOf(path), true);
    if (!page) {
        // §6.1: a set that is no website is shown as a set, for download
        // was: ToDo: offer the set for download in the GUI (file-set.md §2.3) (done: setOffered →
        // the GUI asks, Ed3kSets follows; only for the user's own request)
        if (p->user && page.error() == ffi::Error::NotFound)
            emit setOffered(Ed3kSets::setLink(p->key, p->id));
        answer(p->responder, page.error() == ffi::Error::NotFound ? 404 : 502, "not a website\n", kPlain, p->head);
        return;
    }
    const QByteArray headers = QByteArray::fromStdString(page->headers);
    if (!page->member) {
        answer(p->responder, page->status, {}, headers, p->head);
        return;
    }
    const QByteArray etag = etagOf(headers);
    if (page->status == 200 && !etag.isEmpty() && p->ifNoneMatch == etag) {
        answer(p->responder, 304, {}, headers, true);
        return;
    }
    // was: ToDo: Range requests from verified blocks (§6.3); the whole member for now
    // (done: one range, 206/416, from the blocks verified so far; see parseRange)
    const QByteArray member = headers + "Accept-Ranges: bytes\r\n";
    // If-Range (§13.1.5): the range only while the ETag is the same (no dates: no Last-Modified)
    const bool rangeOk = page->status == 200 && !p->range.isEmpty() && (p->ifRange.isEmpty() || p->ifRange == etag);
    const RangeAsk ask = rangeOk ? parseRange(p->range, page->size) : RangeAsk{};
    if (ask.kind == RangeAsk::Unsatisfiable) {
        answer(p->responder, 416, {}, kPlain + "Content-Range: bytes */" + QByteArray::number(qulonglong(page->size)) + "\r\n", p->head);
        return;
    }
    if (ask.kind == RangeAsk::One) {
        const std::uint64_t len = ask.last - ask.first + 1;
        if (const auto bytes = m_names.siteMemberRange(page->root, ask.first, len)) {
            const QByteArray cr = "Content-Range: bytes " + QByteArray::number(qulonglong(ask.first)) + '-'
                                  + QByteArray::number(qulonglong(ask.last)) + '/' + QByteArray::number(qulonglong(page->size)) + "\r\n";
            answer(p->responder, 206, *bytes, member + cr, p->head);
            return;
        }
        p->rangeFirst = ask.first;
        p->rangeLen = len;
    } else if (const auto bytes = m_names.siteMember(page->root)) {
        // answer(p->responder, page->status, *bytes, headers, p->head);
        answer(p->responder, page->status, *bytes, member, p->head);
        return;
    }
    // only verified bytes are sent: the member is fetched first (§6.3)
    if (!m_names.fetchSiteMember(page->root, page->size)) {
        logEd3kDebug(QStringLiteral("ed3k: website member of %1 B not fetched").arg(page->size));
        answer(p->responder, 504, {}, kPlain, p->head);
        return;
    }
    // a range waits for its blocks, the whole member for all (onTick, onMemberReady; 504 after 60 s)
    p->manifest = manifest;
    p->member = page->root;
    park(std::move(p));
}

void Ed3kSiteServer::onSiteChanged(const QByteArray& key, const QByteArray& id)
{
    const auto k = Ed3kNames::keyFrom(key);
    if (!k)
        return;
    const QByteArray manifest = m_names.siteManifest(*k, id);
    if (manifest.isEmpty())
        return;
    m_fetching.erase(fetchKey(*k, id));
    for (auto& p : take([&](const Pending& x) { return !x.resolve && x.manifest.isEmpty() && x.key == *k && x.id == id; }))
        servePage(std::move(p), manifest);
}

void Ed3kSiteServer::onMemberReady(const QByteArray& root)
{
    for (auto& p : take([&](const Pending& x) { return x.member && Ed3kNames::toBytes(x.member->bytes) == root; })) {
        const QByteArray manifest = p->manifest;
        p->member.reset();
        servePage(std::move(p), manifest);   // the member is held now
    }
}

void Ed3kSiteServer::onResolved(const Ed3kNames::Resolved& r)
{
    for (auto& p : take([&](const Pending& x) { return x.resolve == r.id; })) {
        // §4.4, §5.3: ask the user before the first open; the GUI opens it then
        if (r.confirm || r.how.how == ED3K_NAME_HOW_DNS_UNVERIFIED) {
            emit confirmNeeded(r);
            answer(p->responder, 403, "Confirm this name in eMuleQt first.\n", kPlain, p->head);
            continue;
        }
        const auto set = Ed3kNames::firstSet(r.links);
        if (!set) {
            answer(p->responder, 404, "this name leads to no website\n", kPlain, p->head);
            continue;
        }
        (void)m_names.open(r.id);
        (void)m_names.followSite(set->key, set->id);
        // a 302 and nothing else under a readable host (§6.2)
        const QString path = p->path == QLatin1String("/") ? set->path : p->path;
        const QByteArray location = originUrl(set->key, set->id, QUrl::fromPercentEncoding(path.toLatin1())).toLatin1();
        answer(p->responder, 302, {}, kPlain + "Location: " + location + "\r\n", p->head);
    }
}

void Ed3kSiteServer::onFailed(quint64 id, const QString& name, int why, const QList<QByteArray>& ties)
{
    Q_UNUSED(name) Q_UNUSED(why) Q_UNUSED(ties)
    for (auto& p : take([&](const Pending& x) { return x.resolve == id; }))
        answer(p->responder, 404, "no such name\n", kPlain, p->head);
}

void Ed3kSiteServer::onTick()
{
    // a range whose blocks came in meanwhile (§6.3: sent as blocks pass under the root)
    for (auto& p : take([&](const Pending& x) {
             return x.member && x.rangeLen > 0 && m_names.siteMemberRange(*x.member, x.rangeFirst, x.rangeLen).has_value();
         })) {
        const QByteArray manifest = p->manifest;
        p->member.reset();
        servePage(std::move(p), manifest);
    }
    const qint64 now = nowMs();
    for (auto& p : take([&](const Pending& x) { return x.deadline <= now; })) {
        // a set's manifest: asked again (504 once the core or the local wait says so)
        if (p->setWait && p->manifest.isEmpty())
            waitForSet(std::move(p));
        else
            answer(p->responder, 504, {}, kPlain, p->head);
    }
}

void Ed3kSiteServer::waitForSet(std::unique_ptr<Pending> p)
{
    std::optional<std::uint64_t> since;
    if (const auto it = m_fetching.find(fetchKey(p->key, p->id)); it != m_fetching.end() && m_names.siteFollowed(p->key, p->id))
        since = std::uint64_t(std::max<qint64>(0, it->second));
    const auto st = ffi::siteSetStatus(false, since, std::uint64_t(std::max<qint64>(0, nowS())));
    if (!st || st->status != 0) {
        answer(p->responder, st ? st->status : 503, {}, kPlain, p->head);
        return;
    }
    // up to waitS, and no longer than this server waits for anything (tests shorten it)
    const qint64 now = nowMs();
    const qint64 left = std::min<qint64>(qint64(st->waitS) * 1000, p->parkedAt + m_waitMs - now);
    if (left <= 0) {
        answer(p->responder, 504, {}, kPlain, p->head);
        return;
    }
    p->setWait = true;
    p->deadline = now + left;
    park(std::move(p));
}

void Ed3kSiteServer::park(std::unique_ptr<Pending> p)
{
    m_pending.push_back(std::move(p));
}

std::vector<std::unique_ptr<Ed3kSiteServer::Pending>> Ed3kSiteServer::take(const std::function<bool(const Pending&)>& match)
{
    std::vector<std::unique_ptr<Pending>> out;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (match(**it)) {
            out.push_back(std::move(*it));
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

void Ed3kSiteServer::answer(QHttpServerResponder& r, int status, const QByteArray& body, const QByteArray& headerLines, bool head)
{
    QHttpHeaders h;
    for (const QByteArray& line : headerLines.split('\n')) {
        const QByteArray l = line.trimmed();
        const qsizetype colon = l.indexOf(':');
        if (colon > 0)
            h.append(QByteArrayView(l.left(colon).trimmed()), QByteArrayView(l.mid(colon + 1).trimmed()));
    }
    const auto code = static_cast<QHttpServerResponder::StatusCode>(status);
    // never a CORS header (§6.2); HEAD: the headers alone
    if (head || status == 304 || body.isEmpty())
        r.write(h, code);
    else
        r.write(body, h, code);
}

} // namespace eMule::ed3k
