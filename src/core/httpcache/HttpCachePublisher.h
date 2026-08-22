#pragma once

/// @file HttpCachePublisher.h
/// @brief Uploads one encrypted part to the cache server.
///
/// One instance per in-flight publish. The manager owns the policy (which part,
/// when, how many at once); this owns the mechanics: read the part off disk,
/// encrypt it under a freshly generated key, POST it, and report back what the
/// server said.
///
/// The whole point of the feature is to *save* upstream bandwidth, so the POST
/// is rate limited. An unthrottled 9.28 MB burst would stall exactly the ed2k
/// uploads it exists to relieve.

#include "httpcache/HttpCacheOffer.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <array>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace eMule {

class KnownFile;

/// A rate-limited, sequential read source over an in-memory buffer.
///
/// QNetworkAccessManager pulls from this as fast as the socket allows, so the
/// cap has to live at the source: readData() hands out at most one tick's worth
/// of bytes and returns 0 otherwise, and a timer re-arms the allowance and
/// signals readyRead.
class ThrottledUploadDevice : public QIODevice {
    Q_OBJECT

public:
    /// @param bytesPerSecond 0 means unthrottled.
    ThrottledUploadDevice(QByteArray payload, uint64 bytesPerSecond, QObject* parent = nullptr);

    [[nodiscard]] bool isSequential() const override { return false; }
    [[nodiscard]] qint64 size() const override { return m_payload.size(); }
    [[nodiscard]] qint64 bytesAvailable() const override;

protected:
    qint64 readData(char* data, qint64 maxSize) override;
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    void refill();

    QByteArray m_payload;
    qint64 m_pos = 0;
    uint64 m_bytesPerSecond = 0;
    qint64 m_allowance = 0;
    class QTimer* m_timer = nullptr;
};

/// How far a publish attempt got before it failed.
///
/// The manager scopes its backoff by this. A server that answered 500 will very
/// likely answer 500 for the next chunk too, so nothing should be published
/// until it has had time to recover; an unreadable part file says nothing about
/// the server at all and must not stop the other candidates.
enum class HttpCachePublishStage : uint8 {
    None,        ///< it did not fail
    Local,       ///< never left the machine: bad config, RNG failure, unreadable part
    Transport,   ///< the request went out and the exchange broke
    Server,      ///< the server answered, with a status we cannot use
};

/// Result of one publish attempt.
struct HttpCachePublishResult {
    bool ok = false;
    QString url;          ///< absolute, straight from the server
    QString chunkId;      ///< opaque server id, needed for a later DELETE
    uint64 cipherLength = 0;
    uint32 expiresAt = 0;
    QString error;        ///< human-readable, for the log

    HttpCachePublishStage stage = HttpCachePublishStage::None;
    int httpStatus = 0;          ///< 0 when no response ever arrived
    int retryAfterSeconds = 0;   ///< from Retry-After; 0 when absent or unusable
};

/// One publish job. Self-deleting: it emits finished() and then deleteLater()s.
class HttpCachePublisher : public QObject {
    Q_OBJECT

public:
    struct Request {
        QString baseUrl;
        QString apiKey;
        uint32 ttlSeconds = 21600;
        uint64 rateBytesPerSecond = 0;   ///< 0 = unthrottled

        std::array<uint8, 16> fileHash{};
        uint32 partIndex = 0;
        QString dataFilePath;            ///< KnownFile::dataFilePath()
        uint64 partOffset = 0;
        uint64 partLength = 0;
    };

    explicit HttpCachePublisher(QObject* parent = nullptr);
    ~HttpCachePublisher() override;

    /// Read, encrypt and POST. The generated key/IV/digest come back through
    /// finished() so the caller never has to re-derive them.
    void start(const Request& request);

    /// Fire-and-forget DELETE of a previously published chunk.
    ///
    /// Never called automatically on a failed download — a failure says as much
    /// about the downloader or the network as about the blob, and the chunk may
    /// still be serving other peers. This exists for explicit cleanup.
    ///
    /// Needs the API key the chunk was uploaded under, and there is no working
    /// around that: DELETE is owner-only, and a chunk published without a key
    /// belongs to the server's reserved `anonymous` id, which nobody can ever
    /// authenticate as. Those blobs only lapse at their TTL. So an empty @p apiKey
    /// returns without putting anything on the wire — that is the right answer,
    /// not a missing case.
    static void deleteChunk(const QString& url, const QString& apiKey);

signals:
    /// @param offer  fully populated and ready to send, when result.ok
    void finished(const HttpCachePublishResult& result, const HttpCacheOffer& offer);

private:
    // Private helpers live after the public interface, per the house style.
    void fail(const QString& reason, HttpCachePublishStage stage = HttpCachePublishStage::Local,
              int httpStatus = 0, int retryAfterSeconds = 0);
    void onReplyFinished();

    /// Read the part and encrypt it. Empty on any failure.
    [[nodiscard]] static QByteArray readAndEncrypt(const Request& request, const QByteArray& key,
                                                   const QByteArray& iv, QString* error);

    Request m_request;
    HttpCacheOffer m_offer;
    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_reply = nullptr;
};

} // namespace eMule

Q_DECLARE_METATYPE(eMule::HttpCachePublishResult)
