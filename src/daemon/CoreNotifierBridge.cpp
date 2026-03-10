/// @file CoreNotifierBridge.cpp
/// @brief Core signal to IPC push event bridge — implementation.

#include "CoreNotifierBridge.h"
#include "IpcServer.h"

#include "IpcMessage.h"

#include "app/AppContext.h"
#include "net/SmtpClient.h"
#include "prefs/Preferences.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/PartFile.h"
#include "friends/FriendList.h"
#include "utils/OtherFunctions.h"
#include "kademlia/Kademlia.h"
#include "upnp/UPnPManager.h"
#include "utils/Log.h"
#include "files/SharedFileList.h"
#include "search/SearchList.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"

namespace eMule {

using namespace Ipc;

CoreNotifierBridge::CoreNotifierBridge(IpcServer* ipcServer, QObject* parent)
    : QObject(parent)
    , m_ipcServer(ipcServer)
{
}

CoreNotifierBridge::~CoreNotifierBridge() = default;

void CoreNotifierBridge::connectAll()
{
    // DownloadQueue
    if (theApp.downloadQueue) {
        connect(theApp.downloadQueue, &DownloadQueue::fileAdded,
                this, &CoreNotifierBridge::onDownloadAdded);
        connect(theApp.downloadQueue, &DownloadQueue::fileRemoved,
                this, &CoreNotifierBridge::onDownloadRemoved);
        connect(theApp.downloadQueue, &DownloadQueue::fileCompleted,
                this, &CoreNotifierBridge::onDownloadCompleted);

        // Wire source signals for existing PartFiles (downloading tab)
        for (auto* pf : theApp.downloadQueue->files()) {
            connect(pf->partNotifier(), &PartFileNotifier::sourceAdded,
                    this, &CoreNotifierBridge::onDownloadSourcesChanged);
            connect(pf->partNotifier(), &PartFileNotifier::sourceRemoved,
                    this, &CoreNotifierBridge::onDownloadSourcesChanged);
        }
        // Wire source signals for newly added PartFiles
        connect(theApp.downloadQueue, &DownloadQueue::fileAdded, this, [this](PartFile* pf) {
            connect(pf->partNotifier(), &PartFileNotifier::sourceAdded,
                    this, &CoreNotifierBridge::onDownloadSourcesChanged);
            connect(pf->partNotifier(), &PartFileNotifier::sourceRemoved,
                    this, &CoreNotifierBridge::onDownloadSourcesChanged);
        });
    }

    // ServerConnect
    if (theApp.serverConnect) {
        connect(theApp.serverConnect, &ServerConnect::stateChanged,
                this, &CoreNotifierBridge::onServerStateChanged);
    }

    // Statistics
    if (theApp.statistics) {
        connect(theApp.statistics, &Statistics::statsUpdated,
                this, &CoreNotifierBridge::onStatsUpdated);
    }

    // SearchList
    if (theApp.searchList) {
        connect(theApp.searchList, &SearchList::resultAdded,
                this, &CoreNotifierBridge::onSearchResultAdded);
        connect(theApp.searchList, &SearchList::resultUpdated,
                this, &CoreNotifierBridge::onSearchResultAdded);
    }

    // SharedFileList
    if (theApp.sharedFileList) {
        connect(theApp.sharedFileList, &SharedFileList::fileAdded,
                this, &CoreNotifierBridge::onSharedFileAdded);
    }

    // UploadQueue
    if (theApp.uploadQueue) {
        connect(theApp.uploadQueue, &UploadQueue::uploadStarted,
                this, &CoreNotifierBridge::onUploadChanged);
        connect(theApp.uploadQueue, &UploadQueue::uploadEnded,
                this, &CoreNotifierBridge::onUploadChanged);
        connect(theApp.uploadQueue, &UploadQueue::clientAddedToQueue,
                this, &CoreNotifierBridge::onUploadChanged);
        connect(theApp.uploadQueue, &UploadQueue::clientRemovedFromQueue,
                this, &CoreNotifierBridge::onUploadChanged);
    }

    // ClientList (Known Clients tab)
    if (theApp.clientList) {
        connect(theApp.clientList, &ClientList::clientAdded,
                this, &CoreNotifierBridge::onKnownClientsChanged);
        connect(theApp.clientList, &ClientList::clientRemoved,
                this, &CoreNotifierBridge::onKnownClientsChanged);

        // Wire chat signals for existing clients
        theApp.clientList->forEachClient([this](UpDownClient* c) {
            connectClientChatSignal(c);
        });
        // Wire chat signals for newly added clients
        connect(theApp.clientList, &ClientList::clientAdded,
                this, &CoreNotifierBridge::connectClientChatSignal);
    }

    // FriendList
    if (theApp.friendList) {
        connect(theApp.friendList, &FriendList::friendAdded,
                this, &CoreNotifierBridge::onFriendListChanged);
        connect(theApp.friendList, &FriendList::friendRemoved,
                this, [this](const QString&) { onFriendListChanged(); });
        connect(theApp.friendList, &FriendList::friendUpdated,
                this, [this](Friend*) { onFriendListChanged(); });
    }

    // Kademlia
    if (auto* kad = kad::Kademlia::instance()) {
        connect(kad, &kad::Kademlia::started,  this, &CoreNotifierBridge::onKadStateChanged);
        connect(kad, &kad::Kademlia::stopped,  this, &CoreNotifierBridge::onKadStateChanged);
        connect(kad, &kad::Kademlia::connected, this, &CoreNotifierBridge::onKadStateChanged);
        connect(kad, &kad::Kademlia::firewallStatusChanged,
                this, [this](bool) { onKadStateChanged(); });
        connect(kad, &kad::Kademlia::searchesChanged,
                this, &CoreNotifierBridge::onKadSearchesChanged);
        connect(kad, &kad::Kademlia::statsUpdated,
                this, [this](uint32_t, uint32_t) { onKadStateChanged(); });
    }

    // UPnP
    if (theApp.upnpManager) {
        connect(theApp.upnpManager, &UPnPManager::discoveryComplete,
                this, &CoreNotifierBridge::onUPnPDiscoveryComplete);
    }
}

// ---------------------------------------------------------------------------
// Push event handlers
// ---------------------------------------------------------------------------

void CoreNotifierBridge::onDownloadAdded()
{
    IpcMessage msg(IpcMsgType::PushDownloadAdded, 0);
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onDownloadRemoved()
{
    IpcMessage msg(IpcMsgType::PushDownloadRemoved, 0);
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onDownloadCompleted(PartFile* file)
{
    // Broadcast IPC event (reuses PushDownloadUpdate)
    IpcMessage msg(IpcMsgType::PushDownloadUpdate, 0);
    m_ipcServer->broadcast(msg);

    // Email notification for completed downloads
    if (thePrefs.notifyOnDownloadFinished() && thePrefs.notifyEmailEnabled()) {
        QString name = file ? file->fileName() : QStringLiteral("Unknown");
        sendEmailNotification(
            QStringLiteral("eMule: Download finished"),
            QStringLiteral("Download completed: %1").arg(name));
    }
}

void CoreNotifierBridge::onServerStateChanged()
{
    IpcMessage msg(IpcMsgType::PushServerState, 0);
    QCborMap info;
    bool connected = theApp.isConnected();
    info.insert(QStringLiteral("connected"),  connected);
    info.insert(QStringLiteral("connecting"),
                theApp.serverConnect && theApp.serverConnect->isConnecting());
    info.insert(QStringLiteral("firewalled"), theApp.isFirewalled());
    info.insert(QStringLiteral("clientID"),   static_cast<qint64>(theApp.getID()));
    if (connected && theApp.serverConnect) {
        if (const auto* srv = theApp.serverConnect->currentServer()) {
            info.insert(QStringLiteral("serverIP"), static_cast<qint64>(srv->ip()));
            info.insert(QStringLiteral("serverPort"), static_cast<qint64>(srv->port()));
            info.insert(QStringLiteral("serverId"), static_cast<qint64>(srv->serverId()));
        }
    }
    msg.append(info);
    m_ipcServer->broadcast(msg);

    // Email notification for urgent: server connection lost
    if (!connected && thePrefs.notifyOnUrgent() && thePrefs.notifyEmailEnabled()) {
        sendEmailNotification(
            QStringLiteral("eMule: Server connection lost"),
            QStringLiteral("Warning: Server connection has been lost."));
    }
}

void CoreNotifierBridge::onStatsUpdated()
{
    IpcMessage msg(IpcMsgType::PushStatsUpdate, 0);
    if (theApp.statistics) {
        QCborMap stats;
        stats.insert(QStringLiteral("sessionSentBytes"),
                     static_cast<qint64>(theApp.statistics->sessionSentBytes()));
        stats.insert(QStringLiteral("sessionReceivedBytes"),
                     static_cast<qint64>(theApp.statistics->sessionReceivedBytes()));
        msg.append(stats);
    }
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onSearchResultAdded(SearchFile* file)
{
    IpcMessage msg(IpcMsgType::PushSearchResult, 0);
    if (file)
        msg.append(static_cast<qint64>(file->searchID()));
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onSharedFileAdded()
{
    IpcMessage msg(IpcMsgType::PushSharedFileUpdate, 0);
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onUploadChanged()
{
    IpcMessage msg(IpcMsgType::PushUploadUpdate, 0);
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onKadStateChanged()
{
    IpcMessage msg(IpcMsgType::PushKadUpdate, 0);
    auto* kad = kad::Kademlia::instance();
    QCborMap info;
    info.insert(QStringLiteral("running"),    kad && kad->isRunning());
    info.insert(QStringLiteral("connected"),  kad && kad->isConnected());
    info.insert(QStringLiteral("firewalled"), kad && kad->isFirewalled());
    info.insert(QStringLiteral("users"),  static_cast<qint64>(kad ? kad->getKademliaUsers() : 0));
    info.insert(QStringLiteral("files"),  static_cast<qint64>(kad ? kad->getKademliaFiles() : 0));
    msg.append(info);
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onKadSearchesChanged()
{
    IpcMessage msg(IpcMsgType::PushKadSearchesChanged, 0);
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onDownloadSourcesChanged()
{
    // Reuses existing PushDownloadUpdate — GUI already handles it
    IpcMessage msg(IpcMsgType::PushDownloadUpdate, 0);
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onKnownClientsChanged()
{
    IpcMessage msg(IpcMsgType::PushKnownClientsChanged, 0);
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onFriendListChanged()
{
    IpcMessage msg(IpcMsgType::PushFriendListChanged, 0);
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onChatMessageReceived(const QString& fromUser,
                                                const QString& message)
{
    auto* client = qobject_cast<UpDownClient*>(sender());
    IpcMessage msg(IpcMsgType::PushChatMessage, 0);
    msg.append(client ? md4str(client->userHash()) : QString());
    msg.append(fromUser);
    msg.append(message);
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::connectClientChatSignal(UpDownClient* client)
{
    connect(client, &UpDownClient::chatMessageReceived,
            this, &CoreNotifierBridge::onChatMessageReceived);
}

void CoreNotifierBridge::onUPnPDiscoveryComplete(bool success)
{
    logInfo(QStringLiteral("UPnP: discovery %1")
                .arg(success ? QStringLiteral("succeeded") : QStringLiteral("failed")));
}

void CoreNotifierBridge::sendEmailNotification(const QString& subject, const QString& body)
{
    if (!thePrefs.notifyEmailEnabled())
        return;

    if (!m_smtp)
        m_smtp = new SmtpClient(this);

    m_smtp->sendMail(
        thePrefs.notifyEmailSmtpServer(),
        thePrefs.notifyEmailSmtpPort(),
        thePrefs.notifyEmailSmtpTls(),
        thePrefs.notifyEmailSmtpAuth(),
        thePrefs.notifyEmailSmtpUser(),
        thePrefs.notifyEmailSmtpPassword(),
        thePrefs.notifyEmailSender(),
        thePrefs.notifyEmailRecipient(),
        subject,
        body);
}

} // namespace eMule
