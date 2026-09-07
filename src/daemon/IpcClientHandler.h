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

class PartFile;

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

    /// The news-server list changed; the daemon should re-apply it to the
    /// connection pool. Same forwarding route as webServerConfigChanged.
    void usenetConfigChanged();

    /// The indexer account list changed and the daemon should re-read it.
    void indexerConfigChanged();

    /// The category list changed. IpcServer turns this into a
    /// PushCategoriesChanged broadcast, so a second GUI — and the tab bar of
    /// the one that made the edit — refetch instead of drifting.
    void categoriesChanged();

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
    void handleSetServerOrder(const Ipc::IpcMessage& msg);
    void handleGetConnection(const Ipc::IpcMessage& msg);
    void handleConnectToServer(const Ipc::IpcMessage& msg);
    void handleDisconnectFromServer(const Ipc::IpcMessage& msg);
    void handleStartSearch(const Ipc::IpcMessage& msg);
    void handleGetSearchResults(const Ipc::IpcMessage& msg);
    void handleStopSearch(const Ipc::IpcMessage& msg);
    void handleRemoveSearch(const Ipc::IpcMessage& msg);
    void handleClearAllSearches(const Ipc::IpcMessage& msg);
    void handleDownloadSearchFile(const Ipc::IpcMessage& msg);
    void handleGetKnownTypes(const Ipc::IpcMessage& msg);
    void handleGetSharedFiles(const Ipc::IpcMessage& msg);
    void handleSetSharedFilePriority(const Ipc::IpcMessage& msg);
    void handleReloadSharedFiles(const Ipc::IpcMessage& msg);
    void handleGetEd2kLink(const Ipc::IpcMessage& msg);
    void handleGetFriends(const Ipc::IpcMessage& msg);
    void handleAddFriend(const Ipc::IpcMessage& msg);
    void handleRemoveFriend(const Ipc::IpcMessage& msg);
    void handleSendChatMessage(const Ipc::IpcMessage& msg);
    void handleSetFriendSlot(const Ipc::IpcMessage& msg);
    void handleGetStats(const Ipc::IpcMessage& msg);
    void handleGetSpeedHistory(const Ipc::IpcMessage& msg);
    void handleGetStatsHistory(const Ipc::IpcMessage& msg);
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
    void handleRestoreStats(const Ipc::IpcMessage& msg);

    // The only two handlers that answer after their call returns: both wait on a
    // /v1/info round trip. See the QPointer note in the implementation.
    void handleProbeHttpCacheServer(const Ipc::IpcMessage& msg);
    void handleApplyHttpCacheConfig(const Ipc::IpcMessage& msg);
    void handleRenameSharedFile(const Ipc::IpcMessage& msg);
    void handleDeleteSharedFile(const Ipc::IpcMessage& msg);
    void handleUnshareFile(const Ipc::IpcMessage& msg);
    void handleSetFileShared(const Ipc::IpcMessage& msg);
    void handleBrowseDirectory(const Ipc::IpcMessage& msg);

    // Download categories (268-270)
    void handleGetCategories(const Ipc::IpcMessage& msg);
    void handleSetCategories(const Ipc::IpcMessage& msg);
    void handleSetCategoryStatus(const Ipc::IpcMessage& msg);

    // Indexers (700-719) — the shared newznab/torznab client
    void handleGetIndexers(const Ipc::IpcMessage& msg);
    void handleSetIndexers(const Ipc::IpcMessage& msg);
    void handleTestIndexer(const Ipc::IpcMessage& msg);
    void handleGetIndexerCaps(const Ipc::IpcMessage& msg);
    void handleStartIndexerSearch(const Ipc::IpcMessage& msg);
    void handleStopIndexerSearch(const Ipc::IpcMessage& msg);
    void handleRemoveIndexerSearch(const Ipc::IpcMessage& msg);
    void handleGrabIndexerResult(const Ipc::IpcMessage& msg);

    // Usenet (720-799)
    void handleGetNewsServers(const Ipc::IpcMessage& msg);
    void handleSetNewsServers(const Ipc::IpcMessage& msg);
    void handleTestNewsServer(const Ipc::IpcMessage& msg);
    void handleGetUsenetQueue(const Ipc::IpcMessage& msg);
    void handleAddNzb(const Ipc::IpcMessage& msg);
    void handleRemoveUsenetItem(const Ipc::IpcMessage& msg);
    void handlePauseUsenetItem(const Ipc::IpcMessage& msg);
    void handleResumeUsenetItem(const Ipc::IpcMessage& msg);
    void handleSetUsenetItemPriority(const Ipc::IpcMessage& msg);
    void handleListUsenetArchiveEntries(const Ipc::IpcMessage& msg);
    void handleSetDownloadCategory(const Ipc::IpcMessage& msg);
    void handleGetDownloadDetails(const Ipc::IpcMessage& msg);
    void handlePreviewDownload(const Ipc::IpcMessage& msg);
    void handleRequestClientSharedFiles(const Ipc::IpcMessage& msg);
    void handleGetClientDetails(const Ipc::IpcMessage& msg);
    void handleGetSharedFileDetails(const Ipc::IpcMessage& msg);
    void handleGetSearchResultDetails(const Ipc::IpcMessage& msg);
    void handleGetServerState(const Ipc::IpcMessage& msg);
    void handleGetServerMessages(const Ipc::IpcMessage& msg);
    void handleSearchKadNotes(const Ipc::IpcMessage& msg);
    void handleGetCollectionInfo(const Ipc::IpcMessage& msg);
    void handleSaveCollection(const Ipc::IpcMessage& msg);

    /// Reject @p msg when Kad cannot serve it, using the same wording as the Kad branch of
    /// handleStartSearch so every rejection reads alike in the GUI.
    /// @param requireConnected  true demands a live Kad connection, false only that Kad runs.
    /// @return true when the caller must return without doing any work.
    bool rejectIfKadUnavailable(const Ipc::IpcMessage& msg, bool requireConnected);

    /// Move every category folder that lived inside @p oldIncomingDir along
    /// with it, after the global incoming directory changed. Without this a
    /// user who relocates their downloads finds their categories still writing
    /// into the old tree.
    void rebaseCategoryDirs(const QString& oldIncomingDir);

    /// Cancel one download: remember the hash if asked to, stop it, take it out
    /// of the queue and out of the two lists that hold non-owning references to
    /// it, then free it. Shared by CancelDownload and by cancelling a whole
    /// category, because getting this teardown order wrong dangles a pointer in
    /// KnownFileList and crashes the next known.met save.
    void cancelDownloadFile(PartFile* pf);

    // Preference application helpers (split to avoid MSVC C1061 nesting limit)
    bool applyPreferenceA(const QString& key, const QCborValue& val);
    bool applyPreferenceB(const QString& key, const QCborValue& val);
    bool applyPreferenceC(const QString& key, const QCborValue& val);

    std::unique_ptr<Ipc::IpcConnection> m_connection;
    bool m_isLocal = true;
    bool m_handshaked = false;
    int m_subscriptionMask = 0;
};

} // namespace eMule
