/// @file IpcConnection.cpp
/// @brief Framed CBOR-over-TCP connection — implementation.

#include "IpcConnection.h"

#include <QCborValue>
#include <QPointer>
#include <QScopeGuard>

namespace eMule::Ipc {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

IpcConnection::IpcConnection(QTcpSocket* socket, QObject* parent)
    : QObject(parent)
    , m_socket(socket)
{
    m_socket->setParent(this);

    connect(m_socket, &QTcpSocket::readyRead, this, &IpcConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &IpcConnection::onDisconnected);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &IpcConnection::onErrorOccurred);
}

IpcConnection::~IpcConnection() = default;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void IpcConnection::sendMessage(const IpcMessage& msg)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    QByteArray cborBytes = msg.toArray().toCborValue().toCbor();
    if (!m_encryptionKey.isEmpty())
        cborBytes = aesEncryptPayload(cborBytes, m_encryptionKey);
    m_socket->write(encodeFrame(cborBytes));
    m_socket->flush();
}

bool IpcConnection::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void IpcConnection::close()
{
    if (m_socket)
        m_socket->disconnectFromHost();
}

QTcpSocket* IpcConnection::socket() const
{
    return m_socket;
}

void IpcConnection::setEncryptionKey(const QByteArray& key)
{
    m_encryptionKey = key;
}

bool IpcConnection::isEncrypted() const
{
    return !m_encryptionKey.isEmpty();
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void IpcConnection::onReadyRead()
{
    m_readBuffer.append(m_socket->readAll());

    // Re-entrancy. Handlers of messageReceived open modal dialogs — the eD2K link
    // confirmation and the HTTP Cache one both do — and a nested event loop
    // delivers this socket's next readyRead right back into here. Decoding from a
    // nested call would consume frames off the front of the buffer the outer call
    // is still walking, and deliver them out of order on the way. Appending is
    // safe and keeps the socket drained; the outer loop picks the bytes up on its
    // next turn, because it re-reads m_readBuffer every iteration.
    if (m_dispatching)
        return;

    m_dispatching = true;
    const QPointer<IpcConnection> self(this);
    const auto clearDispatching = qScopeGuard([self] {
        if (self)
            self->m_dispatching = false;
    });

    // Decode as many complete frames as possible
    for (;;) {
        if (m_encryptionKey.isEmpty()) {
            // Unencrypted path
            auto result = tryDecodeFrame(m_readBuffer);
            if (!result)
                break;

            m_readBuffer.remove(0, result->bytesConsumed);

            IpcMessage msg(std::move(result->message));
            if (msg.isValid())
                emit messageReceived(msg);
            else
                emit protocolError(QStringLiteral("Invalid IPC message received"));

            // A handler is allowed to tear this connection down — Shutdown does.
            if (!self)
                return;
        } else {
            // Encrypted path
            auto raw = tryExtractRawFrame(m_readBuffer);
            if (!raw)
                break;

            m_readBuffer.remove(0, raw->bytesConsumed);

            QByteArray plain = aesDecryptPayload(raw->payload, m_encryptionKey);
            if (plain.isEmpty()) {
                emit protocolError(QStringLiteral("AES decryption failed"));
                close();
                return;
            }

            QCborValue val = QCborValue::fromCbor(plain);
            if (!val.isArray()) {
                emit protocolError(QStringLiteral("Invalid CBOR after decryption"));
                close();
                return;
            }

            IpcMessage msg(val.toArray());
            if (msg.isValid())
                emit messageReceived(msg);
            else
                emit protocolError(QStringLiteral("Invalid IPC message received"));

            if (!self)
                return;
        }
    }

    // Check for oversized frame in progress
    if (m_readBuffer.size() >= FrameHeaderSize) {
        const auto payloadLen = qFromBigEndian<uint32_t>(
            reinterpret_cast<const uint8_t*>(m_readBuffer.constData()));
        if (payloadLen > MaxPayloadSize) {
            emit protocolError(QStringLiteral("Oversized frame: %1 bytes").arg(payloadLen));
            close();
        }
    }
}

void IpcConnection::onDisconnected()
{
    if (!m_disconnectEmitted) {
        m_disconnectEmitted = true;
        emit disconnected();
    }
}

void IpcConnection::onErrorOccurred(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    if (!m_disconnectEmitted) {
        m_disconnectEmitted = true;
        emit disconnected();
    }
}

} // namespace eMule::Ipc
