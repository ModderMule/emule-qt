#include "pch.h"
/// @file IpcClient.cpp
/// @brief GUI-side IPC client — implementation.

#include "app/IpcClient.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QPointer>

#include <memory>
#include <utility>


namespace eMule {

static QString ipcMsgTypeName(Ipc::IpcMsgType type)
{
    using T = Ipc::IpcMsgType;
    switch (type) {
    case T::Handshake:            return QStringLiteral("Handshake");
    case T::Ping:                 return QStringLiteral("Ping");
    case T::GetDownloads:         return QStringLiteral("GetDownloads");
    case T::GetDownload:          return QStringLiteral("GetDownload");
    case T::PauseDownload:        return QStringLiteral("PauseDownload");
    case T::ResumeDownload:       return QStringLiteral("ResumeDownload");
    case T::CancelDownload:       return QStringLiteral("CancelDownload");
    case T::SetDownloadPriority:  return QStringLiteral("SetDownloadPriority");
    case T::ClearCompleted:       return QStringLiteral("ClearCompleted");
    case T::GetDownloadSources:   return QStringLiteral("GetDownloadSources");
    case T::GetUploads:           return QStringLiteral("GetUploads");
    case T::GetDownloadClients:   return QStringLiteral("GetDownloadClients");
    case T::GetKnownClients:      return QStringLiteral("GetKnownClients");
    case T::GetServers:           return QStringLiteral("GetServers");
    case T::RemoveServer:         return QStringLiteral("RemoveServer");
    case T::RemoveAllServers:     return QStringLiteral("RemoveAllServers");
    case T::SetServerPriority:    return QStringLiteral("SetServerPriority");
    case T::SetServerStatic:      return QStringLiteral("SetServerStatic");
    case T::AddServer:            return QStringLiteral("AddServer");
    case T::SetServerEnabled:     return QStringLiteral("SetServerEnabled");
    case T::GetConnection:        return QStringLiteral("GetConnection");
    case T::ConnectToServer:      return QStringLiteral("ConnectToServer");
    case T::DisconnectFromServer: return QStringLiteral("DisconnectFromServer");
    case T::ReleaseConnectHold: return QStringLiteral("ReleaseConnectHold");
    case T::StartSearch:          return QStringLiteral("StartSearch");
    case T::GetSearchResults:     return QStringLiteral("GetSearchResults");
    case T::StopSearch:           return QStringLiteral("StopSearch");
    case T::SearchMore:           return QStringLiteral("SearchMore");
    case T::RemoveSearch:         return QStringLiteral("RemoveSearch");
    case T::ClearAllSearches:     return QStringLiteral("ClearAllSearches");
    case T::DownloadSearchFile:   return QStringLiteral("DownloadSearchFile");
    case T::GetSharedFiles:       return QStringLiteral("GetSharedFiles");
    case T::SetSharedFilePriority: return QStringLiteral("SetSharedFilePriority");
    case T::ReloadSharedFiles:    return QStringLiteral("ReloadSharedFiles");
    case T::GetEd2kLink:          return QStringLiteral("GetEd2kLink");
    case T::GetFriends:           return QStringLiteral("GetFriends");
    case T::AddFriend:            return QStringLiteral("AddFriend");
    case T::RemoveFriend:         return QStringLiteral("RemoveFriend");
    case T::SendChatMessage:      return QStringLiteral("SendChatMessage");
    case T::SetFriendSlot:        return QStringLiteral("SetFriendSlot");
    case T::EndChatSession:       return QStringLiteral("EndChatSession");
    case T::GetStats:             return QStringLiteral("GetStats");
    case T::GetPreferences:       return QStringLiteral("GetPreferences");
    case T::SetPreferences:       return QStringLiteral("SetPreferences");
    case T::Subscribe:            return QStringLiteral("Subscribe");
    case T::GetKadContacts:       return QStringLiteral("GetKadContacts");
    case T::GetKadStatus:         return QStringLiteral("GetKadStatus");
    case T::GetKadStats:          return QStringLiteral("GetKadStats");
    case T::BootstrapKad:         return QStringLiteral("BootstrapKad");
    case T::DisconnectKad:        return QStringLiteral("DisconnectKad");
    case T::SyncLogs:             return QStringLiteral("SyncLogs");
    case T::Shutdown:             return QStringLiteral("Shutdown");
    case T::GetKadSearches:       return QStringLiteral("GetKadSearches");
    case T::GetKadLookupHistory:  return QStringLiteral("GetKadLookupHistory");
    case T::GetNetworkInfo:       return QStringLiteral("GetNetworkInfo");
    case T::GetNetworkInterfaces: return QStringLiteral("GetNetworkInterfaces");
    case T::RecheckFirewall:      return QStringLiteral("RecheckFirewall");
    case T::ReloadIPFilter:       return QStringLiteral("ReloadIPFilter");
    case T::GetSchedules:         return QStringLiteral("GetSchedules");
    case T::SaveSchedules:        return QStringLiteral("SaveSchedules");
    case T::ScanImportFolder:     return QStringLiteral("ScanImportFolder");
    case T::GetConvertJobs:       return QStringLiteral("GetConvertJobs");
    case T::RemoveConvertJob:     return QStringLiteral("RemoveConvertJob");
    case T::RetryConvertJob:      return QStringLiteral("RetryConvertJob");
    case T::StopDownload:         return QStringLiteral("StopDownload");
    case T::OpenDownloadFile:     return QStringLiteral("OpenDownloadFile");
    case T::OpenDownloadFolder:   return QStringLiteral("OpenDownloadFolder");
    case T::MarkSearchSpam:       return QStringLiteral("MarkSearchSpam");
    case T::ResetStats:           return QStringLiteral("ResetStats");
    case T::RestoreStats:         return QStringLiteral("RestoreStats");
    case T::ProbeHttpCacheServer: return QStringLiteral("ProbeHttpCacheServer");
    case T::ApplyHttpCacheConfig: return QStringLiteral("ApplyHttpCacheConfig");
    case T::RenameSharedFile:     return QStringLiteral("RenameSharedFile");
    case T::DeleteSharedFile:     return QStringLiteral("DeleteSharedFile");
    case T::UnshareFile:          return QStringLiteral("UnshareFile");
    case T::SetDownloadCategory:  return QStringLiteral("SetDownloadCategory");
    case T::GetCategories:        return QStringLiteral("GetCategories");
    case T::SetCategories:        return QStringLiteral("SetCategories");
    case T::SetCategoryStatus:    return QStringLiteral("SetCategoryStatus");
    case T::GetDownloadDetails:   return QStringLiteral("GetDownloadDetails");
    case T::PreviewDownload:      return QStringLiteral("PreviewDownload");
    case T::RequestClientSharedFiles: return QStringLiteral("RequestClientSharedFiles");
    case T::RequestSearchPreview: return QStringLiteral("RequestSearchPreview");
    case T::GetClientDetails:     return QStringLiteral("GetClientDetails");
    case T::HandshakeOk:          return QStringLiteral("HandshakeOk");
    case T::Result:               return QStringLiteral("Result");
    case T::Error:                return QStringLiteral("Error");
    case T::PushStatsUpdate:      return QStringLiteral("PushStatsUpdate");
    case T::PushDownloadUpdate:   return QStringLiteral("PushDownloadUpdate");
    case T::PushDownloadAdded:    return QStringLiteral("PushDownloadAdded");
    case T::PushDownloadRemoved:  return QStringLiteral("PushDownloadRemoved");
    case T::GetKnownFiles:        return QStringLiteral("GetKnownFiles");
    case T::RecoverArchivePreview: return QStringLiteral("RecoverArchivePreview");
    case T::SwapA4AFToFile:       return QStringLiteral("SwapA4AFToFile");
    case T::UpdateIpFilterNow:    return QStringLiteral("UpdateIpFilterNow");
    case T::RehashAllSharedFiles: return QStringLiteral("RehashAllSharedFiles");
    case T::PushNotifierEvent:    return QStringLiteral("PushNotifierEvent");
    case T::PushServerState:      return QStringLiteral("PushServerState");
    case T::PushSearchResult:     return QStringLiteral("PushSearchResult");
    case T::PushLogMessage:       return QStringLiteral("PushLogMessage");
    case T::PushLogBatch:         return QStringLiteral("PushLogBatch");
    case T::PushCoreLoading:      return QStringLiteral("PushCoreLoading");
    case T::PushCoreRestarting:   return QStringLiteral("PushCoreRestarting");
    case T::PushSharedFileUpdate: return QStringLiteral("PushSharedFileUpdate");
    case T::PushSharedFileRemoved: return QStringLiteral("PushSharedFileRemoved");
    case T::PushUploadUpdate:     return QStringLiteral("PushUploadUpdate");
    case T::PushKadUpdate:        return QStringLiteral("PushKadUpdate");
    case T::PushKadSearchesChanged: return QStringLiteral("PushKadSearchesChanged");
    case T::PushKnownClientsChanged: return QStringLiteral("PushKnownClientsChanged");
    case T::PushChatMessage:      return QStringLiteral("PushChatMessage");
    case T::PushChatState:        return QStringLiteral("PushChatState");
    case T::PushChatCaptcha:      return QStringLiteral("PushChatCaptcha");
    case T::PushChatCaptchaResult: return QStringLiteral("PushChatCaptchaResult");
    case T::PushFriendListChanged: return QStringLiteral("PushFriendListChanged");
    case T::PushClientSharedFiles: return QStringLiteral("PushClientSharedFiles");
    case T::PushSearchPreview:    return QStringLiteral("PushSearchPreview");
    case T::PushPortMapStatus: return QStringLiteral("PushPortMapStatus");
    case T::GetIndexers:          return QStringLiteral("GetIndexers");
    case T::SetIndexers:          return QStringLiteral("SetIndexers");
    case T::TestIndexer:          return QStringLiteral("TestIndexer");
    case T::GetIndexerCaps:       return QStringLiteral("GetIndexerCaps");
    case T::StartIndexerSearch:   return QStringLiteral("StartIndexerSearch");
    case T::StopIndexerSearch:    return QStringLiteral("StopIndexerSearch");
    case T::RemoveIndexerSearch:  return QStringLiteral("RemoveIndexerSearch");
    case T::GrabIndexerResult:    return QStringLiteral("GrabIndexerResult");
    case T::PushIndexerResults:   return QStringLiteral("PushIndexerResults");
    case T::PushIndexerProgress:  return QStringLiteral("PushIndexerProgress");
    case T::PushIndexerSearchDone: return QStringLiteral("PushIndexerSearchDone");
    case T::PushIndexerFeedStatus: return QStringLiteral("PushIndexerFeedStatus");
    case T::GetNewsServers:       return QStringLiteral("GetNewsServers");
    case T::SetNewsServers:       return QStringLiteral("SetNewsServers");
    case T::TestNewsServer:       return QStringLiteral("TestNewsServer");
    case T::SetNewsServerUsage:   return QStringLiteral("SetNewsServerUsage");
    case T::GetUsenetStats:       return QStringLiteral("GetUsenetStats");
    case T::GetUsenetQueue:       return QStringLiteral("GetUsenetQueue");
    case T::GetUsenetKnownTypes:  return QStringLiteral("GetUsenetKnownTypes");
    case T::AddNzb:               return QStringLiteral("AddNzb");
    case T::AddNzbUrl:            return QStringLiteral("AddNzbUrl");
    case T::RemoveUsenetItem:     return QStringLiteral("RemoveUsenetItem");
    case T::PauseUsenetItem:      return QStringLiteral("PauseUsenetItem");
    case T::ResumeUsenetItem:     return QStringLiteral("ResumeUsenetItem");
    case T::SetUsenetItemPriority: return QStringLiteral("SetUsenetItemPriority");
    case T::SetUsenetItemPassword: return QStringLiteral("SetUsenetItemPassword");
    case T::CheckUsenetItem:      return QStringLiteral("CheckUsenetItem");
    case T::ListUsenetArchiveEntries: return QStringLiteral("ListUsenetArchiveEntries");
    case T::PushUsenetQueueItem:  return QStringLiteral("PushUsenetQueueItem");
    case T::PushUsenetItemRemoved: return QStringLiteral("PushUsenetItemRemoved");
    case T::PushUsenetItemFinished: return QStringLiteral("PushUsenetItemFinished");
    case T::PushUsenetEngineState: return QStringLiteral("PushUsenetEngineState");
    case T::SetUsenetFilesSkipped: return QStringLiteral("SetUsenetFilesSkipped");
    case T::SetUsenetPaused:      return QStringLiteral("SetUsenetPaused");
    case T::InspectNzb:           return QStringLiteral("InspectNzb");
    case T::ClearUsenetCompleted: return QStringLiteral("ClearUsenetCompleted");
    case T::GetTorrents:          return QStringLiteral("GetTorrents");
    case T::AddTorrentFile:       return QStringLiteral("AddTorrentFile");
    case T::AddMagnet:            return QStringLiteral("AddMagnet");
    case T::RemoveTorrent:        return QStringLiteral("RemoveTorrent");
    case T::PauseTorrent:         return QStringLiteral("PauseTorrent");
    case T::ResumeTorrent:        return QStringLiteral("ResumeTorrent");
    case T::GetTorrentDetails:    return QStringLiteral("GetTorrentDetails");
    case T::SetTorrentFilePriorities: return QStringLiteral("SetTorrentFilePriorities");
    case T::RecheckTorrent:       return QStringLiteral("RecheckTorrent");
    case T::ReannounceTorrent:    return QStringLiteral("ReannounceTorrent");
    case T::SetTorrentOptions:    return QStringLiteral("SetTorrentOptions");
    case T::AddTorrentTrackers:   return QStringLiteral("AddTorrentTrackers");
    case T::RemoveTorrentTrackers: return QStringLiteral("RemoveTorrentTrackers");
    case T::GetBtStatus:          return QStringLiteral("GetBtStatus");
    case T::GetBtStats:           return QStringLiteral("GetBtStats");
    case T::PushTorrentAdded:     return QStringLiteral("PushTorrentAdded");
    case T::PushTorrentUpdate:    return QStringLiteral("PushTorrentUpdate");
    case T::PushTorrentRemoved:   return QStringLiteral("PushTorrentRemoved");
    case T::PushBtStatus:         return QStringLiteral("PushBtStatus");
    case T::PushTorrentFinished:  return QStringLiteral("PushTorrentFinished");
    case T::GetEd3kStatus:        return QStringLiteral("GetEd3kStatus");
    case T::PushEd3kStatus:       return QStringLiteral("PushEd3kStatus");
    default:
        return QStringLiteral("Unknown(%1)").arg(static_cast<int>(type));
    }
}

using namespace Ipc;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

IpcClient::IpcClient(QObject* parent)
    : QObject(parent)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &IpcClient::attemptReconnect);

    m_handshakeTimer.setSingleShot(true);
    connect(&m_handshakeTimer, &QTimer::timeout, this, [this]() {
        if (waitOnBusyCore(/*handshake=*/true)) {
            m_handshakeTimer.start(m_handshakeTimeoutMs);
            return;
        }
        logWarning(QStringLiteral("IPC handshake timed out — will retry"));
        // A refusal already says more than "no answer" does
        if (m_coreLink != CoreLink::Refused)
            setCoreLink(CoreLink::NotResponding);
        onConnectionLost();
    });

    connect(&m_keepaliveTimer, &QTimer::timeout, this, &IpcClient::sendKeepalive);

    m_keepaliveTimeoutTimer.setSingleShot(true);
    connect(&m_keepaliveTimeoutTimer, &QTimer::timeout, this, [this]() {
        // A busy GUI reads late: the pong may sit behind a backlog of frames. A
        // reconnect then would refetch every list on top of that backlog.
        if (peerStillTalking()) {
            m_keepaliveTimeoutTimer.start(m_keepaliveTimeoutMs);
            return;
        }
        if (waitOnBusyCore(/*handshake=*/false)) {
            m_keepaliveTimeoutTimer.start(m_keepaliveTimeoutMs);
            return;
        }
        logWarning(QStringLiteral("IPC keepalive timed out — reconnecting"));
        setCoreLink(CoreLink::NotResponding);
        onConnectionLost();
    });
}

IpcClient::~IpcClient()
{
    // Silent: a listener told "offline" from in here would look at an object
    // that is half gone, or write to state of its own that already is.
    blockSignals(true);
    disconnectFromDaemon();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void IpcClient::connectToDaemon(const QHostAddress& address, uint16_t port)
{
    // Store params for auto-reconnect
    m_address = address;
    m_hostname.clear();
    m_port = port;
    m_autoReconnect = true;
    m_reconnectDelayMs = 1000;
    m_reconnectTimer.stop();

    m_connectAttempts = 0;
    m_coreLink = CoreLink::Offline;   // a fresh target: nothing sticks from the last
    beginConnect();
}

void IpcClient::connectToDaemon(const QString& host, uint16_t port)
{
    // Try parsing as IP address first — avoids unnecessary DNS lookup
    QHostAddress addr(host);
    if (!addr.isNull()) {
        connectToDaemon(addr, port);
        return;
    }

    // Hostname — store for reconnect; QTcpSocket handles DNS internally
    m_hostname = host;
    m_address = QHostAddress{};
    m_port = port;
    m_autoReconnect = true;
    m_reconnectDelayMs = 1000;
    m_reconnectTimer.stop();

    m_connectAttempts = 0;
    m_coreLink = CoreLink::Offline;
    beginConnect();
}

void IpcClient::disconnectFromDaemon()
{
    m_autoReconnect = false;
    m_reconnectTimer.stop();

    m_handshaked = false;
    failPendingRequests();

    dropConnection();
    m_socket = nullptr;
    setCoreLink(CoreLink::Offline);
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

void IpcClient::setAuthToken(const QString& token)
{
    m_authToken = token;
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

void IpcClient::sendRestart()
{
    if (!isConnected())
        return;
    expectRestart();
    IpcMessage msg(IpcMsgType::Shutdown);
    msg.append(true);
    sendRequest(std::move(msg));
}

bool IpcClient::daemonRestarting() const
{
    return !m_restartDeadline.hasExpired();
}

void IpcClient::expectDaemonStart()
{
    m_startDeadline = QDeadlineTimer(m_startGraceMs);
    if (m_coreLink == CoreLink::Connecting)
        setCoreLink(CoreLink::Starting);
}

bool IpcClient::daemonStarting() const
{
    return !m_startDeadline.hasExpired();
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

    if (thePrefs.enableIpcLog())
        emit ipcLogMessage(
            QStringLiteral("%1 [seq=%2]").arg(ipcMsgTypeName(tagged.type())).arg(seqId),
            /*outgoing=*/true);

    return seqId;
}

void IpcClient::cancelRequest(int seqId)
{
    m_pendingCallbacks.erase(seqId);
}

void IpcClient::sendPoll(const QObject* owner, IpcMessage msg, ResponseCallback callback)
{
    if (!m_connection || !m_handshaked)
        return;

    const QByteArray identity = requestIdentity(msg)
        + QByteArray::number(reinterpret_cast<quintptr>(owner), 16);
    if (const auto it = m_polls.find(identity); it != m_polls.end()) {
        it->again = true;
        it->msg = std::move(msg);
        it->callback = std::move(callback);
        ++m_pollsSkipped;
        return;
    }

    // The entry goes in before the send and is matched by seqId on the way out: a
    // reply for a connection since dropped must not release the poll of the new one.
    auto seq = std::make_shared<int>(-1);
    m_polls.insert(identity, Poll{});
    *seq = sendRequest(std::move(msg), [this, owner, identity, seq, callback](const IpcMessage& reply) {
        Poll next;
        if (const auto it = m_polls.find(identity); it != m_polls.end() && it->seqId == *seq) {
            next = std::move(*it);
            m_polls.erase(it);
        }
        if (callback)
            callback(reply);
        // One turn of the event loop first: applying a list and asking for the next
        // back to back would leave no room for input on a machine that is behind.
        if (next.again && reply.isValid()) {
            QTimer::singleShot(0, this, [this, owner, next = std::move(next)]() mutable {
                sendPoll(owner, std::move(next.msg), std::move(next.callback));
            });
        }
    });
    if (*seq < 0)
        m_polls.remove(identity);
    else
        m_polls[identity].seqId = *seq;
}

void IpcClient::setTimeouts(int handshakeMs, int keepaliveIntervalMs, int keepaliveTimeoutMs)
{
    m_handshakeTimeoutMs = handshakeMs;
    m_keepaliveIntervalMs = keepaliveIntervalMs;
    m_keepaliveTimeoutMs = keepaliveTimeoutMs;
    if (m_keepaliveTimer.isActive())
        m_keepaliveTimer.start(m_keepaliveIntervalMs);
}

void IpcClient::sendBatchRequest(const QStringList& keys,
                                 const std::function<IpcMessage(const QString&)>& build,
                                 QObject* context, std::function<void()> onAllDone,
                                 std::function<void(const QString&, const IpcMessage&)> onEach)
{
    if (keys.isEmpty() || !build || !isConnected())
        return;

    auto pending = std::make_shared<qsizetype>(keys.size());
    auto done = std::make_shared<std::function<void()>>(std::move(onAllDone));
    auto each = std::make_shared<std::function<void(const QString&, const IpcMessage&)>>(
        std::move(onEach));
    const QPointer<QObject> guard(context);

    auto settle = [pending, done, guard]() {
        if (--(*pending) == 0 && guard && *done)
            (*done)();
    };
    auto report = [each, guard](const QString& key, const IpcMessage& reply) {
        if (guard && *each)
            (*each)(key, reply);
    };

    for (const QString& key : keys) {
        if (key.isEmpty()) {
            report(key, {});
            settle();
            continue;
        }
        // A request that never got queued must still settle, or a disconnect in the middle
        // of a batch would leave the completion — and with it the list refresh — hanging.
        if (sendRequest(build(key), [settle, report, key](const IpcMessage& reply) {
                report(key, reply);
                settle();
            }) < 0) {
            report(key, {});
            settle();
        }
    }
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void IpcClient::onSocketConnected()
{
    // A connected() that outlived its socket being dropped. onSocketError()
    // disconnects before deleting now, so this should be unreachable — it stays
    // because the cost of being wrong here is a null dereference in
    // IpcConnection's constructor rather than a missed reconnect.
    if (!m_socket)
        return;

    // Darwin 27 reports a refused connect as connected(), with no peer. Taken for
    // real, it never raised connectionFailed — so the core was never launched.
    if (m_socket->peerPort() != m_port) {
        failConnect(QStringLiteral("Connection refused"));
        return;
    }

    // Reset backoff on successful TCP connect
    m_reconnectDelayMs = 1000;

    // When connected by hostname, capture the resolved peer address so that
    // isLoopback() checks and handshake encryption decisions work correctly.
    if (!m_hostname.isEmpty() && m_socket)
        m_address = m_socket->peerAddress();

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
    // Any byte from the core proves it alive, the pong included or not
    connect(m_connection.get(), &IpcConnection::activity,
            &m_keepaliveTimeoutTimer, &QTimer::stop);
    connect(m_connection.get(), &IpcConnection::dispatchBlocked,
            this, &IpcClient::dispatchBlocked);
    // Of the state pushes waiting to be handled only the newest of a kind matters
    m_connection->setCoalesceIncoming(true);

    m_waitingOnBusyCore = false;
    if (m_coreLink != CoreLink::NotResponding && m_coreLink != CoreLink::Refused)
        setCoreLink(CoreLink::Handshaking);
    performHandshake();
    m_handshakeTimer.start(m_handshakeTimeoutMs);
}

void IpcClient::onMessageReceived(const IpcMessage& msg)
{
    if (thePrefs.enableIpcLog())
        emit ipcLogMessage(
            QStringLiteral("%1 [seq=%2]").arg(ipcMsgTypeName(msg.type())).arg(msg.seqId()),
            /*outgoing=*/false);
    m_waitingOnBusyCore = false;   // it answers again: the next timeout is judged afresh

    // Check if this is a handshake response
    if (!m_handshaked && msg.type() == IpcMsgType::HandshakeOk) {
        m_handshakeTimer.stop();
        m_handshaked = true;
        m_keepaliveTimer.start(m_keepaliveIntervalMs);
        m_connectAttempts = 0;
        m_refusalText.clear();
        // Fields 3, 4: the core is still reading its files, and which
        m_loadStage = msg.fieldString(4);
        // Field 5: the core's pid; 0 from an older core
        m_corePid = msg.fieldInt(5);
        setCoreLink(msg.fieldBool(3) ? CoreLink::Loading : CoreLink::Ready);
        logInfo(QStringLiteral("IPC handshake complete — daemon version: %1")
                    .arg(msg.fieldString(0)));

        // Subscribe to all events
        IpcMessage sub(IpcMsgType::Subscribe, m_nextSeqId++);
        sub.append(qint64(0xFFFF));  // Subscribe to all event types
        m_connection->sendMessage(sub);

        // Field 2: daemon session token.
        // On token change (GUI restart or daemon restart), reset per-type IDs to 0
        // so the full daemon buffer is replayed. On reconnect within the same GUI
        // session (same token), IDs are kept so only missed events are fetched.
        const QString newToken = msg.fieldString(2);
        if (newToken != m_daemonToken) {
            m_lastKadId = m_lastUsenetId = m_lastServerId = m_lastLogId = m_lastVerboseId = 0;
            m_lastServerMsgId = 0;
            m_daemonToken = newToken;
        }
        // Delay the initial log sync by 500 ms so any brief startup instability
        // has settled before we fetch the buffer.
        QTimer::singleShot(500, this, [this]() {
            if (m_handshaked) {
                requestLogSync();
                requestServerMessages();
            }
        });

        m_restartDeadline = QDeadlineTimer();
        m_startDeadline = QDeadlineTimer();
        emit connected();
        return;
    }

    // The handshake was turned down (wrong or missing token, version). The core
    // closes right after; without this the only trace was a later timeout.
    if (!m_handshaked && msg.type() == IpcMsgType::Error) {
        m_refusalText = msg.fieldString(1);
        logWarning(QStringLiteral("Core refused the connection: %1").arg(m_refusalText));
        setCoreLink(CoreLink::Refused);
        return;
    }

    // Check for response to a pending request
    const int seqId = msg.seqId();
    if (seqId > 0) {
        auto it = m_pendingCallbacks.find(seqId);
        if (it != m_pendingCallbacks.end()) {
            // Taken out of the map and onto the stack *before* it runs. A callback
            // that opens a modal dialog — several do — spins a nested event loop,
            // and the replies that arrive during it re-enter here: an insert can
            // rehash this unordered_map and leave `it` dangling, a clear() on
            // disconnect would destroy the std::function mid-call, and a nested
            // erase of the same seqId would make the erase below a second one.
            ResponseCallback callback = std::move(it->second);
            m_pendingCallbacks.erase(it);
            if (callback)
                callback(msg);
            return;
        }
    }

    // Push event (seqId == 0)
    if (seqId == 0)
        dispatchPushEvent(msg);
}

void IpcClient::onConnectionLost()
{
    m_handshakeTimer.stop();
    m_keepaliveTimer.stop();
    m_keepaliveTimeoutTimer.stop();
    m_handshaked = false;
    failPendingRequests();
    // The core went away on its own; a timeout or a refusal has set its state already
    if (m_coreLink == CoreLink::Ready || m_coreLink == CoreLink::Loading
        || m_coreLink == CoreLink::Handshaking)
        setCoreLink(m_autoReconnect ? CoreLink::Connecting : CoreLink::Offline);
    emit disconnected();

    scheduleReconnect();
}

void IpcClient::onSocketError()
{
    failConnect(m_socket ? m_socket->errorString() : QStringLiteral("Unknown error"));
}

void IpcClient::attemptReconnect()
{
    if (!m_autoReconnect || m_port == 0)
        return;

    if (!daemonStarting()) {
        const QString display = m_hostname.isEmpty() ? m_address.toString() : m_hostname;
        logInfo(QStringLiteral("Reconnecting to daemon at %1:%2...")
                    .arg(display).arg(m_port));
    }

    beginConnect();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void IpcClient::performHandshake()
{
    IpcMessage handshake(IpcMsgType::Handshake, m_nextSeqId++);
    handshake.append(QString::fromLatin1(ProtocolVersion));
    if (!m_authToken.isEmpty())
        handshake.append(m_authToken);
    m_connection->sendMessage(handshake);

    // Enable encryption BEFORE the response arrives so we can decrypt HandshakeOk
    if (!m_authToken.isEmpty())
        m_connection->setEncryptionKey(deriveAesKey(m_authToken));
}

void IpcClient::dispatchPushEvent(const IpcMessage& msg)
{
    switch (msg.type()) {
    case IpcMsgType::PushStatsUpdate:      emit statsUpdated(msg); break;
    case IpcMsgType::PushDownloadUpdate:   emit downloadUpdated(msg); break;
    case IpcMsgType::PushDownloadAdded:    emit downloadAdded(msg); break;
    case IpcMsgType::PushDownloadRemoved:  emit downloadRemoved(msg); break;
    case IpcMsgType::PushNotifierEvent:    emit notifierEvent(msg); break;
    case IpcMsgType::PushServerState:      emit serverStateChanged(msg); break;
    case IpcMsgType::PushServerMessage: {
        // Track the checkpoint here so a live push and a replayed backlog entry
        // advance the same counter — a reconnect must not re-show either.
        if (const qint64 id = msg.fieldInt(0); id > m_lastServerMsgId)
            m_lastServerMsgId = id;
        emit serverMessageReceived(msg);
        break;
    }
    case IpcMsgType::PushSearchResult:     emit searchResultReceived(msg); break;
    case IpcMsgType::PushGlobalSearchProgress: emit globalSearchProgress(msg); break;
    case IpcMsgType::PushSearchState:     emit searchStateChanged(msg); break;
    case IpcMsgType::PushCoreLoading:
        if (m_handshaked) {
            m_loadStage = msg.fieldString(1);
            const CoreLink link = msg.fieldBool(0) ? CoreLink::Loading : CoreLink::Ready;
            if (link == m_coreLink)
                emit coreLinkChanged();   // same state, another stage
            else
                setCoreLink(link);
        }
        break;
    case IpcMsgType::PushCoreRestarting:
        expectRestart();
        break;
    case IpcMsgType::PushLogBatch:
        replayLogEntries(msg.fieldArray(0));
        break;
    case IpcMsgType::PushLogMessage: {
        const qint64 logId  = msg.fieldInt(0);
        const QString cat    = msg.fieldString(1);
        const auto severity  = static_cast<QtMsgType>(msg.fieldInt(2));

        const bool isKad     = (cat == QStringLiteral("emule.kad"));
        const bool isUsenet  = (cat == QStringLiteral("emule.usenet"));
        const bool isServer  = (cat == QStringLiteral("emule.server"));
        const bool isVerbose = (severity == QtDebugMsg);

        int64_t& typeId = isKad    ? m_lastKadId
                        : isUsenet ? m_lastUsenetId
                        : isServer ? m_lastServerId
                        : isVerbose? m_lastVerboseId
                        :            m_lastLogId;
        if (logId > typeId)
            typeId = logId;

        emit logMessageReceived(msg);
        break;
    }
    case IpcMsgType::PushIndexerResults:   emit indexerResultsReceived(msg); break;
    case IpcMsgType::PushIndexerProgress:  emit indexerSearchProgress(msg); break;
    case IpcMsgType::PushIndexerSearchDone: emit indexerSearchFinished(msg); break;
    case IpcMsgType::PushIndexerFeedStatus: emit indexerFeedStatus(msg); break;
    case IpcMsgType::PushUsenetQueueItem:  emit usenetItemUpdated(msg); break;
    case IpcMsgType::PushUsenetItemRemoved: emit usenetItemRemoved(msg); break;
    case IpcMsgType::PushUsenetItemFinished: emit usenetItemFinished(msg); break;
    case IpcMsgType::PushUsenetEngineState:
        setUsenetEnginePaused(msg.fieldMap(0).value(QStringLiteral("paused")).toBool());
        break;
    case IpcMsgType::PushTorrentAdded:     emit torrentAdded(msg); break;
    case IpcMsgType::PushTorrentUpdate:    emit torrentUpdated(msg); break;
    case IpcMsgType::PushTorrentRemoved:   emit torrentRemoved(msg); break;
    case IpcMsgType::PushTorrentFinished:  emit torrentFinished(msg); break;
    case IpcMsgType::PushBtStatus:         emit btStatusChanged(msg); break;
    case IpcMsgType::PushEd3kStatus:       emit ed3kStatusChanged(msg); break;
    case IpcMsgType::PushSharedFileUpdate: emit sharedFileUpdated(msg); break;
    case IpcMsgType::PushSharedFileRemoved: emit sharedFileRemoved(msg); break;
    case IpcMsgType::PushUploadUpdate:     emit uploadUpdated(msg); break;
    case IpcMsgType::PushKadUpdate:        emit kadUpdated(msg); break;
    case IpcMsgType::PushKadSearchesChanged: emit kadSearchesChanged(msg); break;
    case IpcMsgType::PushKnownClientsChanged: emit knownClientsChanged(msg); break;
    case IpcMsgType::PushCategoriesChanged:   emit categoriesChanged(msg); break;
    case IpcMsgType::PushChatMessage:        emit chatMessageReceived(msg); break;
    case IpcMsgType::PushChatState:          emit chatStateReceived(msg); break;
    case IpcMsgType::PushChatCaptcha:        emit chatCaptchaReceived(msg); break;
    case IpcMsgType::PushChatCaptchaResult:  emit chatCaptchaResultReceived(msg); break;
    case IpcMsgType::PushFriendListChanged:  emit friendListChanged(msg); break;
    case IpcMsgType::PushClientSharedFiles:  emit clientSharedFilesReceived(msg); break;
    case IpcMsgType::PushSearchPreview:      emit searchPreviewReceived(msg); break;
    case IpcMsgType::PushPortMapStatus:      emit portMapStatusChanged(msg); break;
    default: break;
    }
}

void IpcClient::requestLogSync()
{
    // Request from the minimum of all per-type IDs:
    //  - Fresh GUI start: all IDs == currentMax → min == currentMax → empty response.
    //  - Daemon restart: all IDs == 0 → min == 0 → full new-daemon buffer shown.
    //  - Reconnect within session: min of per-type IDs → only missed events fetched.
    const int64_t fromId = std::min({m_lastKadId, m_lastUsenetId, m_lastServerId,
                                     m_lastLogId,  m_lastVerboseId});
    IpcMessage req(IpcMsgType::SyncLogs);
    req.append(static_cast<qint64>(fromId));

    sendRequest(std::move(req), [this](const IpcMessage& resp) {
        replayLogEntries(resp.fieldArray(1));
    });
}

void IpcClient::replayLogEntries(const QCborArray& entries)
{
    if (entries.isEmpty())
        return;

    // Bracketed so the log view lays out and repaints once for the lot
    emit logBatchStarted();
    for (int i = 0; i < entries.size(); ++i) {
        const QCborArray entry = entries[i].toArray();
        if (entry.size() < 4)
            continue;

        const qint64 logId   = entry[0].toInteger();
        const QString cat     = entry[1].toString();
        const auto severity   = static_cast<QtMsgType>(entry[2].toInteger());

        // Select per-type checkpoint; skip if already seen for this type
        const bool isKad     = (cat == QStringLiteral("emule.kad"));
        const bool isUsenet  = (cat == QStringLiteral("emule.usenet"));
        const bool isServer  = (cat == QStringLiteral("emule.server"));
        const bool isVerbose = (severity == QtDebugMsg);

        int64_t& typeId = isKad    ? m_lastKadId
                        : isUsenet ? m_lastUsenetId
                        : isServer ? m_lastServerId
                        : isVerbose? m_lastVerboseId
                        :            m_lastLogId;
        if (logId <= typeId)
            continue;   // already displayed
        typeId = logId;

        // Build synthetic PushLogMessage for uniform GUI handling
        IpcMessage synth(IpcMsgType::PushLogMessage, 0);
        synth.append(logId);
        synth.append(cat);
        synth.append(static_cast<qint64>(entry[2].toInteger()));  // severity
        synth.append(entry[3].toString());                       // message
        synth.append(entry.size() >= 5 ? entry[4].toInteger() : qint64(0));
        emit logMessageReceived(synth);
    }
    emit logBatchFinished();
}

void IpcClient::requestServerMessages()
{
    IpcMessage req(IpcMsgType::GetServerMessages);
    req.append(static_cast<qint64>(m_lastServerMsgId));

    sendRequest(std::move(req), [this](const IpcMessage& resp) {
        const QCborArray entries = resp.fieldArray(1);
        for (int i = 0; i < entries.size(); ++i) {
            const QCborArray entry = entries[i].toArray();
            if (entry.size() < 3)
                continue;

            const qint64 id = entry[0].toInteger();
            if (id <= m_lastServerMsgId)
                continue;   // already displayed
            m_lastServerMsgId = id;

            // Replay as a synthetic push so the GUI has a single handling path.
            IpcMessage synth(IpcMsgType::PushServerMessage, 0);
            synth.append(id);
            synth.append(entry[1].toInteger());
            synth.append(entry[2].toString());
            emit serverMessageReceived(synth);
        }
    });
}

void IpcClient::sendKeepalive()
{
    sendRequest(IpcMessage(IpcMsgType::Ping), [this](const IpcMessage&) {
        m_keepaliveTimeoutTimer.stop();
    });
    m_keepaliveTimeoutTimer.start(m_keepaliveTimeoutMs);
}

void IpcClient::scheduleReconnect()
{
    if (!m_autoReconnect || m_reconnectTimer.isActive())
        return;

    // A core we launched is still loading: ask again shortly, quietly. Backing
    // off here left the window empty for seconds after the port had opened.
    if (daemonStarting()) {
        m_reconnectTimer.start(StartRetryDelayMs);
        return;
    }

    logInfo(QStringLiteral("Will retry in %1s...").arg(m_reconnectDelayMs / 1000));
    m_reconnectTimer.start(m_reconnectDelayMs);

    // Exponential backoff: 1s → 2s → 4s → 8s → 16s → 30s
    m_reconnectDelayMs = std::min(m_reconnectDelayMs * 2, MaxReconnectDelay);
}

void IpcClient::failPendingRequests()
{
    // They used to be dropped: a batch never settled and anything waiting on a reply
    // waited forever. Deferred so no callback runs inside the socket teardown (one may
    // open a dialog), and skipped entirely if this client is destroyed first.
    m_polls.clear();   // their replies will not come; the callbacks fail below
    if (m_pendingCallbacks.empty())
        return;
    using Pending = std::vector<std::pair<int, ResponseCallback>>;
    auto pending = std::make_shared<Pending>(std::make_move_iterator(m_pendingCallbacks.begin()),
                                             std::make_move_iterator(m_pendingCallbacks.end()));
    m_pendingCallbacks = {};
    std::ranges::sort(*pending, {}, &Pending::value_type::first);   // in send order

    QTimer::singleShot(0, this, [pending] {
        for (auto& [seqId, callback] : *pending) {
            if (callback)
                callback(IpcMessage{});
        }
    });
}

void IpcClient::failConnect(const QString& error)
{
    emit connectionFailed(error);

    // Clean up the failed socket so scheduleReconnect starts fresh.
    //
    // Disconnect *before* deleteLater, exactly as resetConnection() does.
    // deleteLater is deferred, so the socket outlives this slot and stays wired
    // to onSocketConnected() — and QAbstractSocket can still emit connected()
    // afterwards from the same fetchConnectionParameters() pass. That reached
    // onSocketConnected() with m_socket already null and crashed in
    // IpcConnection's constructor, which dereferences the socket it is handed.
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr);
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    scheduleReconnect();
}

void IpcClient::resetConnection()
{
    m_handshakeTimer.stop();
    m_keepaliveTimer.stop();
    m_keepaliveTimeoutTimer.stop();
    m_handshaked = false;
    failPendingRequests();

    dropConnection();
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void IpcClient::dropConnection()
{
    if (!m_connection)
        return;
    // Disconnect all signals before closing so that close() → disconnectFromHost()
    // does not cascade into onConnectionLost() → scheduleReconnect() while we are
    // already inside attemptReconnect(), which would queue a second reconnect on
    // top of the one being set up right now.
    disconnect(m_connection.get(), nullptr, this, nullptr);
    m_connection->close();
    // Not destroyed here: a quit while a prompt opened by a message handler is up
    // arrives with this socket's readyRead still on the stack below the prompt.
    // Stays our child, so it goes with us if the loop never turns again.
    m_connection.release()->deleteLater();
}

void IpcClient::beginConnect()
{
    resetConnection();

    ++m_connectAttempts;
    // NotResponding and Refused stay up until a handshake goes through: the retry
    // in between is not news, and flipping back to "Connecting" would hide them.
    if (m_coreLink != CoreLink::NotResponding && m_coreLink != CoreLink::Refused)
        setCoreLink(daemonStarting() ? CoreLink::Starting : CoreLink::Connecting);
    else
        emit coreLinkChanged();   // the attempt count moved

    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected,
            this, &IpcClient::onSocketConnected);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &IpcClient::onSocketError);

    if (m_hostname.isEmpty())
        m_socket->connectToHost(m_address, m_port);
    else
        m_socket->connectToHost(m_hostname, m_port);
}

void IpcClient::setCoreLink(CoreLink link)
{
    if (link == m_coreLink)
        return;
    m_coreLink = link;
    if (link != CoreLink::Loading)
        m_loadStage.clear();
    emit coreLinkChanged();
}

bool IpcClient::peerStillTalking() const
{
    return m_connection && m_connection->isConnected() && m_connection->hasPendingInput();
}

bool IpcClient::waitOnBusyCore(bool handshake)
{
    // Remote: a silent link may be a dead path, and only a reconnect finds out.
    // Locally a core that is gone resets the socket at once.
    if (!m_connection || !m_connection->isConnected() || !m_address.isLoopback())
        return false;
    // Handshake: a core we launched or restarted (its load stages block the loop
    // for seconds with a large share). Keepalive: a core that said it is loading.
    const bool busy = m_waitingOnBusyCore
                   || (handshake ? daemonStarting() || daemonRestarting()
                                 : m_coreLink == CoreLink::Loading);
    if (!busy)
        return false;
    if (!m_waitingOnBusyCore) {
        logInfo(handshake ? QStringLiteral("Core is busy starting up, still waiting for its answer")
                          : QStringLiteral("Core is busy loading its files, still waiting for its answer"));
        m_waitingOnBusyCore = true;
    }
    return true;
}

void IpcClient::expectRestart()
{
    // Time for a graceful stop (saving part files) plus the start
    constexpr int kRestartGraceMs = 60'000;
    m_restartDeadline = QDeadlineTimer(kRestartGraceMs);
}

} // namespace eMule
