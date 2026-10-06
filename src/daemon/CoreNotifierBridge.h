#pragma once

/// @file CoreNotifierBridge.h
/// @brief Connects core Qt signals to IPC push events.
///
/// Listens to signals from DownloadQueue, ServerConnect, Statistics,
/// SearchList, SharedFileList, and UploadQueue, then broadcasts
/// corresponding IPC push messages to all connected GUI clients.

#include "files/KnownFileList.h"
#include "portmap/PortMapTypes.h"
#include "server/ServerMsgType.h"
#include "friends/FriendConnectProgress.h"

#include <QCborArray>

#include <array>
#include <vector>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <QString>

#include <deque>
#include <unordered_set>

namespace eMule {

namespace Ipc {
class IpcMessage;
class PushCoalescer;
}

class IpcServer;
class PartFile;
class SearchFile;
class SmtpClient;
class UpDownClient;
class Friend;

class CoreNotifierBridge : public QObject {
    Q_OBJECT

public:
    explicit CoreNotifierBridge(IpcServer* ipcServer, QObject* parent = nullptr);
    ~CoreNotifierBridge() override;

    /// Connect all available core signals to push event handlers.
    void connectAll();

    /// One buffered Server Info line. The monotonic @c id lets a reconnecting GUI
    /// ask for only what it has not already displayed, the same way SyncLogs does.
    struct ServerMessage {
        qint64 id = 0;
        ServerMsgType type = ServerMsgType::Info;
        QString text;
    };

    /// Backlog of Server Info lines, oldest first, so a GUI that (re)connects to an
    /// already-running daemon can repopulate its pane. The reference keeps the pane
    /// for the whole application lifetime; here the daemon outlives the GUI, so the
    /// history has to live on this side of the IPC boundary.
    [[nodiscard]] static const std::deque<ServerMessage>& serverMessageHistory();

private slots:
    // DownloadQueue signals
    void onDownloadAdded();
    void onDownloadRemoved();
    void onDownloadCompleted(eMule::PartFile* file);

    // ServerConnect signals
    void onServerStateChanged();
    void onServerMessage(eMule::ServerMsgType type, const QString& text);

    // Statistics signals
    void onStatsUpdated();

    // SearchList signals
    void onSearchResultAdded(eMule::SearchFile* file);

    // GlobalSearchScheduler signals
    void onGlobalSearchProgress(uint32 searchID, uint32 asked, uint32 total, bool running);

    // SharedFileList signals
    void onSharedFileChanged(const QByteArray& fileHash);
    void onSharedFileRemoved(eMule::KnownFile* file);
    void flushSharedFiles();

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
    void onCaptchaRequestReceived(const QString& fromUser, const QImage& captchaImage);
    void onCaptchaResultReceived(const QString& fromUser, bool solved);
    void onFriendConnectionProgress(eMule::Friend* f, eMule::ChatConnectProgress step);
    void onFriendConnectingResult(eMule::Friend* f, bool success);

    // Client shared files signals
    void onClientSharedFilesReceived(const QByteArray& userHash,
                                     const QString& userName,
                                     uint32 searchID);
    void onPreviewAnswerReceived(const std::array<uint8, 16>& fileHash,
                                 const std::vector<QImage>& images);

    // Port-mapping signals
    void onPortMapStatusChanged(eMule::PortMapStatus status);

private:
    /// Full Kad state snapshot. Built at send time rather than at signal time, so a
    /// coalesced push reports where Kad ended up rather than where it started.
    [[nodiscard]] static Ipc::IpcMessage buildKadUpdate();

    void connectClientChatSignal(eMule::UpDownClient* client);
    void connectClientSharedFilesSignal(eMule::UpDownClient* client);
    void sendEmailNotification(const QString& subject, const QString& body);

    IpcServer* m_ipcServer;
    SmtpClient* m_smtp = nullptr;

    /// Caps how often each push type is broadcast. Several core signals fire once
    /// per *item* — per search result, per download source, per file visited by a
    /// shared-directory scan — and a client that refetches a list on each one is
    /// quadratic in the item count. Owned as a child so its timers stop before the
    /// core objects its builders read.
    Ipc::PushCoalescer* m_pushes = nullptr;

    /// Shared files whose row has to go out again. Sent in batches by
    /// flushSharedFiles(); the file is looked up by hash then, so one that left
    /// meanwhile is simply skipped.
    std::unordered_set<MD4Key> m_dirtySharedFiles;
    QTimer m_sharedFlushTimer;
    QTimer m_sharedRatesTimer;
};

} // namespace eMule
