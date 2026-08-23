/// @file IpcClientHandler.cpp
/// @brief Per-connection IPC request handler — implementation.

#include "IpcClientHandler.h"
#include "CoreNotifierBridge.h"
#include "DaemonApp.h"

#include "ipc/CborSerializers.h"
#include "webserver/WebServer.h"

#include <QDir>
#include <QHostInfo>

#include "app/AppContext.h"
#include "app/CoreSession.h"
#include "files/Collection.h"
#include "files/CollectionFile.h"
#include "files/CollectionKeys.h"
#include "ipfilter/IPFilter.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/PartFileConvert.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadUDPListener.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadLookupHistory.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadIndexed.h"
#include "kademlia/KadMiscUtils.h"
#include "httpcache/HttpCacheServerProbe.h"
#include "portmap/PortMapper.h"
#include "kademlia/KadPrefs.h"
#include "net/ListenSocket.h"
#include "prefs/Preferences.h"
#include "net/Packet.h"
#include "protocol/ED2KLink.h"
#include "search/GlobalSearchScheduler.h"
#include "search/SearchExpr.h"
#include "search/SearchExprParser.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "stats/StatsHistory.h"
#include "stats/StatsSnapshot.h"
#include "transfer/DownloadQueue.h"
#include "transfer/Scheduler.h"
#include "transfer/UploadQueue.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHostAddress>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QStorageInfo>


namespace eMule {

using namespace Ipc;

namespace {

/// Snapshot the connectivity that decides which network an Automatic search uses.
/// Kept separate from resolveAutomaticSearchType() so the rule itself stays free of
/// theApp and can be unit-tested without a network stack.
AutoSearchState gatherAutoSearchState()
{
    AutoSearchState state;

    const Server* server = nullptr;
    if (theApp.serverConnect && theApp.serverConnect->isConnected()) {
        state.serverConnected = true;
        server = theApp.serverConnect->currentServer();
    }
    if (server) {
        state.serverIsStatic = server->isStaticMember();
        state.serverUsers = server->users();
        state.serverFiles = server->files();
    }

    const auto* kadInst = kad::Kademlia::instance();
    state.kadConnected = kadInst != nullptr && kadInst->isRunning() && kadInst->isConnected();

    state.serverCount = theApp.serverList ? theApp.serverList->serverCount() : 0;
    return state;
}

/// Open a filesystem path with the OS default handler. This runs from the
/// headless daemon (a QCoreApplication), where QDesktopServices::openUrl has no
/// QPA platform backend and silently fails — so invoke the platform opener
/// directly (matches the QProcess::startDetached pattern used elsewhere).
bool openPathWithDefaultApp(const QString& path)
{
#if defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("open"), {path});
#elif defined(Q_OS_WIN)
    return QProcess::startDetached(QStringLiteral("cmd"),
                                   {QStringLiteral("/c"), QStringLiteral("start"),
                                    QString(), QDir::toNativeSeparators(path)});
#else
    return QProcess::startDetached(QStringLiteral("xdg-open"), {path});
#endif
}

/// Resolve the server a request refers to. Server-keyed requests carry the numeric IP in
/// field @p ipField and the port in @p ipField+1; newer clients append the literal address
/// at @p addrField, which is the only form that can identify an IPv6 server (its uint32
/// projection is 0). Returns nullptr when the server is unknown or the list is unavailable.
Server* resolveServerFromMsg(const IpcMessage& msg, int ipField, int addrField)
{
    if (!theApp.serverList)
        return nullptr;

    const auto port = static_cast<uint16>(msg.fieldInt(ipField + 1));

    if (addrField >= 0) {
        const QString addrStr = msg.fieldString(addrField);
        const Address addr = Address::fromString(addrStr);
        if (!addr.isNull())
            return theApp.serverList->findByIPTcp(addr, port);
        // Not a literal: a dynIP server identified by its hostname.
        if (!addrStr.isEmpty()) {
            if (Server* srv = theApp.serverList->findByAddress(addrStr, port))
                return srv;
        }
    }
    return theApp.serverList->findByIPTcp(static_cast<uint32>(msg.fieldInt(ipField)), port);
}

/// Determine the KnownType for a search result by checking live subsystems.
/// Priority matches MFC eMule: downloading > shared > downloaded > cancelled.
SearchFile::KnownType determineKnownType(const uint8* hash)
{
    if (theApp.downloadQueue && theApp.downloadQueue->fileByID(hash))
        return SearchFile::KnownType::Downloading;
    if (theApp.sharedFileList && theApp.sharedFileList->getFileByID(hash))
        return SearchFile::KnownType::Shared;
    if (theApp.knownFileList && theApp.knownFileList->findKnownFileByID(hash))
        return SearchFile::KnownType::Downloaded;
    if (theApp.knownFileList && theApp.knownFileList->isCancelledFileByID(hash))
        return SearchFile::KnownType::Cancelled;
    return SearchFile::KnownType::Unknown;
}

/// Helper: decode a hex hash string to a 16-byte array, returns false on error.
bool hexToHash(const QString& hex, uint8* out)
{
    return decodeBase16(hex, out, 16) == 16;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

IpcClientHandler::IpcClientHandler(QTcpSocket* socket, bool isLocal, QObject* parent)
    : QObject(parent)
    , m_connection(std::make_unique<IpcConnection>(socket, this))
    , m_isLocal(isLocal)
{
    connect(m_connection.get(), &IpcConnection::messageReceived,
            this, &IpcClientHandler::onMessageReceived);
    connect(m_connection.get(), &IpcConnection::disconnected,
            this, &IpcClientHandler::onConnectionLost);
    connect(m_connection.get(), &IpcConnection::protocolError,
            this, [this](const QString& reason) {
                logWarning(QStringLiteral("IPC protocol error: %1").arg(reason));
                m_connection->close();
            });
}

IpcClientHandler::~IpcClientHandler() = default;

void IpcClientHandler::sendMessage(const IpcMessage& msg)
{
    m_connection->sendMessage(msg);
    if (auto* sock = m_connection->socket())
        sock->flush();
}

bool IpcClientHandler::isHandshaked() const
{
    return m_handshaked;
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void IpcClientHandler::onMessageReceived(const IpcMessage& msg)
{
    // Require handshake first for all non-handshake messages
    if (!m_handshaked && msg.type() != IpcMsgType::Handshake) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 401,
            QStringLiteral("Handshake required")));
        return;
    }

    switch (msg.type()) {
    case IpcMsgType::Handshake:            handleHandshake(msg); break;
    case IpcMsgType::Ping:
        sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue{}));
        break;
    case IpcMsgType::GetDownloads:         handleGetDownloads(msg); break;
    case IpcMsgType::GetDownload:          handleGetDownload(msg); break;
    case IpcMsgType::PauseDownload:        handlePauseDownload(msg); break;
    case IpcMsgType::ResumeDownload:       handleResumeDownload(msg); break;
    case IpcMsgType::CancelDownload:       handleCancelDownload(msg); break;
    case IpcMsgType::GetUploads:           handleGetUploads(msg); break;
    case IpcMsgType::GetDownloadClients:   handleGetDownloadClients(msg); break;
    case IpcMsgType::GetKnownClients:      handleGetKnownClients(msg); break;
    case IpcMsgType::SetDownloadPriority:  handleSetDownloadPriority(msg); break;
    case IpcMsgType::ClearCompleted:       handleClearCompleted(msg); break;
    case IpcMsgType::GetDownloadSources:   handleGetDownloadSources(msg); break;
    case IpcMsgType::GetServers:           handleGetServers(msg); break;
    case IpcMsgType::RemoveServer:         handleRemoveServer(msg); break;
    case IpcMsgType::RemoveAllServers:     handleRemoveAllServers(msg); break;
    case IpcMsgType::SetServerPriority:    handleSetServerPriority(msg); break;
    case IpcMsgType::SetServerStatic:      handleSetServerStatic(msg); break;
    case IpcMsgType::AddServer:            handleAddServer(msg); break;
    case IpcMsgType::SetServerOrder:       handleSetServerOrder(msg); break;
    case IpcMsgType::GetConnection:        handleGetConnection(msg); break;
    case IpcMsgType::ConnectToServer:      handleConnectToServer(msg); break;
    case IpcMsgType::DisconnectFromServer: handleDisconnectFromServer(msg); break;
    case IpcMsgType::StartSearch:          handleStartSearch(msg); break;
    case IpcMsgType::GetSearchResults:     handleGetSearchResults(msg); break;
    case IpcMsgType::StopSearch:           handleStopSearch(msg); break;
    case IpcMsgType::RemoveSearch:         handleRemoveSearch(msg); break;
    case IpcMsgType::ClearAllSearches:     handleClearAllSearches(msg); break;
    case IpcMsgType::DownloadSearchFile:   handleDownloadSearchFile(msg); break;
    case IpcMsgType::GetKnownTypes:        handleGetKnownTypes(msg); break;
    case IpcMsgType::GetSharedFiles:       handleGetSharedFiles(msg); break;
    case IpcMsgType::SetSharedFilePriority: handleSetSharedFilePriority(msg); break;
    case IpcMsgType::ReloadSharedFiles: handleReloadSharedFiles(msg); break;
    case IpcMsgType::GetEd2kLink:       handleGetEd2kLink(msg); break;
    case IpcMsgType::GetFriends:           handleGetFriends(msg); break;
    case IpcMsgType::AddFriend:            handleAddFriend(msg); break;
    case IpcMsgType::RemoveFriend:         handleRemoveFriend(msg); break;
    case IpcMsgType::SendChatMessage:      handleSendChatMessage(msg); break;
    case IpcMsgType::SetFriendSlot:        handleSetFriendSlot(msg); break;
    case IpcMsgType::GetStats:             handleGetStats(msg); break;
    case IpcMsgType::GetSpeedHistory:      handleGetSpeedHistory(msg); break;
    case IpcMsgType::GetStatsHistory:      handleGetStatsHistory(msg); break;
    case IpcMsgType::GetPreferences:       handleGetPreferences(msg); break;
    case IpcMsgType::SetPreferences:       handleSetPreferences(msg); break;
    case IpcMsgType::Subscribe:            handleSubscribe(msg); break;
    case IpcMsgType::GetKadContacts:       handleGetKadContacts(msg); break;
    case IpcMsgType::GetKadStatus:         handleGetKadStatus(msg); break;
    case IpcMsgType::BootstrapKad:         handleBootstrapKad(msg); break;
    case IpcMsgType::DisconnectKad:        handleDisconnectKad(msg); break;
    case IpcMsgType::GetKadSearches:       handleGetKadSearches(msg); break;
    case IpcMsgType::GetKadLookupHistory:  handleGetKadLookupHistory(msg); break;
    case IpcMsgType::GetNetworkInfo:       handleGetNetworkInfo(msg); break;
    case IpcMsgType::RecheckFirewall:      handleRecheckFirewall(msg); break;
    case IpcMsgType::SyncLogs:             handleSyncLogs(msg); break;
    case IpcMsgType::Shutdown:             handleShutdown(msg); break;
    case IpcMsgType::ReloadIPFilter:       handleReloadIPFilter(msg); break;
    case IpcMsgType::GetSchedules:         handleGetSchedules(msg); break;
    case IpcMsgType::SaveSchedules:        handleSaveSchedules(msg); break;
    case IpcMsgType::ScanImportFolder:     handleScanImportFolder(msg); break;
    case IpcMsgType::GetConvertJobs:       handleGetConvertJobs(msg); break;
    case IpcMsgType::RemoveConvertJob:     handleRemoveConvertJob(msg); break;
    case IpcMsgType::RetryConvertJob:      handleRetryConvertJob(msg); break;
    case IpcMsgType::StopDownload:         handleStopDownload(msg); break;
    case IpcMsgType::OpenDownloadFile:     handleOpenDownloadFile(msg); break;
    case IpcMsgType::OpenDownloadFolder:   handleOpenDownloadFolder(msg); break;
    case IpcMsgType::MarkSearchSpam:       handleMarkSearchSpam(msg); break;
    case IpcMsgType::ResetStats:           handleResetStats(msg); break;
    case IpcMsgType::RestoreStats:         handleRestoreStats(msg); break;
    case IpcMsgType::ProbeHttpCacheServer: handleProbeHttpCacheServer(msg); break;
    case IpcMsgType::ApplyHttpCacheConfig: handleApplyHttpCacheConfig(msg); break;
    case IpcMsgType::RenameSharedFile:     handleRenameSharedFile(msg); break;
    case IpcMsgType::DeleteSharedFile:     handleDeleteSharedFile(msg); break;
    case IpcMsgType::UnshareFile:          handleUnshareFile(msg); break;
    case IpcMsgType::SetFileShared:        handleSetFileShared(msg); break;
    case IpcMsgType::BrowseDirectory:      handleBrowseDirectory(msg); break;
    case IpcMsgType::SetDownloadCategory:  handleSetDownloadCategory(msg); break;
    case IpcMsgType::GetDownloadDetails:   handleGetDownloadDetails(msg); break;
    case IpcMsgType::PreviewDownload:      handlePreviewDownload(msg); break;
    case IpcMsgType::RequestClientSharedFiles: handleRequestClientSharedFiles(msg); break;
    case IpcMsgType::GetClientDetails:   handleGetClientDetails(msg); break;
    case IpcMsgType::GetSharedFileDetails: handleGetSharedFileDetails(msg); break;
    case IpcMsgType::GetSearchResultDetails: handleGetSearchResultDetails(msg); break;
    case IpcMsgType::GetServerState:      handleGetServerState(msg); break;
    case IpcMsgType::GetServerMessages:   handleGetServerMessages(msg); break;
    case IpcMsgType::SearchKadNotes:      handleSearchKadNotes(msg); break;
    case IpcMsgType::GetCollectionInfo:  handleGetCollectionInfo(msg); break;
    case IpcMsgType::SaveCollection:     handleSaveCollection(msg); break;
    default:
        sendMessage(IpcMessage::makeError(msg.seqId(), 400,
            QStringLiteral("Unknown message type: %1").arg(static_cast<int>(msg.type()))));
        break;
    }
}

void IpcClientHandler::onConnectionLost()
{
    m_handshaked = false;  // Prevent broadcast() from writing to this dying connection
    emit disconnected(this);
}

// ---------------------------------------------------------------------------
// Request handlers
// ---------------------------------------------------------------------------

void IpcClientHandler::handleHandshake(const IpcMessage& msg)
{
    const QString version = msg.fieldString(0);
    logInfo(QStringLiteral("IPC handshake from client, version: %1").arg(version));

    // Authenticate: required for remote clients, optional for localhost.
    // A local client that sends a token (e.g. Docker port-forward) gets validated + encrypted.
    const QString clientToken = msg.fieldString(1);
    if (!m_isLocal || !clientToken.isEmpty()) {
        const QStringList tokens = thePrefs.ipcTokens();
        if (tokens.isEmpty() || !tokens.contains(clientToken)) {
            logWarning(QStringLiteral("IPC auth failed — invalid token"));
            sendMessage(IpcMessage::makeError(msg.seqId(), 403,
                QStringLiteral("Authentication failed")));
            m_connection->close();
            return;
        }
        m_connection->setEncryptionKey(deriveAesKey(clientToken));
    }

    m_handshaked = true;

    IpcMessage reply(IpcMsgType::HandshakeOk, msg.seqId());
    reply.append(QString::fromLatin1(ProtocolVersion));
    reply.append(QStringLiteral("eMule Core Daemon"));
    // Field 2: daemon session token — random UUID per daemon process lifetime.
    // GUI uses this to detect daemon restarts and reset its per-type log checkpoints.
    reply.append(DaemonApp::sessionToken());
    sendMessage(reply);
}

void IpcClientHandler::handleGetDownloads(const IpcMessage& msg)
{
    QCborArray files;
    if (theApp.downloadQueue) {
        for (const auto* pf : theApp.downloadQueue->files())
            files.append(toCbor(*pf));
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(files)));
}

void IpcClientHandler::handleGetDownload(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    const auto* pf = theApp.downloadQueue->fileByID(hashBuf);
    if (!pf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Download not found")));
        return;
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(toCbor(*pf))));
}

void IpcClientHandler::handlePauseDownload(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* pf = theApp.downloadQueue->fileByID(hashBuf);
    if (!pf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Download not found")));
        return;
    }
    pf->pauseFile();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleResumeDownload(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* pf = theApp.downloadQueue->fileByID(hashBuf);
    if (!pf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Download not found")));
        return;
    }
    pf->resumeFile();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleCancelDownload(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* pf = theApp.downloadQueue->fileByID(hashBuf);
    if (!pf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Download not found")));
        return;
    }
    if (thePrefs.rememberCancelledFiles() && theApp.knownFileList)
        theApp.knownFileList->addCancelledFileID(pf->fileHash());
    pf->stopFile(true);
    theApp.downloadQueue->removeFile(pf);
    // A *completed* download was handed to KnownFileList/SharedFileList, which
    // then hold non-owning references to it. Cancelling removes the file entirely,
    // so unlink it from both before freeing — otherwise the freed pointer would
    // dangle in their maps and crash the next known.met save (KnownFile::writeToFile).
    if (theApp.knownFileList)
        theApp.knownFileList->remove(pf);
    if (theApp.sharedFileList)
        theApp.sharedFileList->removeFile(pf);
    delete pf;
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleGetUploads(const IpcMessage& msg)
{
    QCborArray uploading;
    QCborArray waiting;
    if (theApp.uploadQueue) {
        theApp.uploadQueue->forEachUploading([&](UpDownClient* c) {
            uploading.append(toCbor(*c));
        });
        theApp.uploadQueue->forEachWaiting([&](UpDownClient* c) {
            waiting.append(toCbor(*c));
        });
    }
    QCborMap result;
    result.insert(QStringLiteral("uploading"), uploading);
    result.insert(QStringLiteral("waiting"), waiting);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(result)));
}

void IpcClientHandler::handleGetDownloadClients(const IpcMessage& msg)
{
    QCborArray clients;
    if (theApp.downloadQueue) {
        for (const auto* pf : theApp.downloadQueue->files()) {
            for (const auto* c : pf->srcList())
                clients.append(toCbor(*c));
        }
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(clients)));
}

void IpcClientHandler::handleGetDownloadSources(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    const auto* pf = theApp.downloadQueue->fileByID(hashBuf);
    if (!pf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Download not found")));
        return;
    }

    QCborArray clients;
    for (const auto* c : pf->srcList())
        clients.append(toCbor(*c));
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(clients)));
}

void IpcClientHandler::handleGetKnownClients(const IpcMessage& msg)
{
    QCborArray clients;
    if (theApp.clientList) {
        theApp.clientList->forEachClient([&](UpDownClient* c) {
            clients.append(toCbor(*c));
        });
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(clients)));
}

void IpcClientHandler::handleSetDownloadPriority(const IpcMessage& msg)
{
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    const QString hash = msg.fieldString(0);
    const auto priority = static_cast<uint8>(msg.fieldInt(1));
    const bool isAuto = msg.fieldBool(2);

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* pf = theApp.downloadQueue->fileByID(hashBuf);
    if (!pf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Download not found")));
        return;
    }
    pf->setAutoDownPriority(isAuto);
    if (!isAuto)
        pf->setDownPriority(priority);
    else
        pf->updateAutoDownPriority();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleClearCompleted(const IpcMessage& msg)
{
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    // Collect completed files first, then remove (avoid modifying during iteration)
    std::vector<PartFile*> completed;
    for (auto* pf : theApp.downloadQueue->files()) {
        if (pf->status() == PartFileStatus::Complete)
            completed.push_back(pf);
    }
    for (auto* pf : completed)
        theApp.downloadQueue->removeFile(pf);

    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleGetServers(const IpcMessage& msg)
{
    QCborArray servers;
    if (theApp.serverList) {
        for (const auto& srv : theApp.serverList->servers())
            servers.append(toCbor(*srv));
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(servers)));
}

void IpcClientHandler::handleRemoveServer(const IpcMessage& msg)
{
    if (!theApp.serverList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerList unavailable")));
        return;
    }
    auto* srv = resolveServerFromMsg(msg, 0, 2);
    if (!srv) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Server not found")));
        return;
    }
    theApp.serverList->removeServer(srv);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleRemoveAllServers(const IpcMessage& msg)
{
    if (!theApp.serverList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerList unavailable")));
        return;
    }
    theApp.serverList->removeAllServers();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleSetServerPriority(const IpcMessage& msg)
{
    if (!theApp.serverList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerList unavailable")));
        return;
    }
    const auto prio = static_cast<int>(msg.fieldInt(2));
    auto* srv = resolveServerFromMsg(msg, 0, 3);
    if (!srv) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Server not found")));
        return;
    }
    srv->setPreference(static_cast<ServerPriority>(prio));
    if (srv->isStaticMember())
        theApp.serverList->saveStaticServers(
            QDir(thePrefs.configDir()).filePath(QStringLiteral("staticservers.dat")));
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleSetServerStatic(const IpcMessage& msg)
{
    if (!theApp.serverList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerList unavailable")));
        return;
    }
    const bool isStatic = msg.fieldBool(2);
    auto* srv = resolveServerFromMsg(msg, 0, 3);
    if (!srv) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Server not found")));
        return;
    }
    srv->setStaticMember(isStatic);
    theApp.serverList->saveStaticServers(
        QDir(thePrefs.configDir()).filePath(QStringLiteral("staticservers.dat")));
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleAddServer(const IpcMessage& msg)
{
    if (!theApp.serverList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerList unavailable")));
        return;
    }
    const QString address = msg.fieldString(0);
    const auto port = static_cast<uint16>(msg.fieldInt(1));
    const QString name = msg.fieldString(2);

    if (port == 0 || address.trimmed().isEmpty() || address.contains(u'|')) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid address or port")));
        return;
    }

    // No DNS here. fromAddressString() keeps a hostname as a dynIP, which ServerSocket
    // re-resolves on every connect (A then AAAA) — the old blocking QHostInfo::fromName
    // ran on the daemon thread, accepted IPv4 answers only, and pinned the server to a
    // one-shot address that never refreshed.
    auto server = Server::fromAddressString(address, port);
    if (!server) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid address or port")));
        return;
    }
    if (!name.isEmpty())
        server->setName(name);
    if (thePrefs.manualServerHighPriority())
        server->setPreference(ServerPriority::High);

    const QString key = server->address();
    Server* added = theApp.serverList->addServer(std::move(server));
    if (added) {
        sendMessage(IpcMessage::makeResult(msg.seqId(), true));
    } else {
        // MFC parity: update name on existing duplicate if name is meaningful
        if (!name.isEmpty() && !name.startsWith(QStringLiteral("Server"))) {
            if (auto* existing = theApp.serverList->findByAddress(key, port))
                existing->setName(name);
        }
        sendMessage(IpcMessage::makeResult(msg.seqId(), false));
    }
}

void IpcClientHandler::handleSetServerOrder(const IpcMessage& msg)
{
    if (!theApp.serverList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerList unavailable")));
        return;
    }
    // Payload field 0: a CBOR array of [ip:int64, port:int64] pairs in the desired
    // display order (#24), each optionally followed by the literal address at index 2 —
    // the only form that identifies an IPv6 server. Reorder and persist immediately.
    const QCborArray entries = msg.fieldArray(0);
    std::vector<std::pair<Address, uint16>> order;
    order.reserve(static_cast<size_t>(entries.size()));
    for (const QCborValue& e : entries) {
        const QCborArray pair = e.toArray();
        if (pair.size() < 2)
            continue;
        Address addr;
        if (pair.size() >= 3)
            addr = Address::fromString(pair.at(2).toString());
        if (addr.isNull())
            addr = Address::fromNetworkOrder(static_cast<uint32>(pair.at(0).toInteger()));
        order.emplace_back(addr, static_cast<uint16>(pair.at(1).toInteger()));
    }
    theApp.serverList->applyUserOrder(order);
    theApp.serverList->saveServerMet(
        QDir(thePrefs.configDir()).filePath(QStringLiteral("server.met")));
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleGetConnection(const IpcMessage& msg)
{
    QCborMap info;
    // ED2K-only: the GUI maps this onto its eD2K status indicator and the
    // Connect/Disconnect button — theApp.isConnected() is true for Kad too.
    info.insert(QStringLiteral("connected"),
                theApp.serverConnect && theApp.serverConnect->isConnected());
    info.insert(QStringLiteral("connecting"),
                theApp.serverConnect && theApp.serverConnect->isConnecting());
    info.insert(QStringLiteral("firewalled"), theApp.isFirewalled());
    // Per-network eD2K LowID — "firewalled" above is the combined ed2k+kad state and
    // goes false as soon as Kad is open, so it cannot drive the eD2K LowID indicators.
    info.insert(QStringLiteral("lowID"),
                theApp.serverConnect && theApp.serverConnect->isConnected()
                    && theApp.serverConnect->isLowID());
    info.insert(QStringLiteral("clientID"),   static_cast<qint64>(theApp.getID()));

    if (theApp.serverConnect) {
        const auto* srv = theApp.serverConnect->currentServer();
        if (srv)
            info.insert(QStringLiteral("server"), QCborValue(toCbor(*srv)));
    }

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(info)));
}

void IpcClientHandler::handleGetServerState(const IpcMessage& msg)
{
    QCborMap info;
    // ED2K-only — see handleGetConnection().
    bool connected = theApp.serverConnect && theApp.serverConnect->isConnected();
    info.insert(QStringLiteral("connected"),  connected);
    info.insert(QStringLiteral("connecting"),
                theApp.serverConnect && theApp.serverConnect->isConnecting());
    info.insert(QStringLiteral("firewalled"), theApp.isFirewalled());
    // Per-network eD2K LowID — see handleGetConnection().
    info.insert(QStringLiteral("lowID"),
                theApp.serverConnect && theApp.serverConnect->isConnected()
                    && theApp.serverConnect->isLowID());
    info.insert(QStringLiteral("clientID"),   static_cast<qint64>(theApp.getID()));
    if (connected && theApp.serverConnect) {
        info.insert(QStringLiteral("publicIP"),
                    static_cast<qint64>(theApp.publicIP()));
        info.insert(QStringLiteral("publicIPv6"), theApp.publicIPv6().toString());
        info.insert(QStringLiteral("obfuscated"),
                    theApp.serverConnect->isConnectedObfuscated());
        if (const auto* srv = theApp.serverConnect->currentServer()) {
            info.insert(QStringLiteral("serverIP"), static_cast<qint64>(srv->ipAddress().toNetworkUint32()));
            info.insert(QStringLiteral("serverAddr"), srv->ipAddress().toString());
            info.insert(QStringLiteral("serverPort"), static_cast<qint64>(srv->port()));
            info.insert(QStringLiteral("serverId"), static_cast<qint64>(srv->serverId()));
            info.insert(QStringLiteral("serverName"), srv->name());
            info.insert(QStringLiteral("serverDescription"), srv->description());
            info.insert(QStringLiteral("serverAddress"), srv->address());
            info.insert(QStringLiteral("serverVersion"), srv->version());
            info.insert(QStringLiteral("serverUsers"), static_cast<qint64>(srv->users()));
            info.insert(QStringLiteral("serverFiles"), static_cast<qint64>(srv->files()));
        }
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(info)));
}

void IpcClientHandler::handleGetServerMessages(const IpcMessage& msg)
{
    // Replay for the Server Info pane: the daemon outlives the GUI, so a GUI that
    // starts against a running daemon would otherwise show an empty pane even
    // though the server greeting arrived long ago. fromId lets a GUI that merely
    // reconnected skip what it already displayed instead of duplicating it.
    const qint64 fromId = msg.fieldInt(0);

    QCborArray entries;
    for (const auto& [id, type, text] : CoreNotifierBridge::serverMessageHistory())
        if (id > fromId)
            entries.append(QCborArray{id, static_cast<qint64>(type), text});

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(entries)));
}

void IpcClientHandler::handleSearchKadNotes(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    const QString fileName = msg.fieldString(1); // optional; shown in the Kad search list
    if (hash.size() != 32) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }

    if (rejectIfKadUnavailable(msg, /*requireConnected*/ true))
        return;

    const QByteArray hashBytes = QByteArray::fromHex(hash.toLatin1());
    if (hashBytes.size() != 16) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }

    kad::UInt128 target;
    target.setValueBE(reinterpret_cast<const uint8*>(hashBytes.constData()));

    // prepareLookup returns null when a notes lookup for this file is already running.
    if (kad::SearchManager::prepareLookup(kad::SearchType::Notes, true, target, fileName) == nullptr) {
        sendMessage(IpcMessage::makeResult(msg.seqId(), false,
            QCborValue(tr("Another search is already in progress. Please try again later!"))));
        return;
    }

    // Let any search result for this hash render "(Kad search in progress...)".
    // MFC does the same right after PrepareLookup (CommentDialogLst.cpp:174-177).
    if (theApp.searchList)
        theApp.searchList->setNotesSearchStatus(
            reinterpret_cast<const uint8*>(hashBytes.constData()), true);

    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleConnectToServer(const IpcMessage& msg)
{
    if (!theApp.serverConnect) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerConnect unavailable")));
        return;
    }

    // A ConnectToServer request is an explicit user action (toolbar Connect, or
    // double-click / "connect" on a specific server), so it proceeds regardless of
    // the "eD2K network" pref. That pref gates *auto*-connect at startup only
    // (CoreSession::start: networkED2K() && autoConnect()), not manual connects.

    // If IP and port fields are provided, connect to a specific server
    if (msg.fieldCount() >= 2) {
        const auto port = static_cast<uint16>(msg.fieldInt(1));

        if (!theApp.serverList) {
            sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerList unavailable")));
            return;
        }

        auto* srv = resolveServerFromMsg(msg, 0, 2);
        if (!srv) {
            sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Server not found")));
            return;
        }

        // Already connected to this exact server — no-op
        if (theApp.serverConnect->isConnected()) {
            const auto* cur = theApp.serverConnect->currentServer();
            if (cur && cur->ipAddress() == srv->ipAddress() && cur->port() == port) {
                sendMessage(IpcMessage::makeResult(msg.seqId(), true));
                return;
            }
            // Connected to a different server — disconnect first
            theApp.serverConnect->disconnect();
        }

        theApp.serverConnect->connectToServer(srv);
        sendMessage(IpcMessage::makeResult(msg.seqId(), true));
        return;
    }

    // No fields — connect to any server
    theApp.serverConnect->connectToAnyServer();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleDisconnectFromServer(const IpcMessage& msg)
{
    if (!theApp.serverConnect) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerConnect unavailable")));
        return;
    }
    if (theApp.serverConnect->isConnecting())
        theApp.serverConnect->stopConnectionTry();
    theApp.serverConnect->disconnect();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleStartSearch(const IpcMessage& msg)
{
    if (!theApp.searchList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("SearchList unavailable")));
        return;
    }

    SearchParams params;
    params.expression      = msg.fieldString(0);
    params.fileType        = msg.fieldString(1);
    params.type            = static_cast<SearchType>(msg.fieldInt(2));
    params.minSize         = static_cast<uint64>(msg.fieldInt(3));
    params.maxSize         = static_cast<uint64>(msg.fieldInt(4));
    params.availability    = static_cast<uint32>(msg.fieldInt(5));
    params.extension       = msg.fieldString(6);
    params.completeSources = static_cast<uint32>(msg.fieldInt(7));
    params.codec           = msg.fieldString(8);
    params.minBitrate      = static_cast<uint32>(msg.fieldInt(9));
    params.minLength       = static_cast<uint32>(msg.fieldInt(10));
    params.title           = msg.fieldString(11);
    params.album           = msg.fieldString(12);
    params.artist          = msg.fieldString(13);

    // "Automatic" is a chooser, not a network: resolve it to exactly one before
    // anything is created or sent, so every path below sees a concrete type.
    // MFC: CSearchResultsWnd::StartNewSearch — srchybrid/SearchResultsWnd.cpp:1134-1165.
    if (params.type == SearchType::Automatic) {
        const auto resolved = resolveAutomaticSearchType(gatherAutoSearchState());
        if (!resolved) {
            sendMessage(IpcMessage::makeResult(msg.seqId(), false,
                QCborValue(tr("You are not connected to a server or the Kad network!"))));
            return;
        }
        params.type = *resolved;
        logInfo(tr("Automatic search method resolved to %1")
                    .arg(params.type == SearchType::Kademlia ? tr("Kad") : tr("eD2K server")));
    }

    bool started = false;
    uint32 searchID = 0;
    kad::KeywordSelection kadKeywordSel;

    if (params.type == SearchType::Kademlia) {
        auto* kadInst = kad::Kademlia::instance();
        if (!kadInst || !kadInst->isConnected()) {
            sendMessage(IpcMessage::makeResult(msg.seqId(), false,
                QCborValue(tr("Kad is not connected.\n\nWait until Kad is connected "
                              "before starting a Kad search."))));
            return;
        }

        const auto replyAlreadySearching = [this, &msg](const QString& keyword) {
            sendMessage(IpcMessage::makeResult(msg.seqId(), false,
                QCborValue(tr("There is already a Kad search ongoing for the keyword \"%1\".\n\n"
                              "To search again for that keyword, either wait until this keyword "
                              "search is finished or close the according search results pane.")
                               .arg(keyword))));
        };

        // A Kad search is indexed under a single keyword. When the expression's
        // first keyword is already the target of a running search, fall back to
        // the next word long enough to be a keyword instead of refusing.
        kadKeywordSel = kad::SearchManager::selectKeyword(params.expression);
        if (kadKeywordSel.status == kad::KeywordStatus::TooShort) {
            sendMessage(IpcMessage::makeResult(msg.seqId(), false,
                QCborValue(tr("Keyword too short.\n\nThe keyword(s) used in a Kad search "
                              "expression must have a minimum length of 3 characters."))));
            return;
        }
        if (kadKeywordSel.status == kad::KeywordStatus::AllActive) {
            replyAlreadySearching(kadKeywordSel.primaryKeyword);
            return;
        }
        if (kadKeywordSel.isFallback) {
            logInfo(tr("Kad: \"%1\" is already being searched — using \"%2\" as the search "
                       "target for \"%3\"")
                        .arg(kadKeywordSel.primaryKeyword, kadKeywordSel.keyword,
                             params.expression));
        }

        // Build the AND/OR/NOT expression tree + filters that travel with the
        // KADEMLIA2_SEARCH_KEY_REQ. Without it a multi-word search degenerates
        // to a bare single-keyword query and remote nodes return everything
        // indexed under the first keyword.
        const QByteArray searchTerms = buildSearchTermsPayload(params, kadKeywordSel.keyword);

        // Create Kad search first to get its auto-assigned ID
        auto* kadSearch = kad::SearchManager::prepareFindKeywords(
            params.expression,
            static_cast<uint32>(searchTerms.size()),
            searchTerms.isEmpty()
                ? nullptr
                : reinterpret_cast<const uint8*>(searchTerms.constData()),
            kadKeywordSel.keyword);
        if (kadSearch) {
            searchID = kadSearch->getSearchID();
            theApp.searchList->newSearch(params.fileType, params, searchID);
            started = kad::SearchManager::startSearch(kadSearch);
            if (!started) {
                // Target was taken between selection and start — drop the
                // half-built search instead of leaving an orphaned session.
                delete kadSearch;
                theApp.searchList->removeResults(searchID);
                searchID = 0;
            }
        }
        if (!started) {
            replyAlreadySearching(kadKeywordSel.keyword);
            return;
        }
    } else {
        // Ed2k searches use SearchList's own counter
        searchID = theApp.searchList->newSearch(params.fileType, params);
    }
    const bool isEd2kSearch = params.type == SearchType::Ed2kServer
                              || params.type == SearchType::Ed2kGlobal;

    // One ED2K search at a time — a new one supersedes whatever sweep is still
    // running. Scoped to ED2K on purpose: MFC cancels from DoNewEd2kSearch
    // (srchybrid/SearchResultsWnd.cpp:1225) and *not* from DoNewKadSearch, so
    // opening a Kad tab must leave a running sweep alone.
    if (isEd2kSearch && theApp.globalSearch)
        theApp.globalSearch->cancel();

    // Both ED2K methods start by asking the connected server over TCP; "global" then
    // walks the rest of the list over UDP once that answer is in.
    bool localRequestSent = false;
    QByteArray payload;
    if (isEd2kSearch) {
        auto parsed = parseSearchExpression(params.expression);
        payload = parsed.expr.toBytes();

        if (payload.isEmpty()) {
            logServerVerbose(QStringLiteral("Search \"%1\" produced an empty request payload")
                                 .arg(params.expression));
        } else if (theApp.serverConnect && theApp.serverConnect->isConnected()) {
            auto pkt = std::make_unique<Packet>(OP_SEARCHREQUEST,
                                                static_cast<uint32>(payload.size()));
            pkt->prot = OP_EDONKEYPROT;
            std::memcpy(pkt->pBuffer, payload.constData(), static_cast<size_t>(payload.size()));
            const Server* cur = theApp.serverConnect->currentServer();
            logServerVerbose(QStringLiteral(">>> TCP server search: expr=\"%1\" -> %2 (%3 byte payload)")
                                 .arg(params.expression)
                                 .arg(cur ? cur->name() : QStringLiteral("connected server"))
                                 .arg(payload.size()));
            theApp.serverConnect->sendPacket(std::move(pkt));
            localRequestSent = true;
            started = true;
        } else {
            logServerVerbose(QStringLiteral("TCP server search skipped for \"%1\" — not connected to a server")
                                 .arg(params.expression));
        }
    }

    if (params.type == SearchType::Ed2kGlobal && !payload.isEmpty() && theApp.globalSearch) {
        // Hand the server list to the scheduler rather than blasting it here: it
        // queries one server per 750 ms, and only after the local server has answered
        // (or timed out). Without a local request there is nothing to wait for, so
        // the sweep starts right away — that is how a Kad-only session still gets a
        // global search, which MFC does not allow at all.
        // The keyword expression carries no 64-bit size tag → is64=false.
        theApp.globalSearch->start(searchID, payload, /*is64BitSearch*/ false,
                                   /*awaitLocalAnswer*/ localRequestSent);
        started = true;
    }

    QCborMap result;
    result.insert(QStringLiteral("searchID"), static_cast<qint64>(searchID));
    result.insert(QStringLiteral("started"), started);
    // The network actually used — Automatic has been resolved by now, and the GUI
    // needs it for the tab icon.
    result.insert(QStringLiteral("type"), static_cast<int>(params.type));
    if (kadKeywordSel.isFallback) {
        // Tells the GUI which keyword was used instead of the expression's first
        result.insert(QStringLiteral("keyword"), kadKeywordSel.keyword);
        result.insert(QStringLiteral("primaryKeyword"), kadKeywordSel.primaryKeyword);
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(result)));
}

void IpcClientHandler::handleGetSearchResults(const IpcMessage& msg)
{
    if (!theApp.searchList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("SearchList unavailable")));
        return;
    }

    const auto searchID = static_cast<uint32>(msg.fieldInt(0));
    QCborArray results;
    theApp.searchList->forEachResult(searchID, [&results](const SearchFile* sf) {
        QCborMap m = toCbor(*sf);
        m.insert(QStringLiteral("knownType"),
                 static_cast<int>(determineKnownType(sf->fileHash())));
        results.append(m);
    });
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(results)));
}

void IpcClientHandler::handleStopSearch(const IpcMessage& msg)
{
    const auto searchID = static_cast<uint32>(msg.fieldInt(0));
    kad::SearchManager::stopSearch(searchID, false);
    if (theApp.globalSearch)
        theApp.globalSearch->cancelSearch(searchID);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleRemoveSearch(const IpcMessage& msg)
{
    if (!theApp.searchList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("SearchList unavailable")));
        return;
    }
    const auto searchID = static_cast<uint32>(msg.fieldInt(0));
    kad::SearchManager::stopSearch(searchID, false);
    if (theApp.globalSearch)
        theApp.globalSearch->cancelSearch(searchID);
    theApp.searchList->removeResults(searchID);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleClearAllSearches(const IpcMessage& msg)
{
    if (!theApp.searchList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("SearchList unavailable")));
        return;
    }
    kad::SearchManager::stopAllSearches();
    if (theApp.globalSearch)
        theApp.globalSearch->cancel();
    theApp.searchList->clear();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleDownloadSearchFile(const IpcMessage& msg)
{
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("DownloadQueue unavailable")));
        return;
    }

    const QString hash     = msg.fieldString(0);
    const QString fileName = msg.fieldString(1);
    const auto fileSize    = static_cast<uint64>(msg.fieldInt(2));
    const QString rawLink  = msg.fieldString(3);

    // Prefer the original link text when the GUI has one: it carries the AICH hash,
    // part hashes and source hints, none of which survive a hash/name/size round-trip.
    // Rebuilding is only for search results, which never had a link — and it has to
    // re-encode the name, since a '%' or '|' in it would otherwise corrupt the link.
    const QString ed2kLink = rawLink.startsWith(QStringLiteral("ed2k://"), Qt::CaseInsensitive)
        ? rawLink
        : QStringLiteral("ed2k://|file|%1|%2|%3|/")
              .arg(urlEncode(stripInvalidFilenameChars(fileName))).arg(fileSize).arg(hash);

    const QStringList tempDirs = thePrefs.tempDirs();
    const QString tempDir = tempDirs.isEmpty()
        ? QDir(thePrefs.configDir()).filePath(QStringLiteral("Temp"))
        : tempDirs.first();
    const bool ok = theApp.downloadQueue->addDownloadFromED2KLink(ed2kLink, tempDir, 0);
    sendMessage(IpcMessage::makeResult(msg.seqId(), ok));
}

void IpcClientHandler::handleGetKnownTypes(const IpcMessage& msg)
{
    const auto hashes = msg.fieldArray(0);
    QCborArray types;
    uint8 hashBuf[16]{};

    for (const auto& val : hashes) {
        const QString hashStr = val.toString();
        if (hexToHash(hashStr, hashBuf))
            types.append(static_cast<int>(determineKnownType(hashBuf)));
        else
            types.append(0);
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(types)));
}

void IpcClientHandler::handleGetSharedFiles(const IpcMessage& msg)
{
    QCborArray files;
    QSet<QString> addedHashes;

    auto appendFile = [&](KnownFile* kf, bool isPartFile, int64_t completedSz) {
        QCborMap m;
        const QString hash = md4str(kf->fileHash());
        m.insert(QStringLiteral("hash"), hash);
        m.insert(QStringLiteral("fileName"), kf->fileName());
        m.insert(QStringLiteral("fileSize"), static_cast<qint64>(kf->fileSize()));
        m.insert(QStringLiteral("fileType"), kf->fileType());
        m.insert(QStringLiteral("upPriority"), static_cast<int>(kf->upPriority()));
        m.insert(QStringLiteral("isAutoUpPriority"), kf->isAutoUpPriority());
        m.insert(QStringLiteral("requests"), static_cast<qint64>(kf->statistic.requests()));
        m.insert(QStringLiteral("acceptedUploads"), static_cast<qint64>(kf->statistic.accepts()));
        m.insert(QStringLiteral("transferred"), static_cast<qint64>(kf->statistic.transferred()));
        m.insert(QStringLiteral("allTimeRequests"), static_cast<qint64>(kf->statistic.allTimeRequests()));
        m.insert(QStringLiteral("allTimeAccepted"), static_cast<qint64>(kf->statistic.allTimeAccepts()));
        m.insert(QStringLiteral("allTimeTransferred"), static_cast<qint64>(kf->statistic.allTimeTransferred()));
        m.insert(QStringLiteral("completeSources"), static_cast<int>(kf->completeSourcesCount()));
        m.insert(QStringLiteral("publishedED2K"), kf->publishedED2K());
        m.insert(QStringLiteral("kadPublished"), kf->kadFileSearchID() != 0);
        m.insert(QStringLiteral("filePath"), kf->filePath());
        // Whether the user is allowed to unshare it — the incoming directory is not
        // unshareable by accident, it is unshareable by design (MFC ShouldBeShared with
        // bMustBeShared, srchybrid/SharedFilesCtrl.cpp:1598). The GUI greys the menu
        // entry with this rather than guessing at the incoming path from the rows.
        m.insert(QStringLiteral("canUnshare"),
                 theApp.sharedFileList
                     && !theApp.sharedFileList->shouldBeShared(kf->path(), kf->filePath(), true));
        m.insert(QStringLiteral("path"), QFileInfo(kf->filePath()).absolutePath());
        m.insert(QStringLiteral("ed2kLink"), kf->getED2kLink());
        m.insert(QStringLiteral("isPartFile"), isPartFile);
        m.insert(QStringLiteral("uploadingClients"), kf->uploadingClientCount());

        int queuedClients = 0;
        int64_t uploadDataRate = 0;
        if (theApp.uploadQueue) {
            const uint8* fileHashPtr = kf->fileHash();
            theApp.uploadQueue->forEachWaiting([&](UpDownClient* c) {
                if (md4equ(c->reqUpFileId(), fileHashPtr))
                    ++queuedClients;
            });
            theApp.uploadQueue->forEachUploading([&](UpDownClient* c) {
                if (md4equ(c->reqUpFileId(), fileHashPtr))
                    uploadDataRate += c->upDatarate();
            });
        }
        m.insert(QStringLiteral("queuedClients"), queuedClients);
        m.insert(QStringLiteral("uploadDataRate"), static_cast<qint64>(uploadDataRate));

        // ED2K link building components for GUI-side link variants
        const auto& fid = kf->fileIdentifier();
        QString partHashesStr;
        if (fid.getAvailableMD4PartHashCount() > 0 && fid.hasExpectedMD4HashCount()) {
            partHashesStr = QStringLiteral("p=");
            for (uint16 j = 0; j < fid.getAvailableMD4PartHashCount(); ++j) {
                if (j > 0)
                    partHashesStr += QChar(u':');
                partHashesStr += encodeBase16({fid.getMD4PartHash(j), 16});
            }
            partHashesStr += QChar(u'|');
        }
        m.insert(QStringLiteral("partHashesStr"), partHashesStr);

        QString aichHashStr;
        if (fid.hasAICHHash())
            aichHashStr = QStringLiteral("h=%1|").arg(fid.getAICHHash().getString());
        m.insert(QStringLiteral("aichHashStr"), aichHashStr);

        m.insert(QStringLiteral("partCount"), static_cast<int>(kf->partCount()));
        m.insert(QStringLiteral("completedSize"), static_cast<qint64>(completedSz));

        // Build per-part availability map for share status bar
        {
            QCborArray partMapArr;
            const auto& availFreq = kf->availPartFrequency();
            const int pc = static_cast<int>(kf->partCount());

            if (isPartFile) {
                auto* pf = static_cast<PartFile*>(kf);
                const auto& srcFreq = pf->srcPartFrequency();
                const bool hasSources = kf->hasUploadingClients() || kf->completeSourcesCountHi() > 0;

                if (hasSources || pf->status() != PartFileStatus::Paused) {
                    const uint16 baseSources = kf->completeSourcesCountLo()
                                               ? kf->completeSourcesCountLo() - 1 : 0;
                    for (int i = 0; i < pc; ++i) {
                        if (!pf->isComplete(static_cast<uint32>(i))) {
                            partMapArr.append(255); // gap — light grey
                        } else {
                            // Use srcPartFrequency when actively downloading, availPartFrequency otherwise
                            uint16 freq = 0;
                            if (pf->status() != PartFileStatus::Paused && i < static_cast<int>(srcFreq.size()))
                                freq = srcFreq[static_cast<size_t>(i)];
                            else if (i < static_cast<int>(availFreq.size()))
                                freq = std::max(availFreq[static_cast<size_t>(i)], baseSources);
                            // Encode: 0 → 1(red), else clamp(freq+1, 2, 254)
                            partMapArr.append(freq == 0 ? 1 : std::clamp<int>(freq + 1, 2, 254));
                        }
                    }
                } else {
                    // Paused with no sources — complete=0, incomplete=255
                    for (int i = 0; i < pc; ++i)
                        partMapArr.append(pf->isComplete(static_cast<uint32>(i)) ? 0 : 255);
                }
            } else {
                // Complete KnownFile
                if (kf->hasUploadingClients() || kf->completeSourcesCountHi() > 1) {
                    const uint16 baseSources = kf->completeSourcesCountLo()
                                               ? kf->completeSourcesCountLo() - 1 : 0;
                    for (int i = 0; i < pc; ++i) {
                        uint16 freq = baseSources;
                        if (i < static_cast<int>(availFreq.size()))
                            freq = std::max(availFreq[static_cast<size_t>(i)], baseSources);
                        partMapArr.append(freq == 0 ? 1 : std::clamp<int>(freq + 1, 2, 254));
                    }
                }
                // else: empty array → delegate draws solid dark grey
            }

            if (!partMapArr.isEmpty())
                m.insert(QStringLiteral("sharePartMap"), partMapArr);
        }

        // Collection metadata
        const bool isColl = Collection::hasCollectionExtension(kf->fileName());
        m.insert(QStringLiteral("isCollection"), isColl);
        m.insert(QStringLiteral("hasCollectionAuthorKey"),
                 isColl && kf->collection() && !kf->collection()->m_authorKey.isEmpty());

        files.append(m);
        addedHashes.insert(hash);
    };

    if (theApp.sharedFileList) {
        theApp.sharedFileList->forEachFile([&](KnownFile* kf) {
            appendFile(kf, false, static_cast<int64_t>(kf->fileSize()));
        });
    }

    // Also include PartFiles with at least some completed data
    if (theApp.downloadQueue) {
        for (auto* pf : theApp.downloadQueue->files()) {
            if (static_cast<uint64>(pf->completedSize()) > 0) {
                const QString hash = md4str(pf->fileHash());
                if (!addedHashes.contains(hash))
                    appendFile(pf, true, static_cast<int64_t>(pf->completedSize()));
            }
        }
    }

    // Compute aggregate totals across all shared files for percentage bars
    int64_t totalRequests = 0, totalAccepted = 0, totalTransferred = 0;
    int64_t totalAllTimeReqs = 0, totalAllTimeAcc = 0, totalAllTimeTx = 0;
    for (int i = 0; i < files.size(); ++i) {
        const QCborMap m = files.at(i).toMap();
        totalRequests    += m.value(QStringLiteral("requests")).toInteger();
        totalAccepted    += m.value(QStringLiteral("acceptedUploads")).toInteger();
        totalTransferred += m.value(QStringLiteral("transferred")).toInteger();
        totalAllTimeReqs += m.value(QStringLiteral("allTimeRequests")).toInteger();
        totalAllTimeAcc  += m.value(QStringLiteral("allTimeAccepted")).toInteger();
        totalAllTimeTx   += m.value(QStringLiteral("allTimeTransferred")).toInteger();
    }

    QCborMap result;
    result.insert(QStringLiteral("files"), files);
    result.insert(QStringLiteral("totalRequests"), static_cast<qint64>(totalRequests));
    result.insert(QStringLiteral("totalAccepted"), static_cast<qint64>(totalAccepted));
    result.insert(QStringLiteral("totalTransferred"), static_cast<qint64>(totalTransferred));
    result.insert(QStringLiteral("totalAllTimeRequests"), static_cast<qint64>(totalAllTimeReqs));
    result.insert(QStringLiteral("totalAllTimeAccepted"), static_cast<qint64>(totalAllTimeAcc));
    result.insert(QStringLiteral("totalAllTimeTransferred"), static_cast<qint64>(totalAllTimeTx));
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(result)));
}

void IpcClientHandler::handleSetSharedFilePriority(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    const auto priority = static_cast<uint8>(msg.fieldInt(1));
    const bool isAuto = msg.fieldBool(2);

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }

    // Look up in shared file list first, then download queue
    KnownFile* kf = theApp.sharedFileList ? theApp.sharedFileList->getFileByID(hashBuf) : nullptr;
    if (!kf && theApp.downloadQueue)
        kf = theApp.downloadQueue->fileByID(hashBuf);

    if (!kf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("File not found")));
        return;
    }

    kf->setAutoUpPriority(isAuto);
    if (!isAuto)
        kf->setUpPriority(priority);
    else
        kf->updateAutoUpPriority();

    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleReloadSharedFiles(const IpcMessage& msg)
{
    if (theApp.sharedFileList)
        theApp.sharedFileList->reload();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleGetEd2kLink(const IpcMessage& msg)
{
    const QCborArray hashes = msg.fieldArray(0);
    const bool hashset    = msg.fieldBool(1);
    const bool sourceHint = msg.fieldBool(2);
    const bool html       = msg.fieldBool(3);

    // One entry per requested hash, empty for anything we cannot resolve: the selection
    // moves under the GUI between its poll and this reply, and failing the whole batch over
    // one stale row would blank a list of otherwise good links.
    QCborArray links;
    qsizetype budget = 8 * 1024 * 1024;   // hard stop well inside Ipc::MaxPayloadSize
    int taken = 0;

    for (const auto& value : hashes) {
        if (taken >= Ipc::MaxEd2kLinkBatch || budget <= 0) {
            logWarning(QStringLiteral("GetEd2kLink: truncated at %1 of %2 links")
                           .arg(taken).arg(hashes.size()));
            break;
        }
        ++taken;

        // The link grammar and the "what may we advertise" policy both live in the core:
        // the GUI process cannot see our public IPv6, which is runtime state, not a pref.
        const AbstractFile* file = nullptr;
        uint8 hash[16]{};
        if (hexToHash(value.toString(), hash)) {
            if (theApp.sharedFileList)
                file = theApp.sharedFileList->getFileByID(hash);
            if (!file && theApp.downloadQueue)
                file = theApp.downloadQueue->fileByID(hash);
        }

        const QString link = file ? file->getED2kLink(hashset, html, sourceHint) : QString{};
        budget -= link.size();
        links.append(link);
    }

    QCborArray result;
    result.append(links);
    result.append(!ownLinkSourceHints().empty());   // global state — one answer per batch
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(result)));
}

void IpcClientHandler::handleReloadIPFilter(const IpcMessage& msg)
{
    int count = 0;
    if (theApp.ipFilter)
        count = theApp.ipFilter->loadFromDefaultFile(thePrefs.configDir());
    sendMessage(IpcMessage::makeResult(msg.seqId(), true,
        QCborValue(static_cast<qint64>(count))));
}

void IpcClientHandler::handleGetFriends(const IpcMessage& msg)
{
    QCborArray friends;
    if (theApp.friendList) {
        for (const auto& f : theApp.friendList->friends())
            friends.append(toCbor(*f));
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(friends)));
}

void IpcClientHandler::handleAddFriend(const IpcMessage& msg)
{
    if (!theApp.friendList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("FriendList unavailable")));
        return;
    }

    const QString hashStr = msg.fieldString(0);
    const QString name = msg.fieldString(1);
    const auto ip = static_cast<uint32>(msg.fieldInt(2));
    const auto port = static_cast<uint16>(msg.fieldInt(3));
    // Field 4 (optional, added for IPv6): the literal address. Older clients omit it and
    // fall back to the uint32, which is 0 for an IPv6 peer.
    const QString addrStr = msg.fieldString(4);

    Address addr = Address::fromString(addrStr);
    if (addr.isNull())
        addr = Address::fromNetworkOrder(ip);

    uint8 hashBuf[16]{};
    const bool hasHash = hexToHash(hashStr, hashBuf);
    theApp.friendList->addFriend(hashBuf, addr, port, name, hasHash);
    theApp.friendList->save(thePrefs.configDir());
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleRemoveFriend(const IpcMessage& msg)
{
    if (!theApp.friendList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("FriendList unavailable")));
        return;
    }

    const QString hashStr = msg.fieldString(0);
    uint8 hashBuf[16]{};
    if (!hexToHash(hashStr, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }

    // Find friend by hash
    Friend* target = nullptr;
    for (const auto& f : theApp.friendList->friends()) {
        if (f->hasUserhash() && std::memcmp(f->userHash().data(), hashBuf, 16) == 0) {
            target = f.get();
            break;
        }
    }
    if (target) {
        theApp.friendList->removeFriend(target);
        theApp.friendList->save(thePrefs.configDir());
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleSendChatMessage(const IpcMessage& msg)
{
    if (!theApp.clientList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ClientList unavailable")));
        return;
    }

    const QString hashStr = msg.fieldString(0);
    const QString message = msg.fieldString(1);

    uint8 hashBuf[16]{};
    if (!hexToHash(hashStr, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }

    // Find the client by user hash
    auto* client = theApp.clientList->findByUserHash(hashBuf, 0, 0);
    if (!client) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404,
            QStringLiteral("Client not found or not connected")));
        return;
    }

    client->sendChatMessage(message);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleSetFriendSlot(const IpcMessage& msg)
{
    if (!theApp.friendList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("FriendList unavailable")));
        return;
    }

    const QString hashStr = msg.fieldString(0);
    const bool enabled = msg.fieldBool(1);

    uint8 hashBuf[16]{};
    if (!hexToHash(hashStr, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }

    // Only one friend slot at a time — MFC srchybrid/FriendListCtrl.cpp:218-224.
    //
    // removeAllFriendSlots() propagates through Friend::setFriendSlot() to each entry's
    // linked client, which is what actually enforces the rule: the flag the upload queue
    // scores lives on UpDownClient, and clearing it only on the Friend objects (as this used
    // to) left every previously-slotted client holding a slot forever.
    theApp.friendList->removeAllFriendSlots();

    // Find and set the target friend's slot. Propagates to its linked client if the peer is
    // connected; if it is not, the flag is carried across by Friend::setLinkedClient() when
    // it next says hello.
    for (const auto& f : theApp.friendList->friends()) {
        if (f->hasUserhash() && std::memcmp(f->userHash().data(), hashBuf, 16) == 0) {
            f->setFriendSlot(enabled);
            break;
        }
    }

    theApp.friendList->save(thePrefs.configDir());
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleGetStats(const IpcMessage& msg)
{
    QCborMap stats = toCborMap(collectStatsSnapshot());

    // Not part of the core snapshot: the stream token belongs to the web server this
    // daemon runs, which core has no handle on.
    if (auto* da = DaemonApp::instance()) {
        if (auto* ws = da->webServer())
            stats.insert(QStringLiteral("streamToken"), ws->streamToken());
    }

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(stats)));
}

// ---------------------------------------------------------------------------
// Graph history — replay whatever the caller is missing
// ---------------------------------------------------------------------------
//
// Same shape as handleGetServerMessages' fromId replay: the daemon outlives the
// GUI, so a GUI that starts against a running daemon would otherwise draw an empty
// graph even though core has been sampling all along. `epoch` tells a caller its
// buffer is no longer a prefix of ours (daemon restarted, or statistics were reset);
// `oldestSeq` tells it samples aged out while it wasn't looking. Either way it drops
// what it has and takes the reply whole.

void IpcClientHandler::handleGetSpeedHistory(const IpcMessage& msg)
{
    const auto fromSeq = static_cast<uint32>(msg.fieldInt(0));

    QCborMap out;
    QCborArray samples;
    if (auto* hist = theApp.statsHistory) {
        for (const auto& s : hist->speedSince(fromSeq))
            samples.append(QCborArray{static_cast<qint64>(s.seq),
                                      static_cast<double>(s.down),
                                      static_cast<double>(s.up)});
        out.insert(QStringLiteral("epoch"), static_cast<qint64>(hist->epoch()));
        out.insert(QStringLiteral("oldestSeq"), static_cast<qint64>(hist->oldestSpeedSeq()));
    }
    out.insert(QStringLiteral("samples"), samples);

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(out)));
}

void IpcClientHandler::handleGetStatsHistory(const IpcMessage& msg)
{
    const auto fromSeq = static_cast<uint32>(msg.fieldInt(0));

    QCborMap out;
    QCborArray samples;
    if (auto* hist = theApp.statsHistory) {
        // Field order is StatsGraphSample's, which is MFC's scope order — the GUI
        // unpacks it positionally into the download/upload/connection graphs.
        for (const auto& s : hist->statsSince(fromSeq))
            samples.append(QCborArray{static_cast<qint64>(s.seq),
                                      static_cast<qint64>(s.timestamp),
                                      static_cast<double>(s.downAvgSession),
                                      static_cast<double>(s.downAvgTime),
                                      static_cast<double>(s.downCurrent),
                                      static_cast<double>(s.upAvgSession),
                                      static_cast<double>(s.upAvgTime),
                                      static_cast<double>(s.upCurrent),
                                      static_cast<double>(s.upNoOverhead),
                                      static_cast<double>(s.upFriend),
                                      static_cast<qint64>(s.connActive),
                                      static_cast<qint64>(s.upActive),
                                      static_cast<qint64>(s.upTotal),
                                      static_cast<qint64>(s.downTransferring)});
        out.insert(QStringLiteral("epoch"), static_cast<qint64>(hist->epoch()));
        out.insert(QStringLiteral("oldestSeq"), static_cast<qint64>(hist->oldestStatsSeq()));
    }
    out.insert(QStringLiteral("intervalSec"), static_cast<qint64>(thePrefs.graphsUpdateSec()));
    out.insert(QStringLiteral("samples"), samples);

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(out)));
}

void IpcClientHandler::handleGetPreferences(const IpcMessage& msg)
{
    QCborMap prefs;
    prefs.insert(QStringLiteral("nick"), thePrefs.nick());
    prefs.insert(QStringLiteral("port"), thePrefs.port());
    prefs.insert(QStringLiteral("udpPort"), thePrefs.udpPort());
    prefs.insert(QStringLiteral("maxUpload"), static_cast<qint64>(thePrefs.maxUpload()));
    prefs.insert(QStringLiteral("maxDownload"), static_cast<qint64>(thePrefs.maxDownload()));
    prefs.insert(QStringLiteral("maxGraphDownloadRate"), static_cast<qint64>(thePrefs.maxGraphDownloadRate()));
    prefs.insert(QStringLiteral("maxGraphUploadRate"), static_cast<qint64>(thePrefs.maxGraphUploadRate()));
    prefs.insert(QStringLiteral("maxConnections"), static_cast<qint64>(thePrefs.maxConnections()));
    prefs.insert(QStringLiteral("maxSourcesPerFile"), static_cast<qint64>(thePrefs.maxSourcesPerFile()));
    prefs.insert(QStringLiteral("autoConnect"), thePrefs.autoConnect());
    prefs.insert(QStringLiteral("reconnect"), thePrefs.reconnect());
    prefs.insert(QStringLiteral("showOverhead"), thePrefs.showOverhead());
    prefs.insert(QStringLiteral("networkED2K"), thePrefs.networkED2K());
    prefs.insert(QStringLiteral("kadEnabled"), thePrefs.kadEnabled());
    prefs.insert(QStringLiteral("schedulerEnabled"), thePrefs.schedulerEnabled());
    prefs.insert(QStringLiteral("enableUPnP"), thePrefs.enableUPnP());
    prefs.insert(QStringLiteral("separateIPv6Queue"), thePrefs.separateIPv6Queue());

    // Server
    prefs.insert(QStringLiteral("safeServerConnect"), thePrefs.safeServerConnect());
    prefs.insert(QStringLiteral("autoConnectStaticOnly"), thePrefs.autoConnectStaticOnly());
    prefs.insert(QStringLiteral("useServerPriorities"), thePrefs.useServerPriorities());
    prefs.insert(QStringLiteral("addServersFromServer"), thePrefs.addServersFromServer());
    prefs.insert(QStringLiteral("useUserSortedServerList"), thePrefs.useUserSortedServerList());
    prefs.insert(QStringLiteral("addServersFromClients"), thePrefs.addServersFromClients());
    prefs.insert(QStringLiteral("deadServerRetries"), static_cast<qint64>(thePrefs.deadServerRetries()));
    prefs.insert(QStringLiteral("autoUpdateServerList"), thePrefs.autoUpdateServerList());
    prefs.insert(QStringLiteral("serverListURL"), thePrefs.serverListURL());
    prefs.insert(QStringLiteral("smartLowIdCheck"), thePrefs.smartLowIdCheck());
    prefs.insert(QStringLiteral("manualServerHighPriority"), thePrefs.manualServerHighPriority());

    // Proxy
    prefs.insert(QStringLiteral("proxyType"), thePrefs.proxyType());
    prefs.insert(QStringLiteral("proxyHost"), thePrefs.proxyHost());
    prefs.insert(QStringLiteral("proxyPort"), static_cast<qint64>(thePrefs.proxyPort()));
    prefs.insert(QStringLiteral("proxyEnablePassword"), thePrefs.proxyEnablePassword());
    prefs.insert(QStringLiteral("proxyUser"), thePrefs.proxyUser());
    prefs.insert(QStringLiteral("proxyPassword"), thePrefs.proxyPassword());

    // Files page (daemon-side)
    prefs.insert(QStringLiteral("addNewFilesPaused"), thePrefs.addNewFilesPaused());
    prefs.insert(QStringLiteral("useSaveLoadSources"), thePrefs.useSaveLoadSources());
    prefs.insert(QStringLiteral("autoDownloadPriority"), thePrefs.autoDownloadPriority());
    prefs.insert(QStringLiteral("autoSharedFilesPriority"), thePrefs.autoSharedFilesPriority());
    prefs.insert(QStringLiteral("transferFullChunks"), thePrefs.transferFullChunks());
    prefs.insert(QStringLiteral("previewPrio"), thePrefs.previewPrio());
    prefs.insert(QStringLiteral("startNextPausedFile"), thePrefs.startNextPausedFile());
    prefs.insert(QStringLiteral("startNextPausedFileSameCat"), thePrefs.startNextPausedFileSameCat());
    prefs.insert(QStringLiteral("startNextPausedFileOnlySameCat"), thePrefs.startNextPausedFileOnlySameCat());
    prefs.insert(QStringLiteral("rememberDownloadedFiles"), thePrefs.rememberDownloadedFiles());
    prefs.insert(QStringLiteral("rememberCancelledFiles"), thePrefs.rememberCancelledFiles());

    // Notifications (daemon-side)
    prefs.insert(QStringLiteral("notifyOnLog"), thePrefs.notifyOnLog());
    prefs.insert(QStringLiteral("notifyOnChat"), thePrefs.notifyOnChat());
    prefs.insert(QStringLiteral("notifyOnChatMsg"), thePrefs.notifyOnChatMsg());
    prefs.insert(QStringLiteral("notifyOnDownloadAdded"), thePrefs.notifyOnDownloadAdded());
    prefs.insert(QStringLiteral("notifyOnDownloadFinished"), thePrefs.notifyOnDownloadFinished());
    prefs.insert(QStringLiteral("notifyOnNewVersion"), thePrefs.notifyOnNewVersion());
    prefs.insert(QStringLiteral("notifyOnUrgent"), thePrefs.notifyOnUrgent());
    prefs.insert(QStringLiteral("notifyEmailEnabled"), thePrefs.notifyEmailEnabled());
    prefs.insert(QStringLiteral("notifyEmailSmtpServer"), thePrefs.notifyEmailSmtpServer());
    prefs.insert(QStringLiteral("notifyEmailSmtpPort"), static_cast<qint64>(thePrefs.notifyEmailSmtpPort()));
    prefs.insert(QStringLiteral("notifyEmailSmtpAuth"), thePrefs.notifyEmailSmtpAuth());
    prefs.insert(QStringLiteral("notifyEmailSmtpTls"), thePrefs.notifyEmailSmtpTls());
    prefs.insert(QStringLiteral("notifyEmailSmtpUser"), thePrefs.notifyEmailSmtpUser());
    prefs.insert(QStringLiteral("notifyEmailSmtpPassword"), thePrefs.notifyEmailSmtpPassword());
    prefs.insert(QStringLiteral("notifyEmailRecipient"), thePrefs.notifyEmailRecipient());
    prefs.insert(QStringLiteral("notifyEmailSender"), thePrefs.notifyEmailSender());

    // Messages and Comments
    prefs.insert(QStringLiteral("msgOnlyFriends"), thePrefs.msgOnlyFriends());
    prefs.insert(QStringLiteral("enableSpamFilter"), thePrefs.enableSpamFilter());
    prefs.insert(QStringLiteral("useChatCaptchas"), thePrefs.useChatCaptchas());
    prefs.insert(QStringLiteral("messageFilter"), thePrefs.messageFilter());
    prefs.insert(QStringLiteral("commentFilter"), thePrefs.commentFilter());

    // Security
    prefs.insert(QStringLiteral("filterServerByIP"), thePrefs.filterServerByIP());
    prefs.insert(QStringLiteral("ipFilterLevel"), static_cast<qint64>(thePrefs.ipFilterLevel()));
    prefs.insert(QStringLiteral("viewSharedFilesAccess"), thePrefs.viewSharedFilesAccess());
    prefs.insert(QStringLiteral("cryptLayerSupported"), thePrefs.cryptLayerSupported());
    prefs.insert(QStringLiteral("cryptLayerRequested"), thePrefs.cryptLayerRequested());
    prefs.insert(QStringLiteral("cryptLayerRequired"), thePrefs.cryptLayerRequired());
    prefs.insert(QStringLiteral("useSecureIdent"), thePrefs.useSecureIdent());
    prefs.insert(QStringLiteral("enableSearchResultFilter"), thePrefs.enableSearchResultFilter());
    prefs.insert(QStringLiteral("warnUntrustedFiles"), thePrefs.warnUntrustedFiles());
    prefs.insert(QStringLiteral("ipFilterUpdateUrl"), thePrefs.ipFilterUpdateUrl());
    prefs.insert(QStringLiteral("appToken"), thePrefs.appToken());

    // Statistics
    prefs.insert(QStringLiteral("statsAverageMinutes"), static_cast<qint64>(thePrefs.statsAverageMinutes()));
    prefs.insert(QStringLiteral("graphsUpdateSec"), static_cast<qint64>(thePrefs.graphsUpdateSec()));
    prefs.insert(QStringLiteral("statsUpdateSec"), static_cast<qint64>(thePrefs.statsUpdateSec()));
    prefs.insert(QStringLiteral("fillGraphs"), thePrefs.fillGraphs());
    prefs.insert(QStringLiteral("statsConnectionsMax"), static_cast<qint64>(thePrefs.statsConnectionsMax()));
    prefs.insert(QStringLiteral("statsConnectionsRatio"), static_cast<qint64>(thePrefs.statsConnectionsRatio()));

    // Extended (PPgTweaks)
    prefs.insert(QStringLiteral("maxConsPerFive"), static_cast<qint64>(thePrefs.maxConsPerFive()));
    prefs.insert(QStringLiteral("maxHalfConnections"), static_cast<qint64>(thePrefs.maxHalfConnections()));
    prefs.insert(QStringLiteral("serverKeepAliveTimeout"), static_cast<qint64>(thePrefs.serverKeepAliveTimeout()));
    prefs.insert(QStringLiteral("filterLANIPs"), thePrefs.filterLANIPs());
    prefs.insert(QStringLiteral("checkDiskspace"), thePrefs.checkDiskspace());
    prefs.insert(QStringLiteral("minFreeDiskSpace"), static_cast<qint64>(thePrefs.minFreeDiskSpace()));
    prefs.insert(QStringLiteral("logToDiskCore"), thePrefs.logToDiskCore());
    prefs.insert(QStringLiteral("logToDiskGui"), thePrefs.logToDiskGui());
    prefs.insert(QStringLiteral("verbose"), thePrefs.verbose());
    prefs.insert(QStringLiteral("serverVerboseLog"), thePrefs.serverVerboseLog());
    prefs.insert(QStringLiteral("logPublicIP"), thePrefs.logPublicIP());
    prefs.insert(QStringLiteral("closeUPnPOnExit"), thePrefs.closeUPnPOnExit());
    prefs.insert(QStringLiteral("portMapProtocols"),
                 static_cast<int>(thePrefs.portMapProtocols()));
    prefs.insert(QStringLiteral("portMapIPv6"), thePrefs.portMapIPv6());
    prefs.insert(QStringLiteral("portMapLeaseSecs"),
                 static_cast<int>(thePrefs.portMapLeaseSecs()));
    prefs.insert(QStringLiteral("fileBufferSize"), static_cast<qint64>(thePrefs.fileBufferSize()));
    prefs.insert(QStringLiteral("useCreditSystem"), thePrefs.useCreditSystem());
    prefs.insert(QStringLiteral("a4afSaveCpu"), thePrefs.a4afSaveCpu());
    prefs.insert(QStringLiteral("autoArchivePreviewStart"), thePrefs.autoArchivePreviewStart());
    prefs.insert(QStringLiteral("ed2kHostname"), thePrefs.ed2kHostname());
    prefs.insert(QStringLiteral("ed2kLinkAdvertiseIPv6"), thePrefs.ed2kLinkAdvertiseIPv6());
    prefs.insert(QStringLiteral("showExtControls"), thePrefs.showExtControls());
    prefs.insert(QStringLiteral("commitFiles"), thePrefs.commitFiles());
    prefs.insert(QStringLiteral("extractMetaData"), thePrefs.extractMetaData());
    prefs.insert(QStringLiteral("logLevel"), thePrefs.logLevel());
    prefs.insert(QStringLiteral("logSourceExchange"), thePrefs.logSourceExchange());
    prefs.insert(QStringLiteral("logBannedClients"), thePrefs.logBannedClients());
    prefs.insert(QStringLiteral("logRatingDescReceived"), thePrefs.logRatingDescReceived());
    prefs.insert(QStringLiteral("logSecureIdent"), thePrefs.logSecureIdent());
    prefs.insert(QStringLiteral("logFilteredIPs"), thePrefs.logFilteredIPs());
    prefs.insert(QStringLiteral("logFileSaving"), thePrefs.logFileSaving());
    prefs.insert(QStringLiteral("logA4AF"), thePrefs.logA4AF());
    prefs.insert(QStringLiteral("logUlDlEvents"), thePrefs.logUlDlEvents());
    prefs.insert(QStringLiteral("logRawSocketPackets"), thePrefs.logRawSocketPackets());
    prefs.insert(QStringLiteral("logWebServer"), thePrefs.logWebServer());
    prefs.insert(QStringLiteral("startCoreWithConsole"), thePrefs.startCoreWithConsole());
    prefs.insert(QStringLiteral("queueSize"), static_cast<qint64>(thePrefs.queueSize()));
    prefs.insert(QStringLiteral("rememberUploadQueue"), thePrefs.rememberUploadQueue());
    // USS
    prefs.insert(QStringLiteral("dynUpEnabled"), thePrefs.dynUpEnabled());
    prefs.insert(QStringLiteral("dynUpPingTolerance"), static_cast<qint64>(thePrefs.dynUpPingTolerance()));
    prefs.insert(QStringLiteral("dynUpPingToleranceMs"), static_cast<qint64>(thePrefs.dynUpPingToleranceMs()));
    prefs.insert(QStringLiteral("dynUpUseMillisecondPingTolerance"), thePrefs.dynUpUseMillisecondPingTolerance());
    prefs.insert(QStringLiteral("dynUpGoingUpDivider"), static_cast<qint64>(thePrefs.dynUpGoingUpDivider()));
    prefs.insert(QStringLiteral("dynUpGoingDownDivider"), static_cast<qint64>(thePrefs.dynUpGoingDownDivider()));
    prefs.insert(QStringLiteral("dynUpNumberOfPings"), static_cast<qint64>(thePrefs.dynUpNumberOfPings()));
#ifdef Q_OS_WIN
    prefs.insert(QStringLiteral("autotakeEd2kLinks"), thePrefs.autotakeEd2kLinks());
    prefs.insert(QStringLiteral("openPortsOnWinFirewall"), thePrefs.openPortsOnWinFirewall());
    prefs.insert(QStringLiteral("sparsePartFiles"), thePrefs.sparsePartFiles());
    prefs.insert(QStringLiteral("allocFullFile"), thePrefs.allocFullFile());
    prefs.insert(QStringLiteral("resolveShellLinks"), thePrefs.resolveShellLinks());
    prefs.insert(QStringLiteral("multiUserSharing"), thePrefs.multiUserSharing());
#endif

    // Directories
    prefs.insert(QStringLiteral("incomingDir"), thePrefs.incomingDir());
    QCborArray tempArr;
    for (const auto& t : thePrefs.tempDirs())
        tempArr.append(t);
    prefs.insert(QStringLiteral("tempDirs"), tempArr);
    QCborArray sharedArr;
    for (const auto& s : thePrefs.sharedDirs())
        sharedArr.append(s);
    prefs.insert(QStringLiteral("sharedDirs"), sharedArr);

    // Web Server
    prefs.insert(QStringLiteral("webServerEnabled"), thePrefs.webServerEnabled());
    prefs.insert(QStringLiteral("webServerPort"), static_cast<qint64>(thePrefs.webServerPort()));
    prefs.insert(QStringLiteral("webServerApiKey"), thePrefs.webServerApiKey());
    prefs.insert(QStringLiteral("webServerListenAddress"), thePrefs.webServerListenAddress());
    prefs.insert(QStringLiteral("webServerRestApiEnabled"), thePrefs.webServerRestApiEnabled());
    prefs.insert(QStringLiteral("webServerGzipEnabled"), thePrefs.webServerGzipEnabled());
    prefs.insert(QStringLiteral("webServerUPnP"), thePrefs.webServerUPnP());
    prefs.insert(QStringLiteral("webServerTemplatePath"), thePrefs.webServerTemplatePath());
    prefs.insert(QStringLiteral("webServerSessionTimeout"), static_cast<qint64>(thePrefs.webServerSessionTimeout()));
    prefs.insert(QStringLiteral("webServerHttpsEnabled"), thePrefs.webServerHttpsEnabled());
    prefs.insert(QStringLiteral("webServerCertPath"), thePrefs.webServerCertPath());
    prefs.insert(QStringLiteral("webServerKeyPath"), thePrefs.webServerKeyPath());
    prefs.insert(QStringLiteral("webServerAdminPassword"), thePrefs.webServerAdminPassword());
    prefs.insert(QStringLiteral("webServerAdminAllowHiLevFunc"), thePrefs.webServerAdminAllowHiLevFunc());
    prefs.insert(QStringLiteral("webServerGuestEnabled"), thePrefs.webServerGuestEnabled());
    prefs.insert(QStringLiteral("webServerGuestPassword"), thePrefs.webServerGuestPassword());

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(prefs)));
}

void IpcClientHandler::handleSetPreferences(const IpcMessage& msg)
{
    // Fields come in key-value pairs: [key1, val1, key2, val2, ...]
    for (int i = 0; i + 1 < msg.fieldCount(); i += 2) {
        const QString key = msg.fieldString(i);
        const QCborValue val = msg.field(i + 1);

        if (!applyPreferenceA(key, val) && !applyPreferenceB(key, val))
            applyPreferenceC(key, val);
    }
    // Detect whether shared directory settings changed before saving
    bool sharedDirsChanged = false;
    for (int i = 0; i + 1 < msg.fieldCount(); i += 2) {
        const QString k = msg.fieldString(i);
        if (k == QStringLiteral("incomingDir")
            || k == QStringLiteral("sharedDirs")
            || k == QStringLiteral("tempDirs")) {
            sharedDirsChanged = true;
            break;
        }
    }

    thePrefs.save();

    // Update scheduler baselines so restoreOriginals() doesn't revert these changes
    if (theApp.scheduler)
        theApp.scheduler->saveOriginals();

    // Notify web server config changes
    emit webServerConfigChanged();

    // Propagate config changes to running ServerConnect
    if (theApp.serverConnect) {
        auto cfg = theApp.serverConnect->config();
        cfg.userNick               = thePrefs.nick();
        cfg.reconnectOnDisconnect  = thePrefs.reconnect();
        cfg.safeServerConnect      = thePrefs.safeServerConnect();
        cfg.cryptLayerPreferred    = thePrefs.cryptLayerRequested();
        cfg.cryptLayerRequired     = thePrefs.cryptLayerRequired();
        cfg.cryptLayerEnabled      = thePrefs.cryptLayerSupported();
        cfg.autoConnectStaticOnly   = thePrefs.autoConnectStaticOnly();
        cfg.useServerPriorities    = thePrefs.useServerPriorities();
        cfg.addServersFromServer   = thePrefs.addServersFromServer();
        cfg.serverKeepAliveTimeout = thePrefs.serverKeepAliveTimeout();
        cfg.listenPort             = thePrefs.port();
        cfg.smartLowIdCheck        = thePrefs.smartLowIdCheck();
        theApp.serverConnect->setConfig(cfg);
    }

    // Rescan shared files only if directory settings actually changed
    if (sharedDirsChanged && theApp.sharedFileList)
        theApp.sharedFileList->reload();

    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleSubscribe(const IpcMessage& msg)
{
    // ToDo: honour the mask. It is recorded but never read — IpcServer::broadcast
    // sends every push to every handshaked client regardless of what was requested.
    // It reads like a working filter and is not, so a client that subscribes
    // narrowly still pays for everything.
    m_subscriptionMask = static_cast<int>(msg.fieldInt(0));
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleGetKadContacts(const IpcMessage& msg)
{
    QCborArray contacts;
    auto* kad = kad::Kademlia::instance();
    if (kad && kad->isRunning()) {
        auto* zone = kad->getRoutingZone();
        if (zone) {
            kad::ContactArray allContacts;
            zone->getAllEntries(allContacts);
            for (const auto* c : allContacts) {
                QCborMap m;
                m.insert(QStringLiteral("clientId"), c->getClientID().toHexString());
                m.insert(QStringLiteral("distance"), c->getDistance().toBinaryString());
                m.insert(QStringLiteral("ip"), static_cast<qint64>(c->address().toUint32()));
                m.insert(QStringLiteral("addr"), c->address().toString());   // IPv6-capable form
                m.insert(QStringLiteral("udpPort"), c->getUDPPort());
                m.insert(QStringLiteral("tcpPort"), c->getTCPPort());
                m.insert(QStringLiteral("version"), c->getVersion());
                m.insert(QStringLiteral("type"), c->getType());
                contacts.append(m);
            }
        }
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(contacts)));
}

void IpcClientHandler::handleGetKadStatus(const IpcMessage& msg)
{
    QCborMap status;
    auto* kad = kad::Kademlia::instance();
    status.insert(QStringLiteral("running"), kad && kad->isRunning());
    status.insert(QStringLiteral("connected"), kad && kad->isConnected());
    status.insert(QStringLiteral("firewalled"), kad && kad->isFirewalled());

    if (kad && kad->isRunning()) {
        auto* zone = kad->getRoutingZone();
        if (zone) {
            kad::ContactArray allContacts;
            zone->getAllEntries(allContacts);
            status.insert(QStringLiteral("contactCount"),
                          static_cast<qint64>(allContacts.size()));
        }
        auto* udp = kad->getUDPListener();
        if (udp) {
            status.insert(QStringLiteral("hellosSent"),
                          static_cast<qint64>(udp->totalHellosSent()));
            status.insert(QStringLiteral("hellosReceived"),
                          static_cast<qint64>(udp->totalHellosReceived()));
        }
        status.insert(QStringLiteral("users"),
                      static_cast<qint64>(kad->getKademliaUsers()));
        status.insert(QStringLiteral("usersExperimental"),
                      static_cast<qint64>(kad->getKademliaUsers(true)));
        status.insert(QStringLiteral("files"),
                      static_cast<qint64>(kad->getKademliaFiles()));
    }
    if (kad && kad->isConnected()) {
        status.insert(QStringLiteral("udpFirewalled"),
                      kad::UDPFirewallTester::isFirewalledUDP(true));
        status.insert(QStringLiteral("udpVerified"),
                      kad::UDPFirewallTester::isVerified());
        auto* prefs = kad->getPrefs();
        if (prefs) {
            status.insert(QStringLiteral("ip"),
                          static_cast<qint64>(prefs->ipAddress()));
            // ID is the IP in eD2K byte order (first octet in LSB)
            const uint32_t kadIp = prefs->ipAddress();
            const uint32_t ed2kId = ((kadIp & 0xFF) << 24) | ((kadIp & 0xFF00) << 8)
                                  | ((kadIp >> 8) & 0xFF00) | ((kadIp >> 24) & 0xFF);
            status.insert(QStringLiteral("id"),
                          static_cast<qint64>(ed2kId));
            status.insert(QStringLiteral("internPort"), prefs->internKadPort());
            status.insert(QStringLiteral("externPort"),
                          prefs->useExternKadPort()
                              ? prefs->externalKadPort() : 0);
        }
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(status)));
}

void IpcClientHandler::handleBootstrapKad(const IpcMessage& msg)
{
    auto* kad = kad::Kademlia::instance();
    if (!kad) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503,
            QStringLiteral("Kademlia unavailable")));
        return;
    }

    const QString ip = msg.fieldString(0);
    const auto port = static_cast<uint16>(msg.fieldInt(1));

    if (!ip.isEmpty() && port > 0) {
        kad->bootstrap(ip, port);
    } else if (!kad->isRunning()) {
        kad->start();
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleDisconnectKad(const IpcMessage& msg)
{
    auto* kad = kad::Kademlia::instance();
    if (kad && kad->isRunning())
        kad->stop();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleRecheckFirewall(const IpcMessage& msg)
{
    // Re-race the port-mapping backends too: "am I reachable" and "is my port
    // forwarded" are the same question to a user, and a stale mapping is a very
    // common reason a firewall re-check keeps coming back negative. No new IPC
    // opcode needed.
    if (theApp.portMapper)
        theApp.portMapper->reprobe();

    if (rejectIfKadUnavailable(msg, /*requireConnected*/ false))
        return;

    switch (kad::Kademlia::instance()->recheckFirewalled()) {
    case kad::RecheckFirewallResult::Started:
        sendMessage(IpcMessage::makeResult(msg.seqId(), true));
        break;
    case kad::RecheckFirewallResult::AlreadyRunning:
        sendMessage(IpcMessage::makeResult(msg.seqId(), false,
            QCborValue(tr("A firewall re-check is already in progress.\n\n"
                          "Please wait for the current check to finish."))));
        break;
    case kad::RecheckFirewallResult::LanMode:
        sendMessage(IpcMessage::makeResult(msg.seqId(), false,
            QCborValue(tr("Kad is running in LAN mode — firewall checks are disabled."))));
        break;
    case kad::RecheckFirewallResult::NotRunning:
        sendMessage(IpcMessage::makeResult(msg.seqId(), false,
            QCborValue(tr("Kad is not running.\n\nConnect to the Kad network first."))));
        break;
    }
}

void IpcClientHandler::handleGetKadSearches(const IpcMessage& msg)
{
    QCborArray searches;
    auto* kad = kad::Kademlia::instance();
    if (kad) {
        for (const auto& [id, search] : kad::SearchManager::getSearches()) {
            QCborMap m;
            m.insert(QStringLiteral("searchId"), static_cast<qint64>(search->getSearchID()));
            m.insert(QStringLiteral("key"), search->getTarget().toHexString());
            m.insert(QStringLiteral("type"), kad::Search::getTypeName(search->getSearchType()));
            m.insert(QStringLiteral("name"), search->getGUIName());
            m.insert(QStringLiteral("status"),
                     search->stopping() ? QStringLiteral("Stopping") : QStringLiteral("Active"));
            m.insert(QStringLiteral("load"), static_cast<qint64>(search->getNodeLoad()));
            m.insert(QStringLiteral("packetsSent"), static_cast<qint64>(search->getKadPacketSent()));
            m.insert(QStringLiteral("requestAnswers"), static_cast<qint64>(search->getRequestAnswer()));
            m.insert(QStringLiteral("responses"), static_cast<qint64>(search->getAnswers()));
            searches.append(m);
        }
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(searches)));
}

void IpcClientHandler::handleGetKadLookupHistory(const IpcMessage& msg)
{
    const uint32_t searchId = static_cast<uint32_t>(msg.fieldInt(0));
    QCborArray entries;

    auto* kad = kad::Kademlia::instance();
    if (kad) {
        // Find the search with the given ID
        for (const auto& [id, search] : kad::SearchManager::getSearches()) {
            if (search->getSearchID() == searchId) {
                auto* history = search->getLookupHistory();
                if (history) {
                    auto& he = history->getHistoryEntries();
                    for (size_t i = 0; i < he.size(); ++i) {
                        const auto* e = he[i];
                        QCborMap m;
                        m.insert(QStringLiteral("contactID"), e->contactID.toHexString());
                        m.insert(QStringLiteral("distance"), e->distance.toHexString());
                        m.insert(QStringLiteral("contactVersion"), static_cast<qint64>(e->contactVersion));
                        m.insert(QStringLiteral("askedContactsTime"), static_cast<qint64>(e->askedContactsTime));
                        m.insert(QStringLiteral("respondedContact"), static_cast<qint64>(e->respondedContact));
                        m.insert(QStringLiteral("askedSearchItemTime"), static_cast<qint64>(e->askedSearchItemTime));
                        m.insert(QStringLiteral("respondedSearchItem"), static_cast<qint64>(e->respondedSearchItem));
                        m.insert(QStringLiteral("providedCloser"), e->providedCloser);
                        m.insert(QStringLiteral("forcedInteresting"), e->forcedInteresting);

                        // Encode receivedFromIdx as CBOR array
                        QCborArray fromArr;
                        for (int idx : e->receivedFromIdx)
                            fromArr.append(static_cast<qint64>(idx));
                        m.insert(QStringLiteral("receivedFromIdx"), fromArr);

                        // 128-bit distance as 4x uint32 chunks for scaling
                        m.insert(QStringLiteral("dist0"), static_cast<qint64>(e->distance.get32BitChunk(0)));
                        m.insert(QStringLiteral("dist1"), static_cast<qint64>(e->distance.get32BitChunk(1)));
                        m.insert(QStringLiteral("dist2"), static_cast<qint64>(e->distance.get32BitChunk(2)));
                        m.insert(QStringLiteral("dist3"), static_cast<qint64>(e->distance.get32BitChunk(3)));

                        entries.append(m);
                    }
                }
                break;
            }
        }
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(entries)));
}

void IpcClientHandler::handleGetNetworkInfo(const IpcMessage& msg)
{
    QCborMap info;

    // -- Client section -------------------------------------------------------
    QCborMap client;
    client.insert(QStringLiteral("nick"), thePrefs.nick());
    const auto hash = thePrefs.userHash();
    client.insert(QStringLiteral("hash"), md4str(hash.data()));
    client.insert(QStringLiteral("tcpPort"), thePrefs.port());
    client.insert(QStringLiteral("udpPort"), thePrefs.udpPort());
    info.insert(QStringLiteral("client"), client);

    // -- eD2K section ---------------------------------------------------------
    QCborMap ed2k;
    // ED2K-only: this feeds the "ed2k" section, reported separately from "kad" below.
    const bool ed2kConnected = theApp.serverConnect && theApp.serverConnect->isConnected();
    const bool ed2kConnecting = theApp.serverConnect && theApp.serverConnect->isConnecting();
    const bool ed2kFirewalled = theApp.isFirewalled();
    ed2k.insert(QStringLiteral("connected"), ed2kConnected);
    ed2k.insert(QStringLiteral("connecting"), ed2kConnecting);
    ed2k.insert(QStringLiteral("firewalled"), ed2kFirewalled);

    // Reported whether or not a server session exists: this comes from LocalIPv6 scanning the
    // local interfaces at startup, not from the server handshake. Gating it on ed2kConnected
    // would withhold it exactly when someone is diagnosing why they cannot connect. Empty when
    // the host has no usable public IPv6.
    ed2k.insert(QStringLiteral("publicIPv6"), theApp.publicIPv6().toString());
    // Dotted-quad form of publicIP, so callers that just need a literal (the port test URL) do
    // not each reimplement the ED2K byte order. Empty until a server tells us our IPv4.
    ed2k.insert(QStringLiteral("publicIPv4"),
                theApp.publicIP() != 0 ? ipstr(theApp.publicIP()) : QString());

    if (ed2kConnected && theApp.serverConnect) {
        ed2k.insert(QStringLiteral("clientID"),
                     static_cast<qint64>(theApp.serverConnect->clientID()));
        ed2k.insert(QStringLiteral("lowID"), theApp.serverConnect->isLowID());
        ed2k.insert(QStringLiteral("publicIP"),
                     static_cast<qint64>(theApp.publicIP()));

        // Total users/files across all servers
        if (theApp.serverList) {
            uint32 totalUsers = 0, totalFiles = 0;
            for (const auto& srv : theApp.serverList->servers()) {
                totalUsers += srv->users();
                totalFiles += srv->files();
            }
            ed2k.insert(QStringLiteral("totalUsers"), static_cast<qint64>(totalUsers));
            ed2k.insert(QStringLiteral("totalFiles"), static_cast<qint64>(totalFiles));
        }

        // Current server details
        const auto* srv = theApp.serverConnect->currentServer();
        if (srv) {
            QCborMap server;
            server.insert(QStringLiteral("name"), srv->name());
            server.insert(QStringLiteral("description"), srv->description());
            server.insert(QStringLiteral("address"), srv->address());
            server.insert(QStringLiteral("addr"), srv->ipAddress().toString());   // IPv6-capable
            server.insert(QStringLiteral("port"), srv->port());
            server.insert(QStringLiteral("version"), srv->version());
            server.insert(QStringLiteral("users"), static_cast<qint64>(srv->users()));
            server.insert(QStringLiteral("files"), static_cast<qint64>(srv->files()));
            server.insert(QStringLiteral("obfuscated"),
                          theApp.serverConnect->isConnectedObfuscated());
            server.insert(QStringLiteral("lowIDUsers"),
                          static_cast<qint64>(srv->lowIDUsers()));
            server.insert(QStringLiteral("ping"), static_cast<qint64>(srv->ping()));
            server.insert(QStringLiteral("softFiles"),
                          static_cast<qint64>(srv->softFiles()));
            server.insert(QStringLiteral("hardFiles"),
                          static_cast<qint64>(srv->hardFiles()));
            server.insert(QStringLiteral("tcpFlags"),
                          static_cast<qint64>(srv->tcpFlags()));
            server.insert(QStringLiteral("udpFlags"),
                          static_cast<qint64>(srv->udpFlags()));
            ed2k.insert(QStringLiteral("server"), server);
        }
    }
    info.insert(QStringLiteral("ed2k"), ed2k);

    // -- Kad section ----------------------------------------------------------
    QCborMap kadInfo;
    auto* kad = kad::Kademlia::instance();
    const bool kadRunning = kad && kad->isRunning();
    const bool kadConnected = kad && kad->isConnected();
    const bool kadFirewalled = kad && kad->isFirewalled();

    kadInfo.insert(QStringLiteral("running"), kadRunning);
    kadInfo.insert(QStringLiteral("connected"), kadConnected);
    kadInfo.insert(QStringLiteral("firewalled"), kadFirewalled);

    if (kadConnected && kad) {
        kadInfo.insert(QStringLiteral("udpFirewalled"),
                       kad::UDPFirewallTester::isFirewalledUDP(true));
        kadInfo.insert(QStringLiteral("udpVerified"),
                       kad::UDPFirewallTester::isVerified());

        auto* prefs = kad->getPrefs();
        if (prefs) {
            kadInfo.insert(QStringLiteral("ip"),
                           static_cast<qint64>(prefs->ipAddress()));
            kadInfo.insert(QStringLiteral("id"),
                           static_cast<qint64>(prefs->ipAddress()));
            kadInfo.insert(QStringLiteral("hash"),
                           prefs->kadId().toHexString());
            kadInfo.insert(QStringLiteral("internPort"), prefs->internKadPort());
            kadInfo.insert(QStringLiteral("externPort"),
                           prefs->useExternKadPort()
                               ? prefs->externalKadPort() : 0);
        }

        kadInfo.insert(QStringLiteral("users"),
                       static_cast<qint64>(kad->getKademliaUsers()));
        kadInfo.insert(QStringLiteral("usersExperimental"),
                       static_cast<qint64>(kad->getKademliaUsers(true)));
        kadInfo.insert(QStringLiteral("files"),
                       static_cast<qint64>(kad->getKademliaFiles()));

        auto* indexed = kad->getIndexed();
        if (indexed) {
            QCborMap idx;
            idx.insert(QStringLiteral("source"),
                       static_cast<qint64>(indexed->m_totalIndexSource));
            idx.insert(QStringLiteral("keyword"),
                       static_cast<qint64>(indexed->m_totalIndexKeyword));
            idx.insert(QStringLiteral("notes"),
                       static_cast<qint64>(indexed->m_totalIndexNotes));
            idx.insert(QStringLiteral("load"),
                       static_cast<qint64>(indexed->m_totalIndexLoad));
            kadInfo.insert(QStringLiteral("indexed"), idx);
        }
    }
    info.insert(QStringLiteral("kad"), kadInfo);

    // Port mapping — reported alongside the firewall state because they answer
    // the same user question, and because a Degraded mapping is precisely the
    // case where "port forwarded" and "still firewalled" are both true.
    QCborMap portMapInfo;
    if (theApp.portMapper != nullptr) {
        const PortMapper* mapper = theApp.portMapper;
        portMapInfo.insert(QStringLiteral("status"), static_cast<int>(mapper->status()));
        portMapInfo.insert(QStringLiteral("statusText"), portMapStatusName(mapper->status()));
        portMapInfo.insert(QStringLiteral("method"), static_cast<int>(mapper->activeMethod()));
        portMapInfo.insert(QStringLiteral("methodText"),
                           portMapMethodName(mapper->activeMethod()));
        portMapInfo.insert(QStringLiteral("externalAddress"),
                           mapper->externalAddress().toString());

        QCborArray mappings;
        for (const PortMapping& mapping : mapper->mappings()) {
            QCborMap entry;
            entry.insert(QStringLiteral("purpose"),
                         portMapPurposeName(mapping.request.purpose));
            entry.insert(QStringLiteral("protocol"),
                         mapping.request.protocol == PortMapProtocol::Udp
                             ? QStringLiteral("UDP") : QStringLiteral("TCP"));
            entry.insert(QStringLiteral("family"),
                         mapping.request.family == PortMapFamily::IPv6 ? 6 : 4);
            entry.insert(QStringLiteral("internalPort"), mapping.request.internalPort);
            entry.insert(QStringLiteral("externalPort"), mapping.externalPort);
            entry.insert(QStringLiteral("lifetime"),
                         static_cast<qint64>(mapping.lifetimeSecs));
            entry.insert(QStringLiteral("usable"), mapping.isUsable());
            mappings.append(entry);
        }
        portMapInfo.insert(QStringLiteral("mappings"), mappings);
    } else {
        portMapInfo.insert(QStringLiteral("status"),
                           static_cast<int>(PortMapStatus::Disabled));
        portMapInfo.insert(QStringLiteral("statusText"),
                           portMapStatusName(PortMapStatus::Disabled));
    }
    info.insert(QStringLiteral("portmap"), portMapInfo);

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(info)));
}

void IpcClientHandler::handleSyncLogs(const IpcMessage& msg)
{
    const int64_t lastLogId = msg.fieldInt(0);
    auto entries = DaemonApp::logsSince(lastLogId);

    QCborArray arr;
    for (const auto& e : entries) {
        QCborArray entry;
        entry.append(static_cast<qint64>(e.id));
        entry.append(e.category);
        entry.append(static_cast<qint64>(e.severity));
        entry.append(e.message);
        entry.append(e.timestamp);
        arr.append(entry);
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(arr)));
}

void IpcClientHandler::handleShutdown(const IpcMessage& msg)
{
    logInfo(QStringLiteral("Shutdown requested by IPC client"));
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));

    // Schedule graceful quit on the next event loop iteration so the
    // response frame is flushed to the socket before we tear down.
    QMetaObject::invokeMethod(QCoreApplication::instance(),
                              &QCoreApplication::quit, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// handleGetSchedules — return scheduler enabled flag + all schedule entries
// ---------------------------------------------------------------------------

void IpcClientHandler::handleGetSchedules(const IpcMessage& msg)
{
    QCborMap result;
    result.insert(QStringLiteral("enabled"), thePrefs.schedulerEnabled());

    QCborArray schedArr;
    if (theApp.scheduler) {
        for (int i = 0; i < theApp.scheduler->count(); ++i) {
            auto* entry = theApp.scheduler->schedule(i);
            if (!entry) continue;

            QCborMap sched;
            sched.insert(QStringLiteral("title"), entry->title);
            sched.insert(QStringLiteral("startTime"), static_cast<qint64>(entry->startTime));
            sched.insert(QStringLiteral("endTime"), static_cast<qint64>(entry->endTime));
            sched.insert(QStringLiteral("day"), static_cast<int>(entry->day));
            sched.insert(QStringLiteral("enabled"), entry->enabled);

            QCborArray actArr;
            for (size_t a = 0; a < 16; ++a) {
                if (entry->actions[a] == ScheduleAction::None)
                    break;
                QCborMap actMap;
                actMap.insert(QStringLiteral("action"), static_cast<int>(entry->actions[a]));
                actMap.insert(QStringLiteral("value"), entry->values[a]);
                actArr.append(actMap);
            }
            sched.insert(QStringLiteral("actions"), actArr);
            schedArr.append(sched);
        }
    }
    result.insert(QStringLiteral("schedules"), schedArr);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(result)));
}

// ---------------------------------------------------------------------------
// handleSaveSchedules — replace all schedules from GUI data
// ---------------------------------------------------------------------------

void IpcClientHandler::handleSaveSchedules(const IpcMessage& msg)
{
    bool enabled = msg.fieldBool(0);
    thePrefs.setSchedulerEnabled(enabled);

    if (theApp.scheduler) {
        theApp.scheduler->restoreOriginals();
        theApp.scheduler->removeAll();

        const QCborArray schedArr = msg.fieldArray(1);
        for (const auto& item : schedArr) {
            const QCborMap m = item.toMap();
            auto entry = std::make_unique<ScheduleEntry>();
            entry->title = m.value(QStringLiteral("title")).toString();
            entry->startTime = static_cast<time_t>(m.value(QStringLiteral("startTime")).toInteger());
            entry->endTime = static_cast<time_t>(m.value(QStringLiteral("endTime")).toInteger());
            entry->day = static_cast<ScheduleDay>(m.value(QStringLiteral("day")).toInteger());
            entry->enabled = m.value(QStringLiteral("enabled")).toBool();

            const QCborArray actArr = m.value(QStringLiteral("actions")).toArray();
            for (qsizetype a = 0; a < actArr.size() && a < 16; ++a) {
                const QCborMap actMap = actArr.at(a).toMap();
                entry->actions[static_cast<size_t>(a)] = static_cast<ScheduleAction>(actMap.value(QStringLiteral("action")).toInteger());
                entry->values[static_cast<size_t>(a)] = actMap.value(QStringLiteral("value")).toString();
            }
            theApp.scheduler->addSchedule(std::move(entry));
        }

        theApp.scheduler->saveToFile(thePrefs.configDir());
        theApp.scheduler->saveOriginals();

        if (enabled)
            theApp.scheduler->check(true);
    }

    thePrefs.save();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleScanImportFolder — scan folder for convertible files and return jobs
// ---------------------------------------------------------------------------

void IpcClientHandler::handleScanImportFolder(const IpcMessage& msg)
{
    const QString folder = msg.fieldString(0);
    const bool removeSource = msg.fieldBool(1);

    PartFileConvert::scanFolderToAdd(folder, /*recursive=*/true, removeSource);
    PartFileConvert::processQueue();

    // Return the full job list
    QCborArray arr;
    const int count = PartFileConvert::jobCount();
    for (int i = 0; i < count; ++i) {
        const auto job = PartFileConvert::jobAt(i);
        QCborMap m;
        m.insert(QStringLiteral("filename"), job.filename);
        m.insert(QStringLiteral("folder"), job.folder);
        m.insert(QStringLiteral("state"), static_cast<int>(job.state));
        m.insert(QStringLiteral("size"), static_cast<qint64>(job.size));
        m.insert(QStringLiteral("fileHash"), job.fileHash);
        m.insert(QStringLiteral("format"), job.format);
        arr.append(QCborValue(m));
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(arr)));
}

// ---------------------------------------------------------------------------
// handleGetConvertJobs — return snapshot of all conversion jobs
// ---------------------------------------------------------------------------

void IpcClientHandler::handleGetConvertJobs(const IpcMessage& msg)
{
    QCborArray arr;
    const int count = PartFileConvert::jobCount();
    for (int i = 0; i < count; ++i) {
        const auto job = PartFileConvert::jobAt(i);
        QCborMap m;
        m.insert(QStringLiteral("filename"), job.filename);
        m.insert(QStringLiteral("folder"), job.folder);
        m.insert(QStringLiteral("state"), static_cast<int>(job.state));
        m.insert(QStringLiteral("size"), static_cast<qint64>(job.size));
        m.insert(QStringLiteral("fileHash"), job.fileHash);
        m.insert(QStringLiteral("format"), job.format);
        arr.append(QCborValue(m));
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(arr)));
}

// ---------------------------------------------------------------------------
// handleRemoveConvertJob — remove a non-in-progress job by index
// ---------------------------------------------------------------------------

void IpcClientHandler::handleRemoveConvertJob(const IpcMessage& msg)
{
    const int index = static_cast<int>(msg.fieldInt(0));
    if (index < 0 || index >= PartFileConvert::jobCount()) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400,
            QStringLiteral("Invalid job index: %1").arg(index)));
        return;
    }

    const auto job = PartFileConvert::jobAt(index);
    if (job.state == ConvertStatus::InProgress) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400,
            QStringLiteral("Cannot remove in-progress job")));
        return;
    }

    PartFileConvert::removeJob(index);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleRetryConvertJob — re-queue a failed/completed job by index
// ---------------------------------------------------------------------------

void IpcClientHandler::handleRetryConvertJob(const IpcMessage& msg)
{
    const int index = static_cast<int>(msg.fieldInt(0));
    if (index < 0 || index >= PartFileConvert::jobCount()) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400,
            QStringLiteral("Invalid job index: %1").arg(index)));
        return;
    }

    PartFileConvert::retryJob(index);
    PartFileConvert::processQueue();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleStopDownload — stop (not pause) a download
// ---------------------------------------------------------------------------

void IpcClientHandler::handleStopDownload(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* pf = theApp.downloadQueue->fileByID(hashBuf);
    if (!pf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Download not found")));
        return;
    }
    pf->stopFile(false);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleOpenDownloadFile — open a completed/partial file on daemon host
// ---------------------------------------------------------------------------

void IpcClientHandler::handleOpenDownloadFile(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    const auto* pf = theApp.downloadQueue->fileByID(hashBuf);
    if (!pf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Download not found")));
        return;
    }
    const QString path = pf->filePath().isEmpty() ? pf->fullName() : pf->filePath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("File not found on disk")));
        return;
    }
    if (!openPathWithDefaultApp(path)) {
        logWarning(QStringLiteral("Failed to open file with default app: %1").arg(path));
        sendMessage(IpcMessage::makeError(msg.seqId(), 500, QStringLiteral("Failed to launch default application")));
        return;
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleOpenDownloadFolder — open folder containing a download
// ---------------------------------------------------------------------------

void IpcClientHandler::handleOpenDownloadFolder(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    const auto* pf = theApp.downloadQueue->fileByID(hashBuf);
    if (!pf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Download not found")));
        return;
    }
    const QString path = pf->filePath().isEmpty() ? pf->fullName() : pf->filePath();
    const QString folder = QFileInfo(path).absolutePath();
    if (folder.isEmpty() || !QFileInfo::exists(folder)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Folder not found")));
        return;
    }
    if (!openPathWithDefaultApp(folder)) {
        logWarning(QStringLiteral("Failed to open folder with default app: %1").arg(folder));
        sendMessage(IpcMessage::makeError(msg.seqId(), 500, QStringLiteral("Failed to launch file manager")));
        return;
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleMarkSearchSpam — mark a search result as spam
// ---------------------------------------------------------------------------

void IpcClientHandler::handleMarkSearchSpam(const IpcMessage& msg)
{
    const auto searchID = static_cast<uint32>(msg.fieldInt(0));
    const QString hash = msg.fieldString(1);

    if (!theApp.searchList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Search list unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* file = theApp.searchList->searchFileByHash(hashBuf, searchID);
    if (!file) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Search file not found")));
        return;
    }

    // Field 2 is the direction, so the GUI can offer MFC's "Mark as not Spam".
    // Absent (older sender) means mark-as-spam, the only behaviour there used to be.
    const bool isSpam = (msg.fieldCount() < 3) || msg.fieldBool(2);
    if (isSpam)
        theApp.searchList->markFileAsSpam(file, true);
    else
        theApp.searchList->markFileAsNotSpam(file, true);

    // MFC re-scores the whole tab afterwards (SearchListCtrl.cpp:845-866): a file
    // moving in or out of the filter changes the ratings of its neighbours.
    theApp.searchList->recalculateSpamRatings(searchID);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleResetStats — reset session statistics
// ---------------------------------------------------------------------------

void IpcClientHandler::handleResetStats(const IpcMessage& msg)
{
    // Bank the session before backing up, so the backup holds the totals the user
    // was looking at rather than the last flush's. The flush is idempotent, so
    // this costs nothing (MFC's SaveStats(1) likewise writes cum + session).
    flushCumulativeStats(thePrefs);
    thePrefs.backupCumulativeStats();

    // MFC's reset zeroes the whole cumulative block and stamps the time
    // (CPreferences::ResetCumulativeStatistics, srchybrid/Preferences.cpp:1095-1168).
    // The session counters are deliberately left running, as they are there too.
    thePrefs.resetCumulativeStats(static_cast<uint64>(QDateTime::currentSecsSinceEpoch()));
    thePrefs.save();

    if (theApp.statistics) {
        theApp.statistics->resetDownDatarateOverhead();
        theApp.statistics->resetUpDatarateOverhead();
        // Re-read the now-zero cumulative baseline and rate records. Without this
        // the next flush would write the pre-reset totals straight back.
        theApp.statistics->init(thePrefs);
    }
    // Clears the traces in every attached GUI: reset() bumps the epoch, which is
    // what tells a viewer to drop its buffer instead of appending to it.
    if (theApp.statsHistory)
        theApp.statsHistory->reset();

    logInfo(QStringLiteral("Statistics have been reset!"));
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleRestoreStats — put back the cumulative totals a reset saved
// ---------------------------------------------------------------------------

void IpcClientHandler::handleRestoreStats(const IpcMessage& msg)
{
    // The current totals become the new backup (Preferences swaps the two files),
    // so this has to bank the session first for the same reason the reset does.
    flushCumulativeStats(thePrefs);

    if (!thePrefs.restoreCumulativeStats()) {
        logError(QStringLiteral("ERROR: The backup statistics file was not found..."));
        sendMessage(IpcMessage::makeError(msg.seqId(), 404,
                                          QStringLiteral("No statistics backup to restore")));
        return;
    }
    thePrefs.save();

    // Rebase onto the restored totals, or the next flush writes the pre-restore
    // ones straight back — the same trap handleResetStats documents. The graphs
    // and the session counters are left alone: MFC's restore only ever touches
    // the cumulative branch (srchybrid/StatisticsTree.cpp:175-185).
    if (theApp.statistics)
        theApp.statistics->init(thePrefs);

    logInfo(QStringLiteral("Loaded backed up statistical data..."));
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// HTTP Cache configuration links
//
// Both handlers below answer only once GET /v1/info has come back, which makes
// them the only asynchronous ones in this file. Everything they touch afterwards
// is reached through a QPointer to `this`: the handler is destroyed with its
// connection, and a user who closes the GUI mid-probe must not take the daemon
// with them.
//
// The secret never appears in a log line, an error string or a reply. See
// docs/protocol/http-cache-spec.md §8.1.
// ---------------------------------------------------------------------------

void IpcClientHandler::handleProbeHttpCacheServer(const IpcMessage& msg)
{
    const QString baseUrl = msg.fieldString(0);
    const QString secret  = msg.fieldString(1);

    // Answered from the stored configuration, not from the server: whether this
    // link would change anything is a local question, and asking it here is what
    // lets the clipboard watcher stay quiet about a link that is already applied.
    QString clean = baseUrl.trimmed();
    while (clean.endsWith(QLatin1Char('/')))
        clean.chop(1);
    const bool unchanged = !clean.isEmpty()
        && clean == thePrefs.httpCacheBaseUrl()
        && secret == thePrefs.httpCacheApiKey()
        && thePrefs.httpCacheEnabled()
        && thePrefs.httpCacheAllowUpload();

    const QPointer<IpcClientHandler> self(this);
    const int seqId = msg.seqId();
    const QString currentBaseUrl = thePrefs.httpCacheBaseUrl();

    HttpCacheServerProbe::probe(baseUrl, this,
        [self, seqId, unchanged, currentBaseUrl](const HttpCacheServerInfo& info) {
            if (!self)
                return;

            QCborMap out;
            out[QStringLiteral("ok")]                 = info.ok;
            out[QStringLiteral("error")]              = info.error;
            out[QStringLiteral("service")]            = info.service;
            out[QStringLiteral("version")]            = info.version;
            out[QStringLiteral("implementation")]     = info.implementation;
            out[QStringLiteral("uploadRequiresAuth")] = info.uploadRequiresAuth;
            out[QStringLiteral("maxChunkSize")]       = static_cast<qint64>(info.maxChunkSize);
            out[QStringLiteral("currentBaseUrl")]     = currentBaseUrl;
            out[QStringLiteral("unchanged")]          = unchanged;

            self->sendMessage(IpcMessage::makeResult(seqId, true, QCborValue(out)));
        });
}

void IpcClientHandler::handleApplyHttpCacheConfig(const IpcMessage& msg)
{
    const QString baseUrl = msg.fieldString(0);
    const QString secret  = msg.fieldString(1);

    if (baseUrl.isEmpty() || secret.isEmpty()) {
        sendMessage(IpcMessage::makeResult(
            msg.seqId(), false,
            QCborValue(QStringLiteral("Incomplete HTTP Cache configuration"))));
        return;
    }

    const QPointer<IpcClientHandler> self(this);
    const int seqId = msg.seqId();

    // Probed again here even when the caller just probed. This is the handler
    // that writes, so this is where "handshake before you store anything" has to
    // hold — and --add-link reaches it without probing at all.
    HttpCacheServerProbe::probe(baseUrl, this,
        [self, seqId, baseUrl, secret](const HttpCacheServerInfo& info) {
            if (!self)
                return;

            if (!info.ok) {
                self->sendMessage(IpcMessage::makeResult(seqId, false, QCborValue(info.error)));
                return;
            }

            thePrefs.setHttpCacheBaseUrl(baseUrl);
            thePrefs.setHttpCacheApiKey(secret);
            thePrefs.setHttpCacheEnabled(true);
            thePrefs.setHttpCacheAllowUpload(true);
            thePrefs.save();

            // No restart needed: HttpCacheManager is always constructed and reads
            // these every tick (CoreSession.cpp), and a changed baseUrl/apiKey
            // clears any publish backoff on its own.
            logInfo(QStringLiteral("HTTP Cache: configured for %1 (%2), uploads enabled")
                        .arg(QUrl(baseUrl).host(),
                             info.implementation.isEmpty() ? QStringLiteral("v%1").arg(info.version)
                                                           : info.implementation));

            self->sendMessage(IpcMessage::makeResult(seqId, true, QCborValue(QString())));
        });
}

// ---------------------------------------------------------------------------
// handleRenameSharedFile — rename a shared file on disk
// ---------------------------------------------------------------------------

void IpcClientHandler::handleRenameSharedFile(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    const QString newName = msg.fieldString(1);

    if (!theApp.sharedFileList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Shared files unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* file = theApp.sharedFileList->getFileByID(hashBuf);
    if (!file) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Shared file not found")));
        return;
    }
    if (newName.isEmpty()) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("New name cannot be empty")));
        return;
    }
    const QString oldPath = file->filePath();
    const QString dir = QFileInfo(oldPath).absolutePath();
    const QString newPath = dir + QDir::separator() + newName;
    if (!QFile::rename(oldPath, newPath)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 500, QStringLiteral("Rename failed")));
        return;
    }
    file->setFileName(newName);
    file->setFilePath(newPath);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleDeleteSharedFile — delete a shared file from disk
// ---------------------------------------------------------------------------

void IpcClientHandler::handleDeleteSharedFile(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);

    if (!theApp.sharedFileList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Shared files unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* file = theApp.sharedFileList->getFileByID(hashBuf);
    if (!file) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Shared file not found")));
        return;
    }
    const QString path = file->filePath();
    theApp.sharedFileList->removeFile(file);
    QFile::remove(path);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleUnshareFile — remove from shared list but keep on disk
// ---------------------------------------------------------------------------

void IpcClientHandler::handleUnshareFile(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);

    if (!theApp.sharedFileList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Shared files unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* file = theApp.sharedFileList->getFileByID(hashBuf);
    if (!file) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Shared file not found")));
        return;
    }

    // Through excludeFile(), not removeFile(): dropping it from the map alone lasts
    // only until the next directory scan puts it straight back.
    if (!theApp.sharedFileList->excludeFile(file->filePath())) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 403,
            QStringLiteral("This file cannot be unshared")));
        return;
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleSetFileShared — share/unshare one file by path
// ---------------------------------------------------------------------------

void IpcClientHandler::handleSetFileShared(const IpcMessage& msg)
{
    const QString path = msg.fieldString(0);
    const bool shared = msg.fieldBool(1);

    if (!theApp.sharedFileList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Shared files unavailable")));
        return;
    }
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid file path")));
        return;
    }

    const bool ok = shared ? theApp.sharedFileList->addSingleSharedFile(path)
                           : theApp.sharedFileList->excludeFile(path);
    if (!ok) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 403,
            shared ? QStringLiteral("This file cannot be shared")
                   : QStringLiteral("This file cannot be unshared")));
        return;
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleBrowseDirectory — one directory's files with their share state
// ---------------------------------------------------------------------------

void IpcClientHandler::handleBrowseDirectory(const IpcMessage& msg)
{
    const QString dirPath = msg.fieldString(0);

    if (!theApp.sharedFileList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Shared files unavailable")));
        return;
    }
    QDir dir(dirPath);
    if (dirPath.isEmpty() || !dir.exists()) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Directory not found")));
        return;
    }

    // A directory eMule uses for its own storage can hold no shared file at all, so
    // every row in it is unshared and locked (MFC's CBS_UNCHECKEDDISABLED).
    const bool dirShareable = thePrefs.isShareableDirectory(dirPath);

    // One pass over the share, not one per file: the browsed directory can hold
    // thousands of entries and forEachFile() holds the map lock for its duration.
    QHash<QString, QString> hashByPath;
    theApp.sharedFileList->forEachFile([&](KnownFile* f) {
        if (!f->filePath().isEmpty())
            hashByPath.insert(f->filePath().toLower(), md4str(f->fileHash()));
    });

    QCborArray files;
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        const QString filePath = fi.absoluteFilePath();
        const QString name = fi.fileName();
        if (fi.size() == 0
            || name.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)
            || name.endsWith(QStringLiteral(".part.met"), Qt::CaseInsensitive))
            continue;

        const bool isShared = theApp.sharedFileList->shouldBeShared(dirPath, filePath, false);
        // Forced on for the incoming directory, forced off where sharing is impossible.
        const bool forcedOn = theApp.sharedFileList->shouldBeShared(dirPath, filePath, true);
        const bool canToggle = !forcedOn && dirShareable;

        // Only known once the file has been hashed and shared; the GUI uses it to line
        // a browsed row up with the shared-files list.
        const QString hash = hashByPath.value(filePath.toLower());

        files.append(QCborMap{
            {QStringLiteral("name"),      name},
            {QStringLiteral("path"),      filePath},
            {QStringLiteral("size"),      static_cast<qint64>(fi.size())},
            {QStringLiteral("shared"),    isShared},
            {QStringLiteral("canToggle"), canToggle},
            {QStringLiteral("hash"),      hash},
        });
    }

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(files)));
}

// ---------------------------------------------------------------------------
// handleSetDownloadCategory — assign a download to a category
// ---------------------------------------------------------------------------

void IpcClientHandler::handleSetDownloadCategory(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    const auto cat = static_cast<uint32>(msg.fieldInt(1));

    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* pf = theApp.downloadQueue->fileByID(hashBuf);
    if (!pf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Download not found")));
        return;
    }
    pf->setCategory(cat);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleGetDownloadDetails — return extended download info
// ---------------------------------------------------------------------------

namespace {

/// Merge a file's cached Kad notes-search results (filenames + comments) into the
/// File Names (name→count) and Comments aggregates shared by the detail handlers.
/// The "count" for a name is the number of distinct note publishers reporting it.
void mergeKadNotes(const KnownFile& file,
                   std::unordered_map<QString, int>& nameCounts,
                   QCborArray& comments)
{
    for (const auto& [publisherId, info] : file.kadNotes()) {
        if (!info.fileName.isEmpty())
            nameCounts[info.fileName]++;
        if (!info.comment.isEmpty() || info.rating > 0)
            comments.append(QCborMap{
                {QLatin1StringView("userName"), QStringLiteral("Kad")},
                {QLatin1StringView("rating"), info.rating},
                {QLatin1StringView("comment"), info.comment}});
    }
}

/// Comments aggregate for a file whose only note source is the Kad notes cache
/// on AbstractFile — a search result, which has no local sources and no .met
/// record to merge in. The search detail sheet has no File Names page, so unlike
/// mergeKadNotes() this collects comments only.
QCborArray searchKadComments(const AbstractFile& file)
{
    QCborArray comments;
    for (const auto& [publisherId, note] : file.kadNotesCache()) {
        if (note.comment.isEmpty() && note.rating == 0)
            continue;
        comments.append(QCborMap{
            {QLatin1StringView("userName"), QStringLiteral("Kad")},
            {QLatin1StringView("rating"), note.rating},
            {QLatin1StringView("comment"), note.comment}});
    }
    return comments;
}

} // namespace

void IpcClientHandler::handleGetDownloadDetails(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.downloadQueue) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Download queue unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    const auto* pf = theApp.downloadQueue->fileByID(hashBuf);
    if (!pf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Download not found")));
        return;
    }

    QCborMap details = toCbor(*pf);
    // Add extended fields
    const QString path = pf->filePath().isEmpty() ? pf->fullName() : pf->filePath();
    details.insert(QLatin1StringView("filePath"), path);
    details.insert(QLatin1StringView("fullName"), pf->fullName());
    details.insert(QLatin1StringView("a4afSourceCount"), static_cast<qint64>(pf->a4afSourceCount()));
    details.insert(QLatin1StringView("isComplete"),
        pf->status() == PartFileStatus::Complete);

    // AICH hash (if available)
    if (pf->aichRecoveryHashSet().hasValidMasterHash())
        details.insert(QLatin1StringView("aichHash"),
            pf->aichRecoveryHashSet().getMasterHash().getString());

    // Source names: unique filenames with count, from live sources...
    std::unordered_map<QString, int> nameMap;
    for (const auto* client : pf->srcList()) {
        if (!client->clientFilename().isEmpty())
            nameMap[client->clientFilename()]++;
    }

    // Comments: from live sources...
    QCborArray comments;
    for (const auto* client : pf->srcList()) {
        if (!client->fileComment().isEmpty() || client->fileRating() > 0)
            comments.append(QCborMap{
                {QLatin1StringView("userName"), client->userName()},
                {QLatin1StringView("rating"), client->fileRating()},
                {QLatin1StringView("comment"), client->fileComment()}});
    }

    // ...augmented with cached Kad notes (filenames + comments by file hash).
    mergeKadNotes(*pf, nameMap, comments);

    QCborArray sourceNames;
    for (const auto& [name, count] : nameMap)
        sourceNames.append(QCborMap{
            {QLatin1StringView("name"), name},
            {QLatin1StringView("count"), count}});
    details.insert(QLatin1StringView("sourceNames"), sourceNames);
    details.insert(QLatin1StringView("comments"), comments);

    // Plain ED2K link. Flag combinations are served on demand by GetEd2kLink, which
    // can express e.g. "hashset + hostname" — picking between pre-generated variants
    // could not.
    details.insert(QLatin1StringView("ed2kLink"),          pf->getED2kLink(false, false, false));

    // Media metadata
    details.insert(QLatin1StringView("mediaArtist"),  pf->getStrTagValue(FT_MEDIA_ARTIST));
    details.insert(QLatin1StringView("mediaAlbum"),   pf->getStrTagValue(FT_MEDIA_ALBUM));
    details.insert(QLatin1StringView("mediaTitle"),   pf->getStrTagValue(FT_MEDIA_TITLE));
    details.insert(QLatin1StringView("mediaLength"),  static_cast<qint64>(pf->getIntTagValue(FT_MEDIA_LENGTH)));
    details.insert(QLatin1StringView("mediaBitrate"), static_cast<qint64>(pf->getIntTagValue(FT_MEDIA_BITRATE)));
    details.insert(QLatin1StringView("mediaCodec"),   pf->getStrTagValue(FT_MEDIA_CODEC));

    // All file tags for metadata tab
    QCborArray tagArr;
    for (const auto& tag : pf->tags()) {
        QCborMap t;
        if (tag.nameId())
            t.insert(QLatin1StringView("nameId"), tag.nameId());
        if (tag.hasName())
            t.insert(QLatin1StringView("name"), QString::fromLatin1(tag.name()));
        t.insert(QLatin1StringView("type"), tag.type());
        if (tag.isStr())
            t.insert(QLatin1StringView("strValue"), tag.strValue());
        else if (tag.isInt())
            t.insert(QLatin1StringView("intValue"), static_cast<qint64>(tag.int64Value()));
        else if (tag.isFloat())
            t.insert(QLatin1StringView("floatValue"), static_cast<double>(tag.floatValue()));
        else if (tag.isHash())
            t.insert(QLatin1StringView("hashValue"), encodeBase16({tag.hashValue(), 16}));
        tagArr.append(t);
    }
    details.insert(QLatin1StringView("tags"), tagArr);

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(details)));
}

// ---------------------------------------------------------------------------
// handlePreviewDownload — deprecated, preview is now handled GUI-side via HTTP streaming
// ---------------------------------------------------------------------------

void IpcClientHandler::handlePreviewDownload(const IpcMessage& msg)
{
    sendMessage(IpcMessage::makeError(msg.seqId(), 410,
        QStringLiteral("Preview is handled GUI-side via HTTP streaming")));
}

// ---------------------------------------------------------------------------
// handleRequestClientSharedFiles — ask a client for its shared file list
// ---------------------------------------------------------------------------

void IpcClientHandler::handleRequestClientSharedFiles(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.clientList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Client list unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* client = theApp.clientList->findByUserHash(hashBuf);
    if (!client) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Client not found")));
        return;
    }
    client->requestSharedFileList();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleGetClientDetails — extended client info for the detail dialog
// ---------------------------------------------------------------------------

void IpcClientHandler::handleGetClientDetails(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.clientList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Client list unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* client = theApp.clientList->findByUserHash(hashBuf);
    if (!client) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Client not found")));
        return;
    }

    const QCborMap details = toCborDetailed(*client, theApp);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(details)));
}

void IpcClientHandler::handleGetSharedFileDetails(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (!theApp.sharedFileList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Shared file list unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    auto* kf = theApp.sharedFileList->getFileByID(hashBuf);
    if (!kf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Shared file not found")));
        return;
    }

    QCborMap details;
    // Basic info
    details.insert(QLatin1StringView("hash"), md4str(kf->fileHash()));
    details.insert(QLatin1StringView("fileName"), kf->fileName());
    details.insert(QLatin1StringView("fileSize"), static_cast<qint64>(kf->fileSize()));
    details.insert(QLatin1StringView("fileType"), kf->fileType());
    details.insert(QLatin1StringView("filePath"), kf->filePath());
    details.insert(QLatin1StringView("path"), QFileInfo(kf->filePath()).absolutePath());

    // Statistics (session + all-time)
    details.insert(QLatin1StringView("requests"), static_cast<qint64>(kf->statistic.requests()));
    details.insert(QLatin1StringView("acceptedUploads"), static_cast<qint64>(kf->statistic.accepts()));
    details.insert(QLatin1StringView("transferred"), static_cast<qint64>(kf->statistic.transferred()));
    details.insert(QLatin1StringView("allTimeRequests"), static_cast<qint64>(kf->statistic.allTimeRequests()));
    details.insert(QLatin1StringView("allTimeAccepted"), static_cast<qint64>(kf->statistic.allTimeAccepts()));
    details.insert(QLatin1StringView("allTimeTransferred"), static_cast<qint64>(kf->statistic.allTimeTransferred()));
    details.insert(QLatin1StringView("completeSources"), static_cast<qint64>(kf->completeSourcesCount()));

    // Plain ED2K link — flag combinations come from GetEd2kLink on demand.
    details.insert(QLatin1StringView("ed2kLink"),          kf->getED2kLink(false, false, false));

    // Media metadata
    details.insert(QLatin1StringView("mediaArtist"),  kf->getStrTagValue(FT_MEDIA_ARTIST));
    details.insert(QLatin1StringView("mediaAlbum"),   kf->getStrTagValue(FT_MEDIA_ALBUM));
    details.insert(QLatin1StringView("mediaTitle"),   kf->getStrTagValue(FT_MEDIA_TITLE));
    details.insert(QLatin1StringView("mediaLength"),  static_cast<qint64>(kf->getIntTagValue(FT_MEDIA_LENGTH)));
    details.insert(QLatin1StringView("mediaBitrate"), static_cast<qint64>(kf->getIntTagValue(FT_MEDIA_BITRATE)));
    details.insert(QLatin1StringView("mediaCodec"),   kf->getStrTagValue(FT_MEDIA_CODEC));

    // All file tags for Metadata tab
    QCborArray tagArr;
    for (const auto& tag : kf->tags()) {
        QCborMap t;
        if (tag.nameId())
            t.insert(QLatin1StringView("nameId"), tag.nameId());
        if (tag.hasName())
            t.insert(QLatin1StringView("name"), QString::fromLatin1(tag.name()));
        t.insert(QLatin1StringView("type"), tag.type());
        if (tag.isStr())
            t.insert(QLatin1StringView("strValue"), tag.strValue());
        else if (tag.isInt())
            t.insert(QLatin1StringView("intValue"), static_cast<qint64>(tag.int64Value()));
        else if (tag.isFloat())
            t.insert(QLatin1StringView("floatValue"), static_cast<double>(tag.floatValue()));
        else if (tag.isHash())
            t.insert(QLatin1StringView("hashValue"), encodeBase16({tag.hashValue(), 16}));
        tagArr.append(t);
    }
    details.insert(QLatin1StringView("tags"), tagArr);

    // Completed/shared files have no live download sources, so File Names and
    // Comments come entirely from cached Kad notes-search results (if any).
    std::unordered_map<QString, int> nameMap;
    QCborArray comments;
    mergeKadNotes(*kf, nameMap, comments);

    QCborArray sourceNames;
    for (const auto& [name, count] : nameMap)
        sourceNames.append(QCborMap{
            {QLatin1StringView("name"), name},
            {QLatin1StringView("count"), count}});
    details.insert(QLatin1StringView("sourceNames"), sourceNames);
    details.insert(QLatin1StringView("comments"), comments);

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(details)));
}

// ---------------------------------------------------------------------------
// handleGetSearchResultDetails — comments + metadata for one search hit
// ---------------------------------------------------------------------------

void IpcClientHandler::handleGetSearchResultDetails(const IpcMessage& msg)
{
    const auto searchID = static_cast<uint32>(msg.fieldInt(0));
    const QString hash = msg.fieldString(1);

    if (!theApp.searchList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Search list unavailable")));
        return;
    }

    uint8 hashBuf[16]{};
    if (!hexToHash(hash, hashBuf)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }
    // Scoped to the tab: the same hash can appear in several searches, and the
    // lookup skips grouped child results, exactly as MarkSearchSpam does.
    auto* sf = theApp.searchList->searchFileByHash(hashBuf, searchID);
    if (!sf) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Search result not found")));
        return;
    }

    // Same key names as GetSharedFileDetails, so the dialog and the shared
    // Kad-notes wiring need no special-casing for search results.
    QCborMap details;
    details.insert(QLatin1StringView("hash"),     md4str(sf->fileHash()));
    details.insert(QLatin1StringView("fileName"), sf->fileName());
    details.insert(QLatin1StringView("fileSize"), static_cast<qint64>(sf->fileSize()));
    details.insert(QLatin1StringView("fileType"), sf->fileType());
    details.insert(QLatin1StringView("ed2kLink"), sf->getED2kLink(false, false, false));
    details.insert(QLatin1StringView("searchID"), static_cast<qint64>(searchID));
    details.insert(QLatin1StringView("isSpam"),   sf->isConsideredSpam());

    // Drives the "(Kad search in progress...)" state of the Search Kad button.
    details.insert(QLatin1StringView("notesSearchRunning"), sf->isKadCommentSearchRunning());

    QCborArray tagArr;
    for (const auto& tag : sf->tags()) {
        QCborMap t;
        if (tag.nameId())
            t.insert(QLatin1StringView("nameId"), tag.nameId());
        if (tag.hasName())
            t.insert(QLatin1StringView("name"), QString::fromLatin1(tag.name()));
        t.insert(QLatin1StringView("type"), tag.type());
        if (tag.isStr())
            t.insert(QLatin1StringView("strValue"), tag.strValue());
        else if (tag.isInt())
            t.insert(QLatin1StringView("intValue"), static_cast<qint64>(tag.int64Value()));
        else if (tag.isFloat())
            t.insert(QLatin1StringView("floatValue"), static_cast<double>(tag.floatValue()));
        else if (tag.isHash())
            t.insert(QLatin1StringView("hashValue"), encodeBase16({tag.hashValue(), 16}));
        tagArr.append(t);
    }
    details.insert(QLatin1StringView("tags"), tagArr);
    details.insert(QLatin1StringView("comments"), searchKadComments(*sf));

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(details)));
}

// --- Private preference helpers (split to avoid MSVC C1061 nesting limit) ---

bool IpcClientHandler::applyPreferenceA(const QString& key, const QCborValue& val)
{
    // General
    if (key == QStringLiteral("nick"))
        thePrefs.setNick(val.toString());
    else if (key == QStringLiteral("port"))
        thePrefs.setPort(static_cast<uint16>(val.toInteger()));
    else if (key == QStringLiteral("udpPort"))
        thePrefs.setUdpPort(static_cast<uint16>(val.toInteger()));
    else if (key == QStringLiteral("maxUpload"))
        thePrefs.setMaxUpload(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("maxDownload"))
        thePrefs.setMaxDownload(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("maxGraphDownloadRate"))
        thePrefs.setMaxGraphDownloadRate(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("maxGraphUploadRate"))
        thePrefs.setMaxGraphUploadRate(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("maxConnections"))
        thePrefs.setMaxConnections(static_cast<uint16>(val.toInteger()));
    else if (key == QStringLiteral("maxSourcesPerFile"))
        thePrefs.setMaxSourcesPerFile(static_cast<uint16>(val.toInteger()));
    else if (key == QStringLiteral("autoConnect"))
        thePrefs.setAutoConnect(val.toBool());
    else if (key == QStringLiteral("reconnect"))
        thePrefs.setReconnect(val.toBool());
    else if (key == QStringLiteral("showOverhead"))
        thePrefs.setShowOverhead(val.toBool());
    else if (key == QStringLiteral("networkED2K"))
        thePrefs.setNetworkED2K(val.toBool());
    else if (key == QStringLiteral("kadEnabled"))
        thePrefs.setKadEnabled(val.toBool());
    else if (key == QStringLiteral("schedulerEnabled"))
        thePrefs.setSchedulerEnabled(val.toBool());
    else if (key == QStringLiteral("enableUPnP"))
        thePrefs.setEnableUPnP(val.toBool());
    else if (key == QStringLiteral("separateIPv6Queue"))
        thePrefs.setSeparateIPv6Queue(val.toBool(true));
    // Proxy
    else if (key == QStringLiteral("proxyType"))
        thePrefs.setProxyType(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("proxyHost"))
        thePrefs.setProxyHost(val.toString());
    else if (key == QStringLiteral("proxyPort"))
        thePrefs.setProxyPort(static_cast<uint16>(val.toInteger()));
    else if (key == QStringLiteral("proxyEnablePassword"))
        thePrefs.setProxyEnablePassword(val.toBool());
    else if (key == QStringLiteral("proxyUser"))
        thePrefs.setProxyUser(val.toString());
    else if (key == QStringLiteral("proxyPassword"))
        thePrefs.setProxyPassword(val.toString());
    // Server
    else if (key == QStringLiteral("safeServerConnect"))
        thePrefs.setSafeServerConnect(val.toBool());
    else if (key == QStringLiteral("autoConnectStaticOnly"))
        thePrefs.setAutoConnectStaticOnly(val.toBool());
    else if (key == QStringLiteral("useServerPriorities"))
        thePrefs.setUseServerPriorities(val.toBool());
    else if (key == QStringLiteral("addServersFromServer"))
        thePrefs.setAddServersFromServer(val.toBool());
    else if (key == QStringLiteral("useUserSortedServerList"))
        thePrefs.setUseUserSortedServerList(val.toBool());
    else if (key == QStringLiteral("addServersFromClients"))
        thePrefs.setAddServersFromClients(val.toBool());
    else if (key == QStringLiteral("deadServerRetries"))
        thePrefs.setDeadServerRetries(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("autoUpdateServerList"))
        thePrefs.setAutoUpdateServerList(val.toBool());
    else if (key == QStringLiteral("serverListURL"))
        thePrefs.setServerListURL(val.toString());
    else if (key == QStringLiteral("smartLowIdCheck"))
        thePrefs.setSmartLowIdCheck(val.toBool());
    else if (key == QStringLiteral("manualServerHighPriority"))
        thePrefs.setManualServerHighPriority(val.toBool());
    // Files page
    else if (key == QStringLiteral("addNewFilesPaused"))
        thePrefs.setAddNewFilesPaused(val.toBool());
    else if (key == QStringLiteral("useSaveLoadSources"))
        thePrefs.setUseSaveLoadSources(val.toBool());
    else if (key == QStringLiteral("autoDownloadPriority"))
        thePrefs.setAutoDownloadPriority(val.toBool());
    else if (key == QStringLiteral("autoSharedFilesPriority"))
        thePrefs.setAutoSharedFilesPriority(val.toBool());
    else if (key == QStringLiteral("transferFullChunks"))
        thePrefs.setTransferFullChunks(val.toBool());
    else if (key == QStringLiteral("previewPrio"))
        thePrefs.setPreviewPrio(val.toBool());
    else if (key == QStringLiteral("startNextPausedFile"))
        thePrefs.setStartNextPausedFile(val.toBool());
    else if (key == QStringLiteral("startNextPausedFileSameCat"))
        thePrefs.setStartNextPausedFileSameCat(val.toBool());
    else if (key == QStringLiteral("startNextPausedFileOnlySameCat"))
        thePrefs.setStartNextPausedFileOnlySameCat(val.toBool());
    else if (key == QStringLiteral("rememberDownloadedFiles"))
        thePrefs.setRememberDownloadedFiles(val.toBool());
    else if (key == QStringLiteral("rememberCancelledFiles"))
        thePrefs.setRememberCancelledFiles(val.toBool());
    // Notifications
    else if (key == QStringLiteral("notifyOnLog"))
        thePrefs.setNotifyOnLog(val.toBool());
    else if (key == QStringLiteral("notifyOnChat"))
        thePrefs.setNotifyOnChat(val.toBool());
    else if (key == QStringLiteral("notifyOnChatMsg"))
        thePrefs.setNotifyOnChatMsg(val.toBool());
    else if (key == QStringLiteral("notifyOnDownloadAdded"))
        thePrefs.setNotifyOnDownloadAdded(val.toBool());
    else if (key == QStringLiteral("notifyOnDownloadFinished"))
        thePrefs.setNotifyOnDownloadFinished(val.toBool());
    else if (key == QStringLiteral("notifyOnNewVersion"))
        thePrefs.setNotifyOnNewVersion(val.toBool());
    else if (key == QStringLiteral("notifyOnUrgent"))
        thePrefs.setNotifyOnUrgent(val.toBool());
    else if (key == QStringLiteral("notifyEmailEnabled"))
        thePrefs.setNotifyEmailEnabled(val.toBool());
    else if (key == QStringLiteral("notifyEmailSmtpServer"))
        thePrefs.setNotifyEmailSmtpServer(val.toString());
    else if (key == QStringLiteral("notifyEmailSmtpPort"))
        thePrefs.setNotifyEmailSmtpPort(static_cast<uint16>(val.toInteger()));
    else if (key == QStringLiteral("notifyEmailSmtpAuth"))
        thePrefs.setNotifyEmailSmtpAuth(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("notifyEmailSmtpTls"))
        thePrefs.setNotifyEmailSmtpTls(val.toBool());
    else if (key == QStringLiteral("notifyEmailSmtpUser"))
        thePrefs.setNotifyEmailSmtpUser(val.toString());
    else if (key == QStringLiteral("notifyEmailSmtpPassword"))
        thePrefs.setNotifyEmailSmtpPassword(val.toString());
    else if (key == QStringLiteral("notifyEmailRecipient"))
        thePrefs.setNotifyEmailRecipient(val.toString());
    else if (key == QStringLiteral("notifyEmailSender"))
        thePrefs.setNotifyEmailSender(val.toString());
    // Messages and Comments
    else if (key == QStringLiteral("msgOnlyFriends"))
        thePrefs.setMsgOnlyFriends(val.toBool());
    else if (key == QStringLiteral("enableSpamFilter"))
        thePrefs.setEnableSpamFilter(val.toBool());
    else if (key == QStringLiteral("useChatCaptchas"))
        thePrefs.setUseChatCaptchas(val.toBool());
    else if (key == QStringLiteral("messageFilter"))
        thePrefs.setMessageFilter(val.toString());
    else if (key == QStringLiteral("commentFilter"))
        thePrefs.setCommentFilter(val.toString());
    // Security
    else if (key == QStringLiteral("filterServerByIP"))
        thePrefs.setFilterServerByIP(val.toBool());
    else if (key == QStringLiteral("ipFilterLevel"))
        thePrefs.setIpFilterLevel(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("viewSharedFilesAccess"))
        thePrefs.setViewSharedFilesAccess(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("cryptLayerSupported"))
        thePrefs.setCryptLayerSupported(val.toBool());
    else if (key == QStringLiteral("cryptLayerRequested"))
        thePrefs.setCryptLayerRequested(val.toBool());
    else if (key == QStringLiteral("cryptLayerRequired"))
        thePrefs.setCryptLayerRequired(val.toBool());
    else if (key == QStringLiteral("useSecureIdent"))
        thePrefs.setUseSecureIdent(val.toBool());
    else if (key == QStringLiteral("enableSearchResultFilter"))
        thePrefs.setEnableSearchResultFilter(val.toBool());
    else if (key == QStringLiteral("warnUntrustedFiles"))
        thePrefs.setWarnUntrustedFiles(val.toBool());
    else if (key == QStringLiteral("ipFilterUpdateUrl"))
        thePrefs.setIpFilterUpdateUrl(val.toString());
    else if (key == QStringLiteral("appToken"))
        thePrefs.setAppToken(val.toString());
    else
        return false;
    return true;
}

bool IpcClientHandler::applyPreferenceB(const QString& key, const QCborValue& val)
{
    // Extended (PPgTweaks)
    if (key == QStringLiteral("maxConsPerFive"))
        thePrefs.setMaxConsPerFive(static_cast<uint16>(val.toInteger()));
    else if (key == QStringLiteral("maxHalfConnections"))
        thePrefs.setMaxHalfConnections(static_cast<uint16>(val.toInteger()));
    else if (key == QStringLiteral("serverKeepAliveTimeout"))
        thePrefs.setServerKeepAliveTimeout(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("filterLANIPs"))
        thePrefs.setFilterLANIPs(val.toBool());
    else if (key == QStringLiteral("checkDiskspace"))
        thePrefs.setCheckDiskspace(val.toBool());
    else if (key == QStringLiteral("minFreeDiskSpace"))
        thePrefs.setMinFreeDiskSpace(static_cast<uint64>(val.toInteger()));
    else if (key == QStringLiteral("logToDiskCore")) {
        thePrefs.setLogToDiskCore(val.toBool());
        DaemonApp::applyLogFileSettings();
    }
    // Acted on by the GUI, which opens its own files; the daemon only persists it.
    else if (key == QStringLiteral("logToDiskGui"))
        thePrefs.setLogToDiskGui(val.toBool());
    else if (key == QStringLiteral("verbose")) {
        thePrefs.setVerbose(val.toBool());
        DaemonApp::applyLogFilterRules();
    }
    else if (key == QStringLiteral("serverVerboseLog")) {
        thePrefs.setServerVerboseLog(val.toBool());
        DaemonApp::applyLogFilterRules();
    }
    else if (key == QStringLiteral("logPublicIP"))
        thePrefs.setLogPublicIP(val.toBool());
    else if (key == QStringLiteral("closeUPnPOnExit"))
        thePrefs.setCloseUPnPOnExit(val.toBool());
    else if (key == QStringLiteral("portMapProtocols"))
        thePrefs.setPortMapProtocols(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("portMapIPv6"))
        thePrefs.setPortMapIPv6(val.toBool());
    else if (key == QStringLiteral("portMapLeaseSecs"))
        thePrefs.setPortMapLeaseSecs(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("fileBufferSize"))
        thePrefs.setFileBufferSize(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("useCreditSystem"))
        thePrefs.setUseCreditSystem(val.toBool());
    else if (key == QStringLiteral("a4afSaveCpu"))
        thePrefs.setA4afSaveCpu(val.toBool());
    else if (key == QStringLiteral("autoArchivePreviewStart"))
        thePrefs.setAutoArchivePreviewStart(val.toBool());
    else if (key == QStringLiteral("ed2kHostname"))
        thePrefs.setEd2kHostname(val.toString());
    else if (key == QStringLiteral("ed2kLinkAdvertiseIPv6"))
        thePrefs.setEd2kLinkAdvertiseIPv6(val.toBool());
    else if (key == QStringLiteral("showExtControls"))
        thePrefs.setShowExtControls(val.toBool());
    else if (key == QStringLiteral("commitFiles"))
        thePrefs.setCommitFiles(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("extractMetaData"))
        thePrefs.setExtractMetaData(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("logLevel"))
        thePrefs.setLogLevel(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("logSourceExchange"))
        thePrefs.setLogSourceExchange(val.toBool());
    else if (key == QStringLiteral("logBannedClients"))
        thePrefs.setLogBannedClients(val.toBool());
    else if (key == QStringLiteral("logRatingDescReceived"))
        thePrefs.setLogRatingDescReceived(val.toBool());
    else if (key == QStringLiteral("logSecureIdent"))
        thePrefs.setLogSecureIdent(val.toBool());
    else if (key == QStringLiteral("logFilteredIPs"))
        thePrefs.setLogFilteredIPs(val.toBool());
    else if (key == QStringLiteral("logFileSaving"))
        thePrefs.setLogFileSaving(val.toBool());
    else if (key == QStringLiteral("logA4AF"))
        thePrefs.setLogA4AF(val.toBool());
    else if (key == QStringLiteral("logUlDlEvents"))
        thePrefs.setLogUlDlEvents(val.toBool());
    else if (key == QStringLiteral("logRawSocketPackets"))
        thePrefs.setLogRawSocketPackets(val.toBool());
    else if (key == QStringLiteral("logWebServer"))
        thePrefs.setLogWebServer(val.toBool());
    else if (key == QStringLiteral("enableIpcLog"))
        thePrefs.setEnableIpcLog(val.toBool());
    else if (key == QStringLiteral("startCoreWithConsole"))
        thePrefs.setStartCoreWithConsole(val.toBool());
    else if (key == QStringLiteral("queueSize"))
        thePrefs.setQueueSize(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("rememberUploadQueue"))
        thePrefs.setRememberUploadQueue(val.toBool());
    // USS
    else if (key == QStringLiteral("dynUpEnabled"))
        thePrefs.setDynUpEnabled(val.toBool());
    else if (key == QStringLiteral("dynUpPingTolerance"))
        thePrefs.setDynUpPingTolerance(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("dynUpPingToleranceMs"))
        thePrefs.setDynUpPingToleranceMs(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("dynUpUseMillisecondPingTolerance"))
        thePrefs.setDynUpUseMillisecondPingTolerance(val.toBool());
    else if (key == QStringLiteral("dynUpGoingUpDivider"))
        thePrefs.setDynUpGoingUpDivider(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("dynUpGoingDownDivider"))
        thePrefs.setDynUpGoingDownDivider(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("dynUpNumberOfPings"))
        thePrefs.setDynUpNumberOfPings(static_cast<int>(val.toInteger()));
#ifdef Q_OS_WIN
    else if (key == QStringLiteral("autotakeEd2kLinks"))
        thePrefs.setAutotakeEd2kLinks(val.toBool());
    else if (key == QStringLiteral("openPortsOnWinFirewall"))
        thePrefs.setOpenPortsOnWinFirewall(val.toBool());
    else if (key == QStringLiteral("sparsePartFiles"))
        thePrefs.setSparsePartFiles(val.toBool());
    else if (key == QStringLiteral("allocFullFile"))
        thePrefs.setAllocFullFile(val.toBool());
    else if (key == QStringLiteral("resolveShellLinks"))
        thePrefs.setResolveShellLinks(val.toBool());
    else if (key == QStringLiteral("multiUserSharing"))
        thePrefs.setMultiUserSharing(static_cast<int>(val.toInteger()));
#endif
    // Statistics
    else if (key == QStringLiteral("statsAverageMinutes"))
        thePrefs.setStatsAverageMinutes(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("graphsUpdateSec"))
        thePrefs.setGraphsUpdateSec(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("statsUpdateSec"))
        thePrefs.setStatsUpdateSec(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("fillGraphs"))
        thePrefs.setFillGraphs(val.toBool());
    else if (key == QStringLiteral("statsConnectionsMax"))
        thePrefs.setStatsConnectionsMax(static_cast<uint32>(val.toInteger()));
    else if (key == QStringLiteral("statsConnectionsRatio"))
        thePrefs.setStatsConnectionsRatio(static_cast<uint32>(val.toInteger()));
    // Directories
    else if (key == QStringLiteral("incomingDir"))
        thePrefs.setIncomingDir(val.toString());
    else if (key == QStringLiteral("tempDirs")) {
        QStringList dirs;
        for (const auto& item : val.toArray())
            dirs.append(item.toString());
        thePrefs.setTempDirs(dirs);
    }
    else if (key == QStringLiteral("sharedDirs")) {
        QStringList dirs;
        for (const auto& item : val.toArray())
            dirs.append(item.toString());
        thePrefs.setSharedDirs(dirs);
    }

    else
        return false;
    return true;
}

bool IpcClientHandler::applyPreferenceC(const QString& key, const QCborValue& val)
{
    // Web Server
    if (key == QStringLiteral("webServerEnabled"))
        thePrefs.setWebServerEnabled(val.toBool());
    else if (key == QStringLiteral("webServerPort"))
        thePrefs.setWebServerPort(static_cast<uint16>(val.toInteger()));
    else if (key == QStringLiteral("webServerApiKey"))
        thePrefs.setWebServerApiKey(val.toString());
    else if (key == QStringLiteral("webServerListenAddress"))
        thePrefs.setWebServerListenAddress(val.toString());
    else if (key == QStringLiteral("webServerRestApiEnabled"))
        thePrefs.setWebServerRestApiEnabled(val.toBool());
    else if (key == QStringLiteral("webServerGzipEnabled"))
        thePrefs.setWebServerGzipEnabled(val.toBool());
    else if (key == QStringLiteral("webServerUPnP"))
        thePrefs.setWebServerUPnP(val.toBool());
    else if (key == QStringLiteral("webServerTemplatePath"))
        thePrefs.setWebServerTemplatePath(val.toString());
    else if (key == QStringLiteral("webServerSessionTimeout"))
        thePrefs.setWebServerSessionTimeout(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("webServerHttpsEnabled"))
        thePrefs.setWebServerHttpsEnabled(val.toBool());
    else if (key == QStringLiteral("webServerCertPath"))
        thePrefs.setWebServerCertPath(val.toString());
    else if (key == QStringLiteral("webServerKeyPath"))
        thePrefs.setWebServerKeyPath(val.toString());
    else if (key == QStringLiteral("webServerAdminPassword"))
        thePrefs.setWebServerAdminPassword(val.toString());
    else if (key == QStringLiteral("webServerAdminAllowHiLevFunc"))
        thePrefs.setWebServerAdminAllowHiLevFunc(val.toBool());
    else if (key == QStringLiteral("webServerGuestEnabled"))
        thePrefs.setWebServerGuestEnabled(val.toBool());
    else if (key == QStringLiteral("webServerGuestPassword"))
        thePrefs.setWebServerGuestPassword(val.toString());

    // GUI-only settings (synced for YAML persistence)
    // General page
    else if (key == QStringLiteral("promptOnExit"))
        thePrefs.setPromptOnExit(val.toBool());
    else if (key == QStringLiteral("startMinimized"))
        thePrefs.setStartMinimized(val.toBool());
    else if (key == QStringLiteral("showSplashScreen"))
        thePrefs.setShowSplashScreen(val.toBool());
    else if (key == QStringLiteral("enableOnlineSignature"))
        thePrefs.setEnableOnlineSignature(val.toBool());
    else if (key == QStringLiteral("enableMiniMule"))
        thePrefs.setEnableMiniMule(val.toBool());
    else if (key == QStringLiteral("preventStandby"))
        thePrefs.setPreventStandby(val.toBool());
    else if (key == QStringLiteral("versionCheckEnabled"))
        thePrefs.setVersionCheckEnabled(val.toBool());
    else if (key == QStringLiteral("versionCheckDays"))
        thePrefs.setVersionCheckDays(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("bringToFrontOnLinkClick"))
        thePrefs.setBringToFrontOnLinkClick(val.toBool());
    else if (key == QStringLiteral("language"))
        thePrefs.setLanguage(val.toString());
    else if (key == QStringLiteral("startWithOS"))
        thePrefs.setStartWithOS(val.toBool());

    // Display page
    else if (key == QStringLiteral("depth3D"))
        thePrefs.setDepth3D(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("tooltipDelay"))
        thePrefs.setTooltipDelay(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("minimizeToTray"))
        thePrefs.setMinimizeToTray(val.toBool());
    else if (key == QStringLiteral("transferDoubleClick"))
        thePrefs.setTransferDoubleClick(val.toBool());
    else if (key == QStringLiteral("showDwlPercentage"))
        thePrefs.setShowDwlPercentage(val.toBool());
    else if (key == QStringLiteral("showRatesInTitle"))
        thePrefs.setShowRatesInTitle(val.toBool());
    else if (key == QStringLiteral("showCatTabInfos"))
        thePrefs.setShowCatTabInfos(val.toBool());
    else if (key == QStringLiteral("autoRemoveFinishedDownloads"))
        thePrefs.setAutoRemoveFinishedDownloads(val.toBool());
    else if (key == QStringLiteral("showTransToolbar"))
        thePrefs.setShowTransToolbar(val.toBool());
    else if (key == QStringLiteral("showSpeedGraph"))
        thePrefs.setShowSpeedGraph(val.toBool());
    else if (key == QStringLiteral("speedGraphTimeRangeMin"))
        thePrefs.setSpeedGraphTimeRangeMin(static_cast<uint32_t>(val.toInteger()));
    else if (key == QStringLiteral("storeSearches"))
        thePrefs.setStoreSearches(val.toBool());
    else if (key == QStringLiteral("disableKnownClientList"))
        thePrefs.setDisableKnownClientList(val.toBool());
    else if (key == QStringLiteral("disableQueueList"))
        thePrefs.setDisableQueueList(val.toBool());
    else if (key == QStringLiteral("useAutoCompletion"))
        thePrefs.setUseAutoCompletion(val.toBool());
    else if (key == QStringLiteral("useOriginalIcons"))
        thePrefs.setUseOriginalIcons(val.toBool());
    else if (key == QStringLiteral("logFont"))
        thePrefs.setLogFont(val.toString());

    // Files page (GUI-only)
    else if (key == QStringLiteral("watchClipboard4ED2KLinks"))
        thePrefs.setWatchClipboard4ED2KLinks(val.toBool());
    else if (key == QStringLiteral("useAdvancedCalcRemainingTime"))
        thePrefs.setUseAdvancedCalcRemainingTime(val.toBool());
    else if (key == QStringLiteral("videoPlayerCommand"))
        thePrefs.setVideoPlayerCommand(val.toString());
    else if (key == QStringLiteral("videoPlayerArgs"))
        thePrefs.setVideoPlayerArgs(val.toString());
    else if (key == QStringLiteral("createBackupToPreview"))
        thePrefs.setCreateBackupToPreview(val.toBool());
    else if (key == QStringLiteral("autoCleanupFilenames"))
        thePrefs.setAutoCleanupFilenames(val.toBool());

    // Notifications page (GUI-side)
    else if (key == QStringLiteral("notifySoundType"))
        thePrefs.setNotifySoundType(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("notifySoundFile"))
        thePrefs.setNotifySoundFile(val.toString());

    // IRC page
    else if (key == QStringLiteral("ircServer"))
        thePrefs.setIrcServer(val.toString());
    else if (key == QStringLiteral("ircNick"))
        thePrefs.setIrcNick(val.toString());
    else if (key == QStringLiteral("ircUseChannelFilter"))
        thePrefs.setIrcUseChannelFilter(val.toBool());
    else if (key == QStringLiteral("ircChannelFilter"))
        thePrefs.setIrcChannelFilter(val.toString());
    else if (key == QStringLiteral("ircUsePerform"))
        thePrefs.setIrcUsePerform(val.toBool());
    else if (key == QStringLiteral("ircPerformString"))
        thePrefs.setIrcPerformString(val.toString());
    else if (key == QStringLiteral("ircConnectHelpChannel"))
        thePrefs.setIrcConnectHelpChannel(val.toBool());
    else if (key == QStringLiteral("ircLoadChannelList"))
        thePrefs.setIrcLoadChannelList(val.toBool());
    else if (key == QStringLiteral("ircAddTimestamp"))
        thePrefs.setIrcAddTimestamp(val.toBool());
    else if (key == QStringLiteral("ircIgnoreMiscInfoMessages"))
        thePrefs.setIrcIgnoreMiscInfoMessages(val.toBool());
    else if (key == QStringLiteral("ircIgnoreJoinMessages"))
        thePrefs.setIrcIgnoreJoinMessages(val.toBool());
    else if (key == QStringLiteral("ircIgnorePartMessages"))
        thePrefs.setIrcIgnorePartMessages(val.toBool());
    else if (key == QStringLiteral("ircIgnoreQuitMessages"))
        thePrefs.setIrcIgnoreQuitMessages(val.toBool());

    // Messages page (GUI-only)
    else if (key == QStringLiteral("showSmileys"))
        thePrefs.setShowSmileys(val.toBool());
    else if (key == QStringLiteral("indicateRatings"))
        thePrefs.setIndicateRatings(val.toBool());

    else
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// handleGetCollectionInfo
// ---------------------------------------------------------------------------

void IpcClientHandler::handleGetCollectionInfo(const IpcMessage& msg)
{
    const QString hash = msg.fieldString(0);
    if (hash.size() != 32) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid hash")));
        return;
    }

    if (!theApp.sharedFileList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Shared list unavailable")));
        return;
    }

    const QByteArray hashBytes = QByteArray::fromHex(hash.toLatin1());
    KnownFile* kf = theApp.sharedFileList->getFileByID(
        reinterpret_cast<const uint8*>(hashBytes.constData()));
    if (!kf || !kf->collection()) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("No collection for this file")));
        return;
    }

    const Collection* coll = kf->collection();
    QCborMap result;
    result.insert(QStringLiteral("name"), coll->m_name);
    result.insert(QStringLiteral("authorName"), coll->m_authorName);
    result.insert(QStringLiteral("authorKeyHash"), coll->authorKeyHashString());
    result.insert(QStringLiteral("authorKeyHex"), coll->authorKeyString());
    result.insert(QStringLiteral("textFormat"), coll->m_textFormat);

    QCborArray filesArr;
    for (const auto& [key, cf] : coll->files()) {
        QCborMap fm;
        fm.insert(QStringLiteral("hash"), md4str(cf->fileHash()));
        fm.insert(QStringLiteral("fileName"), cf->fileName());
        fm.insert(QStringLiteral("fileSize"), static_cast<qint64>(cf->fileSize()));
        filesArr.append(fm);
    }
    result.insert(QStringLiteral("files"), filesArr);

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(result)));
}

// ---------------------------------------------------------------------------
// handleSaveCollection
// ---------------------------------------------------------------------------

void IpcClientHandler::handleSaveCollection(const IpcMessage& msg)
{
    const QString name = msg.fieldString(0);
    const QCborArray hashesArr = msg.fieldArray(1);
    const bool textFormat = msg.fieldBool(2);
    const bool sign = msg.fieldBool(3);

    if (name.isEmpty()) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Collection name required")));
        return;
    }

    if (!theApp.sharedFileList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("Shared list unavailable")));
        return;
    }

    Collection coll;
    coll.m_name = name;
    coll.m_textFormat = textFormat;

    // Add files by hash
    for (int i = 0; i < hashesArr.size(); ++i) {
        const QString fileHash = hashesArr.at(i).toString();
        const QByteArray hashBytes = QByteArray::fromHex(fileHash.toLatin1());
        if (hashBytes.size() != 16)
            continue;
        KnownFile* kf = theApp.sharedFileList->getFileByID(
            reinterpret_cast<const uint8*>(hashBytes.constData()));
        if (kf)
            coll.addFile(kf, true);
    }

    if (coll.fileCount() == 0) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("No valid files for collection")));
        return;
    }

    // Signing
    evp_pkey_st* signKey = nullptr;
    if (sign && !textFormat) {
        auto* da = DaemonApp::instance();
        if (da && da->coreSession() && da->coreSession()->collectionKeys()) {
            auto* ck = da->coreSession()->collectionKeys();
            signKey = ck->signKey();
            coll.m_authorKey = ck->publicKeyDer();
            coll.m_authorName = thePrefs.nick();
        }
    }

    // Save to incoming directory
    const QString filePath = QDir(thePrefs.incomingDir())
                                 .filePath(name + QStringLiteral(".emulecollection"));

    // Remove existing file if present
    if (QFile::exists(filePath))
        QFile::remove(filePath);

    if (!coll.writeToFile(filePath, signKey)) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 500, QStringLiteral("Failed to write collection file")));
        return;
    }

    // Trigger rescan so the new file gets picked up and hashed
    theApp.sharedFileList->reload();

    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool IpcClientHandler::rejectIfKadUnavailable(const IpcMessage& msg, bool requireConnected)
{
    auto* kadInst = kad::Kademlia::instance();
    if (kadInst && (requireConnected ? kadInst->isConnected() : kadInst->isRunning()))
        return false;

    // Same wording as the Kad branch of handleStartSearch, so the GUI shows one
    // consistent message no matter which button the user pressed.
    const QString text = requireConnected
        ? tr("Kad is not connected.\n\nWait until Kad is connected before starting "
             "a Kad search.")
        : tr("Kad is not running.\n\nConnect to the Kad network first.");
    sendMessage(IpcMessage::makeResult(msg.seqId(), false, QCborValue(text)));
    return true;
}

} // namespace eMule
