#pragma once

/// @file MetaApiClient.h
/// @brief Client for an eNode server's Meta API (MetaApi + AccountApi).
///
/// Messages are generated from external/enodemeta/proto (api.proto,
/// meta.proto); calls go out as unary gRPC-Web POSTs (see GrpcWeb.h).
/// The base URL comes from OP_SERVERIDENT tag ST_META_API.
///
/// Implemented RPCs: MetaApi.GetCaps, MetaApi.GetMetaFile,
/// AccountApi.GetAuthStatus, AccountApi.Login, AccountApi.Logout.
/// MetaApi.Search is reserved server-side (phase 7).

#include "api.qpb.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace eMule::enodemeta {

namespace pb = enode::meta::v1;

/// Where a server's Meta API lives.
struct MetaEndpoint {
    QString baseUrl;      ///< ST_META_API, http(s)://host:port[/prefix]
    QString pin;          ///< ST_META_API_FP "sha256/<base64>", may be empty
    QString serverName;   ///< for display

    [[nodiscard]] bool isValid() const;
    /// scheme://host:port — the key tokens and caps are stored under.
    [[nodiscard]] QString origin() const;
};

/// Outcome of one call.
struct CallResult {
    int grpcCode = -1;                    ///< -1 = transport or protocol failure
    QString message;                      ///< human-readable reason when not ok
    std::optional<pb::ErrorInfo> errorInfo;

    [[nodiscard]] bool ok() const { return grpcCode == 0; }
};

class MetaApiClient : public QObject {
    Q_OBJECT

public:
    template <class T>
    using Callback = std::function<void(const CallResult&, const T&)>;

    explicit MetaApiClient(QObject* parent = nullptr);
    ~MetaApiClient() override;

    /// Caps, cached per origin for 10 minutes. Never needs auth.
    void getCaps(const MetaEndpoint& ep, Callback<pb::Caps> cb);

    /// The .torrent/.nzb behind a row, verified against @p hash16 before the
    /// callback sees it (a failed check is reported as DataLoss).
    void getMetaFile(const MetaEndpoint& ep, const QByteArray& hash16, const QString& catalogId,
                     const QString& token, quint32 maxBytes, Callback<pb::MetaFile> cb);

    void getAuthStatus(const MetaEndpoint& ep, const QString& token, Callback<pb::AuthStatus> cb);
    void login(const MetaEndpoint& ep, const QString& username, const QString& password,
               Callback<pb::LoginResponse> cb);
    void logout(const MetaEndpoint& ep, const QString& token, Callback<pb::LogoutResponse> cb);

    /// Drop cached caps (e.g. after a login changed what the server reports).
    void forgetCaps(const QString& origin);

    /// Credentials only travel over https, or plain http to a loopback host.
    [[nodiscard]] static bool credentialsAllowed(const QUrl& baseUrl);

    /// LoginRequest.client, e.g. "eMuleQt 0.5.3".
    [[nodiscard]] static QString clientName();

    /// "sha256/<base64>" of a DER SubjectPublicKeyInfo.
    [[nodiscard]] static QString spkiPin(const QByteArray& spkiDer);

private:
    using RawCallback = std::function<void(CallResult, QByteArray payload)>;

    void call(const MetaEndpoint& ep, QByteArrayView service, QByteArrayView method,
              const QByteArray& request, const QString& token, qint64 maxBytes, RawCallback cb);
    void finish(QNetworkReply* reply, const RawCallback& cb);

    template <class T>
    static bool decode(const QByteArray& payload, T& out);

    struct CachedCaps {
        pb::Caps caps;
        qint64 fetchedMs = 0;
    };

    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, CachedCaps> m_caps;
};

} // namespace eMule::enodemeta
