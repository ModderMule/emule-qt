#include "pch.h"
/// @file HttpStreamDownload.cpp
/// @brief Streams one HTTP GET to disk with a size cap and SHA-256 check.

#include "net/HttpStreamDownload.h"
#include "net/HttpDefaults.h"

#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace eMule {

HttpStreamDownload::HttpStreamDownload(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
    , m_nam(nam)
{
}

HttpStreamDownload::~HttpStreamDownload()
{
    abort();
}

bool HttpStreamDownload::start(const QUrl& url, const QString& destPath, const Options& opts)
{
    if (m_reply || !m_nam)
        return false;
    m_dest = destPath;
    m_opts = opts;
    m_error = Error::None;
    m_errorString.clear();

    m_file = std::make_unique<QFile>(destPath + QStringLiteral(".part"));
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = Error::Write;
        m_errorString = m_file->errorString();
        m_file.reset();
        return false;
    }
    m_sha = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_reply = m_nam->get(Http::makeRequest(url));
    connect(m_reply, &QNetworkReply::readyRead, this, &HttpStreamDownload::onData);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        emit progress(got, total > 0 ? total : m_opts.expectedSize);
    });
    connect(m_reply, &QNetworkReply::finished, this, &HttpStreamDownload::onDone);
    return true;
}

void HttpStreamDownload::abort()
{
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    if (m_file) {
        const QString part = m_file->fileName();
        m_file.reset();
        QFile::remove(part);
    }
    m_sha.reset();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void HttpStreamDownload::onData()
{
    if (!m_reply || !m_file)
        return;
    const QByteArray chunk = m_reply->readAll();
    if (m_opts.maxBytes > 0 && m_file->size() + chunk.size() > m_opts.maxBytes) {
        fail(Error::TooLarge, tr("The download is larger than expected"));
        return;
    }
    if (m_file->write(chunk) != chunk.size()) {
        fail(Error::Write, m_file->errorString());
        return;
    }
    m_sha->addData(chunk);
}

void HttpStreamDownload::onDone()
{
    if (!m_reply || !m_file)
        return;
    onData();
    if (!m_reply)
        return;   // failed while draining
    QNetworkReply* r = m_reply;
    m_reply = nullptr;
    r->deleteLater();

    const QString part = m_file->fileName();
    const qint64 got = m_file->size();
    m_file->close();
    m_file.reset();
    const QString sha = QString::fromLatin1(m_sha->result().toHex());
    m_sha.reset();

    auto failFile = [this, &part](Error e, const QString& text) {
        QFile::remove(part);
        m_error = e;
        m_errorString = text;
        emit finished(false);
    };
    if (r->error() != QNetworkReply::NoError)
        return failFile(Error::Network, r->errorString());
    if (m_opts.expectedSize > 0 && got != m_opts.expectedSize)
        return failFile(Error::SizeMismatch, tr("Size mismatch: got %1 bytes, expected %2").arg(got).arg(m_opts.expectedSize));
    if (!m_opts.sha256.isEmpty() && sha != m_opts.sha256.toLower())
        return failFile(Error::HashMismatch, tr("SHA-256 mismatch"));

    QFile::remove(m_dest);
    if (!QFile::rename(part, m_dest))
        return failFile(Error::Write, tr("Cannot rename %1").arg(part));
    emit finished(true);
}

void HttpStreamDownload::fail(Error e, const QString& text)
{
    abort();
    m_error = e;
    m_errorString = text;
    emit finished(false);
}

} // namespace eMule
