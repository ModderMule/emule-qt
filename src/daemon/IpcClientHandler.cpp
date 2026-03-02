/// @file IpcClientHandler.cpp
/// @brief Per-connection IPC request handler — implementation.

#include "IpcClientHandler.h"
#include "DaemonApp.h"

#include "ipc/CborSerializers.h"

#include <QDir>

#include "app/AppContext.h"
#include "app/CoreSession.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
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
#include "prefs/Preferences.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHostAddress>
#include <QSet>

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

IpcClientHandler::IpcClientHandler(QTcpSocket* socket, QObject* parent)
    : QObject(parent)
    , m_connection(std::make_unique<IpcConnection>(socket, this))
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

    // No fields — connect to any server (backward compatible)
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
    // ToDo: ED2K server search (Ed2kServer, Ed2kGlobal, Automatic)

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

    const QString tempDir = QDir(thePrefs.configDir()).filePath(QStringLiteral("Temp"));
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
    if (theApp.statistics) {
        stats.insert(QStringLiteral("sessionSentBytes"),
                     static_cast<qint64>(theApp.statistics->sessionSentBytes()));
        stats.insert(QStringLiteral("sessionReceivedBytes"),
                     static_cast<qint64>(theApp.statistics->sessionReceivedBytes()));
        stats.insert(QStringLiteral("uptime"),
                     static_cast<qint64>(std::time(nullptr) - theApp.statistics->startTime()));
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
    prefs.insert(QStringLiteral("networkED2K"), thePrefs.networkED2K());
    prefs.insert(QStringLiteral("kadEnabled"), thePrefs.kadEnabled());
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

    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(prefs)));
}

void IpcClientHandler::handleSetPreferences(const IpcMessage& msg)
{
    // Fields come in key-value pairs: [key1, val1, key2, val2, ...]
    for (int i = 0; i + 1 < msg.fieldCount(); i += 2) {
        const QString key = msg.fieldString(i);
        const QCborValue val = msg.field(i + 1);

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
        else if (key == QStringLiteral("networkED2K"))
            thePrefs.setNetworkED2K(val.toBool());
        else if (key == QStringLiteral("kadEnabled"))
            thePrefs.setKadEnabled(val.toBool());
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
    }
    thePrefs.save();

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

    // Rescan shared files if directory settings changed
    if (theApp.sharedFileList)
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

} // namespace eMule
