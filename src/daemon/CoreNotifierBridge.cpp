/// @file CoreNotifierBridge.cpp
/// @brief Core signal to IPC push event bridge — implementation.

#include "CoreNotifierBridge.h"
#include "SharedFileRows.h"
#include "IpcServer.h"

#include "IpcMessage.h"
#include "PushCoalescer.h"

#include "app/AppContext.h"
#include "net/SmtpClient.h"
#include "prefs/Preferences.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/PartFile.h"
#include "friends/FriendList.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadPrefs.h"
#include "portmap/PortMapper.h"
#include "utils/Log.h"
#include "files/SharedFileList.h"
#include "search/GlobalSearchScheduler.h"
#include "search/SearchList.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"
#include "UsenetBridge.h"

#include <QBuffer>

namespace eMule {

using namespace Ipc;

namespace {

/// Cap on the Server Info backlog. A greeting is a handful of lines and a session
/// reconnects to a small number of servers, so this is generous headroom rather
/// than a limit anyone should hit.
constexpr std::size_t kMaxServerMessageBacklog = 200;

/// Minimum spacing between two broadcasts of the same push. Kept below the GUI's
/// 500 ms local poll (IpcClient::LocalPollingMs) so a push still beats the poll to
/// the data, while a per-item signal storm collapses into a handful of sends.
constexpr int kPushWindowMs = 250;

/// Shared-file rows go out in batches: this often, at most this many at a time. A
/// scan that adds thousands of files becomes a few pushes a second, each row sent once.
constexpr int kSharedFlushMs = 250;
constexpr int kSharedFlushRows = 500;

/// Function-local static: the backlog outlives individual GUI connections but is
/// per-daemon-process, mirroring how DaemonApp keeps its log ring buffer.
std::deque<CoreNotifierBridge::ServerMessage>& serverMessageBacklog()
{
    static std::deque<CoreNotifierBridge::ServerMessage> backlog;
    return backlog;
}

} // namespace

CoreNotifierBridge::CoreNotifierBridge(IpcServer* ipcServer, QObject* parent)
    : QObject(parent)
    , m_ipcServer(ipcServer)
    , m_pushes(new Ipc::PushCoalescer(this))
{
    connect(m_pushes, &Ipc::PushCoalescer::ready, this, [this](const IpcMessage& msg) {
        m_ipcServer->broadcast(msg);
    });

    m_sharedFlushTimer.setSingleShot(true);
    m_sharedFlushTimer.setInterval(kSharedFlushMs);
    connect(&m_sharedFlushTimer, &QTimer::timeout, this, &CoreNotifierBridge::flushSharedFiles);

    // The transferred counters and the rate of a file being uploaded change with
    // every block and have no signal; only the handful of files in upload slots do.
    m_sharedRatesTimer.setInterval(1000);
    connect(&m_sharedRatesTimer, &QTimer::timeout, this, [this] {
        if (m_ipcServer->clientCount() == 0 || !theApp.uploadQueue)
            return;
        theApp.uploadQueue->forEachUploading([this](UpDownClient* c) {
            onSharedFileChanged(QByteArray(reinterpret_cast<const char*>(c->reqUpFileId()), 16));
        });
    });
    m_sharedRatesTimer.start();
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
        connect(theApp.serverConnect, &ServerConnect::serverMessageReceived,
                this, &CoreNotifierBridge::onServerMessage);
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
        connect(theApp.searchList, &SearchList::searchStateChanged,
                this, &CoreNotifierBridge::onSearchStateChanged);
    }

    // Global (UDP) search sweep — one progress event per server queried.
    if (theApp.globalSearch) {
        connect(theApp.globalSearch, &GlobalSearchScheduler::progress,
                this, &CoreNotifierBridge::onGlobalSearchProgress);
    }

    // SharedFileList
    if (theApp.sharedFileList) {
        const auto changed = [this](KnownFile* file) {
            onSharedFileChanged(QByteArray(reinterpret_cast<const char*>(file->fileHash()), 16));
        };
        connect(theApp.sharedFileList, &SharedFileList::fileAdded, this, changed);
        connect(theApp.sharedFileList, &SharedFileList::fileRelocated, this, changed);
        connect(theApp.sharedFileList, &SharedFileList::fileChanged,
                this, &CoreNotifierBridge::onSharedFileChanged);
        connect(theApp.sharedFileList, &SharedFileList::fileRemoved,
                this, &CoreNotifierBridge::onSharedFileRemoved);
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

        // Each of these moves a count on the row of the file the client asks for.
        const auto fileOfClient = [this](UpDownClient* c) {
            if (c)
                onSharedFileChanged(QByteArray(reinterpret_cast<const char*>(c->reqUpFileId()), 16));
        };
        connect(theApp.uploadQueue, &UploadQueue::uploadStarted, this, fileOfClient);
        connect(theApp.uploadQueue, &UploadQueue::uploadEnded, this, fileOfClient);
        connect(theApp.uploadQueue, &UploadQueue::clientAddedToQueue, this, fileOfClient);
        connect(theApp.uploadQueue, &UploadQueue::clientRemovedFromQueue, this, fileOfClient);
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
            connectClientSharedFilesSignal(c);
        });
        // Wire chat signals for newly added clients
        connect(theApp.clientList, &ClientList::clientAdded,
                this, &CoreNotifierBridge::connectClientChatSignal);
        connect(theApp.clientList, &ClientList::clientAdded,
                this, &CoreNotifierBridge::connectClientSharedFilesSignal);
    }

    // FriendList
    if (theApp.friendList) {
        connect(theApp.friendList, &FriendList::friendAdded,
                this, &CoreNotifierBridge::onFriendListChanged);
        connect(theApp.friendList, &FriendList::friendRemoved,
                this, [this](const QString&) { onFriendListChanged(); });
        connect(theApp.friendList, &FriendList::friendUpdated,
                this, [this](Friend*) { onFriendListChanged(); });
        connect(theApp.friendList, &FriendList::friendConnectionProgress,
                this, &CoreNotifierBridge::onFriendConnectionProgress);
        connect(theApp.friendList, &FriendList::friendConnectingResult,
                this, &CoreNotifierBridge::onFriendConnectingResult);
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

    // Port mapping
    if (theApp.portMapper) {
        connect(theApp.portMapper, &PortMapper::statusChanged,
                this, &CoreNotifierBridge::onPortMapStatusChanged);
    }
}

// ---------------------------------------------------------------------------
// Push event handlers
// ---------------------------------------------------------------------------

void CoreNotifierBridge::onDownloadAdded()
{
    m_pushes->post(IpcMsgType::PushDownloadAdded,
                   [] { return IpcMessage(IpcMsgType::PushDownloadAdded, 0); },
                   kPushWindowMs);
}

void CoreNotifierBridge::onDownloadRemoved()
{
    m_pushes->post(IpcMsgType::PushDownloadRemoved,
                   [] { return IpcMessage(IpcMsgType::PushDownloadRemoved, 0); },
                   kPushWindowMs);
}

void CoreNotifierBridge::onDownloadCompleted(PartFile* file)
{
    // Broadcast IPC event (reuses PushDownloadUpdate)
    m_pushes->post(IpcMsgType::PushDownloadUpdate,
                   [] { return IpcMessage(IpcMsgType::PushDownloadUpdate, 0); },
                   kPushWindowMs);

    // Email notification for completed downloads. Deliberately outside the
    // coalescer: a suppressed push costs the GUI nothing, but a suppressed mail
    // would lose a completion the user asked to be told about.
    if (thePrefs.notifyOnDownloadFinished() && thePrefs.notifyEmailEnabled()) {
        QString name = file ? file->fileName() : QStringLiteral("Unknown");
        sendEmailNotification(
            QStringLiteral("eMule: Download finished"),
            QStringLiteral("Download completed: %1").arg(name));
    }
}

void CoreNotifierBridge::onServerStateChanged()
{
    // Not coalesced, unlike every other state snapshot. It is low-rate, and it is
    // the one push whose *transitions* matter rather than just its latest value —
    // the GUI raises a "Connection Lost" notification from it. Collapsing a
    // connected → lost → connected blip inside a window would drop that silently.
    IpcMessage msg(IpcMsgType::PushServerState, 0);
    QCborMap info;
    // ED2K-only: drives the GUI's eD2K indicator and the "connection lost"
    // notification/email below — theApp.isConnected() is true for Kad too.
    bool connected = theApp.serverConnect && theApp.serverConnect->isConnected();
    info.insert(QStringLiteral("connected"),  connected);
    info.insert(QStringLiteral("connecting"),
                theApp.serverConnect && theApp.serverConnect->isConnecting());
    info.insert(QStringLiteral("firewalled"), theApp.isFirewalled());
    // Per-network eD2K LowID — "firewalled" above is the combined ed2k+kad state and
    // goes false as soon as Kad is open, so it cannot drive the eD2K LowID indicators.
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
            // The address this session dialed: a dual-stack server may be on its IPv6
            info.insert(QStringLiteral("serverAddress"),
                        srv->hasDynIP() ? srv->address()
                                        : theApp.serverConnect->sessionAddress().toString());
            info.insert(QStringLiteral("serverVersion"), srv->version());
            info.insert(QStringLiteral("serverUsers"), static_cast<qint64>(srv->users()));
            info.insert(QStringLiteral("serverFiles"), static_cast<qint64>(srv->files()));
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

void CoreNotifierBridge::onServerMessage(ServerMsgType type, const QString& text)
{
    // Deliberately NOT routed through the log: the reference writes this pane via
    // CemuleDlg::AddServerMessageLine, a channel separate from AddLogText (no
    // per-line timestamp, no disk log, no notifier) — srchybrid/EmuleDlg.cpp:961-972.
    static qint64 nextId = 1;
    const qint64 id = nextId++;

    serverMessageBacklog().push_back({id, type, text});
    while (serverMessageBacklog().size() > kMaxServerMessageBacklog)
        serverMessageBacklog().pop_front();

    IpcMessage msg(IpcMsgType::PushServerMessage, 0);
    msg.append(id);
    msg.append(static_cast<qint64>(type));
    msg.append(text);
    m_ipcServer->broadcast(msg);
}

const std::deque<CoreNotifierBridge::ServerMessage>& CoreNotifierBridge::serverMessageHistory()
{
    return serverMessageBacklog();
}

void CoreNotifierBridge::onStatsUpdated()
{
    // Snapshot push: the payload is built when it is actually sent, so a suppressed
    // event costs nothing and the message that does go out carries the newest
    // figures rather than the ones that happened to open the window.
    m_pushes->post(IpcMsgType::PushStatsUpdate, [] {
        IpcMessage msg(IpcMsgType::PushStatsUpdate, 0);
        QCborMap stats;
        if (theApp.statistics) {
            stats.insert(QStringLiteral("sessionSentBytes"),
                         static_cast<qint64>(theApp.statistics->sessionSentBytes()));
            stats.insert(QStringLiteral("sessionReceivedBytes"),
                         static_cast<qint64>(theApp.statistics->sessionReceivedBytes()));
        }
        // Same key as GetStats. The Transfer window's "Clients on queue: N" label is
        // on screen whichever client list is showing, and this is the only thing it
        // needs — without it the GUI had to refetch the whole waiting list twice a
        // second just to count it, even with both queue lists hidden.
        if (theApp.uploadQueue) {
            stats.insert(QStringLiteral("upWaiting"),
                         static_cast<qint64>(theApp.uploadQueue->waitingUserCount()));
        }
        // The Usenet panel says why its rate stops where it does, and the split
        // moves with the other engine's demand, so it rides the once-a-second push.
        UsenetBridge::insertDownloadSplit(stats);
        if (!stats.isEmpty())
            msg.append(stats);
        return msg;
    }, kPushWindowMs);
}

void CoreNotifierBridge::onSearchResultAdded(SearchFile* file)
{
    // Both resultAdded and resultUpdated land here, so every source-count bump on an
    // existing row is another event. Keyed by search so a busy tab cannot suppress
    // a quiet one running beside it.
    const auto searchID = file ? file->searchID() : 0u;
    m_pushes->post(IpcMsgType::PushSearchResult, [searchID] {
        IpcMessage msg(IpcMsgType::PushSearchResult, 0);
        if (searchID != 0)
            msg.append(static_cast<qint64>(searchID));
        return msg;
    }, kPushWindowMs, searchID);
}

void CoreNotifierBridge::onSearchStateChanged(const SearchStatus& status)
{
    // Keyed by search: when two changes fall into one window the newer is the one
    // sent, and that is the one that still holds.
    m_pushes->post(IpcMsgType::PushSearchState, [status] {
        IpcMessage msg(IpcMsgType::PushSearchState, 0);
        msg.append(static_cast<qint64>(status.searchID));
        msg.append(static_cast<qint64>(status.state));
        msg.append(status.reason);
        msg.append(status.error);
        msg.append(static_cast<qint64>(status.type));
        msg.append(status.keyword);
        msg.append(status.primaryKeyword);
        return msg;
    }, kPushWindowMs, status.searchID);
}

void CoreNotifierBridge::onGlobalSearchProgress(uint32 searchID, uint32 asked,
                                                uint32 total, bool running)
{
    // Ticks are 750ms apart so the window is normally a no-op; it only bounds a
    // burst. The coalescer always sends the newest builder when a window closes, so
    // the final running=false is never the one dropped.
    m_pushes->post(IpcMsgType::PushGlobalSearchProgress, [searchID, asked, total, running] {
        IpcMessage msg(IpcMsgType::PushGlobalSearchProgress, 0);
        msg.append(static_cast<qint64>(searchID));
        msg.append(static_cast<qint64>(asked));
        msg.append(static_cast<qint64>(total));
        msg.append(running);
        return msg;
    }, kPushWindowMs, searchID);
}

void CoreNotifierBridge::onSharedFileChanged(const QByteArray& fileHash)
{
    // Nobody to tell: a client that connects later starts from the full list.
    if (fileHash.size() != 16 || m_ipcServer->clientCount() == 0)
        return;
    m_dirtySharedFiles.insert(MD4Key(reinterpret_cast<const uint8*>(fileHash.constData())));
    if (!m_sharedFlushTimer.isActive())
        m_sharedFlushTimer.start();
}

void CoreNotifierBridge::onSharedFileRemoved(KnownFile* file)
{
    if (!file)
        return;
    m_dirtySharedFiles.erase(MD4Key(file->fileHash()));
    // Not held back: an update for the same file that is sent later must find the
    // removal already delivered.
    IpcMessage msg(IpcMsgType::PushSharedFileRemoved, 0);
    msg.append(md4str(file->fileHash()));
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::flushSharedFiles()
{
    if (m_ipcServer->clientCount() == 0) {
        m_dirtySharedFiles.clear();
        return;
    }

    const SharedFileList::ShareRules rules = theApp.sharedFileList
        ? theApp.sharedFileList->shareRules() : SharedFileList::ShareRules{};
    // one walk of the upload queue for a big batch, one per file for a small one
    const bool bulk = m_dirtySharedFiles.size() > 8;
    const auto loads = bulk ? sharedFileLoads() : std::unordered_map<MD4Key, SharedFileLoad>{};

    QCborArray rows;
    auto it = m_dirtySharedFiles.begin();
    while (it != m_dirtySharedFiles.end() && rows.size() < kSharedFlushRows) {
        const MD4Key key = *it;
        it = m_dirtySharedFiles.erase(it);

        KnownFile* file = theApp.sharedFileList
            ? theApp.sharedFileList->getFileByID(key.data.data()) : nullptr;
        if (!file && theApp.downloadQueue) {
            // listed too: a part file with data that is not shared yet
            PartFile* part = theApp.downloadQueue->fileByID(key.data.data());
            if (part && static_cast<uint64>(part->completedSize()) > 0)
                file = part;
        }
        if (!file)
            continue;   // hashed but not shared, or gone since

        SharedFileLoad load;
        if (bulk) {
            if (const auto loadIt = loads.find(key); loadIt != loads.end())
                load = loadIt->second;
        } else {
            load = sharedFileLoad(key.data.data());
        }
        rows.append(sharedFileRow(file, rules, load));
    }

    if (!rows.isEmpty()) {
        IpcMessage msg(IpcMsgType::PushSharedFileUpdate, 0);
        msg.append(rows);
        m_ipcServer->broadcast(msg);
    }
    if (!m_dirtySharedFiles.empty())
        m_sharedFlushTimer.start();
}

void CoreNotifierBridge::onUploadChanged()
{
    m_pushes->post(IpcMsgType::PushUploadUpdate,
                   [] { return IpcMessage(IpcMsgType::PushUploadUpdate, 0); },
                   kPushWindowMs);
}

void CoreNotifierBridge::onKadStateChanged()
{
    m_pushes->post(IpcMsgType::PushKadUpdate, [] { return buildKadUpdate(); }, kPushWindowMs);
}

void CoreNotifierBridge::onKadSearchesChanged()
{
    m_pushes->post(IpcMsgType::PushKadSearchesChanged,
                   [] { return IpcMessage(IpcMsgType::PushKadSearchesChanged, 0); },
                   kPushWindowMs);
}

void CoreNotifierBridge::onDownloadSourcesChanged()
{
    // Fires per source added or removed, on every PartFile — the busiest push the
    // daemon produces. Reuses PushDownloadUpdate; the GUI already handles it.
    m_pushes->post(IpcMsgType::PushDownloadUpdate,
                   [] { return IpcMessage(IpcMsgType::PushDownloadUpdate, 0); },
                   kPushWindowMs);
}

void CoreNotifierBridge::onKnownClientsChanged()
{
    m_pushes->post(IpcMsgType::PushKnownClientsChanged,
                   [] { return IpcMessage(IpcMsgType::PushKnownClientsChanged, 0); },
                   kPushWindowMs);
}

void CoreNotifierBridge::onFriendListChanged()
{
    m_pushes->post(IpcMsgType::PushFriendListChanged,
                   [] { return IpcMessage(IpcMsgType::PushFriendListChanged, 0); },
                   kPushWindowMs);
}

IpcMessage CoreNotifierBridge::buildKadUpdate()
{
    IpcMessage msg(IpcMsgType::PushKadUpdate, 0);
    auto* kad = kad::Kademlia::instance();
    QCborMap info;
    const bool kadRunning   = kad && kad->isRunning();
    const bool kadConnected = kad && kad->isConnected();
    info.insert(QStringLiteral("running"),    kadRunning);
    info.insert(QStringLiteral("connected"),  kadConnected);
    info.insert(QStringLiteral("firewalled"), kad && kad->isFirewalled());
    info.insert(QStringLiteral("users"),  static_cast<qint64>(kad ? kad->getKademliaUsers() : 0));
    info.insert(QStringLiteral("files"),  static_cast<qint64>(kad ? kad->getKademliaFiles() : 0));
    if (kadConnected && kad) {
        info.insert(QStringLiteral("usersExperimental"),
                    static_cast<qint64>(kad->getKademliaUsers(true)));
        info.insert(QStringLiteral("udpFirewalled"),
                    kad::UDPFirewallTester::isFirewalledUDP(true));
        info.insert(QStringLiteral("udpVerified"),
                    kad::UDPFirewallTester::isVerified());
        auto* prefs = kad->getPrefs();
        if (prefs) {
            info.insert(QStringLiteral("ip"),
                        static_cast<qint64>(prefs->ipAddress()));
            info.insert(QStringLiteral("id"),
                        static_cast<qint64>(prefs->ipAddress()));
            info.insert(QStringLiteral("internPort"), prefs->internKadPort());
            info.insert(QStringLiteral("externPort"),
                        prefs->useExternKadPort()
                            ? prefs->externalKadPort() : 0);
        }
    }
    msg.append(info);
    return msg;
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

void CoreNotifierBridge::onCaptchaRequestReceived(const QString& fromUser,
                                                  const QImage& captchaImage)
{
    auto* client = qobject_cast<UpDownClient*>(sender());
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!client || !captchaImage.save(&buffer, "PNG"))
        return;

    IpcMessage msg(IpcMsgType::PushChatCaptcha, 0);
    msg.append(md4str(client->userHash()));
    msg.append(fromUser);
    msg.append(QString::fromLatin1(png.toBase64()));
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onCaptchaResultReceived(const QString& /*fromUser*/, bool solved)
{
    auto* client = qobject_cast<UpDownClient*>(sender());
    if (!client)
        return;
    IpcMessage msg(IpcMsgType::PushChatCaptchaResult, 0);
    msg.append(md4str(client->userHash()));
    msg.append(solved);
    m_ipcServer->broadcast(msg);
}

// These are transitions, not a latest value, so they go out uncoalesced — a swallowed
// "*** Connecting" leaves the chat window with a gap in the story.
void CoreNotifierBridge::onFriendConnectionProgress(Friend* f, ChatConnectProgress step)
{
    if (!f)
        return;
    IpcMessage msg(IpcMsgType::PushChatState, 0);
    msg.append(md4str(f->userHash().data()));
    msg.append(static_cast<qint64>(step));
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onFriendConnectingResult(Friend* f, bool success)
{
    onFriendConnectionProgress(f, success ? ChatConnectProgress::Connected
                                          : ChatConnectProgress::Failed);
}

void CoreNotifierBridge::connectClientChatSignal(UpDownClient* client)
{
    connect(client, &UpDownClient::chatMessageReceived,
            this, &CoreNotifierBridge::onChatMessageReceived);
    connect(client, &UpDownClient::captchaRequestReceived,
            this, &CoreNotifierBridge::onCaptchaRequestReceived);
    connect(client, &UpDownClient::captchaResultReceived,
            this, &CoreNotifierBridge::onCaptchaResultReceived);
}

void CoreNotifierBridge::connectClientSharedFilesSignal(UpDownClient* client)
{
    connect(client, &UpDownClient::sharedFileListReceived,
            this, &CoreNotifierBridge::onClientSharedFilesReceived);
    connect(client, &UpDownClient::previewAnswerReceived,
            this, &CoreNotifierBridge::onPreviewAnswerReceived);
}

void CoreNotifierBridge::onPreviewAnswerReceived(const std::array<uint8, 16>& fileHash,
                                                 const std::vector<QImage>& images)
{
    auto* client = qobject_cast<UpDownClient*>(sender());
    QCborArray frames;
    for (const QImage& image : images) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (image.save(&buffer, "PNG"))
            frames.append(QCborValue(png));
    }
    if (frames.isEmpty())
        logStatusWarning(QStringLiteral("No preview from %1")
                             .arg(client ? client->userName() : QString()));

    IpcMessage msg(IpcMsgType::PushSearchPreview, 0);
    msg.append(md4str(fileHash.data()));
    msg.append(client ? client->userName() : QString());
    msg.append(frames);
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onClientSharedFilesReceived(const QByteArray& userHash,
                                                      const QString& userName,
                                                      uint32 searchID)
{
    IpcMessage msg(IpcMsgType::PushClientSharedFiles, 0);
    msg.append(md4str(reinterpret_cast<const uint8*>(userHash.constData())));
    msg.append(userName);
    msg.append(static_cast<qint64>(searchID));
    m_ipcServer->broadcast(msg);
}

void CoreNotifierBridge::onPortMapStatusChanged(eMule::PortMapStatus status)
{
    // Push it, rather than only logging as the old UPnP handler did: the GUI
    // needs the state to show real forwarding status instead of the wizard's
    // 30-second guess.
    if (m_ipcServer == nullptr)
        return;

    m_pushes->post(IpcMsgType::PushPortMapStatus, [status] {
        IpcMessage msg(IpcMsgType::PushPortMapStatus, 0);
        QCborMap info;
        info.insert(QStringLiteral("status"), static_cast<int>(status));
        info.insert(QStringLiteral("statusText"), eMule::portMapStatusName(status));
        if (theApp.portMapper != nullptr) {
            info.insert(QStringLiteral("method"), static_cast<int>(theApp.portMapper->activeMethod()));
            info.insert(QStringLiteral("methodText"),
                        eMule::portMapMethodName(theApp.portMapper->activeMethod()));
            info.insert(QStringLiteral("externalAddress"),
                        theApp.portMapper->externalAddress().toString());
        }
        msg.append(info);
        return msg;
    }, kPushWindowMs);
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
