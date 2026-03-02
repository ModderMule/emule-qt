#pragma once

/// @file CoreNotifierBridge.h
/// @brief Connects core Qt signals to IPC push events.
///
/// Listens to signals from DownloadQueue, ServerConnect, Statistics,
/// SearchList, SharedFileList, and UploadQueue, then broadcasts
/// corresponding IPC push messages to all connected GUI clients.

#include <QObject>

namespace eMule {

class IpcServer;
class SearchFile;
class UpDownClient;

class CoreNotifierBridge : public QObject {
    Q_OBJECT

public:
    explicit CoreNotifierBridge(IpcServer* ipcServer, QObject* parent = nullptr);
    ~CoreNotifierBridge() override;

    /// Connect all available core signals to push event handlers.
    void connectAll();

private slots:
    // DownloadQueue signals
    void onDownloadAdded();
    void onDownloadRemoved();

    // ServerConnect signals
    void onServerStateChanged();

    // Statistics signals
    void onStatsUpdated();

    // SearchList signals
    void onSearchResultAdded(eMule::SearchFile* file);

    // SharedFileList signals
    void onSharedFileAdded();

    // UploadQueue signals
    void onUploadChanged();

    // Download source signals
    void onDownloadSourcesChanged();

    // Known clients signals
    void onKnownClientsChanged();

    // Kademlia signals
    void onKadStateChanged();
    void onKadSearchesChanged();

    // FriendList signals
    void onFriendListChanged();

    // Chat signals
    void onChatMessageReceived(const QString& fromUser, const QString& message);

private:
    void connectClientChatSignal(eMule::UpDownClient* client);
    IpcServer* m_ipcServer;
};

} // namespace eMule
