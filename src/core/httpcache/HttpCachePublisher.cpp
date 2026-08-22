#include "pch.h"
/// @file HttpCachePublisher.cpp
/// @brief Uploads one encrypted part to the cache server — implementation.

#include "httpcache/HttpCachePublisher.h"

#include "crypto/AesCbc.h"
#include "net/HttpDefaults.h"
#include "utils/Log.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace eMule {

namespace {

/// Slice size for the read-and-encrypt pass. Big enough that the syscall cost
/// disappears, small enough that it never shows up as a latency spike.
constexpr qint64 kReadSlice = 256 * 1024;

/// How often the throttle re-arms. 10 Hz keeps the burst small without making
/// the timer itself a cost.
constexpr int kThrottleIntervalMs = 100;

/// Give up on a publish that has produced nothing for this long.
constexpr int kPublishTimeoutMs = 120'000;

/// Seconds from a Retry-After header.
///
/// Only the delta-seconds form is honoured. The HTTP-date form needs the two
/// machines to agree on the wall clock, and a cache server that wants to be
/// obeyed sends the delta.
int parseRetryAfter(const QByteArray& value)
{
    bool ok = false;
    const int seconds = value.trimmed().toInt(&ok);

    // A day is already far longer than any chunk's TTL, so anything beyond it is
    // indistinguishable from "never" and not worth honouring literally.
    if (!ok || seconds <= 0 || seconds > 86'400)
        return 0;

    return seconds;
}

/// The server's own explanation, which is worth far more than the status alone.
///
/// Falls back to the raw body when it is not the documented {"error": ...} shape,
/// because a 500 is exactly the case where the body may be a stack trace, a proxy
/// error page, or nothing at all.
QString describeErrorBody(const QByteArray& body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QString message = doc.object().value(QStringLiteral("error")).toString();
        if (!message.isEmpty())
            return message;
    }

    const QString raw = QString::fromUtf8(body.left(200)).simplified();
    return raw.isEmpty() ? QStringLiteral("(no body)") : raw;
}

QNetworkRequest makeRequest(const QUrl& url, const QString& apiKey)
{
    QNetworkRequest req = Http::makeRequest(url);
    req.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    // The throttle deliberately makes the upload slow, so this has to be a stall
    // detector rather than a deadline — setTransferTimeout measures inactivity.
    req.setTransferTimeout(kPublishTimeoutMs);
    return req;
}

} // namespace

// ---------------------------------------------------------------------------
// ThrottledUploadDevice
// ---------------------------------------------------------------------------

ThrottledUploadDevice::ThrottledUploadDevice(QByteArray payload, uint64 bytesPerSecond,
                                            QObject* parent)
    : QIODevice(parent)
    , m_payload(std::move(payload))
    , m_bytesPerSecond(bytesPerSecond)
{
    open(QIODevice::ReadOnly);

    if (m_bytesPerSecond == 0)
        return;

    m_allowance = static_cast<qint64>(m_bytesPerSecond) * kThrottleIntervalMs / 1000;

    m_timer = new QTimer(this);
    m_timer->setInterval(kThrottleIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &ThrottledUploadDevice::refill);
    m_timer->start();
}

qint64 ThrottledUploadDevice::bytesAvailable() const
{
    const qint64 left = m_payload.size() - m_pos;
    if (m_bytesPerSecond == 0)
        return left + QIODevice::bytesAvailable();

    return std::min(left, m_allowance) + QIODevice::bytesAvailable();
}

qint64 ThrottledUploadDevice::readData(char* data, qint64 maxSize)
{
    const qint64 left = m_payload.size() - m_pos;
    if (left <= 0)
        return 0;

    qint64 n = std::min(maxSize, left);

    if (m_bytesPerSecond != 0) {
        n = std::min(n, m_allowance);
        if (n <= 0)
            return 0; // out of allowance; refill() will wake the reader
        m_allowance -= n;
    }

    std::memcpy(data, m_payload.constData() + m_pos, static_cast<size_t>(n));
    m_pos += n;

    return n;
}

void ThrottledUploadDevice::refill()
{
    if (m_pos >= m_payload.size()) {
        m_timer->stop();
        return;
    }

    // Hand out one interval's worth, and do not let unused allowance accumulate
    // into a burst that defeats the point of the cap.
    m_allowance = static_cast<qint64>(m_bytesPerSecond) * kThrottleIntervalMs / 1000;
    emit readyRead();
}

// ---------------------------------------------------------------------------
// HttpCachePublisher
// ---------------------------------------------------------------------------

HttpCachePublisher::HttpCachePublisher(QObject* parent)
    : QObject(parent)
{
}

HttpCachePublisher::~HttpCachePublisher() = default;

void HttpCachePublisher::start(const Request& request)
{
    m_request = request;

    if (m_request.baseUrl.isEmpty() || m_request.apiKey.isEmpty()) {
        fail(QStringLiteral("no cache server configured"));
        return;
    }

    if (m_request.partLength == 0 || m_request.partLength > kHttpCachePlainMax) {
        fail(QStringLiteral("part length %1 out of range").arg(m_request.partLength));
        return;
    }

    const QUrl url(m_request.baseUrl + QStringLiteral("/v1/chunks"));
    if (!url.isValid() || url.host().isEmpty()) {
        fail(QStringLiteral("bad cache base url '%1'").arg(m_request.baseUrl));
        return;
    }

    // A fresh key and IV for every chunk. Reusing either across chunks would let
    // the server correlate two uploads of the same part, which is exactly what
    // this design is meant to prevent.
    const QByteArray key = aesRandomKey();
    const QByteArray iv = aesRandomIv();
    if (key.isEmpty() || iv.isEmpty()) {
        fail(QStringLiteral("RNG failure"));
        return;
    }

    QString readError;
    const QByteArray cipher = readAndEncrypt(m_request, key, iv, &readError);
    if (cipher.isEmpty()) {
        fail(readError);
        return;
    }

    m_offer.fileHash = m_request.fileHash;
    m_offer.partIndex = m_request.partIndex;
    m_offer.plainLength = m_request.partLength;
    m_offer.cipherLength = static_cast<uint64>(cipher.size());
    m_offer.key = key;
    m_offer.iv = iv;
    m_offer.cipherSha256 = QCryptographicHash::hash(cipher, QCryptographicHash::Sha256);

    QNetworkRequest req = makeRequest(url, m_request.apiKey);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/octet-stream"));
    req.setHeader(QNetworkRequest::ContentLengthHeader, cipher.size());
    req.setRawHeader("X-Chunk-TTL", QByteArray::number(m_request.ttlSeconds));

    m_nam = new QNetworkAccessManager(this);

    auto* body = new ThrottledUploadDevice(cipher, m_request.rateBytesPerSecond, this);
    m_reply = m_nam->post(req, body);
    body->setParent(m_reply);

    connect(m_reply, &QNetworkReply::finished, this, &HttpCachePublisher::onReplyFinished);
}

void HttpCachePublisher::deleteChunk(const QString& url, const QString& apiKey)
{
    // No key, no delete. An anonymous upload is owned by the server's reserved
    // `anonymous` id that no client can authenticate as, so a keyless DELETE can
    // only ever come back 401 or 404 — and the chunk stays up until its TTL
    // either way. Leaving early beats a request whose failure means nothing.
    if (url.isEmpty() || apiKey.isEmpty())
        return;

    const QUrl target(url);
    if (!target.isValid() || target.host().isEmpty())
        return;

    // Owns itself: nothing depends on the outcome, so the reply just tidies up.
    auto* nam = new QNetworkAccessManager();
    QNetworkReply* reply = nam->deleteResource(makeRequest(target, apiKey));

    QObject::connect(reply, &QNetworkReply::finished, reply, [nam, reply]() {
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // 204 is the only success. 404 means somebody got there first, or the TTL
        // already lapsed, which is the outcome we asked for either way. A 500 says
        // the server could not remove the blob — it stays downloadable until its
        // TTL runs out, so this is worth seeing rather than swallowing.
        if (status == 204 || status == 404) {
            logDebug(QStringLiteral("HTTP Cache: DELETE %1 -> %2")
                         .arg(reply->url().toString())
                         .arg(status));
        } else {
            logWarning(QStringLiteral("HTTP Cache: DELETE %1 failed with %2 — the chunk may "
                                      "stay available until it expires")
                           .arg(reply->url().toString())
                           .arg(status));
        }

        reply->deleteLater();
        nam->deleteLater();
    });
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void HttpCachePublisher::fail(const QString& reason, HttpCachePublishStage stage, int httpStatus,
                              int retryAfterSeconds)
{
    HttpCachePublishResult result;
    result.ok = false;
    result.error = reason;
    result.stage = stage;
    result.httpStatus = httpStatus;
    result.retryAfterSeconds = retryAfterSeconds;

    emit finished(result, HttpCacheOffer{});
    deleteLater();
}

void HttpCachePublisher::onReplyFinished()
{
    if (!m_reply) {
        fail(QStringLiteral("reply vanished"));
        return;
    }

    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = m_reply->readAll();
    const QNetworkReply::NetworkError netError = m_reply->error();
    const int retryAfter = parseRetryAfter(m_reply->rawHeader("Retry-After"));

    m_reply->deleteLater();
    m_reply = nullptr;

    // No status at all means the exchange never got as far as a response line:
    // unreachable host, TLS failure, or the stall detector firing mid-body.
    if (status == 0) {
        fail(QStringLiteral("transport error: %1").arg(static_cast<int>(netError)),
             HttpCachePublishStage::Transport);
        return;
    }

    if (status != 201 && status != 200) {
        // A 5xx in particular is worth reporting verbatim: the chunk may well be
        // sitting on the server's disk, holding storage and quota, with its id in
        // a response we never received. Only its TTL will reclaim it.
        fail(QStringLiteral("server returned %1: %2").arg(status).arg(describeErrorBody(body)),
             HttpCachePublishStage::Server, status, retryAfter);
        return;
    }

    // Everything below here is a 2xx that we still cannot use. Those are server
    // faults too, not local ones — the next chunk would hit exactly the same
    // response — so they are staged accordingly and back the whole server off.
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        fail(QStringLiteral("server response is not JSON"), HttpCachePublishStage::Server, status);
        return;
    }

    const QJsonObject obj = doc.object();

    HttpCachePublishResult result;
    result.url = obj.value(QStringLiteral("url")).toString();
    result.chunkId = obj.value(QStringLiteral("id")).toString();
    result.cipherLength = static_cast<uint64>(obj.value(QStringLiteral("size")).toDouble());
    result.expiresAt = static_cast<uint32>(obj.value(QStringLiteral("expires")).toDouble());

    // Always use the server's url verbatim — a backend is free to serve blobs
    // from a different host or a signed CDN link, so reconstructing it from the
    // id would work against PHP and break against everything else.
    if (result.url.isEmpty()) {
        fail(QStringLiteral("server response has no url"), HttpCachePublishStage::Server, status);
        return;
    }

    if (result.cipherLength != 0 && result.cipherLength != m_offer.cipherLength) {
        fail(QStringLiteral("server stored %1 bytes, we sent %2")
                 .arg(result.cipherLength)
                 .arg(m_offer.cipherLength),
             HttpCachePublishStage::Server, status);
        return;
    }

    m_offer.url = result.url;
    m_offer.expiresAt = result.expiresAt;

    if (!m_offer.isWellFormed()) {
        fail(QStringLiteral("published chunk yields a malformed offer: %1")
                 .arg(m_offer.malformedReason()),
             HttpCachePublishStage::Server, status);
        return;
    }

    result.ok = true;
    emit finished(result, m_offer);
    deleteLater();
}

QByteArray HttpCachePublisher::readAndEncrypt(const Request& request, const QByteArray& key,
                                              const QByteArray& iv, QString* error)
{
    const auto bail = [error](const QString& why) {
        if (error)
            *error = why;
        return QByteArray{};
    };

    QFile file(request.dataFilePath);
    if (!file.open(QIODevice::ReadOnly))
        return bail(QStringLiteral("cannot open %1").arg(request.dataFilePath));

    if (!file.seek(static_cast<qint64>(request.partOffset)))
        return bail(QStringLiteral("cannot seek to %1").arg(request.partOffset));

    AesCbcEncryptor enc;
    if (!enc.begin(key, iv))
        return bail(QStringLiteral("cipher init failed"));

    QByteArray cipher;
    cipher.reserve(
        static_cast<qsizetype>(AesCbcEncryptor::cipherLengthFor(request.partLength)));

    uint64 remaining = request.partLength;
    while (remaining > 0) {
        const qint64 want = std::min<qint64>(kReadSlice, static_cast<qint64>(remaining));
        const QByteArray slice = file.read(want);

        // A short read means the part is not actually on disk — publishing what we
        // did get would hand every peer a chunk that fails its MD4 check.
        if (slice.size() != want)
            return bail(QStringLiteral("short read at offset %1 (%2 of %3)")
                            .arg(request.partOffset + request.partLength - remaining)
                            .arg(slice.size())
                            .arg(want));

        cipher.append(enc.update(slice));
        remaining -= static_cast<uint64>(want);
    }

    cipher.append(enc.finish());

    if (static_cast<uint64>(cipher.size())
        != AesCbcEncryptor::cipherLengthFor(request.partLength))
        return bail(QStringLiteral("ciphertext length %1 unexpected").arg(cipher.size()));

    return cipher;
}

} // namespace eMule
