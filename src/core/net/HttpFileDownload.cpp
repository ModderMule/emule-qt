#include "pch.h"
/// @file HttpFileDownload.cpp
/// @brief Shared config-file downloader: manual redirects, conditional requests,
/// transparent archive unwrapping.

#include "net/HttpFileDownload.h"
#include "net/GuardedNetworkAccessManager.h"
#include "net/HttpDefaults.h"
#include "utils/Log.h"

#include <QEventLoop>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace eMule {

namespace {

[[nodiscard]] bool isRedirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

[[nodiscard]] bool isConditionalHeader(const QByteArray& name)
{
    return name.compare("If-None-Match", Qt::CaseInsensitive) == 0
           || name.compare("If-Modified-Since", Qt::CaseInsensitive) == 0;
}

[[nodiscard]] bool isHttpScheme(const QUrl& url)
{
    return url.scheme() == u"http" || url.scheme() == u"https";
}

/// Error text for a failed reply; never quotes the URL when @p redact is set.
[[nodiscard]] QString replyError(QNetworkReply* reply, int status, bool redact)
{
    if (redact)
        return status > 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
    if (reply->error() == QNetworkReply::NoError)
        return QStringLiteral("HTTP %1").arg(status);
    return reply->errorString();
}

} // namespace

// ---------------------------------------------------------------------------
// HttpFileDownload
// ---------------------------------------------------------------------------

HttpDownloadJob* HttpFileDownload::fetch(QObject* context, const QUrl& url, const Options& opts,
                                         Callback done)
{
    return new HttpDownloadJob(context, url, opts, std::move(done));
}

HttpFileDownload::Result HttpFileDownload::fetchBlocking(const QUrl& url, const Options& opts)
{
    QObject context;
    QEventLoop loop;
    Result out;
    bool done = false;

    const QPointer<HttpDownloadJob> job = fetch(&context, url, opts,
        [&](const Result& r) {
            out = r;
            done = true;
            loop.quit();
        });

    // The transfer timeout covers a stalled hop; this is the backstop for a download
    // that never finishes at all, so a startup path cannot hang forever.
    if (!done) {
        QTimer::singleShot(opts.timeoutMs, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (!done && job)
        job->abort(QStringLiteral("timed out after %1 ms").arg(opts.timeoutMs));
    return out;
}

// ---------------------------------------------------------------------------
// HttpDownloadJob
// ---------------------------------------------------------------------------

HttpDownloadJob::HttpDownloadJob(QObject* context, const QUrl& url,
                                 HttpFileDownload::Options opts, HttpFileDownload::Callback done)
    : QObject(context)
    , m_opts(std::move(opts))
    , m_done(std::move(done))
    , m_firstUrl(url)
{
    if (!url.isValid() || !isHttpScheme(url)) {
        // Deferred, so the caller holds the job before its callback runs
        QMetaObject::invokeMethod(this, [this, url] {
            HttpFileDownload::Result r;
            r.error = QStringLiteral("invalid URL: %1")
                          .arg(m_opts.redactUrls ? url.host() : url.toDisplayString());
            complete(std::move(r));
        }, Qt::QueuedConnection);
        return;
    }
    // Parented to the job, and through it to the context: a dialog closing mid-flight
    // tears the request down and the callback never fires against a dead caller.
    m_nam = new GuardedNetworkAccessManager(this);
    startHop(url);
}

HttpDownloadJob::~HttpDownloadJob()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
}

void HttpDownloadJob::abort(const QString& reason)
{
    if (m_finished)
        return;
    if (m_reply) {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    HttpFileDownload::Result r;
    r.error = reason;
    complete(std::move(r));
}

void HttpDownloadJob::startHop(const QUrl& url)
{
    // The shared defaults carry the User-Agent; redirects are ours, so the
    // same-host headers can stay behind when a hop leaves the first host.
    QNetworkRequest request = Http::makeRequest(url);
    request.setTransferTimeout(m_opts.timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QVariant::fromValue(QNetworkRequest::ManualRedirectPolicy));
    m_hopConditional = !m_opts.validators.isEmpty();
    const auto add = [&](const QByteArray& name, const QByteArray& value) {
        request.setRawHeader(name, value);
        m_hopConditional = m_hopConditional || isConditionalHeader(name);
    };
    for (const auto& [name, value] : m_opts.headers)
        add(name, value);
    if (url.host() == m_firstUrl.host())
        for (const auto& [name, value] : m_opts.sameHostHeaders)
            add(name, value);
    if (!m_opts.validators.etag.isEmpty())
        request.setRawHeader("If-None-Match", m_opts.validators.etag);
    if (!m_opts.validators.lastModified.isEmpty())
        request.setRawHeader("If-Modified-Since", m_opts.validators.lastModified);

    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &HttpDownloadJob::onReplyFinished);
}

void HttpDownloadJob::onReplyFinished()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    HttpFileDownload::Result r;
    r.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (isRedirect(r.httpStatus)) {
        const QUrl target = reply->url().resolved(
            reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
        const bool downgrade = target.scheme() == u"http"
                               && (reply->url().scheme() == u"https"
                                   || m_firstUrl.scheme() == u"https");
        if (++m_redirects > kMaxRedirects) {
            r.error = QStringLiteral("too many redirects");
        } else if (!target.isValid() || !isHttpScheme(target) || downgrade) {
            r.error = QStringLiteral("refused redirect (HTTP %1)").arg(r.httpStatus);
        } else {
            startHop(target);
            return;
        }
        complete(std::move(r));
        return;
    }

    r.validators = {reply->rawHeader("ETag"), reply->rawHeader("Last-Modified")};

    // Only a conditional request can be answered "not modified"
    if (r.httpStatus == 304 && m_hopConditional) {
        r.ok = true;
        r.notModified = true;
        complete(std::move(r));
        return;
    }

    if (reply->error() != QNetworkReply::NoError || r.httpStatus < 200 || r.httpStatus > 299) {
        r.error = replyError(reply, r.httpStatus, m_opts.redactUrls);
        complete(std::move(r));
        return;
    }

    const QByteArray body = reply->readAll();
    if (static_cast<uint64>(body.size()) > m_opts.maxBytes) {
        r.error = QStringLiteral("download exceeds %1 MB limit")
                      .arg(m_opts.maxBytes / (1024 * 1024));
        complete(std::move(r));
        return;
    }

    const UnwrapResult unwrapped = unwrapDownload(body, m_opts.preferredNames);
    if (!unwrapped.error.isEmpty()) {
        r.error = unwrapped.error;
        complete(std::move(r));
        return;
    }

    r.ok = true;
    r.data = unwrapped.data;
    r.entryName = unwrapped.entryName;
    if (unwrapped.wasArchive) {
        logInfo(QStringLiteral("Unpacked \"%1\" (%2 bytes) from the downloaded archive")
                    .arg(r.entryName.isEmpty() ? QStringLiteral("<unnamed>") : r.entryName)
                    .arg(r.data.size()));
    }
    complete(std::move(r));
}

void HttpDownloadJob::complete(HttpFileDownload::Result result)
{
    if (m_finished)
        return;
    m_finished = true;

    // The callback may destroy the context, and with it this job
    const QPointer<HttpDownloadJob> self(this);
    if (auto done = std::exchange(m_done, {}))
        done(result);
    if (self)
        deleteLater();
}

} // namespace eMule
