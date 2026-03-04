#pragma once

/// @file IpcClient.h
/// @brief GUI-side IPC client — connects to the daemon and provides
///        request/response + push event handling.
///
/// Connects to the daemon's TCP IPC server, performs the handshake,
/// and provides a request/callback interface for GUI panels.

#include "IpcConnection.h"
#include "IpcMessage.h"

#include <QHostAddress>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <unordered_map>

namespace eMule {

class IpcClient : public QObject {
    Q_OBJECT

public:
    explicit IpcClient(QObject* parent = nullptr);
    ~IpcClient() override;

    /// Connect to the daemon at the given address and port.
    /// Enables auto-reconnect: on failure or disconnect the client will
    /// keep retrying with exponential backoff (1s → 2s → 4s … 30s).
    void connectToDaemon(const QHostAddress& address, uint16_t port);

    /// Connect by hostname (DNS resolved by QTcpSocket internally).
    void connectToDaemon(const QString& host, uint16_t port);

    /// Disconnect from the daemon and stop auto-reconnect.
    void disconnectFromDaemon();

    /// Returns true if connected and handshake is complete.
    [[nodiscard]] bool isConnected() const;

    /// Returns true if connected to a loopback address (localhost).
    [[nodiscard]] bool isLocalConnection() const;

    /// Recommended polling interval: 500ms for localhost, configurable for remote.
    [[nodiscard]] int pollingInterval() const;

    /// Set the remote polling interval (from preferences). Default 1500ms.
    void setRemotePollingMs(int ms);

    /// Set the auth token for non-localhost connections.
    void setAuthToken(const QString& token);

    static constexpr int LocalPollingMs  = 500;

    /// Callback type for request responses.
    using ResponseCallback = std::function<void(const Ipc::IpcMessage&)>;

    /// Send a request and register a callback for the response.
    /// Returns the sequence ID used for this request.
    int sendRequest(Ipc::IpcMessage msg, ResponseCallback callback = nullptr);

    /// Send a Shutdown request to the daemon, then disconnect.
    /// Use this when the GUI launched the daemon and is about to close.
    void sendShutdown();

signals:
    /// Emitted when connection + handshake succeeds.
    void connected();

    /// Emitted when connection is lost.
    void disconnected();

    /// Emitted on connection failure.
    void connectionFailed(const QString& error);

    // -- Push event signals ---------------------------------------------------

    void statsUpdated(const Ipc::IpcMessage& msg);
    void downloadUpdated(const Ipc::IpcMessage& msg);
    void downloadAdded(const Ipc::IpcMessage& msg);
    void downloadRemoved(const Ipc::IpcMessage& msg);
    void serverStateChanged(const Ipc::IpcMessage& msg);
    void searchResultReceived(const Ipc::IpcMessage& msg);
    void logMessageReceived(const Ipc::IpcMessage& msg);
    void sharedFileUpdated(const Ipc::IpcMessage& msg);
    void uploadUpdated(const Ipc::IpcMessage& msg);
    void kadUpdated(const Ipc::IpcMessage& msg);
    void kadSearchesChanged(const Ipc::IpcMessage& msg);
    void knownClientsChanged(const Ipc::IpcMessage& msg);
    void chatMessageReceived(const Ipc::IpcMessage& msg);
    void friendListChanged(const Ipc::IpcMessage& msg);

private slots:
    void onSocketConnected();
    void onMessageReceived(const Ipc::IpcMessage& msg);
    void onConnectionLost();
    void onSocketError();
    void attemptReconnect();

private:
    void performHandshake();
    void dispatchPushEvent(const Ipc::IpcMessage& msg);
    void requestLogSync();
    void scheduleReconnect();
    void resetConnection();

    std::unique_ptr<Ipc::IpcConnection> m_connection;
    QTcpSocket* m_socket = nullptr;  // Owned by IpcConnection after handoff
    QTimer m_reconnectTimer;
    QHostAddress m_address;
    QString m_hostname;  // Non-empty when connected by hostname (DNS)
    uint16_t m_port = 0;
    int m_nextSeqId = 1;
    int m_reconnectDelayMs = 1000;
    bool m_handshaked = false;
    bool m_autoReconnect = false;
    int64_t m_lastKadId     = 0;
    int64_t m_lastServerId  = 0;
    int64_t m_lastLogId     = 0;  // Log tab
    int64_t m_lastVerboseId = 0;
    QString m_daemonToken;
    QString m_authToken;
    std::unordered_map<int, ResponseCallback> m_pendingCallbacks;
    int m_remotePollingMs = 1500;

    static constexpr int MaxReconnectDelay = 30000;
};

} // namespace eMule
