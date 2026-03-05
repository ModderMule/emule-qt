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
    explicit IpcClientHandler(QTcpSocket* socket, bool isLocal, QObject* parent = nullptr);
    ~IpcClientHandler() override;

    /// Send a message to this client.
    void sendMessage(const Ipc::IpcMessage& msg);

    /// Returns true if handshake has completed.
    [[nodiscard]] bool isHandshaked() const;

signals:
    /// Emitted when this client disconnects.
    void disconnected(eMule::IpcClientHandler* handler);

    /// Emitted when web server configuration has changed via SetPreferences.
    void webServerConfigChanged();

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
    void handleGetDownloadClients(const Ipc::IpcMessage& msg);
    void handleGetKnownClients(const Ipc::IpcMessage& msg);
    void handleSetDownloadPriority(const Ipc::IpcMessage& msg);
    void handleClearCompleted(const Ipc::IpcMessage& msg);
    void handleGetDownloadSources(const Ipc::IpcMessage& msg);
    void handleGetServers(const Ipc::IpcMessage& msg);
    void handleRemoveServer(const Ipc::IpcMessage& msg);
    void handleRemoveAllServers(const Ipc::IpcMessage& msg);
    void handleSetServerPriority(const Ipc::IpcMessage& msg);
    void handleSetServerStatic(const Ipc::IpcMessage& msg);
    void handleAddServer(const Ipc::IpcMessage& msg);
    void handleGetConnection(const Ipc::IpcMessage& msg);
    void handleConnectToServer(const Ipc::IpcMessage& msg);
    void handleDisconnectFromServer(const Ipc::IpcMessage& msg);
    void handleStartSearch(const Ipc::IpcMessage& msg);
    void handleGetSearchResults(const Ipc::IpcMessage& msg);
    void handleStopSearch(const Ipc::IpcMessage& msg);
    void handleRemoveSearch(const Ipc::IpcMessage& msg);
    void handleClearAllSearches(const Ipc::IpcMessage& msg);
    void handleDownloadSearchFile(const Ipc::IpcMessage& msg);
    void handleGetSharedFiles(const Ipc::IpcMessage& msg);
    void handleSetSharedFilePriority(const Ipc::IpcMessage& msg);
    void handleReloadSharedFiles(const Ipc::IpcMessage& msg);
    void handleGetFriends(const Ipc::IpcMessage& msg);
    void handleAddFriend(const Ipc::IpcMessage& msg);
    void handleRemoveFriend(const Ipc::IpcMessage& msg);
    void handleSendChatMessage(const Ipc::IpcMessage& msg);
    void handleSetFriendSlot(const Ipc::IpcMessage& msg);
    void handleGetStats(const Ipc::IpcMessage& msg);
    void handleGetPreferences(const Ipc::IpcMessage& msg);
    void handleSetPreferences(const Ipc::IpcMessage& msg);
    void handleSubscribe(const Ipc::IpcMessage& msg);
    void handleGetKadContacts(const Ipc::IpcMessage& msg);
    void handleGetKadStatus(const Ipc::IpcMessage& msg);
    void handleBootstrapKad(const Ipc::IpcMessage& msg);
    void handleDisconnectKad(const Ipc::IpcMessage& msg);
    void handleGetKadSearches(const Ipc::IpcMessage& msg);
    void handleGetKadLookupHistory(const Ipc::IpcMessage& msg);
    void handleGetNetworkInfo(const Ipc::IpcMessage& msg);
    void handleRecheckFirewall(const Ipc::IpcMessage& msg);
    void handleSyncLogs(const Ipc::IpcMessage& msg);
    void handleShutdown(const Ipc::IpcMessage& msg);
    void handleReloadIPFilter(const Ipc::IpcMessage& msg);
    void handleGetSchedules(const Ipc::IpcMessage& msg);
    void handleSaveSchedules(const Ipc::IpcMessage& msg);
    void handleScanImportFolder(const Ipc::IpcMessage& msg);
    void handleGetConvertJobs(const Ipc::IpcMessage& msg);
    void handleRemoveConvertJob(const Ipc::IpcMessage& msg);
    void handleRetryConvertJob(const Ipc::IpcMessage& msg);
    void handleStopDownload(const Ipc::IpcMessage& msg);
    void handleOpenDownloadFile(const Ipc::IpcMessage& msg);
    void handleOpenDownloadFolder(const Ipc::IpcMessage& msg);
    void handleMarkSearchSpam(const Ipc::IpcMessage& msg);
    void handleResetStats(const Ipc::IpcMessage& msg);
    void handleRenameSharedFile(const Ipc::IpcMessage& msg);
    void handleDeleteSharedFile(const Ipc::IpcMessage& msg);
    void handleUnshareFile(const Ipc::IpcMessage& msg);
    void handleSetDownloadCategory(const Ipc::IpcMessage& msg);
    void handleGetDownloadDetails(const Ipc::IpcMessage& msg);
    void handlePreviewDownload(const Ipc::IpcMessage& msg);
    void handleRequestClientSharedFiles(const Ipc::IpcMessage& msg);

    std::unique_ptr<Ipc::IpcConnection> m_connection;
    bool m_isLocal = true;
    bool m_handshaked = false;
    int m_subscriptionMask = 0;
};

} // namespace eMule
