#include "pch.h"
/// @file DohResolver.cpp
/// @brief DNS over HTTPS through the Proxy page's proxy.

#include "net/DohResolver.h"
#include "net/GuardedNetworkAccessManager.h"
#include "net/ProxySettings.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <memory>
#include <utility>

namespace eMule {

namespace {

constexpr qsizetype kMaxReplyBytes = 64 * 1024;
constexpr int kMaxNameBytes = 253;

[[nodiscard]] QNetworkAccessManager* manager()
{
    static QPointer<GuardedNetworkAccessManager> s_nam;
    if (!s_nam)
        s_nam = new GuardedNetworkAccessManager(QCoreApplication::instance());
    return s_nam;
}

[[nodiscard]] quint16 readU16(const QByteArray& m, qsizetype at)
{
    return quint16((quint8(m.at(at)) << 8) | quint8(m.at(at + 1)));
}

/// Offset after the name at @p at; -1 when it runs past the end. A pointer ends the name.
[[nodiscard]] qsizetype skipName(const QByteArray& m, qsizetype at)
{
    while (at < m.size()) {
        const auto len = quint8(m.at(at));
        if ((len & 0xC0) == 0xC0)
            return at + 2 <= m.size() ? at + 2 : -1;
        if (len == 0)
            return at + 1;
        if (len & 0xC0)
            return -1;   // reserved label types
        at += 1 + len;
    }
    return -1;
}

struct Record {
    quint16 type = 0;
    quint16 cls = 0;
    quint32 ttl = 0;
    qsizetype at = 0;   // rdata offset
    quint16 len = 0;
};

/// Answer records of a response; nullopt when not a response or cut short. @p rcode out.
[[nodiscard]] std::optional<QList<Record>> answerRecords(const QByteArray& m, int& rcode)
{
    if (m.size() < 12)
        return std::nullopt;
    const quint16 flags = readU16(m, 2);
    if (!(flags & 0x8000))
        return std::nullopt;
    rcode = flags & 0x000F;
    const quint16 questions = readU16(m, 4);
    const quint16 answers = readU16(m, 6);

    qsizetype at = 12;
    for (int i = 0; i < questions; ++i) {
        at = skipName(m, at);
        if (at < 0 || at + 4 > m.size())
            return std::nullopt;
        at += 4;
    }
    QList<Record> out;
    for (int i = 0; i < answers; ++i) {
        at = skipName(m, at);
        if (at < 0 || at + 10 > m.size())
            return out;   // keep what parsed
        Record r;
        r.type = readU16(m, at);
        r.cls = readU16(m, at + 2);
        r.ttl = quint32(readU16(m, at + 4)) << 16 | readU16(m, at + 6);
        r.len = readU16(m, at + 8);
        r.at = at + 10;
        if (r.at + r.len > m.size())
            return out;
        out << r;
        at = r.at + r.len;
    }
    return out;
}

/// One GET; @p onReply gets the body, or nullopt on a failure. Null when it cannot be sent.
QNetworkReply* startQuery(const QString& dohUrl, const QByteArray& msg, int timeoutMs,
                          const QString& name, std::function<void(std::optional<QByteArray>)> onReply)
{
    QUrl url(dohUrl.trimmed());
    QUrlQuery q(url);
    q.addQueryItem(QStringLiteral("dns"),
                   QString::fromLatin1(msg.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/dns-message");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(timeoutMs);
    QNetworkReply* reply = manager()->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, name, onReply = std::move(onReply)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status == 200) {
            onReply(reply->read(kMaxReplyBytes));
            return;
        }
        if (reply->error() != QNetworkReply::OperationCanceledError)
            logDebug(QStringLiteral("DoH: %1 via %2 failed: %3").arg(name, reply->url().host(), reply->errorString()));
        onReply(std::nullopt);
    });
    return reply;
}

[[nodiscard]] bool canSend(const QString& dohUrl)
{
    auto* app = QCoreApplication::instance();
    return isValidDohUrl(dohUrl) && app && QThread::currentThread() == app->thread();
}

struct Pending {
    QList<QHostAddress> addresses;
    int outstanding = 0;
    bool done = false;
    QPointer<QObject> context;
    std::function<void(QList<QHostAddress>)> callback;
    QList<QPointer<QNetworkReply>> replies;

    void finish()
    {
        if (done)
            return;
        done = true;
        for (const QPointer<QNetworkReply>& r : std::as_const(replies))
            if (r && r->isRunning())
                r->abort();
        if (context && callback)
            callback(addresses);
    }
};

} // namespace

const QList<DohProvider>& dohProviders()
{
    static const QList<DohProvider> s_providers{
        {QStringLiteral("Cloudflare"), QStringLiteral("https://1.1.1.1/dns-query")},
        {QStringLiteral("Google"), QStringLiteral("https://dns.google/dns-query")},
        {QStringLiteral("Quad9"), QStringLiteral("https://dns.quad9.net/dns-query")},
        {QStringLiteral("Mullvad"), QStringLiteral("https://dns.mullvad.net/dns-query")},
        {QStringLiteral("AdGuard (unfiltered)"), QStringLiteral("https://unfiltered.adguard-dns.com/dns-query")},
        {QStringLiteral("Control D (unfiltered)"), QStringLiteral("https://freedns.controld.com/p0")},
        {QStringLiteral("OpenDNS"), QStringLiteral("https://doh.opendns.com/dns-query")},
    };
    return s_providers;
}

QString defaultDohUrl()
{
    return dohProviders().constFirst().url;
}

bool isValidDohUrl(const QString& url)
{
    const QUrl u(url.trimmed(), QUrl::StrictMode);
    if (!u.isValid() || u.host().isEmpty() || !u.userInfo().isEmpty())
        return false;
    if (u.scheme() == QLatin1StringView("https"))
        return true;
    return u.scheme() == QLatin1StringView("http") && bypassesProxy(u.host());
}

QString activeDohUrl()
{
    const ProxySettings p = thePrefs.proxySettings();
    if (!p.useProxy || p.host.isEmpty())
        return {};
    return thePrefs.proxyDohUrl();
}

bool isLocalName(const QString& name)
{
    QString n = name.trimmed().toLower();
    if (n.endsWith(u'.'))
        n.chop(1);
    if (n.isEmpty() || !n.contains(u'.') || n == QLatin1StringView("localhost"))
        return true;
    for (const char* suffix : {".localhost", ".local", ".lan", ".internal", ".home.arpa"})
        if (n.endsWith(QLatin1StringView(suffix)))
            return true;
    return false;
}

void dohLookup(const QString& dohUrl, const QString& name, QObject* context,
               std::function<void(QList<QHostAddress>)> done, int timeoutMs)
{
    auto pending = std::make_shared<Pending>();
    pending->context = context;
    pending->callback = std::move(done);

    const QByteArray queryA = dns::buildQuery(name, dns::kTypeA);
    if (queryA.isEmpty() || !canSend(dohUrl)) {
        QTimer::singleShot(0, context, [pending] { pending->finish(); });
        return;
    }

    for (const quint16 type : {dns::kTypeA, dns::kTypeAAAA}) {
        const QByteArray msg = type == dns::kTypeA ? queryA : dns::buildQuery(name, type);
        ++pending->outstanding;
        pending->replies << startQuery(dohUrl, msg, timeoutMs, name, [pending](std::optional<QByteArray> body) {
            if (pending->done)
                return;
            if (body)
                pending->addresses << dns::parseAddresses(*body);
            if (--pending->outstanding == 0)
                pending->finish();
        });
    }
    QTimer::singleShot(timeoutMs + 500, context, [pending] { pending->finish(); });
}

void dohQuery(const QString& dohUrl, const QString& name, quint16 type, QObject* context,
              std::function<void(std::optional<QByteArray>)> done, int timeoutMs)
{
    struct One {
        bool done = false;
        QPointer<QObject> context;
        std::function<void(std::optional<QByteArray>)> callback;
        QPointer<QNetworkReply> reply;

        void finish(std::optional<QByteArray> body)
        {
            if (std::exchange(done, true))
                return;
            if (reply && reply->isRunning())
                reply->abort();
            if (context && callback)
                callback(std::move(body));
        }
    };
    auto one = std::make_shared<One>();
    one->context = context;
    one->callback = std::move(done);

    const QByteArray msg = dns::buildQuery(name, type);
    if (msg.isEmpty() || !canSend(dohUrl)) {
        QTimer::singleShot(0, context, [one] { one->finish(std::nullopt); });
        return;
    }
    one->reply = startQuery(dohUrl, msg, timeoutMs, name,
                            [one](std::optional<QByteArray> body) { one->finish(std::move(body)); });
    QTimer::singleShot(timeoutMs + 500, context, [one] { one->finish(std::nullopt); });
}

namespace dns {

QByteArray buildQuery(const QString& name, quint16 type)
{
    QString host = name.trimmed();
    if (host.endsWith(u'.'))
        host.chop(1);
    const QByteArray ace = QUrl::toAce(host);
    if (ace.isEmpty() || ace.size() > kMaxNameBytes)
        return {};

    QByteArray m;
    m.reserve(18 + ace.size());
    // id 0, flags RD, 1 question
    m.append("\x00\x00\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00", 12);
    for (const QByteArray& label : ace.split('.')) {
        if (label.isEmpty() || label.size() > 63)
            return {};
        m.append(char(label.size()));
        m.append(label);
    }
    m.append('\0');
    m.append(char(type >> 8));
    m.append(char(type & 0xFF));
    m.append("\x00\x01", 2);   // class IN
    return m;
}

QList<QHostAddress> parseAddresses(const QByteArray& m)
{
    QList<QHostAddress> out;
    int rcode = 0;
    const auto records = answerRecords(m, rcode);
    if (!records || rcode != 0)
        return out;   // not a response, or an error rcode
    for (const Record& r : *records) {
        const auto* p = reinterpret_cast<const quint8*>(m.constData() + r.at);
        if (r.cls == 1 && r.type == kTypeA && r.len == 4)
            out << QHostAddress(quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | p[3]);
        else if (r.cls == 1 && r.type == kTypeAAAA && r.len == 16)
            out << QHostAddress(p);
    }
    return out;
}

std::optional<TxtAnswer> parseTxt(const QByteArray& m)
{
    int rcode = 0;
    const auto records = answerRecords(m, rcode);
    if (!records)
        return std::nullopt;
    if (rcode == 3)
        return TxtAnswer{};   // NXDOMAIN: no record
    if (rcode != 0)
        return std::nullopt;
    TxtAnswer out;
    std::optional<quint32> ttl;
    for (const Record& r : *records) {
        if (r.cls != 1 || r.type != kTypeTXT)
            continue;
        QByteArray joined;
        for (qsizetype at = r.at, end = r.at + r.len; at < end;) {
            const auto n = quint8(m.at(at++));
            if (at + n > end)
                return std::nullopt;
            joined += m.mid(at, n);
            at += n;
        }
        out.records << joined;
        ttl = ttl ? std::min(*ttl, r.ttl) : r.ttl;
    }
    out.ttl = ttl.value_or(0);
    return out;
}

} // namespace dns

} // namespace eMule
