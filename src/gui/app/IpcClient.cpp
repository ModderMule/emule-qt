/// @file IpcClient.cpp
/// @brief GUI-side IPC client — implementation.

#include "app/IpcClient.h"

#include "utils/Log.h"

#include <algorithm>

namespace eMule {

using namespace Ipc;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

IpcClient::IpcClient(QObject* parent)
    : QObject(parent)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &IpcClient::attemptReconnect);
}

IpcClient::~IpcClient()
{
    disconnectFromDaemon();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void IpcClient::connectToDaemon(const QHostAddress& address, uint16_t port)
{
    // Store params for auto-reconnect
    m_address = address;
    m_port = port;
    m_autoReconnect = true;
    m_reconnectDelayMs = 1000;
    m_reconnectTimer.stop();

    resetConnection();

    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected,
            this, &IpcClient::onSocketConnected);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &IpcClient::onSocketError);

    m_socket->connectToHost(address, port);
}

void IpcClient::disconnectFromDaemon()
{
    m_autoReconnect = false;
    m_reconnectTimer.stop();

    m_handshaked = false;
    m_pendingCallbacks.clear();

    if (m_connection) {
        disconnect(m_connection.get(), nullptr, this, nullptr);
        m_connection->close();
        m_connection.reset();
    }
    m_socket = nullptr;
}

bool IpcClient::isConnected() const
{
    return m_handshaked && m_connection && m_connection->isConnected();
}

bool IpcClient::isLocalConnection() const
{
    return m_address.isLoopback();
}

int IpcClient::pollingInterval() const
{
    return isLocalConnection() ? LocalPollingMs : m_remotePollingMs;
}

void IpcClient::setRemotePollingMs(int ms)
{
    m_remotePollingMs = std::clamp(ms, 200, 10000);
}

void IpcClient::sendShutdown()
{
    if (!isConnected())
        return;

    IpcMessage msg(IpcMsgType::Shutdown, m_nextSeqId++);
    m_connection->sendMessage(msg);

    // Flush the socket's write buffer into the OS send buffer synchronously.
    // Without this, disconnectFromDaemon() destroys the socket immediately and
    // the Shutdown frame never reaches the daemon.
    if (QTcpSocket* sock = m_connection->socket())
        sock->flush();

    // Disable auto-reconnect — the daemon is going away intentionally
    disconnectFromDaemon();
}

int IpcClient::sendRequest(IpcMessage msg, ResponseCallback callback)
{
    if (!m_connection || !m_handshaked)
        return -1;

    // Overwrite the seqId with our tracked ID
    const int seqId = m_nextSeqId++;
    // Rebuild message with our seqId
    QCborArray arr = msg.toArray();
    if (arr.size() >= 2) {
        arr[1] = seqId;
    }
    IpcMessage tagged(std::move(arr));

    if (callback)
        m_pendingCallbacks[seqId] = std::move(callback);

    m_connection->sendMessage(tagged);
    return seqId;
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void IpcClient::onSocketConnected()
{
    // Reset backoff on successful TCP connect
    m_reconnectDelayMs = 1000;

    // Detach the pre-handoff error handler: IpcConnection owns the socket now
    // and handles errors through its own onErrorOccurred → disconnected signal.
    // Leaving errorOccurred connected would fire both onConnectionLost() AND
    // onSocketError() on any subsequent error, causing spurious reconnect loops.
    disconnect(m_socket, &QAbstractSocket::errorOccurred,
               this, &IpcClient::onSocketError);

    // Hand off socket to IpcConnection
    m_connection = std::make_unique<IpcConnection>(m_socket, this);
    m_socket = nullptr;  // Now owned by IpcConnection

    connect(m_connection.get(), &IpcConnection::messageReceived,
            this, &IpcClient::onMessageReceived);
    connect(m_connection.get(), &IpcConnection::disconnected,
            this, &IpcClient::onConnectionLost);

    performHandshake();
}

void IpcClient::onMessageReceived(const IpcMessage& msg)
{
    // Check if this is a handshake response
    if (!m_handshaked && msg.type() == IpcMsgType::HandshakeOk) {
        m_handshaked = true;
        logInfo(QStringLiteral("IPC handshake complete — daemon version: %1")
                    .arg(msg.fieldString(0)));

        // Subscribe to all events
        IpcMessage sub(IpcMsgType::Subscribe, m_nextSeqId++);
        sub.append(int64_t(0xFFFF));  // Subscribe to all event types
        m_connection->sendMessage(sub);

        // Field 2: daemon session token.
        // On token change (GUI restart or daemon restart), reset per-type IDs to 0
        // so the full daemon buffer is replayed. On reconnect within the same GUI
        // session (same token), IDs are kept so only missed events are fetched.
        const QString newToken = msg.fieldString(2);
        if (newToken != m_daemonToken) {
            m_lastKadId = m_lastServerId = m_lastLogId = m_lastVerboseId = 0;
            m_daemonToken = newToken;
        }
        // Delay the initial log sync by 500 ms so any brief startup instability
        // has settled before we fetch the buffer.
        QTimer::singleShot(500, this, [this]() {
            if (m_handshaked)
                requestLogSync();
        });

        emit connected();
        return;
    }

    // Check for response to a pending request
    const int seqId = msg.seqId();
    if (seqId > 0) {
        auto it = m_pendingCallbacks.find(seqId);
        if (it != m_pendingCallbacks.end()) {
            if (it->second)
                it->second(msg);
            m_pendingCallbacks.erase(it);
            return;
        }
    }

    // Push event (seqId == 0)
    if (seqId == 0)
        dispatchPushEvent(msg);
}

void IpcClient::onConnectionLost()
{
    m_handshaked = false;
    m_pendingCallbacks.clear();
    emit disconnected();

    scheduleReconnect();
}

void IpcClient::onSocketError()
{
    const QString err = m_socket ? m_socket->errorString()
                                 : QStringLiteral("Unknown error");
    emit connectionFailed(err);

    // Clean up the failed socket so scheduleReconnect starts fresh
    if (m_socket) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    scheduleReconnect();
}

void IpcClient::attemptReconnect()
{
    if (!m_autoReconnect || m_port == 0)
        return;

    logInfo(QStringLiteral("Reconnecting to daemon at %1:%2...")
                .arg(m_address.toString()).arg(m_port));

    resetConnection();

    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected,
            this, &IpcClient::onSocketConnected);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &IpcClient::onSocketError);

    m_socket->connectToHost(m_address, m_port);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void IpcClient::performHandshake()
{
    IpcMessage handshake(IpcMsgType::Handshake, m_nextSeqId++);
    handshake.append(QString::fromLatin1(ProtocolVersion));
    m_connection->sendMessage(handshake);
}

void IpcClient::dispatchPushEvent(const IpcMessage& msg)
{
    switch (msg.type()) {
    case IpcMsgType::PushStatsUpdate:      emit statsUpdated(msg); break;
    case IpcMsgType::PushDownloadUpdate:   emit downloadUpdated(msg); break;
    case IpcMsgType::PushDownloadAdded:    emit downloadAdded(msg); break;
    case IpcMsgType::PushDownloadRemoved:  emit downloadRemoved(msg); break;
    case IpcMsgType::PushServerState:      emit serverStateChanged(msg); break;
    case IpcMsgType::PushSearchResult:     emit searchResultReceived(msg); break;
    case IpcMsgType::PushLogMessage: {
        const int64_t logId  = msg.fieldInt(0);
        const QString cat    = msg.fieldString(1);
        const auto severity  = static_cast<QtMsgType>(msg.fieldInt(2));

        const bool isKad     = (cat == QStringLiteral("emule.kad"));
        const bool isServer  = (cat == QStringLiteral("emule.server"));
        const bool isVerbose = (severity == QtDebugMsg);

        int64_t& typeId = isKad    ? m_lastKadId
                        : isServer ? m_lastServerId
                        : isVerbose? m_lastVerboseId
                        :            m_lastLogId;
        if (logId > typeId)
            typeId = logId;

        emit logMessageReceived(msg);
        break;
    }
    case IpcMsgType::PushSharedFileUpdate: emit sharedFileUpdated(msg); break;
    case IpcMsgType::PushUploadUpdate:     emit uploadUpdated(msg); break;
    case IpcMsgType::PushKadUpdate:        emit kadUpdated(msg); break;
    case IpcMsgType::PushKadSearchesChanged: emit kadSearchesChanged(msg); break;
    case IpcMsgType::PushKnownClientsChanged: emit knownClientsChanged(msg); break;
    case IpcMsgType::PushChatMessage:        emit chatMessageReceived(msg); break;
    case IpcMsgType::PushFriendListChanged:  emit friendListChanged(msg); break;
    default: break;
    }
}

void IpcClient::requestLogSync()
{
    // Request from the minimum of all per-type IDs:
    //  - Fresh GUI start: all IDs == currentMax → min == currentMax → empty response.
    //  - Daemon restart: all IDs == 0 → min == 0 → full new-daemon buffer shown.
    //  - Reconnect within session: min of per-type IDs → only missed events fetched.
    const int64_t fromId = std::min({m_lastKadId, m_lastServerId,
                                     m_lastLogId,  m_lastVerboseId});
    IpcMessage req(IpcMsgType::SyncLogs);
    req.append(fromId);

    sendRequest(std::move(req), [this](const IpcMessage& resp) {
        const QCborArray entries = resp.fieldArray(1);
        for (int i = 0; i < entries.size(); ++i) {
            const QCborArray entry = entries[i].toArray();
            if (entry.size() < 4)
                continue;

            const int64_t logId   = entry[0].toInteger();
            const QString cat     = entry[1].toString();
            const auto severity   = static_cast<QtMsgType>(entry[2].toInteger());

            // Select per-type checkpoint; skip if already seen for this type
            const bool isKad     = (cat == QStringLiteral("emule.kad"));
            const bool isServer  = (cat == QStringLiteral("emule.server"));
            const bool isVerbose = (severity == QtDebugMsg);

            int64_t& typeId = isKad    ? m_lastKadId
                            : isServer ? m_lastServerId
                            : isVerbose? m_lastVerboseId
                            :            m_lastLogId;
            if (logId <= typeId)
                continue;   // already displayed in a previous sync
            typeId = logId;

            // Build synthetic PushLogMessage for uniform GUI handling
            IpcMessage synth(IpcMsgType::PushLogMessage, 0);
            synth.append(logId);
            synth.append(cat);
            synth.append(entry[2].toInteger());          // severity
            synth.append(entry[3].toString());           // message
            synth.append(entry.size() >= 5 ? entry[4].toInteger() : int64_t(0));
            emit logMessageReceived(synth);
        }
    });
}

void IpcClient::scheduleReconnect()
{
    if (!m_autoReconnect || m_reconnectTimer.isActive())
        return;

    logInfo(QStringLiteral("Will retry in %1s...").arg(m_reconnectDelayMs / 1000));
    m_reconnectTimer.start(m_reconnectDelayMs);

    // Exponential backoff: 1s → 2s → 4s → 8s → 16s → 30s
    m_reconnectDelayMs = std::min(m_reconnectDelayMs * 2, MaxReconnectDelay);
}

void IpcClient::resetConnection()
{
    m_handshaked = false;
    m_pendingCallbacks.clear();

    if (m_connection) {
        // Disconnect all signals before closing so that close() → disconnectFromHost()
        // does not cascade into onConnectionLost() → scheduleReconnect() while we are
        // already inside attemptReconnect(), which would queue a second reconnect on
        // top of the one being set up right now.
        disconnect(m_connection.get(), nullptr, this, nullptr);
        m_connection->close();
        m_connection.reset();
    }
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

} // namespace eMule
