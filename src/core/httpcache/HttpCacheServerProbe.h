#pragma once

/// @file HttpCacheServerProbe.h
/// @brief The `GET /v1/info` handshake that proves an endpoint is a cache.
///
/// Step 2 of consuming an `ed2k://|httpcache|` configuration link
/// (docs/protocol/http-cache-spec.md §8.1): before a base URL and its credential
/// are stored, the endpoint has to identify itself. That handshake is the whole
/// reason a hostile link cannot be used to point this client at an arbitrary host.

#include <QString>

#include <functional>

class QObject;

namespace eMule {

/// What `/v1/info` said, or why it could not be believed.
struct HttpCacheServerInfo {
    bool ok = false;
    QString error;                  ///< human-readable; never carries a credential
    QString service;                ///< must be "emule-http-cache"
    int version = 0;
    QString implementation;         ///< free-form, e.g. "php" — display only
    bool uploadRequiresAuth = true;
    quint64 maxChunkSize = 0;
    int httpStatus = 0;
};

namespace HttpCacheServerProbe {

/// The only protocol version this client understands.
inline constexpr int kSupportedVersion = 1;

/// `GET <baseUrl>/v1/info`, then call @p done exactly once.
///
/// **No credential is sent.** `/v1/info` takes no auth, and at this point the URL
/// is whatever a link claimed — putting the API key on that first request would
/// hand it to the very host the handshake exists to vet.
///
/// @p context scopes the callback: when it dies first, @p done is never called.
void probe(const QString& baseUrl, QObject* context,
           std::function<void(const HttpCacheServerInfo&)> done);

} // namespace HttpCacheServerProbe

} // namespace eMule
