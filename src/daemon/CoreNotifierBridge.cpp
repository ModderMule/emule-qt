/// @file CoreNotifierBridge.cpp
/// @brief Core signal to IPC push event bridge — implementation.

#include "CoreNotifierBridge.h"
#include "IpcServer.h"

#include "IpcMessage.h"

#include "app/AppContext.h"
#include "kademlia/Kademlia.h"
#include "files/SharedFileList.h"
#include "search/SearchList.h"
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
    }

    // Kademlia
    if (auto* kad = kad::Kademlia::instance()) {
        connect(kad, &kad::Kademlia::started,  this, &CoreNotifierBridge::onKadStateChanged);
        connect(kad, &kad::Kademlia::stopped,  this, &CoreNotifierBridge::onKadStateChanged);
        connect(kad, &kad::Kademlia::connected, this, &CoreNotifierBridge::onKadStateChanged);
        connect(kad, &kad::Kademlia::firewallStatusChanged,
                this, [this](bool) { onKadStateChanged(); });
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

void CoreNotifierBridge::onServerStateChanged()
{
    IpcMessage msg(IpcMsgType::PushServerState, 0);
    QCborMap info;
    info.insert(QStringLiteral("connected"),  theApp.isConnected());
    info.insert(QStringLiteral("connecting"),
                theApp.serverConnect && theApp.serverConnect->isConnecting());
    info.insert(QStringLiteral("firewalled"), theApp.isFirewalled());
    info.insert(QStringLiteral("clientID"),   static_cast<qint64>(theApp.getID()));
    msg.append(info);
    m_ipcServer->broadcast(msg);
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

void CoreNotifierBridge::onSearchResultAdded()
{
    IpcMessage msg(IpcMsgType::PushSearchResult, 0);
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
    msg.append(info);
    m_ipcServer->broadcast(msg);
}

} // namespace eMule
