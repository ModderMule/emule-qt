#include "pch.h"
/// @file MetaApiClient.cpp
/// @brief Client for an eNode server's Meta API (MetaApi + AccountApi).

#include "enodemeta/MetaApiClient.h"
#include "net/GuardedNetworkAccessManager.h"

#include "enodemeta/GrpcWeb.h"
#include "enodemeta/MetaIdentity.h"
#include "utils/Log.h"
#include "utils/Types.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSslCertificate>
#include <QSslKey>
#include <QtProtobuf/QProtobufSerializer>
#include <QtProtobuf/qprotobufregistration.h>

namespace eMule::enodemeta {

namespace {

constexpr qint64 kCapsTtlMs = 10 * 60 * 1000;
constexpr int kTransferTimeoutMs = 30'000;
constexpr qint64 kSmallReplyMax = 1 << 20;          // caps / auth replies
constexpr qint64 kDefaultMetafileMax = 64LL << 20;  // when caps says nothing
constexpr qint64 kFrameSlack = 64 * 1024;
constexpr qint64 kSearchReplyMax = 8LL << 20;       // one page of rows

constexpr QByteArrayView kMetaApi = "enode.meta.v1.MetaApi";
constexpr QByteArrayView kAccountApi = "enode.meta.v1.AccountApi";

CallResult protocolError(const QString& msg)
{
    CallResult r;
    r.grpcCode = -1;
    r.message = msg;
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// MetaEndpoint
// ---------------------------------------------------------------------------

bool MetaEndpoint::isValid() const
{
    const QUrl url(baseUrl);
    return url.isValid() && !url.host().isEmpty()
        && (url.scheme() == u"https" || url.scheme() == u"http");
}

QString MetaEndpoint::origin() const
{
    const QUrl url(baseUrl);
    const int defPort = url.scheme() == u"https" ? 443 : 80;
    return QStringLiteral("%1://%2:%3").arg(url.scheme(), url.host().toLower()).arg(url.port(defPort));
}

// ---------------------------------------------------------------------------
// MetaApiClient
// ---------------------------------------------------------------------------

MetaApiClient::MetaApiClient(QObject* parent)
    : QObject(parent)
    , m_nam(new GuardedNetworkAccessManager(this))
{
    registerProtobufTypes();
}

MetaApiClient::~MetaApiClient() = default;

void MetaApiClient::getCaps(const MetaEndpoint& ep, Callback<pb::Caps> cb)
{
    const QString origin = ep.origin();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (auto it = m_caps.constFind(origin); it != m_caps.cend() && now - it->fetchedMs < kCapsTtlMs) {
        CallResult ok;
        ok.grpcCode = grpcweb::Ok;
        cb(ok, it->caps);
        return;
    }

    QProtobufSerializer ser;
    const pb::GetCapsRequest req;
    call(ep, kMetaApi, "GetCaps", req.serialize(&ser), {}, kSmallReplyMax,
         [this, origin, cb = std::move(cb)](CallResult r, const QByteArray& payload) {
             pb::Caps caps;
             if (r.ok() && !decode(payload, caps))
                 r = protocolError(QStringLiteral("unreadable Caps reply"));
             if (r.ok())
                 m_caps.insert(origin, {caps, QDateTime::currentMSecsSinceEpoch()});
             cb(r, caps);
         });
}

void MetaApiClient::getMetaFile(const MetaEndpoint& ep, const QByteArray& hash16, const QString& catalogId,
                                const QString& token, quint32 maxBytes, Callback<pb::MetaFile> cb)
{
    QProtobufSerializer ser;
    pb::GetMetaFileRequest req;
    req.setMetaHash(hash16);
    req.setCatalogId(catalogId);

    const qint64 cap = (maxBytes > 0 ? static_cast<qint64>(maxBytes) : kDefaultMetafileMax) + kFrameSlack;
    call(ep, kMetaApi, "GetMetaFile", req.serialize(&ser), token, cap,
         [hash16, cb = std::move(cb)](CallResult r, const QByteArray& payload) {
             pb::MetaFile file;
             if (r.ok() && !decode(payload, file))
                 r = protocolError(QStringLiteral("unreadable MetaFile reply"));
             if (r.ok() && hash16.size() == 16) {
                 // same check the server ran — the row and the bytes crossed two transports
                 const auto v = verifyMetaFile(reinterpret_cast<const uint8*>(hash16.constData()),
                                               file.content());
                 if (!v) {
                     r.grpcCode = grpcweb::DataLoss;
                     r.message = v.error();
                 }
             }
             cb(r, file);
         });
}

void MetaApiClient::search(const MetaEndpoint& ep, const pb::SearchRequest& request, const QString& token,
                           Callback<pb::SearchResponse> cb)
{
    QProtobufSerializer ser;
    call(ep, kMetaApi, "Search", request.serialize(&ser), token, kSearchReplyMax,
         [cb = std::move(cb)](CallResult r, const QByteArray& payload) {
             pb::SearchResponse resp;
             if (r.ok() && !decode(payload, resp))
                 r = protocolError(QStringLiteral("unreadable SearchResponse reply"));
             cb(r, resp);
         });
}

void MetaApiClient::getAuthStatus(const MetaEndpoint& ep, const QString& token, Callback<pb::AuthStatus> cb)
{
    QProtobufSerializer ser;
    const pb::GetAuthStatusRequest req;
    call(ep, kAccountApi, "GetAuthStatus", req.serialize(&ser), token, kSmallReplyMax,
         [cb = std::move(cb)](CallResult r, const QByteArray& payload) {
             pb::AuthStatus st;
             if (r.ok() && !decode(payload, st))
                 r = protocolError(QStringLiteral("unreadable AuthStatus reply"));
             cb(r, st);
         });
}

void MetaApiClient::login(const MetaEndpoint& ep, const QString& username, const QString& password,
                          Callback<pb::LoginResponse> cb)
{
    if (!credentialsAllowed(QUrl(ep.baseUrl))) {
        cb(protocolError(tr("The server offers accounts only over unencrypted HTTP; "
                            "refusing to send a password.")),
           pb::LoginResponse{});
        return;
    }

    QProtobufSerializer ser;
    pb::LoginRequest req;
    req.setUsername(username);
    req.setPassword(password);
    req.setClient(clientName());
    call(ep, kAccountApi, "Login", req.serialize(&ser), {}, kSmallReplyMax,
         [this, origin = ep.origin(), cb = std::move(cb)](CallResult r, const QByteArray& payload) {
             pb::LoginResponse resp;
             if (r.ok() && !decode(payload, resp))
                 r = protocolError(QStringLiteral("unreadable LoginResponse reply"));
             if (r.ok())
                 forgetCaps(origin);
             cb(r, resp);
         });
}

void MetaApiClient::logout(const MetaEndpoint& ep, const QString& token, Callback<pb::LogoutResponse> cb)
{
    QProtobufSerializer ser;
    const pb::LogoutRequest req;
    call(ep, kAccountApi, "Logout", req.serialize(&ser), token, kSmallReplyMax,
         [cb = std::move(cb)](CallResult r, const QByteArray& payload) {
             pb::LogoutResponse resp;
             if (r.ok() && !decode(payload, resp))
                 r = protocolError(QStringLiteral("unreadable LogoutResponse reply"));
             cb(r, resp);
         });
}

void MetaApiClient::forgetCaps(const QString& origin)
{
    m_caps.remove(origin);
}

bool MetaApiClient::credentialsAllowed(const QUrl& baseUrl)
{
    if (baseUrl.scheme() == u"https")
        return true;
    if (baseUrl.host().compare(u"localhost", Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress addr(baseUrl.host());
    return !addr.isNull() && addr.isLoopback();
}

void MetaApiClient::registerProtobufTypes()
{
    // runs the registrars the generated code only queued
    qRegisterProtobufTypes();
}

QString MetaApiClient::clientName()
{
    return QStringLiteral("eMuleQt " EMULE_VERSION_STRING);
}

QString MetaApiClient::spkiPin(const QByteArray& spkiDer)
{
    return QStringLiteral("sha256/")
         + QString::fromLatin1(QCryptographicHash::hash(spkiDer, QCryptographicHash::Sha256).toBase64());
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void MetaApiClient::call(const MetaEndpoint& ep, QByteArrayView service, QByteArrayView method,
                         const QByteArray& request, const QString& token, qint64 maxBytes, RawCallback cb)
{
    if (!ep.isValid()) {
        cb(protocolError(tr("The server announced no usable Meta API address.")), {});
        return;
    }

    QUrl url(ep.baseUrl);
    QString path = url.path();
    if (!path.endsWith(u'/'))
        path += u'/';
    path += QString::fromLatin1(service) + u'/' + QString::fromLatin1(method);
    url.setPath(path);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray(grpcweb::kContentType));
    req.setRawHeader("Accept", grpcweb::kContentType);
    req.setRawHeader("X-Grpc-Web", "1");
    req.setRawHeader("X-User-Agent", "grpc-web-emuleqt/1");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(kTransferTimeoutMs);
    if (!token.isEmpty()) {
        if (!credentialsAllowed(url)) {
            cb(protocolError(tr("Refusing to send an account token over unencrypted HTTP.")), {});
            return;
        }
        req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    }

    QNetworkReply* reply = m_nam->post(req, grpcweb::frame(request));

    // self-signed / bare-IP TLS: accept only the certificate the server pinned
    if (url.scheme() == u"https" && !ep.pin.isEmpty()) {
        const QString pin = ep.pin;
        connect(reply, &QNetworkReply::sslErrors, reply, [reply, pin](const QList<QSslError>& errors) {
            const QSslCertificate cert = reply->sslConfiguration().peerCertificate();
            if (!cert.isNull() && spkiPin(cert.publicKey().toDer()) == pin)
                reply->ignoreSslErrors(errors);
            else
                logWarning(QStringLiteral("Meta API: TLS certificate does not match the pinned key"));
        });
    }

    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 received, qint64) {
        if (received > maxBytes) {
            reply->setProperty("tooLarge", true);
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, cb = std::move(cb)] {
        finish(reply, cb);
        reply->deleteLater();
    });
}

void MetaApiClient::finish(QNetworkReply* reply, const RawCallback& cb)
{
    if (reply->property("tooLarge").toBool()) {
        CallResult r;
        r.grpcCode = grpcweb::ResourceExhausted;
        r.message = tr("The server's reply is larger than allowed.");
        cb(r, {});
        return;
    }

    const QByteArray body = reply->readAll();
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    auto parsed = grpcweb::parseResponse(body, reply->rawHeaderPairs());
    if (!parsed) {
        // no gRPC status at all: surface the transport problem
        QString why = reply->error() != QNetworkReply::NoError ? reply->errorString() : parsed.error();
        if (http != 0 && http != 200)
            why = tr("HTTP %1 from the Meta API (%2)").arg(http).arg(why);
        cb(protocolError(why), {});
        return;
    }

    CallResult r;
    r.grpcCode = parsed->status;
    r.message = parsed->message;
    if (!parsed->statusDetails.isEmpty()) {
        if (const auto raw = grpcweb::statusDetail(parsed->statusDetails, "enode.meta.v1.ErrorInfo")) {
            pb::ErrorInfo info;
            if (decode(*raw, info))
                r.errorInfo = std::move(info);
        }
    }
    if (r.ok() && !parsed->data) {
        cb(protocolError(QStringLiteral("gRPC-Web reply carries no message")), {});
        return;
    }
    if (!r.ok() && r.message.isEmpty())
        r.message = grpcweb::statusName(r.grpcCode);
    cb(r, parsed->data.value_or(QByteArray{}));
}

template <class T>
bool MetaApiClient::decode(const QByteArray& payload, T& out)
{
    QProtobufSerializer ser;
    return out.deserialize(&ser, payload);
}

} // namespace eMule::enodemeta
