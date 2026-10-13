/// @file IpcConnection.cpp
/// @brief Framed CBOR-over-TCP connection — implementation.

#include "IpcConnection.h"

#include <QCborValue>
#include <QPointer>
#include <QScopeGuard>
#include <QtEndian>

#include <utility>

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
    connect(m_socket, &QIODevice::bytesWritten, this, &IpcConnection::onBytesWritten);

    m_stallTimer.setInterval(1000);
    connect(&m_stallTimer, &QTimer::timeout, this, &IpcConnection::onStallCheck);
}

IpcConnection::~IpcConnection() = default;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

IpcConnection::SendResult IpcConnection::sendMessage(const IpcMessage& msg)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return SendResult::Closed;

    // A peer this far behind is not reading at all. Queueing more only grows our
    // memory and the pile it has to chew through; a reconnect resyncs it from zero.
    if (m_hardWriteLimit > 0 && m_socket->bytesToWrite() > m_hardWriteLimit) {
        emit protocolError(QStringLiteral("Write backlog of %1 bytes — peer is not reading")
                               .arg(m_socket->bytesToWrite()));
        abort();
        return SendResult::Overflow;
    }

    QByteArray cborBytes = msg.toArray().toCborValue().toCbor();
    if (!m_encryptionKey.isEmpty())
        cborBytes = aesEncryptPayload(cborBytes, m_encryptionKey);
    m_socket->write(encodeFrame(cborBytes));
    m_socket->flush();
    return SendResult::Sent;
}

IpcConnection::SendResult IpcConnection::sendPush(const IpcMessage& msg)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return SendResult::Closed;

    const quint64 key = snapshotPushKey(msg);
    if (key != 0) {
        // Whatever was held under this key is older than msg either way
        std::erase_if(m_held, [key](const auto& held) { return held.first == key; });
        if (m_softWriteLimit > 0 && m_socket->bytesToWrite() > m_softWriteLimit) {
            m_held.emplace_back(key, msg);
            return SendResult::Held;
        }
    }
    return sendMessage(msg);
}

void IpcConnection::setWriteLimits(qint64 soft, qint64 hard)
{
    m_softWriteLimit = soft;
    m_hardWriteLimit = hard;
}

qint64 IpcConnection::pendingWriteBytes() const
{
    return m_socket ? m_socket->bytesToWrite() : 0;
}

bool IpcConnection::hasPendingInput() const
{
    return !m_queue.empty() || hasCompleteFrame()
        || (!m_inputClosed && m_socket && m_socket->bytesAvailable() > 0);
}

void IpcConnection::abort()
{
    m_held.clear();
    // Nothing queued is delivered either, so disconnected() is not held back
    m_inputClosed = true;
    m_queue.clear();
    m_queuedKeys.clear();
    m_readBuffer.clear();
    if (m_socket)
        m_socket->abort();
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
    if (m_inputClosed) {
        m_socket->readAll();
        return;
    }
    m_readBuffer.append(m_socket->readAll());

    const QPointer<IpcConnection> self(this);
    emit activity();
    if (!self)
        return;

    // Re-entrancy. Handlers of messageReceived open modal dialogs, and dispatching
    // from inside such a nested event loop would run handlers inside handlers.
    // Appending is safe; drain() picks the bytes up when the outer pass resumes.
    // (Qt normally keeps a nested readyRead from this socket back by itself.)
    if (m_dispatching) {
        if (!m_blockedReported) {
            m_blockedReported = true;
            emit dispatchBlocked();
        }
        return;
    }

    drain();
}

void IpcConnection::onBytesWritten()
{
    if (!m_held.empty() && m_socket->bytesToWrite() <= m_softWriteLimit / 2)
        flushHeld();
}

void IpcConnection::drain()
{
    m_drainQueued = false;
    if (m_dispatching || !m_socket)
        return;

    m_dispatching = true;
    m_blockedReported = false;
    m_stallTimer.start();
    const QPointer<IpcConnection> self(this);
    const auto clearDispatching = qScopeGuard([self] {
        if (self) {
            self->m_dispatching = false;
            self->m_stallTimer.stop();
        }
    });

    // What a handler's nested event loop left in the socket on an earlier pass
    if (!m_inputClosed && m_socket->bytesAvailable() > 0)
        m_readBuffer.append(m_socket->readAll());

    // One pass gets m_budgetMs, then the event loop gets a turn: input and paint
    // must not wait for a backlog to clear. Decoding takes at most half of it, so
    // a deep buffer cannot starve the dispatch below.
    QElapsedTimer clock;
    clock.start();
    decodeFrames(clock, m_budgetMs / 2);
    if (!self)
        return;

    bool first = true;
    while (!m_queue.empty()) {
        // At least one per pass, however slow the clock says we are
        if (!first && m_budgetMs > 0 && clock.elapsed() >= m_budgetMs)
            break;

        Queued entry = std::move(m_queue.front());
        m_queue.pop_front();
        const qint64 seq = m_queueBase++;
        if (!entry.live)
            continue;
        if (entry.key != 0 && m_queuedKeys.value(entry.key, -1) == seq)
            m_queuedKeys.remove(entry.key);
        first = false;

        if (entry.msg.isValid())
            emit messageReceived(entry.msg);
        else
            emit protocolError(QStringLiteral("Invalid IPC message received"));

        // A handler is allowed to tear this connection down — Shutdown does.
        if (!self)
            return;
    }

    // Left over for the next turn: what the budget cut off, and what arrived
    // during a handler's nested event loop — no readyRead announced those bytes,
    // so without this they would wait for whatever the peer sends next.
    if (hasPendingInput())
        scheduleDrain();
    else if (m_disconnectPending)
        reportDisconnect();
}

void IpcConnection::onStallCheck()
{
    // Reached only through an event loop running inside a handler
    if (!m_dispatching || m_blockedReported || !hasPendingInput())
        return;
    m_blockedReported = true;
    emit dispatchBlocked();
}

void IpcConnection::onDisconnected()
{
    reportDisconnect();
}

void IpcConnection::onErrorOccurred(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    reportDisconnect();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void IpcConnection::decodeFrames(const QElapsedTimer& clock, int budgetMs)
{
    if (m_inputClosed)
        return;

    for (;;) {
        if (budgetMs > 0 && !m_queue.empty() && clock.elapsed() >= budgetMs)
            break;

        if (m_encryptionKey.isEmpty()) {
            // Unencrypted path
            auto result = tryDecodeFrame(m_readBuffer);
            if (!result)
                break;

            m_readBuffer.remove(0, result->bytesConsumed);
            enqueue(IpcMessage(std::move(result->message)));
        } else {
            // Encrypted path
            auto raw = tryExtractRawFrame(m_readBuffer);
            if (!raw)
                break;

            m_readBuffer.remove(0, raw->bytesConsumed);

            QByteArray plain = aesDecryptPayload(raw->payload, m_encryptionKey);
            if (plain.isEmpty()) {
                closeInput(QStringLiteral("AES decryption failed"));
                return;
            }

            QCborValue val = QCborValue::fromCbor(plain);
            if (!val.isArray()) {
                closeInput(QStringLiteral("Invalid CBOR after decryption"));
                return;
            }

            enqueue(IpcMessage(val.toArray()));
        }
    }

    // Check for oversized frame in progress
    if (m_readBuffer.size() >= FrameHeaderSize) {
        const auto payloadLen = qFromBigEndian<uint32_t>(
            reinterpret_cast<const uint8_t*>(m_readBuffer.constData()));
        if (payloadLen > MaxPayloadSize)
            closeInput(QStringLiteral("Oversized frame: %1 bytes").arg(payloadLen));
    }
}

void IpcConnection::closeInput(const QString& reason)
{
    // What was decoded ahead of the bad frame still counts; nothing after it does
    m_inputClosed = true;
    m_readBuffer.clear();
    const QPointer<IpcConnection> self(this);
    emit protocolError(reason);
    if (self)
        close();
}

void IpcConnection::reportDisconnect()
{
    if (m_disconnectEmitted)
        return;

    // What the peer sent before it closed is still owed to the handlers, and a
    // listener told "gone" now would drop it. drain() comes back here when through.
    if (hasPendingInput()) {
        m_disconnectPending = true;
        scheduleDrain();
        return;
    }
    m_disconnectPending = false;
    m_disconnectEmitted = true;
    emit disconnected();
}

void IpcConnection::enqueue(IpcMessage msg)
{
    const qint64 seq = m_queueBase + static_cast<qint64>(m_queue.size());
    const quint64 key = m_coalesceIncoming && msg.isValid() ? snapshotPushKey(msg) : 0;
    if (key != 0) {
        // The older one reports a state this one replaces. It keeps its slot as a
        // dead entry so the newer is delivered in its own place in the stream.
        if (const auto it = m_queuedKeys.constFind(key); it != m_queuedKeys.constEnd()) {
            m_queue[static_cast<std::size_t>(*it - m_queueBase)].live = false;
            ++m_coalesced;
        }
        m_queuedKeys.insert(key, seq);
    }
    m_queue.push_back({std::move(msg), key, true});
}

bool IpcConnection::hasCompleteFrame() const
{
    if (m_readBuffer.size() < FrameHeaderSize)
        return false;
    const auto payloadLen = qFromBigEndian<uint32_t>(
        reinterpret_cast<const uint8_t*>(m_readBuffer.constData()));
    return payloadLen <= MaxPayloadSize
        && m_readBuffer.size() - FrameHeaderSize >= static_cast<qsizetype>(payloadLen);
}

void IpcConnection::scheduleDrain()
{
    if (m_drainQueued)
        return;
    m_drainQueued = true;
    QMetaObject::invokeMethod(this, &IpcConnection::drain, Qt::QueuedConnection);
}

void IpcConnection::flushHeld()
{
    // Taken first: a send can overflow and abort(), which clears m_held
    const auto held = std::exchange(m_held, {});
    for (const auto& [key, msg] : held) {
        if (sendMessage(msg) != SendResult::Sent)
            return;
    }
}

} // namespace eMule::Ipc
