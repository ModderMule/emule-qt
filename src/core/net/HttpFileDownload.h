#pragma once

/// @file HttpFileDownload.h
/// @brief One-shot HTTP download of a config file, with transparent archive unwrapping.
///
/// Replaces the four near-identical QNetworkAccessManager blocks that used to live in
/// OptionsDialog (ipfilter.dat), KadPanel (nodes.dat), ServerPanel and CoreSession
/// (server.met). They had drifted apart — only one set a User-Agent, only one set a
/// timeout, none capped the response size or unpacked a compressed payload.
///
/// Callers get finished, unwrapped bytes and keep their own UI and logging.

#include "archive/ArchiveUnpack.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace eMule {

/// Ceiling on the *downloaded* (still compressed) size; kMaxUnpackedBytes bounds the
/// result of expanding it.
inline constexpr uint64 kMaxDownloadBytes = 64ull * 1024 * 1024;

class HttpFileDownload {
public:
    struct Options {
        /// Archive members to prefer, most wanted first — e.g. {"ipfilter.dat",
        /// "guarding.p2p"}. Ignored when the payload is not an archive.
        QStringList preferredNames;
        int    timeoutMs = 30000;
        uint64 maxBytes  = kMaxDownloadBytes;
    };

    /// Called with the unwrapped body on success, or ok=false and a human-readable
    /// error. @p entryName names the archive member used, and is empty when the payload
    /// was not an archive.
    using Callback = std::function<void(bool ok, const QByteArray& data,
                                        const QString& entryName, const QString& error)>;

    /// Asynchronous fetch. @p context owns the lifetime of the callback: if it is
    /// destroyed before the reply arrives, the callback is never invoked.
    static void get(QObject* context, const QUrl& url, const Options& opts, Callback done);

    /// Blocking fetch, for startup paths that must finish before the caller continues.
    /// Spins a local event loop, so it must not be called from a slot that is itself
    /// reentrant with the network stack.
    static bool getBlocking(const QUrl& url, const Options& opts,
                            QByteArray& out, QString& entryName, QString& error);

private:
    HttpFileDownload() = delete;
};

} // namespace eMule
