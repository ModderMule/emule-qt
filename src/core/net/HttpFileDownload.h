#pragma once

/// @file HttpFileDownload.h
/// @brief One-shot HTTP download of a config file, with transparent archive unwrapping.
///
/// The one HTTP client for list downloads: ipfilter.dat, server.met, nodes.dat,
/// the country database, NZBs. It used to be four hand-rolled QNetworkAccessManager
/// blocks that had drifted apart (only one set a User-Agent, only one a timeout,
/// none capped the size or unpacked a compressed payload), plus GeoIpUpdater's own
/// copy for redirects and conditional requests.
///
/// Redirects are followed by hand, in one place: at most kMaxRedirects hops, never
/// https -> http, and Options::sameHostHeaders stay behind once a hop leaves the
/// first host (credentials must not follow a redirect to a storage bucket).
///
/// Conditional requests: pass the HttpValidators of the copy on disk; a 304 comes
/// back as ok + notModified with no data. Result::validators carries the server's
/// ETag / Last-Modified for the next run.
///
/// Callers get finished, unwrapped bytes and keep their own UI and logging.

#include "archive/ArchiveUnpack.h"
#include "net/HttpValidators.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <utility>

class QNetworkReply;

namespace eMule {

class GuardedNetworkAccessManager;

/// Ceiling on the *downloaded* (still compressed) size; kMaxUnpackedBytes bounds the
/// result of expanding it.
inline constexpr uint64 kMaxDownloadBytes = 64ull * 1024 * 1024;

/// Most hops a download follows before it gives up.
inline constexpr int kMaxRedirects = 5;

using HttpHeaderList = QList<std::pair<QByteArray, QByteArray>>;

class HttpDownloadJob;

class HttpFileDownload {
public:
    struct Options {
        /// Archive members to prefer, most wanted first — e.g. {"ipfilter.dat",
        /// "guarding.p2p"}. Ignored when the payload is not an archive.
        QStringList preferredNames;
        int    timeoutMs = 30000;
        uint64 maxBytes  = kMaxDownloadBytes;
        /// Sent on every hop.
        HttpHeaderList headers;
        /// Sent only while a hop stays on the first URL's host (Authorization).
        HttpHeaderList sameHostHeaders;
        /// Of the copy on disk; empty = unconditional download.
        HttpValidators validators;
        /// Errors say "HTTP <status>", never Qt's errorString(), which quotes the URL
        /// (a presigned URL or an API key must not reach a log line).
        bool redactUrls = false;
    };

    struct Result {
        bool ok = false;
        /// 304: the copy on disk is current; data is empty.
        bool notModified = false;
        /// Status of the last hop; 0 when no response arrived.
        int httpStatus = 0;
        /// Unwrapped body.
        QByteArray data;
        /// Archive member used; empty when the payload was not an archive.
        QString entryName;
        /// Human-readable, set when !ok.
        QString error;
        /// ETag / Last-Modified of the response, for the next conditional request.
        HttpValidators validators;
    };

    using Callback = std::function<void(const Result&)>;

    /// Asynchronous fetch. @p context owns the job: if it is destroyed before the
    /// reply arrives, the callback is never invoked. Otherwise @p done fires exactly
    /// once, also after HttpDownloadJob::abort(). The job deletes itself afterwards.
    static HttpDownloadJob* fetch(QObject* context, const QUrl& url, const Options& opts,
                                  Callback done);

    /// Blocking fetch, for startup paths that must finish before the caller continues.
    /// Spins a local event loop, so it must not be called from a slot that is itself
    /// reentrant with the network stack.
    [[nodiscard]] static Result fetchBlocking(const QUrl& url, const Options& opts);

private:
    HttpFileDownload() = delete;
};

/// One running download; see HttpFileDownload::fetch().
class HttpDownloadJob : public QObject {
public:
    HttpDownloadJob(QObject* context, const QUrl& url, HttpFileDownload::Options opts,
                    HttpFileDownload::Callback done);
    ~HttpDownloadJob() override;

    /// Stop now; the callback still fires once, with @p reason as the error.
    void abort(const QString& reason = QStringLiteral("canceled"));

    [[nodiscard]] bool isFinished() const { return m_finished; }

private:
    void startHop(const QUrl& url);
    void onReplyFinished();
    void complete(HttpFileDownload::Result result);

    HttpFileDownload::Options m_opts;
    HttpFileDownload::Callback m_done;
    QUrl m_firstUrl;
    GuardedNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    int m_redirects = 0;
    bool m_hopConditional = false;   ///< the current hop sent If-None-Match / If-Modified-Since
    bool m_finished = false;
};

} // namespace eMule
