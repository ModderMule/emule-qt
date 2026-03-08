/// @file IpcClientHandler.cpp
/// @brief Per-connection IPC request handler — implementation.

#include "IpcClientHandler.h"
#include "DaemonApp.h"

#include "ipc/CborSerializers.h"
#include "webserver/WebServer.h"

#include <QDir>

#include "app/AppContext.h"
#include "app/CoreSession.h"
#include "ipfilter/IPFilter.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
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
#include "kademlia/KadPrefs.h"
#include "net/ListenSocket.h"
#include "prefs/Preferences.h"
#include "net/Packet.h"
#include "search/SearchExpr.h"
#include "search/SearchExprParser.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/Scheduler.h"
#include "transfer/UploadQueue.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHostAddress>
#include <QSet>
#include <QStorageInfo>
#include <QUrl>

#include <cstring>
#include <ctime>

namespace eMule {

using namespace Ipc;

namespace {

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
    case IpcMsgType::GetConnection:        handleGetConnection(msg); break;
    case IpcMsgType::ConnectToServer:      handleConnectToServer(msg); break;
    case IpcMsgType::DisconnectFromServer: handleDisconnectFromServer(msg); break;
    case IpcMsgType::StartSearch:          handleStartSearch(msg); break;
    case IpcMsgType::GetSearchResults:     handleGetSearchResults(msg); break;
    case IpcMsgType::StopSearch:           handleStopSearch(msg); break;
    case IpcMsgType::RemoveSearch:         handleRemoveSearch(msg); break;
    case IpcMsgType::ClearAllSearches:     handleClearAllSearches(msg); break;
    case IpcMsgType::DownloadSearchFile:   handleDownloadSearchFile(msg); break;
    case IpcMsgType::GetSharedFiles:       handleGetSharedFiles(msg); break;
    case IpcMsgType::SetSharedFilePriority: handleSetSharedFilePriority(msg); break;
    case IpcMsgType::ReloadSharedFiles: handleReloadSharedFiles(msg); break;
    case IpcMsgType::GetFriends:           handleGetFriends(msg); break;
    case IpcMsgType::AddFriend:            handleAddFriend(msg); break;
    case IpcMsgType::RemoveFriend:         handleRemoveFriend(msg); break;
    case IpcMsgType::SendChatMessage:      handleSendChatMessage(msg); break;
    case IpcMsgType::SetFriendSlot:        handleSetFriendSlot(msg); break;
    case IpcMsgType::GetStats:             handleGetStats(msg); break;
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
    case IpcMsgType::RenameSharedFile:     handleRenameSharedFile(msg); break;
    case IpcMsgType::DeleteSharedFile:     handleDeleteSharedFile(msg); break;
    case IpcMsgType::UnshareFile:          handleUnshareFile(msg); break;
    case IpcMsgType::SetDownloadCategory:  handleSetDownloadCategory(msg); break;
    case IpcMsgType::GetDownloadDetails:   handleGetDownloadDetails(msg); break;
    case IpcMsgType::PreviewDownload:      handlePreviewDownload(msg); break;
    case IpcMsgType::RequestClientSharedFiles: handleRequestClientSharedFiles(msg); break;
    case IpcMsgType::GetClientDetails:   handleGetClientDetails(msg); break;
    case IpcMsgType::GetSharedFileDetails: handleGetSharedFileDetails(msg); break;
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

    // Non-localhost connections require a valid token
    if (!m_isLocal) {
        const QString clientToken = msg.fieldString(1);
        const QStringList tokens = thePrefs.ipcTokens();
        if (tokens.isEmpty() || !tokens.contains(clientToken)) {
            logWarning(QStringLiteral("IPC auth failed — invalid token from remote client"));
            sendMessage(IpcMessage::makeError(msg.seqId(), 403,
                QStringLiteral("Authentication failed")));
            m_connection->close();
            return;
        }
        // Enable AES encryption for all subsequent messages
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
    pf->stopFile(true);
    theApp.downloadQueue->removeFile(pf);
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
    const auto ip = static_cast<uint32>(msg.fieldInt(0));
    const auto port = static_cast<uint16>(msg.fieldInt(1));
    auto* srv = theApp.serverList->findByIPTcp(ip, port);
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
    const auto ip = static_cast<uint32>(msg.fieldInt(0));
    const auto port = static_cast<uint16>(msg.fieldInt(1));
    const auto prio = static_cast<int>(msg.fieldInt(2));
    auto* srv = theApp.serverList->findByIPTcp(ip, port);
    if (!srv) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Server not found")));
        return;
    }
    srv->setPreference(static_cast<ServerPriority>(prio));
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleSetServerStatic(const IpcMessage& msg)
{
    if (!theApp.serverList) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerList unavailable")));
        return;
    }
    const auto ip = static_cast<uint32>(msg.fieldInt(0));
    const auto port = static_cast<uint16>(msg.fieldInt(1));
    const bool isStatic = msg.fieldBool(2);
    auto* srv = theApp.serverList->findByIPTcp(ip, port);
    if (!srv) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Server not found")));
        return;
    }
    srv->setStaticMember(isStatic);
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

    const QHostAddress addr(address);
    if (addr.isNull() || port == 0) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Invalid address or port")));
        return;
    }
    auto server = std::make_unique<Server>(addr.toIPv4Address(), port);
    if (!name.isEmpty())
        server->setName(name);
    if (thePrefs.manualServerHighPriority())
        server->setPreference(ServerPriority::High);
    theApp.serverList->addServer(std::move(server));
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleGetConnection(const IpcMessage& msg)
{
    QCborMap info;
    info.insert(QStringLiteral("connected"),  theApp.isConnected());
    info.insert(QStringLiteral("connecting"),
                theApp.serverConnect && theApp.serverConnect->isConnecting());
    info.insert(QStringLiteral("firewalled"), theApp.isFirewalled());
    info.insert(QStringLiteral("clientID"),   static_cast<qint64>(theApp.getID()));

    if (theApp.serverConnect) {
        const auto* srv = theApp.serverConnect->currentServer();
        if (srv)
            info.insert(QStringLiteral("server"), QCborValue(toCbor(*srv)));
    }

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(info)));
}

void IpcClientHandler::handleConnectToServer(const IpcMessage& msg)
{
    if (!theApp.serverConnect) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerConnect unavailable")));
        return;
    }

    // If IP and port fields are provided, connect to a specific server
    if (msg.fieldCount() >= 2) {
        const auto ip = static_cast<uint32>(msg.fieldInt(0));
        const auto port = static_cast<uint16>(msg.fieldInt(1));

        if (!theApp.serverList) {
            sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerList unavailable")));
            return;
        }

        auto* srv = theApp.serverList->findByIPTcp(ip, port);
        if (!srv) {
            sendMessage(IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Server not found")));
            return;
        }

        // Already connected to this exact server — no-op
        if (theApp.serverConnect->isConnected()) {
            const auto* cur = theApp.serverConnect->currentServer();
            if (cur && cur->ip() == ip && cur->port() == port) {
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

    // No fields — connect to any server (only if eD2K network enabled)
    if (!thePrefs.networkED2K()) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 403, QStringLiteral("eD2K network disabled")));
        return;
    }
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

    // Pre-check: block duplicate Kad keyword searches before creating a session
    if (params.type == SearchType::Kademlia) {
        QString activeKw = kad::SearchManager::findActiveKeyword(params.expression);
        if (!activeKw.isEmpty()) {
            sendMessage(IpcMessage::makeResult(msg.seqId(), false,
                QCborValue(tr("A Kad search for \"%1\" is already active.").arg(activeKw))));
            return;
        }
    }

    // Create search session in SearchList
    const uint32 searchID = theApp.searchList->newSearch(params.fileType, params);

    bool started = false;

    if (params.type == SearchType::Kademlia) {
        // Kademlia keyword search
        auto* kadSearch = kad::SearchManager::prepareFindKeywords(
            params.expression, 0, nullptr);
        if (kadSearch) {
            // Sync Kad search ID with SearchList session ID
            kadSearch->setSearchID(searchID);
            started = kad::SearchManager::startSearch(kadSearch);
        }
    }
    if (params.type == SearchType::Ed2kServer || params.type == SearchType::Automatic) {
        if (theApp.serverConnect && theApp.serverConnect->isConnected()) {
            auto parsed = parseSearchExpression(params.expression);
            const QByteArray payload = parsed.expr.toBytes();
            if (!payload.isEmpty()) {
                auto pkt = std::make_unique<Packet>(OP_SEARCHREQUEST,
                                                    static_cast<uint32>(payload.size()));
                pkt->prot = OP_EDONKEYPROT;
                std::memcpy(pkt->pBuffer, payload.constData(), static_cast<size_t>(payload.size()));
                theApp.serverConnect->sendPacket(std::move(pkt));
                started = true;
            }
        }
    }

    if (params.type == SearchType::Ed2kGlobal || params.type == SearchType::Automatic) {
        if (theApp.serverConnect && theApp.serverList && theApp.searchList) {
            auto parsed = parseSearchExpression(params.expression);
            const QByteArray payload = parsed.expr.toBytes();
            if (!payload.isEmpty()) {
                const size_t count = theApp.serverList->serverCount();
                for (size_t i = 0; i < count; ++i) {
                    Server* srv = theApp.serverList->serverAt(i);
                    theApp.searchList->addSentUDPRequestIP(srv->ip());
                    auto pkt = std::make_unique<Packet>(OP_GLOBSEARCHREQ,
                                                        static_cast<uint32>(payload.size()));
                    pkt->prot = OP_EDONKEYPROT;
                    std::memcpy(pkt->pBuffer, payload.constData(), static_cast<size_t>(payload.size()));
                    const auto udpPort = static_cast<uint16>(srv->port() + 4);
                    theApp.serverConnect->sendUDPPacket(std::move(pkt), *srv, udpPort);
                }
                started = true;
            }
        }
    }

    QCborMap result;
    result.insert(QStringLiteral("searchID"), static_cast<qint64>(searchID));
    result.insert(QStringLiteral("started"), started);
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
        results.append(toCbor(*sf));
    });
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(results)));
}

void IpcClientHandler::handleStopSearch(const IpcMessage& msg)
{
    const auto searchID = static_cast<uint32>(msg.fieldInt(0));
    kad::SearchManager::stopSearch(searchID, false);
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

    // Build ed2k link: ed2k://|file|<name>|<size>|<hash>|/
    const QString ed2kLink = QStringLiteral("ed2k://|file|%1|%2|%3|/")
        .arg(fileName).arg(fileSize).arg(hash);

    const QStringList tempDirs = thePrefs.tempDirs();
    const QString tempDir = tempDirs.isEmpty()
        ? QDir(thePrefs.configDir()).filePath(QStringLiteral("Temp"))
        : tempDirs.first();
    const bool ok = theApp.downloadQueue->addDownloadFromED2KLink(ed2kLink, tempDir, 0);
    sendMessage(IpcMessage::makeResult(msg.seqId(), ok));
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
        m.insert(QStringLiteral("path"), QFileInfo(kf->filePath()).absolutePath());
        m.insert(QStringLiteral("ed2kLink"), kf->getED2kLink());
        m.insert(QStringLiteral("isPartFile"), isPartFile);
        m.insert(QStringLiteral("uploadingClients"), kf->uploadingClientCount());
        m.insert(QStringLiteral("partCount"), static_cast<int>(kf->partCount()));
        m.insert(QStringLiteral("completedSize"), completedSz);

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
    result.insert(QStringLiteral("totalRequests"), totalRequests);
    result.insert(QStringLiteral("totalAccepted"), totalAccepted);
    result.insert(QStringLiteral("totalTransferred"), totalTransferred);
    result.insert(QStringLiteral("totalAllTimeRequests"), totalAllTimeReqs);
    result.insert(QStringLiteral("totalAllTimeAccepted"), totalAllTimeAcc);
    result.insert(QStringLiteral("totalAllTimeTransferred"), totalAllTimeTx);
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

    uint8 hashBuf[16]{};
    const bool hasHash = hexToHash(hashStr, hashBuf);
    theApp.friendList->addFriend(hashBuf, ip, port, name, hasHash);
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

    // MFC behaviour: only one friend slot at a time — clear all first
    for (const auto& f : theApp.friendList->friends())
        f->setFriendSlot(false);

    // Find and set the target friend's slot
    for (const auto& f : theApp.friendList->friends()) {
        if (f->hasUserhash() && std::memcmp(f->userHash().data(), hashBuf, 16) == 0) {
            f->setFriendSlot(enabled);
            // If the client is connected, update their friend slot too
            if (auto* client = theApp.clientList->findByUserHash(hashBuf, 0, 0))
                client->setFriendSlot(enabled);
            break;
        }
    }

    theApp.friendList->save(thePrefs.configDir());
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleGetStats(const IpcMessage& msg)
{
    QCborMap stats;

    const auto now = static_cast<uint32>(std::time(nullptr));

    if (auto* s = theApp.statistics) {
        // Session bytes & uptime
        stats.insert(QStringLiteral("sessionSentBytes"),
                     static_cast<qint64>(s->sessionSentBytes()));
        stats.insert(QStringLiteral("sessionReceivedBytes"),
                     static_cast<qint64>(s->sessionReceivedBytes()));
        stats.insert(QStringLiteral("sessionSentBytesToFriend"),
                     static_cast<qint64>(s->sessionSentBytesToFriend()));
        stats.insert(QStringLiteral("uptime"),
                     static_cast<qint64>(now - s->startTime()));
        stats.insert(QStringLiteral("startTime"),
                     static_cast<qint64>(s->startTime()));

        // Current rates (KB/s)
        stats.insert(QStringLiteral("rateDown"), static_cast<double>(s->rateDown()));
        stats.insert(QStringLiteral("rateUp"), static_cast<double>(s->rateUp()));
        stats.insert(QStringLiteral("upOverheadRate"),
                     static_cast<double>(s->upDatarateOverhead()) / 1024.0);
        stats.insert(QStringLiteral("downOverheadRate"),
                     static_cast<double>(s->downDatarateOverhead()) / 1024.0);
        stats.insert(QStringLiteral("maxDown"), static_cast<double>(s->maxDown()));
        stats.insert(QStringLiteral("maxUp"), static_cast<double>(s->maxUp()));
        stats.insert(QStringLiteral("maxDownAvg"), static_cast<double>(s->maxDownAvg()));
        stats.insert(QStringLiteral("maxUpAvg"), static_cast<double>(s->maxUpAvg()));

        // Session averages
        stats.insert(QStringLiteral("avgDownSession"),
                     static_cast<double>(s->avgDownloadRate(AverageType::Session)));
        stats.insert(QStringLiteral("avgUpSession"),
                     static_cast<double>(s->avgUploadRate(AverageType::Session)));
        stats.insert(QStringLiteral("avgDownTime"),
                     static_cast<double>(s->avgDownloadRate(AverageType::Time)));
        stats.insert(QStringLiteral("avgUpTime"),
                     static_cast<double>(s->avgUploadRate(AverageType::Time)));

        // Cumulative rates
        stats.insert(QStringLiteral("cumDownAvg"), static_cast<double>(s->cumDownAvg()));
        stats.insert(QStringLiteral("cumUpAvg"), static_cast<double>(s->cumUpAvg()));
        stats.insert(QStringLiteral("maxCumDown"), static_cast<double>(s->maxCumDown()));
        stats.insert(QStringLiteral("maxCumUp"), static_cast<double>(s->maxCumUp()));
        stats.insert(QStringLiteral("maxCumDownAvg"), static_cast<double>(s->maxCumDownAvg()));
        stats.insert(QStringLiteral("maxCumUpAvg"), static_cast<double>(s->maxCumUpAvg()));

        // Transfer times (seconds)
        stats.insert(QStringLiteral("transferTime"), static_cast<qint64>(s->transferTime()));
        stats.insert(QStringLiteral("uploadTime"), static_cast<qint64>(s->uploadTime()));
        stats.insert(QStringLiteral("downloadTime"), static_cast<qint64>(s->downloadTime()));
        stats.insert(QStringLiteral("serverDuration"), static_cast<qint64>(s->serverDuration()));

        // Global state
        stats.insert(QStringLiteral("reconnects"), static_cast<qint64>(s->reconnects()));
        stats.insert(QStringLiteral("filteredClients"), static_cast<qint64>(s->filteredClients()));

        // Download overhead (bytes + packets)
        stats.insert(QStringLiteral("downOverheadTotal"),
                     static_cast<qint64>(s->downDataOverheadFileRequest()
                                         + s->downDataOverheadSourceExchange()
                                         + s->downDataOverheadServer()
                                         + s->downDataOverheadKad()
                                         + s->downDataOverheadOther()));
        stats.insert(QStringLiteral("downOverheadTotalPackets"),
                     static_cast<qint64>(s->downDataOverheadFileRequestPackets()
                                         + s->downDataOverheadSourceExchangePackets()
                                         + s->downDataOverheadServerPackets()
                                         + s->downDataOverheadKadPackets()
                                         + s->downDataOverheadOtherPackets()));
        stats.insert(QStringLiteral("downOverheadFileReq"),
                     static_cast<qint64>(s->downDataOverheadFileRequest()));
        stats.insert(QStringLiteral("downOverheadFileReqPkt"),
                     static_cast<qint64>(s->downDataOverheadFileRequestPackets()));
        stats.insert(QStringLiteral("downOverheadSrcExch"),
                     static_cast<qint64>(s->downDataOverheadSourceExchange()));
        stats.insert(QStringLiteral("downOverheadSrcExchPkt"),
                     static_cast<qint64>(s->downDataOverheadSourceExchangePackets()));
        stats.insert(QStringLiteral("downOverheadServer"),
                     static_cast<qint64>(s->downDataOverheadServer()));
        stats.insert(QStringLiteral("downOverheadServerPkt"),
                     static_cast<qint64>(s->downDataOverheadServerPackets()));
        stats.insert(QStringLiteral("downOverheadKad"),
                     static_cast<qint64>(s->downDataOverheadKad()));
        stats.insert(QStringLiteral("downOverheadKadPkt"),
                     static_cast<qint64>(s->downDataOverheadKadPackets()));

        // Upload overhead (bytes + packets)
        stats.insert(QStringLiteral("upOverheadTotal"),
                     static_cast<qint64>(s->upDataOverheadFileRequest()
                                         + s->upDataOverheadSourceExchange()
                                         + s->upDataOverheadServer()
                                         + s->upDataOverheadKad()
                                         + s->upDataOverheadOther()));
        stats.insert(QStringLiteral("upOverheadTotalPackets"),
                     static_cast<qint64>(s->upDataOverheadFileRequestPackets()
                                         + s->upDataOverheadSourceExchangePackets()
                                         + s->upDataOverheadServerPackets()
                                         + s->upDataOverheadKadPackets()
                                         + s->upDataOverheadOtherPackets()));
        stats.insert(QStringLiteral("upOverheadFileReq"),
                     static_cast<qint64>(s->upDataOverheadFileRequest()));
        stats.insert(QStringLiteral("upOverheadFileReqPkt"),
                     static_cast<qint64>(s->upDataOverheadFileRequestPackets()));
        stats.insert(QStringLiteral("upOverheadSrcExch"),
                     static_cast<qint64>(s->upDataOverheadSourceExchange()));
        stats.insert(QStringLiteral("upOverheadSrcExchPkt"),
                     static_cast<qint64>(s->upDataOverheadSourceExchangePackets()));
        stats.insert(QStringLiteral("upOverheadServer"),
                     static_cast<qint64>(s->upDataOverheadServer()));
        stats.insert(QStringLiteral("upOverheadServerPkt"),
                     static_cast<qint64>(s->upDataOverheadServerPackets()));
        stats.insert(QStringLiteral("upOverheadKad"),
                     static_cast<qint64>(s->upDataOverheadKad()));
        stats.insert(QStringLiteral("upOverheadKadPkt"),
                     static_cast<qint64>(s->upDataOverheadKadPackets()));
    }

    // Upload queue stats
    if (auto* uq = theApp.uploadQueue) {
        stats.insert(QStringLiteral("upDatarate"), static_cast<qint64>(uq->datarate()));
        stats.insert(QStringLiteral("upFriendDatarate"), static_cast<qint64>(uq->friendDatarate()));
        stats.insert(QStringLiteral("upSuccessful"), static_cast<qint64>(uq->successfulUploadCount()));
        stats.insert(QStringLiteral("upFailed"), static_cast<qint64>(uq->failedUploadCount()));
        stats.insert(QStringLiteral("upWaiting"), static_cast<qint64>(uq->waitingUserCount()));
        stats.insert(QStringLiteral("upQueueLength"), static_cast<qint64>(uq->uploadQueueLength()));
        stats.insert(QStringLiteral("upAvgTime"), static_cast<qint64>(uq->averageUpTime()));
    }

    // Download queue stats
    if (auto* dq = theApp.downloadQueue) {
        stats.insert(QStringLiteral("downDatarate"), static_cast<qint64>(dq->datarate()));
        stats.insert(QStringLiteral("downFileCount"), static_cast<qint64>(dq->fileCount()));

        // Count completed downloads (for MiniMule) + total found sources
        int completedCount = 0;
        qint64 totalSources = 0;
        for (const auto* f : dq->files()) {
            if (f->status() == PartFileStatus::Complete)
                ++completedCount;
            totalSources += f->sourceCount();
        }
        stats.insert(QStringLiteral("completedDownloads"), completedCount);
        stats.insert(QStringLiteral("downFoundSources"), totalSources);
    }

    // Free space on incoming directory (for MiniMule)
    {
        QStorageInfo storage(thePrefs.incomingDir());
        if (storage.isValid())
            stats.insert(QStringLiteral("freeTempSpace"), storage.bytesAvailable());
    }

    // Connection stats
    if (auto* ls = theApp.listenSocket) {
        stats.insert(QStringLiteral("connActive"), static_cast<qint64>(ls->activeConnections()));
        stats.insert(QStringLiteral("connPeak"), static_cast<qint64>(ls->peakConnections()));
        stats.insert(QStringLiteral("connMaxReached"), static_cast<qint64>(ls->maxConnectionReached()));
        stats.insert(QStringLiteral("connAverage"), static_cast<double>(ls->averageConnections()));
        stats.insert(QStringLiteral("connOpen"), static_cast<qint64>(ls->openSockets()));
    }

    // Server stats
    if (auto* sl = theApp.serverList) {
        auto srvStats = sl->stats();
        stats.insert(QStringLiteral("srvWorking"),
                     static_cast<qint64>(srvStats.total - srvStats.failed));
        stats.insert(QStringLiteral("srvFailed"), static_cast<qint64>(srvStats.failed));
        stats.insert(QStringLiteral("srvTotal"), static_cast<qint64>(srvStats.total));
        stats.insert(QStringLiteral("srvUsers"), static_cast<qint64>(srvStats.users));
        stats.insert(QStringLiteral("srvFiles"), static_cast<qint64>(srvStats.files));
        stats.insert(QStringLiteral("srvLowIDUsers"), static_cast<qint64>(srvStats.lowIDUsers));
    }

    // Client stats
    if (auto* cl = theApp.clientList) {
        stats.insert(QStringLiteral("knownClients"), static_cast<qint64>(cl->clientCount()));
        stats.insert(QStringLiteral("bannedClients"), static_cast<qint64>(cl->bannedCount()));
    }

    // Shared files stats
    if (auto* sf = theApp.sharedFileList) {
        uint64 largest = 0;
        stats.insert(QStringLiteral("sharedCount"), static_cast<qint64>(sf->getCount()));
        stats.insert(QStringLiteral("sharedSize"), static_cast<qint64>(sf->getDataSize(largest)));
        stats.insert(QStringLiteral("sharedLargest"), static_cast<qint64>(largest));
    }

    // Web server stream token (for GUI preview streaming)
    if (auto* da = DaemonApp::instance()) {
        if (auto* ws = da->webServer())
            stats.insert(QStringLiteral("streamToken"), ws->streamToken());
    }

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(stats)));
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

    // Server
    prefs.insert(QStringLiteral("safeServerConnect"), thePrefs.safeServerConnect());
    prefs.insert(QStringLiteral("autoConnectStaticOnly"), thePrefs.autoConnectStaticOnly());
    prefs.insert(QStringLiteral("useServerPriorities"), thePrefs.useServerPriorities());
    prefs.insert(QStringLiteral("addServersFromServer"), thePrefs.addServersFromServer());
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
    prefs.insert(QStringLiteral("logToDisk"), thePrefs.logToDisk());
    prefs.insert(QStringLiteral("verbose"), thePrefs.verbose());
    prefs.insert(QStringLiteral("closeUPnPOnExit"), thePrefs.closeUPnPOnExit());
    prefs.insert(QStringLiteral("skipWANIPSetup"), thePrefs.skipWANIPSetup());
    prefs.insert(QStringLiteral("skipWANPPPSetup"), thePrefs.skipWANPPPSetup());
    prefs.insert(QStringLiteral("fileBufferSize"), static_cast<qint64>(thePrefs.fileBufferSize()));
    prefs.insert(QStringLiteral("useCreditSystem"), thePrefs.useCreditSystem());
    prefs.insert(QStringLiteral("a4afSaveCpu"), thePrefs.a4afSaveCpu());
    prefs.insert(QStringLiteral("autoArchivePreviewStart"), thePrefs.autoArchivePreviewStart());
    prefs.insert(QStringLiteral("ed2kHostname"), thePrefs.ed2kHostname());
    prefs.insert(QStringLiteral("showExtControls"), thePrefs.showExtControls());
    prefs.insert(QStringLiteral("commitFiles"), thePrefs.commitFiles());
    prefs.insert(QStringLiteral("extractMetaData"), thePrefs.extractMetaData());
    prefs.insert(QStringLiteral("logLevel"), thePrefs.logLevel());
    prefs.insert(QStringLiteral("verboseLogToDisk"), thePrefs.verboseLogToDisk());
    prefs.insert(QStringLiteral("logSourceExchange"), thePrefs.logSourceExchange());
    prefs.insert(QStringLiteral("logBannedClients"), thePrefs.logBannedClients());
    prefs.insert(QStringLiteral("logRatingDescReceived"), thePrefs.logRatingDescReceived());
    prefs.insert(QStringLiteral("logSecureIdent"), thePrefs.logSecureIdent());
    prefs.insert(QStringLiteral("logFilteredIPs"), thePrefs.logFilteredIPs());
    prefs.insert(QStringLiteral("logFileSaving"), thePrefs.logFileSaving());
    prefs.insert(QStringLiteral("logA4AF"), thePrefs.logA4AF());
    prefs.insert(QStringLiteral("logUlDlEvents"), thePrefs.logUlDlEvents());
    prefs.insert(QStringLiteral("logRawSocketPackets"), thePrefs.logRawSocketPackets());
    prefs.insert(QStringLiteral("startCoreWithConsole"), thePrefs.startCoreWithConsole());
    prefs.insert(QStringLiteral("queueSize"), static_cast<qint64>(thePrefs.queueSize()));
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

        if (!applyPreferenceA(key, val))
            applyPreferenceB(key, val);
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
                m.insert(QStringLiteral("ip"), static_cast<qint64>(c->getIPAddress()));
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
    auto* kad = kad::Kademlia::instance();
    if (!kad || !kad->isRunning()) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503,
            QStringLiteral("Kademlia not running")));
        return;
    }
    kad->recheckFirewalled();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
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
    const bool ed2kConnected = theApp.isConnected();
    const bool ed2kConnecting = theApp.serverConnect && theApp.serverConnect->isConnecting();
    const bool ed2kFirewalled = theApp.isFirewalled();
    ed2k.insert(QStringLiteral("connected"), ed2kConnected);
    ed2k.insert(QStringLiteral("connecting"), ed2kConnecting);
    ed2k.insert(QStringLiteral("firewalled"), ed2kFirewalled);

    if (ed2kConnected && theApp.serverConnect) {
        ed2k.insert(QStringLiteral("clientID"),
                     static_cast<qint64>(theApp.serverConnect->clientID()));
        ed2k.insert(QStringLiteral("lowID"), theApp.serverConnect->isLowID());
        ed2k.insert(QStringLiteral("publicIP"),
                     static_cast<qint64>(thePrefs.publicIP()));

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

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(info)));
}

void IpcClientHandler::handleSyncLogs(const IpcMessage& msg)
{
    const int64_t lastLogId = msg.fieldInt(0);
    auto entries = DaemonApp::logsSince(lastLogId);

    QCborArray arr;
    for (const auto& e : entries) {
        QCborArray entry;
        entry.append(e.id);
        entry.append(e.category);
        entry.append(static_cast<int64_t>(e.severity));
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
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
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
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
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
    theApp.searchList->markFileAsSpam(file, true);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

// ---------------------------------------------------------------------------
// handleResetStats — reset session statistics
// ---------------------------------------------------------------------------

void IpcClientHandler::handleResetStats(const IpcMessage& msg)
{
    if (theApp.statistics) {
        theApp.statistics->resetDownDatarateOverhead();
        theApp.statistics->resetUpDatarateOverhead();
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
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
    theApp.sharedFileList->removeFile(file);
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
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

    // Source names: collect unique filenames with count
    QCborArray sourceNames;
    std::unordered_map<QString, int> nameMap;
    for (const auto* client : pf->srcList()) {
        if (!client->clientFilename().isEmpty())
            nameMap[client->clientFilename()]++;
    }
    for (const auto& [name, count] : nameMap)
        sourceNames.append(QCborMap{
            {QLatin1StringView("name"), name},
            {QLatin1StringView("count"), count}});
    details.insert(QLatin1StringView("sourceNames"), sourceNames);

    // Comments: from sources + kad notes
    QCborArray comments;
    for (const auto* client : pf->srcList()) {
        if (!client->fileComment().isEmpty() || client->fileRating() > 0)
            comments.append(QCborMap{
                {QLatin1StringView("userName"), client->userName()},
                {QLatin1StringView("rating"), client->fileRating()},
                {QLatin1StringView("comment"), client->fileComment()}});
    }
    details.insert(QLatin1StringView("comments"), comments);

    // ED2K links (pre-generated variants)
    details.insert(QLatin1StringView("ed2kLink"),         pf->getED2kLink(false, false, false));
    details.insert(QLatin1StringView("ed2kLinkHashset"),   pf->getED2kLink(true, false, false));
    details.insert(QLatin1StringView("ed2kLinkHTML"),      pf->getED2kLink(false, true, false));
    details.insert(QLatin1StringView("ed2kLinkHostname"),  pf->getED2kLink(false, false, true));

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

    // ED2K links (pre-generated variants)
    details.insert(QLatin1StringView("ed2kLink"),         kf->getED2kLink(false, false, false));
    details.insert(QLatin1StringView("ed2kLinkHashset"),   kf->getED2kLink(true, false, false));
    details.insert(QLatin1StringView("ed2kLinkHTML"),      kf->getED2kLink(false, true, false));
    details.insert(QLatin1StringView("ed2kLinkHostname"),  kf->getED2kLink(false, false, true));

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

    // Comments and sourceNames (empty for shared KnownFiles — dialog expects these fields)
    details.insert(QLatin1StringView("comments"), QCborArray());
    details.insert(QLatin1StringView("sourceNames"), QCborArray());

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
    else if (key == QStringLiteral("logToDisk"))
        thePrefs.setLogToDisk(val.toBool());
    else if (key == QStringLiteral("verbose"))
        thePrefs.setVerbose(val.toBool());
    else if (key == QStringLiteral("closeUPnPOnExit"))
        thePrefs.setCloseUPnPOnExit(val.toBool());
    else if (key == QStringLiteral("skipWANIPSetup"))
        thePrefs.setSkipWANIPSetup(val.toBool());
    else if (key == QStringLiteral("skipWANPPPSetup"))
        thePrefs.setSkipWANPPPSetup(val.toBool());
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
    else if (key == QStringLiteral("showExtControls"))
        thePrefs.setShowExtControls(val.toBool());
    else if (key == QStringLiteral("commitFiles"))
        thePrefs.setCommitFiles(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("extractMetaData"))
        thePrefs.setExtractMetaData(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("logLevel"))
        thePrefs.setLogLevel(static_cast<int>(val.toInteger()));
    else if (key == QStringLiteral("verboseLogToDisk"))
        thePrefs.setVerboseLogToDisk(val.toBool());
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
    else if (key == QStringLiteral("enableIpcLog"))
        thePrefs.setEnableIpcLog(val.toBool());
    else if (key == QStringLiteral("startCoreWithConsole"))
        thePrefs.setStartCoreWithConsole(val.toBool());
    else if (key == QStringLiteral("queueSize"))
        thePrefs.setQueueSize(static_cast<uint32>(val.toInteger()));
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
    // Web Server
    else if (key == QStringLiteral("webServerEnabled"))
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
    else
        return false;
    return true;
}

} // namespace eMule
