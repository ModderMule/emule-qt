#include "pch.h"
/// @file HttpCacheServerProbe.cpp
/// @brief `/v1/info` handshake — implementation.

#include "httpcache/HttpCacheServerProbe.h"

#include "net/HttpDefaults.h"
#include "utils/Log.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <memory>

namespace eMule {

namespace {

/// Inactivity bound. Generous, because this runs once when a user pastes a link
/// and a slow shared host is not a reason to call it "not a cache".
constexpr int kProbeTimeoutMs = 15'000;

/// `/v1/info` is a few hundred bytes. Anything past this is not the answer we
/// asked for, and the URL is untrusted — so stop reading rather than buffer it.
constexpr qint64 kMaxInfoBytes = 64 * 1024;

/// The endpoint, or an invalid QUrl. Mirrors Preferences::setHttpCacheBaseUrl(),
/// which strips trailing slashes so a request path never doubles one.
QUrl infoUrl(const QString& baseUrl)
{
    QString clean = baseUrl.trimmed();
    while (clean.endsWith(QLatin1Char('/')))
        clean.chop(1);

    const QUrl url(clean + QStringLiteral("/v1/info"), QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty()
        || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")))
        return {};

    return url;
}

/// Turn a finished reply into a verdict.
///
/// @p tooBig tells the two ways an abort can happen apart: Qt reports the read
/// cap and the transfer timeout with the same error code, and "the server sent
/// too much" is a different thing to tell somebody than "it never answered".
HttpCacheServerInfo evaluate(QNetworkReply* reply, bool tooBig)
{
    HttpCacheServerInfo info;
    info.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (tooBig) {
        info.error = QObject::tr("%1 sent more than an /v1/info answer can be — "
                                 "this is not an HTTP Cache server.")
                         .arg(reply->url().host());
        return info;
    }
    if (info.httpStatus == 0) {
        info.error = QObject::tr("Cannot reach %1: %2")
                         .arg(reply->url().host(), reply->errorString());
        return info;
    }
    if (info.httpStatus != 200) {
        info.error = QObject::tr("%1 answered HTTP %2 — this is not an HTTP Cache server.")
                         .arg(reply->url().host()).arg(info.httpStatus);
        return info;
    }

    // An HTML page is the usual answer from a host that is a web server and
    // nothing more, so it deserves the same plain verdict as a 404 rather than a
    // JSON parse error nobody can act on.
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        info.error = QObject::tr("%1 did not answer with an /v1/info document — "
                                 "this is not an HTTP Cache server.")
                         .arg(reply->url().host());
        return info;
    }

    const QJsonObject obj = doc.object();
    info.service            = obj.value(QStringLiteral("service")).toString();
    info.version            = obj.value(QStringLiteral("version")).toInt();
    info.implementation     = obj.value(QStringLiteral("implementation")).toString();
    info.uploadRequiresAuth = obj.value(QStringLiteral("uploadRequiresAuth")).toBool(true);
    info.maxChunkSize =
        static_cast<quint64>(obj.value(QStringLiteral("maxChunkSize")).toDouble(0));

    // The handshake proper. Everything above this line was about reaching the
    // host; this is the part that says the host is what the link claimed.
    if (info.service != QStringLiteral("emule-http-cache")) {
        info.error = QObject::tr("%1 identifies itself as \"%2\", not as an HTTP Cache server.")
                         .arg(reply->url().host(),
                              info.service.isEmpty() ? QObject::tr("(nothing)")
                                                     : info.service.left(64));
        return info;
    }
    if (info.version != HttpCacheServerProbe::kSupportedVersion) {
        info.error = QObject::tr("%1 speaks HTTP Cache version %2; this client understands %3.")
                         .arg(reply->url().host())
                         .arg(info.version)
                         .arg(HttpCacheServerProbe::kSupportedVersion);
        return info;
    }

    info.ok = true;
    return info;
}

} // namespace

namespace HttpCacheServerProbe {

void probe(const QString& baseUrl, QObject* context,
           std::function<void(const HttpCacheServerInfo&)> done)
{
    const QUrl url = infoUrl(baseUrl);
    if (!url.isValid()) {
        HttpCacheServerInfo info;
        info.error = QObject::tr("\"%1\" is not a usable HTTP Cache address.")
                         .arg(baseUrl.left(120));
        if (done)
            done(info);
        return;
    }

    QNetworkRequest req = Http::makeRequest(url);
    req.setTransferTimeout(kProbeTimeoutMs);
    // Deliberately no Authorization header: /v1/info needs none, and at this
    // point the host is only what a link claimed it was.

    // Owns itself. The reply's finished() always arrives — abort() included — so
    // the cleanup is safe to hang off it even when the caller is long gone.
    auto* nam = new QNetworkAccessManager();
    QNetworkReply* reply = nam->get(req);

    auto tooBig = std::make_shared<bool>(false);
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, tooBig]() {
        if (reply->bytesAvailable() > kMaxInfoBytes) {
            *tooBig = true;
            reply->abort();
        }
    });

    const QPointer<QObject> guard(context);
    const bool guarded = context != nullptr;

    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, nam, guard, guarded, tooBig, done = std::move(done)]() {
        const HttpCacheServerInfo info = evaluate(reply, *tooBig);
        reply->deleteLater();
        nam->deleteLater();

        if (!info.ok)
            logDebug(QStringLiteral("HTTP Cache: probe failed — %1").arg(info.error));

        if (done && (!guarded || guard))
            done(info);
    });
}

} // namespace HttpCacheServerProbe

} // namespace eMule
