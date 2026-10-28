/// @file IpcTls.cpp
/// @brief TLS for the GUI <-> core link — implementation.

#include "IpcTls.h"

#include <QCryptographicHash>
#include <QPointer>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>

namespace eMule::Ipc {

namespace {

constexpr char kTlsRecordHandshake = 0x16;
constexpr const char* kStateProp = "ipcTlsState";   // 0 pending, 1 ready, 2 dropped

QString peerText(const QSslSocket* s)
{
    return QStringLiteral("%1:%2").arg(s->peerAddress().toString()).arg(s->peerPort());
}

} // namespace

QString certFingerprint(const QSslCertificate& cert)
{
    return QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex(':').toUpper());
}

IpcTlsServer::IpcTlsServer(QObject* parent)
    : QTcpServer(parent)
{
}

void IpcTlsServer::setIdentity(const QSslCertificate& cert, const QSslKey& key)
{
    m_cert = cert;
    m_key = key;
}

void IpcTlsServer::incomingConnection(qintptr handle)
{
    auto* socket = new QSslSocket(this);
    if (!socket->setSocketDescriptor(handle)) {
        socket->deleteLater();
        return;
    }
    socket->setProperty(kStateProp, 0);

    // A peer that never finishes is dropped, TLS or not
    QPointer<QSslSocket> guard(socket);
    QTimer::singleShot(m_handshakeTimeoutMs, this, [this, guard] {
        if (guard && guard->property(kStateProp).toInt() == 0)
            drop(guard, QStringLiteral("handshake timeout"));
    });
    connect(socket, &QAbstractSocket::disconnected, this, [this, socket] {
        if (socket->property(kStateProp).toInt() == 0)
            drop(socket, QStringLiteral("closed before ready"));
    });

    const bool loopback = socket->peerAddress().isLoopback() && !m_loopbackIsRemote;
    if (!loopback) {
        startTls(socket, false);
        return;
    }
    if (socket->bytesAvailable() > 0)
        decideLoopback(socket);
    else
        connect(socket, &QIODevice::readyRead, this, [this, socket] { decideLoopback(socket); },
                Qt::SingleShotConnection);
}

void IpcTlsServer::decideLoopback(QSslSocket* socket)
{
    if (socket->property(kStateProp).toInt() != 0 || socket->isEncrypted())
        return;
    char first = 0;
    if (socket->peek(&first, 1) != 1)
        return;
    if (first == kTlsRecordHandshake)
        startTls(socket, true);
    else
        finish(socket, true);
}

void IpcTlsServer::startTls(QSslSocket* socket, bool isLocal)
{
    if (!hasIdentity()) {
        drop(socket, QStringLiteral("TLS required but no certificate loaded"));
        return;
    }
    QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
    cfg.setLocalCertificate(m_cert);
    cfg.setPrivateKey(m_key);
    cfg.setProtocol(QSsl::TlsV1_2OrLater);
    cfg.setPeerVerifyMode(QSslSocket::VerifyNone);
    socket->setSslConfiguration(cfg);

    connect(socket, &QSslSocket::encrypted, this, [this, socket, isLocal] { finish(socket, isLocal); });
    connect(socket, &QSslSocket::sslErrors, this, [this, socket](const QList<QSslError>& errors) {
        drop(socket, errors.isEmpty() ? QStringLiteral("TLS error") : errors.first().errorString());
    });
    connect(socket, &QAbstractSocket::errorOccurred, this, [this, socket] {
        if (socket->property(kStateProp).toInt() == 0)
            drop(socket, socket->errorString());
    });
    socket->startServerEncryption();
}

void IpcTlsServer::finish(QSslSocket* socket, bool isLocal)
{
    if (socket->property(kStateProp).toInt() != 0)
        return;
    socket->setProperty(kStateProp, 1);
    disconnect(socket, nullptr, this, nullptr);   // our handshake slots are done
    emit ready(socket, isLocal);
    // Frames that arrived with the peek / handshake raise no new readyRead
    QPointer<QSslSocket> guard(socket);
    QMetaObject::invokeMethod(socket, [guard] {
        if (guard && guard->bytesAvailable() > 0)
            emit guard->readyRead();
    }, Qt::QueuedConnection);
}

void IpcTlsServer::drop(QSslSocket* socket, const QString& reason)
{
    if (socket->property(kStateProp).toInt() == 2)
        return;
    socket->setProperty(kStateProp, 2);
    emit rejected(peerText(socket), reason);
    disconnect(socket, nullptr, this, nullptr);
    socket->abort();
    socket->deleteLater();
}

} // namespace eMule::Ipc
