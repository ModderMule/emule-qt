#include "pch.h"
/// @file HttpFileDownload.cpp
/// @brief Shared config-file downloader with transparent archive unwrapping.

#include "net/HttpFileDownload.h"
#include "net/HttpDefaults.h"
#include "utils/Log.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

namespace eMule {

namespace {

/// The settings that used to differ between the four hand-rolled copies of this code.
[[nodiscard]] QNetworkRequest makeRequest(const QUrl& url,
                                          const HttpFileDownload::Options& opts)
{
    // List mirrors redirect constantly (http→https, apex→www); the shared defaults
    // carry the redirect rule that keeps such a hop from downgrading to plain http.
    QNetworkRequest request = Http::makeRequest(url);
    request.setTransferTimeout(opts.timeoutMs);
    return request;
}

/// Shared tail: size-check the body, then unwrap it. Returns false with @p error set.
bool finishReply(QNetworkReply* reply, const HttpFileDownload::Options& opts,
                 QByteArray& out, QString& entryName, QString& error)
{
    if (reply->error() != QNetworkReply::NoError) {
        error = reply->errorString();
        return false;
    }

    const QByteArray body = reply->readAll();
    if (static_cast<uint64>(body.size()) > opts.maxBytes) {
        error = QStringLiteral("download exceeds %1 MB limit")
                    .arg(opts.maxBytes / (1024 * 1024));
        return false;
    }

    const UnwrapResult unwrapped = unwrapDownload(body, opts.preferredNames);
    if (!unwrapped.error.isEmpty()) {
        error = unwrapped.error;
        return false;
    }

    out = unwrapped.data;
    entryName = unwrapped.entryName;
    if (unwrapped.wasArchive) {
        logInfo(QStringLiteral("Unpacked \"%1\" (%2 bytes) from the downloaded archive")
                    .arg(entryName.isEmpty() ? QStringLiteral("<unnamed>") : entryName)
                    .arg(out.size()));
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// get — asynchronous
// ---------------------------------------------------------------------------

void HttpFileDownload::get(QObject* context, const QUrl& url, const Options& opts,
                           Callback done)
{
    // Parented to context, so a dialog closing mid-flight tears the request down with it
    // and the callback never fires against a dead caller.
    auto* nam = new QNetworkAccessManager(context);
    auto* reply = nam->get(makeRequest(url, opts));

    QObject::connect(reply, &QNetworkReply::finished, context,
                     [reply, nam, opts, done = std::move(done)]() {
        reply->deleteLater();
        nam->deleteLater();

        QByteArray data;
        QString entryName;
        QString error;
        const bool ok = finishReply(reply, opts, data, entryName, error);
        if (done)
            done(ok, data, entryName, error);
    });
}

// ---------------------------------------------------------------------------
// getBlocking — synchronous
// ---------------------------------------------------------------------------

bool HttpFileDownload::getBlocking(const QUrl& url, const Options& opts,
                                   QByteArray& out, QString& entryName, QString& error)
{
    QNetworkAccessManager nam;
    auto* reply = nam.get(makeRequest(url, opts));

    // setTransferTimeout covers a stalled transfer; the timer is the backstop for a reply
    // that never finishes at all, so the caller's startup path cannot hang forever.
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(opts.timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        error = QStringLiteral("timed out after %1 ms").arg(opts.timeoutMs);
        return false;
    }

    const bool ok = finishReply(reply, opts, out, entryName, error);
    reply->deleteLater();
    return ok;
}

} // namespace eMule
