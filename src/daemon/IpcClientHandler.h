#pragma once

/// @file IpcClientHandler.h
/// @brief Per-connection IPC handler — dispatches requests to core.
///
/// Mirrors the WebServer handler pattern: receives IPC requests,
/// dispatches to the appropriate core manager via theApp, and sends responses.

#include "IpcConnection.h"
#include "IpcMessage.h"

#include <QObject>

#include <memory>

namespace eMule {

class IpcClientHandler : public QObject {
    Q_OBJECT

public:
    explicit IpcClientHandler(QTcpSocket* socket, QObject* parent = nullptr);
    ~IpcClientHandler() override;

    /// Send a message to this client.
    void sendMessage(const Ipc::IpcMessage& msg);

    /// Returns true if handshake has completed.
    [[nodiscard]] bool isHandshaked() const;

signals:
    /// Emitted when this client disconnects.
    void disconnected(eMule::IpcClientHandler* handler);

private slots:
    void onMessageReceived(const Ipc::IpcMessage& msg);
    void onConnectionLost();

private:
    // Request handlers
    void handleHandshake(const Ipc::IpcMessage& msg);
    void handleGetDownloads(const Ipc::IpcMessage& msg);
    void handleGetDownload(const Ipc::IpcMessage& msg);
    void handlePauseDownload(const Ipc::IpcMessage& msg);
    void handleResumeDownload(const Ipc::IpcMessage& msg);
    void handleCancelDownload(const Ipc::IpcMessage& msg);
    void handleGetUploads(const Ipc::IpcMessage& msg);
    void handleGetServers(const Ipc::IpcMessage& msg);
    void handleGetConnection(const Ipc::IpcMessage& msg);
    void handleConnectToServer(const Ipc::IpcMessage& msg);
    void handleDisconnectFromServer(const Ipc::IpcMessage& msg);
    void handleStartSearch(const Ipc::IpcMessage& msg);
    void handleGetSearchResults(const Ipc::IpcMessage& msg);
    void handleGetSharedFiles(const Ipc::IpcMessage& msg);
    void handleGetFriends(const Ipc::IpcMessage& msg);
    void handleAddFriend(const Ipc::IpcMessage& msg);
    void handleRemoveFriend(const Ipc::IpcMessage& msg);
    void handleGetStats(const Ipc::IpcMessage& msg);
    void handleGetPreferences(const Ipc::IpcMessage& msg);
    void handleSetPreferences(const Ipc::IpcMessage& msg);
    void handleSubscribe(const Ipc::IpcMessage& msg);
    void handleGetKadContacts(const Ipc::IpcMessage& msg);
    void handleGetKadStatus(const Ipc::IpcMessage& msg);
    void handleBootstrapKad(const Ipc::IpcMessage& msg);
    void handleDisconnectKad(const Ipc::IpcMessage& msg);
    void handleGetKadSearches(const Ipc::IpcMessage& msg);
    void handleSyncLogs(const Ipc::IpcMessage& msg);
    void handleShutdown(const Ipc::IpcMessage& msg);

    std::unique_ptr<Ipc::IpcConnection> m_connection;
    bool m_handshaked = false;
    int m_subscriptionMask = 0;
};

} // namespace eMule
