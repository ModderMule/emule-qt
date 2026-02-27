/// @file IpcClientHandler.cpp
/// @brief Per-connection IPC request handler — implementation.

#include "IpcClientHandler.h"
#include "DaemonApp.h"

#include "ipc/CborSerializers.h"

#include "app/AppContext.h"
#include "app/CoreSession.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchManager.h"
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
    case IpcMsgType::GetServers:           handleGetServers(msg); break;
    case IpcMsgType::GetConnection:        handleGetConnection(msg); break;
    case IpcMsgType::ConnectToServer:      handleConnectToServer(msg); break;
    case IpcMsgType::DisconnectFromServer: handleDisconnectFromServer(msg); break;
    case IpcMsgType::StartSearch:          handleStartSearch(msg); break;
    case IpcMsgType::GetSearchResults:     handleGetSearchResults(msg); break;
    case IpcMsgType::GetSharedFiles:       handleGetSharedFiles(msg); break;
    case IpcMsgType::GetFriends:           handleGetFriends(msg); break;
    case IpcMsgType::AddFriend:            handleAddFriend(msg); break;
    case IpcMsgType::RemoveFriend:         handleRemoveFriend(msg); break;
    case IpcMsgType::GetStats:             handleGetStats(msg); break;
    case IpcMsgType::GetPreferences:       handleGetPreferences(msg); break;
    case IpcMsgType::SetPreferences:       handleSetPreferences(msg); break;
    case IpcMsgType::Subscribe:            handleSubscribe(msg); break;
    case IpcMsgType::GetKadContacts:       handleGetKadContacts(msg); break;
    case IpcMsgType::GetKadStatus:         handleGetKadStatus(msg); break;
    case IpcMsgType::BootstrapKad:         handleBootstrapKad(msg); break;
    case IpcMsgType::DisconnectKad:        handleDisconnectKad(msg); break;
    case IpcMsgType::GetKadSearches:       handleGetKadSearches(msg); break;
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
    QCborArray uploads;
    // ToDo: serialize active uploads when UploadQueue exposes upload list
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(uploads)));
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
    theApp.serverConnect->connectToAnyServer();
    sendMessage(IpcMessage::makeResult(msg.seqId(), true));
}

void IpcClientHandler::handleDisconnectFromServer(const IpcMessage& msg)
{
    if (!theApp.serverConnect) {
        sendMessage(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ServerConnect unavailable")));
        return;
    }
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
    params.expression = msg.fieldString(0);
    params.fileType = msg.fieldString(1);

    const uint32 searchID = theApp.searchList->newSearch(params.fileType, params);
    QCborMap result;
    result.insert(QStringLiteral("searchID"), static_cast<qint64>(searchID));
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

void IpcClientHandler::handleGetSharedFiles(const IpcMessage& msg)
{
    QCborArray files;
    if (theApp.sharedFileList) {
        theApp.sharedFileList->forEachFile([&files](KnownFile* kf) {
            QCborMap m;
            m.insert(QStringLiteral("hash"), md4str(kf->fileHash()));
            m.insert(QStringLiteral("fileName"), kf->fileName());
            m.insert(QStringLiteral("fileSize"), static_cast<qint64>(kf->fileSize()));
            files.append(m);
        });
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(files)));
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
    if (target)
        theApp.friendList->removeFriend(target);
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
    prefs.insert(QStringLiteral("autoConnect"), thePrefs.autoConnect());
    prefs.insert(QStringLiteral("kadEnabled"), thePrefs.kadEnabled());
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
        else if (key == QStringLiteral("maxUpload"))
            thePrefs.setMaxUpload(static_cast<uint32>(val.toInteger()));
        else if (key == QStringLiteral("maxDownload"))
            thePrefs.setMaxDownload(static_cast<uint32>(val.toInteger()));
        else if (key == QStringLiteral("autoConnect"))
            thePrefs.setAutoConnect(val.toBool());
        // ToDo: add more preference keys as needed
    }
    thePrefs.save();
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
            m.insert(QStringLiteral("responses"), static_cast<qint64>(search->getRequestAnswer()));
            searches.append(m);
        }
    }
    sendMessage(IpcMessage::makeResult(msg.seqId(), true, QCborValue(searches)));
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
