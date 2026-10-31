#pragma once

/// @file HttpStreamDownload.h
/// @brief Streams one HTTP GET to disk with a size cap and SHA-256 check.
///
/// Writes `<dest>.part` while running, renames it to `<dest>` only when the hash
/// (and size, when given) match. Any failure removes the partial file.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <memory>

class QCryptographicHash;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace eMule {

class HttpStreamDownload : public QObject {
    Q_OBJECT

public:
    enum class Error { None, Network, Write, TooLarge, SizeMismatch, HashMismatch };

    struct Options {
        qint64  maxBytes = 0;    ///< hard cap; 0 = no cap
        QString sha256;          ///< expected lowercase hex; empty = not checked
        qint64  expectedSize = 0;///< expected byte count; 0 = not checked
    };

    /// @p nam stays owned by the caller (its proxy applies).
    explicit HttpStreamDownload(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~HttpStreamDownload() override;

    /// Start fetching @p url into @p destPath. False when one is already running or
    /// the part file cannot be opened (finished() is not emitted then).
    bool start(const QUrl& url, const QString& destPath, const Options& opts);

    /// Stop and remove the partial file. finished() is not emitted.
    void abort();

    [[nodiscard]] bool isRunning() const { return m_reply != nullptr; }
    [[nodiscard]] Error error() const { return m_error; }
    [[nodiscard]] QString errorString() const { return m_errorString; }

signals:
    void progress(qint64 received, qint64 total);
    /// @p ok: destPath now holds the verified file.
    void finished(bool ok);

private:
    void onData();
    void onDone();
    void fail(Error e, const QString& text);

    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QFile> m_file;
    std::unique_ptr<QCryptographicHash> m_sha;
    QString m_dest;
    Options m_opts;
    Error m_error = Error::None;
    QString m_errorString;
};

} // namespace eMule
