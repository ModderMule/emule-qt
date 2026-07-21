#include "pch.h"
/// @file ServerConnect.cpp
/// @brief ED2K server connection state machine — replaces MFC CServerConnect.

#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "server/Server.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "prefs/Preferences.h"
#include "net/ServerSocket.h"
#include "net/UDPSocket.h"
#include "net/Packet.h"
#include "protocol/Tag.h"
#include "transfer/DownloadQueue.h"
#include "upnp/UPnPManager.h"
#include "utils/Log.h"

#include <QHostAddress>
#include <QNetworkInterface>




namespace eMule {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ServerConnect::ServerConnect(ServerList& serverList, QObject* parent)
    : QObject(parent)
    , m_serverList(serverList)
{
    m_elapsedTimer.start();

    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &ServerConnect::onRetryTimer);
}

ServerConnect::~ServerConnect()
{
    stopConnectionTry();

    destroySocket(m_connectedSocket);
    m_connectedSocket = nullptr;

    // UDPSocket is not owned by us — just clear the pointer
    m_udpSocket = nullptr;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void ServerConnect::setConfig(const ServerConnectConfig& config)
{
    m_config = config;
    m_maxSimCons = m_config.safeServerConnect ? 1 : 2;
}

// ---------------------------------------------------------------------------
// UDP socket
// ---------------------------------------------------------------------------

void ServerConnect::setUDPSocket(UDPSocket* socket)
{
    m_udpSocket = socket;
}

// ---------------------------------------------------------------------------
// TryAnotherConnectionRequest
// ---------------------------------------------------------------------------

void ServerConnect::tryAnotherConnectionRequest()
{
    if (static_cast<int>(m_connectionAttempts.size()) < m_maxSimCons) {
        // Try up to serverCount candidates to find one not already being connected to
        Server* next = nullptr;
        const size_t maxTries = m_serverList.serverCount();
        for (size_t attempt = 0; attempt < maxTries; ++attempt) {
            Server* candidate = m_serverList.nextServer(m_tryObfuscated);
            if (!candidate)
                break;

            bool alreadyConnecting = false;
            for (const auto& [ts, sock] : m_connectionAttempts) {
                const Server* cs = sock ? sock->currentServer() : nullptr;
                if (cs && cs->ipAddress() == candidate->ipAddress() && cs->port() == candidate->port()) {
                    alreadyConnecting = true;
                    break;
                }
            }
            if (!alreadyConnecting) {
                next = candidate;
                break;
            }
        }

        if (!next) {
            if (m_connectionAttempts.empty()) {
                if (m_tryObfuscated && !m_config.cryptLayerRequired) {
                    // Retry all servers on the non-obfuscated port
                    m_tryObfuscated = false;
                    connectToAnyServer(0, true, true, true);
                } else if (!m_retryTimer.isActive()) {
                    logInfo(QStringLiteral("Failed to connect to all servers listed. Making another pass."));
                    logInfo(QStringLiteral("Automatic connection to server will retry in %1 seconds")
                               .arg(kRetryConnectTimeSec));
                    m_startAutoConnectPos = 0;
                    m_retryTimer.start(kRetryConnectTimeSec * 1000);
                }
            }
        } else {
            // Only auto-connect to static servers if configured
            if (!m_config.autoConnectStaticOnly || next->isStaticMember())
                connectToServer(next, true, !m_tryObfuscated);
        }
    }
}

// ---------------------------------------------------------------------------
// ConnectToAnyServer
// ---------------------------------------------------------------------------

void ServerConnect::connectToAnyServer(size_t startAt, bool prioSort,
                                       bool isAuto, bool noCrypt)
{
    stopConnectionTry();
    disconnect();
    m_connecting = true;
    m_singleConnecting = false;
    emit stateChanged();

    m_tryObfuscated = m_config.cryptLayerPreferred && !noCrypt;

    // If only connecting to static servers, verify at least one exists
    if (m_config.autoConnectStaticOnly && isAuto) {
        bool anyStatic = false;
        const size_t count = m_serverList.serverCount();
        for (size_t i = 0; i < count; ++i) {
            if (m_serverList.serverAt(i)->isStaticMember()) {
                anyStatic = true;
                break;
            }
        }
        if (!anyStatic) {
            m_connecting = false;
            emit stateChanged();
            logError(QStringLiteral("No valid servers found (static-only mode)."));
            return;
        }
    }

    m_serverList.setServerPosition(startAt);
    // #24: the persistent m_servers order already reflects the user's manual
    // arrangement (applied via the SetServerOrder IPC → ServerList::applyUserOrder
    // when useUserSortedServerList is enabled in the GUI). sortByPreference() below
    // is a *stable* sort, so that arrangement is preserved within each priority
    // tier — the daemon/GUI-split equivalent of MFC's "GetUserSortedServers() then
    // Sort()". No separate re-fetch step is needed here.
    if (m_config.useServerPriorities && prioSort)
        m_serverList.sortByPreference();

    if (m_serverList.serverCount() == 0) {
        m_connecting = false;
        emit stateChanged();
        logError(QStringLiteral("No valid servers found."));
    } else {
        tryAnotherConnectionRequest();
    }
}

// ---------------------------------------------------------------------------
// ConnectToServer
// ---------------------------------------------------------------------------

void ServerConnect::connectToServer(Server* server, bool multiconnect, bool noCrypt)
{
    if (!server)
        return;

    if (!multiconnect) {
        stopConnectionTry();
        disconnect();
    }

    // Reset triedCrypt so both encrypted and fallback attempts are tried each cycle
    if (!noCrypt)
        server->setTriedCrypt(false);

    m_connecting = true;
    m_singleConnecting = !multiconnect;
    emit stateChanged();

    auto* socket = new ServerSocket(/*!multiconnect*/ m_singleConnecting, this);
    m_openSockets.push_back(socket);

    // Connect signals from this socket using lambda captures for sender identification
    connect(socket, &ServerSocket::connectionStateChanged, this,
            [this, socket](ServerConnState state) {
                onConnectionStateChanged(socket, state);
            });

    connect(socket, &ServerSocket::connectionFailed, this,
            [this, socket](ServerConnState reason) {
                onConnectionFailed(socket, reason);
            });

    // Parse server messages for [emDynIP], "server version X.Y" and ERROR/WARNING
    // before display (#17), then re-emit the surviving text for the GUI.
    connect(socket, &ServerSocket::serverMessage, this,
            [this, socket](const QString& msg) { onServerMessage(socket, msg); });

    connect(socket, &ServerSocket::loginReceived, this,
            [this, socket](uint32 clientID, uint32 tcpFlags, uint32 serverReportedIP) {
                onLoginReceived(socket, clientID, tcpFlags, serverReportedIP);
            });

    connect(socket, &ServerSocket::foundSourcesReceived, this,
            [](const uint8* data, uint32 size, bool obfuscated) {
                if (theApp.downloadQueue)
                    theApp.downloadQueue->addServerSourceResult(data, size, obfuscated);
            });

    // Servers a connected server advertises (OP_SERVERLIST). Gated on the pref,
    // matching MFC (CServerSocket::ProcessPacket, GetAddServersFromServer()).
    connect(socket, &ServerSocket::serverListReceived, this,
            [](const uint8* data, uint32 size) {
                if (theApp.serverList && thePrefs.addServersFromServer())
                    theApp.serverList->addServersFromPacket(data, size);
            });

    // Learned server metadata (name/description/version — #15, user/file counts —
    // #16) reaches only the socket's throwaway copy; wire it to the persistent list
    // entry. MFC: CServerSocket::ProcessPacket() — ServerSocket.cpp:390-397,463-470.
    connect(socket, &ServerSocket::serverIdentReceived, this,
            [this, socket](const uint8* serverHash, uint32 /*ip*/, uint16 /*port*/,
                           const QString& name, const QString& description) {
                onServerIdent(socket, serverHash, name, description);
            });

    connect(socket, &ServerSocket::serverStatusReceived, this,
            [this, socket](uint32 users, uint32 files) {
                onServerStatus(socket, users, files);
            });

    // A dynIP server that resolves leaves stale duplicate entries sharing its
    // address; collapse them (#18). MFC: CServerList::RemoveDuplicatesByAddress().
    connect(socket, &ServerSocket::dynIPResolved, this,
            [this, socket](uint32 /*ip*/, const QString& /*hostname*/) {
                if (!theApp.serverList)
                    return;
                if (Server* entry = resolveListEntry(socket))
                    theApp.serverList->removeDuplicatesByAddress(entry);
            });

    connect(socket, &ServerSocket::searchResultReceived,
            this,   &ServerConnect::searchResultReceived);

    connect(socket, &ServerSocket::callbackRequested, this,
            [](uint32 clientIP, uint16 clientPort, const uint8* cryptData, uint32 cryptSize) {
                if (!theApp.clientList)
                    return;
                if (theApp.clientList->isBannedClient(Address::fromNetworkOrder(clientIP)))
                    return;

                auto* client = theApp.clientList->findByConnIP(clientIP, clientPort);
                if (!client) {
                    client = new UpDownClient(clientPort, 0, clientIP, 0, nullptr);
                    theApp.clientList->addClient(client);
                }

                // Apply crypt options if present (options[1] + hash[16] = 17 bytes)
                if (cryptData && cryptSize >= 17) {
                    uint8 byCryptOptions = cryptData[0];
                    const uint8* userHash = cryptData + 1;
                    if (client->hasValidHash()) {
                        if (md4equ(client->userHash(), userHash)) {
                            client->setConnectOptions(byCryptOptions, true, false);
                        } else {
                            // Userhash mismatch — the real hash is unknown, so drop
                            // all crypt-layer flags defensively (options=0, no
                            // encryption). MFC: CServerSocket — ServerSocket.cpp:553-556.
                            client->setConnectOptions(0, false, false);
                        }
                    } else {
                        client->setUserHash(userHash);
                        client->setConnectOptions(byCryptOptions, true, false);
                    }
                }

                client->tryToConnect();
            });

    socket->initProxySupport(thePrefs.proxySettings());
    socket->connectTo(*server, noCrypt);

    qint64 timestamp = m_elapsedTimer.elapsed();
    m_connectionAttempts[timestamp] = socket;
}

// ---------------------------------------------------------------------------
// StopConnectionTry
// ---------------------------------------------------------------------------

void ServerConnect::stopConnectionTry()
{
    m_connectionAttempts.clear();
    m_connecting = false;
    m_singleConnecting = false;

    if (m_retryTimer.isActive())
        m_retryTimer.stop();

    // Close all sockets except the connected one and those already being deleted
    auto socketsCopy = m_openSockets; // copy — destroySocket modifies m_openSockets
    for (auto* sock : socketsCopy) {
        if (sock != m_connectedSocket)
            destroySocket(sock);
    }

    emit stateChanged();
}

// ---------------------------------------------------------------------------
// Connection state change handler
// ---------------------------------------------------------------------------

void ServerConnect::onConnectionStateChanged(ServerSocket* socket, ServerConnState newState)
{
    if (newState == ServerConnState::WaitForLogin ||
        newState == ServerConnState::Connected) {
        connectionEstablished(socket);
    }
}

void ServerConnect::onConnectionFailed(ServerSocket* socket, ServerConnState /*reason*/)
{
    connectionFailed(socket);
}

// ---------------------------------------------------------------------------
// ConnectionEstablished
// ---------------------------------------------------------------------------

void ServerConnect::connectionEstablished(ServerSocket* sender)
{
    if (!m_connecting) {
        // Already connected to another server
        destroySocket(sender);
        return;
    }

    initLocalIP();

    if (sender->connectionState() == ServerConnState::WaitForLogin) {
        // TCP connected, send login request
        const Server* cserver = sender->currentServer();
        if (cserver) {
            logInfo(QStringLiteral("Connected to %1 (%2:%3), sending login request (obfuscating=%4 encReady=%5)")
                       .arg(cserver->name())
                       .arg(cserver->address())
                       .arg(cserver->port())
                       .arg(sender->isObfuscating())
                       .arg(sender->isEncryptionLayerReady()));

            // Reset failed count on the server list copy
            Server* listServer = m_serverList.findByAddress(cserver->address(), cserver->port());
            if (listServer)
                listServer->resetFailedCount();
        }

        sendLoginPacket(sender);

    } else if (sender->connectionState() == ServerConnState::Connected) {
        // Login successful — we are now connected
        m_connected = true;
        m_connectedSocket = sender;

        const Server* cserver = sender->currentServer();
        if (cserver) {
            logInfo(QStringLiteral("Connected to %1 (%2:%3)")
                       .arg(cserver->name())
                       .arg(cserver->address())
                       .arg(cserver->port()));
        }

        // Stop other connection attempts now that we're connected
        stopConnectionTry();

        // Request server list from connected server if configured
        if (m_config.addServersFromServer) {
            auto pkt = std::make_unique<Packet>(OP_GETSERVERLIST, 0);
            sendPacket(std::move(pkt));
        }

        // Update obfuscation info on the server list entry
        if (cserver) {
            Server* listServer = m_serverList.findByAddress(cserver->address(), cserver->port());
            if (listServer && cserver->supportsObfuscationTCP()) {
                listServer->setTCPFlags(cserver->tcpFlags() | SrvTcpFlag::TcpObfuscation);
                listServer->setObfuscationPortTCP(cserver->obfuscationPortTCP());
                if (!listServer->supportsObfuscationUDP())
                    listServer->setObfuscationPortUDP(cserver->obfuscationPortUDP());
            }
        }

        emit stateChanged();
        emit connectedToServer(cserver ? m_serverList.findByAddress(cserver->address(), cserver->port()) : nullptr);
    }
}

// ---------------------------------------------------------------------------
// ConnectionFailed
// ---------------------------------------------------------------------------

void ServerConnect::connectionFailed(ServerSocket* sender)
{
    if (!m_connecting && sender != m_connectedSocket)
        return;

    const Server* cserver = sender->currentServer();
    Server* listServer = cserver
        ? m_serverList.findByAddress(cserver->address(), cserver->port())
        : nullptr;

    switch (sender->connectionState()) {
    case ServerConnState::FatalError:
        logError(QStringLiteral("Fatal connection error"));
        break;

    case ServerConnState::Disconnected:
        if (cserver) {
            logInfo(QStringLiteral("Lost connection to %1 (%2:%3)")
                        .arg(cserver->name())
                        .arg(cserver->address())
                        .arg(cserver->port()));
        }
        break;

    case ServerConnState::ServerDead:
        if (cserver) {
            logInfo(QStringLiteral("Server %1 (%2:%3) is dead (obfuscating=%4 encReady=%5)")
                        .arg(cserver->name())
                        .arg(cserver->address())
                        .arg(cserver->port())
                        .arg(sender->isObfuscating())
                        .arg(sender->isEncryptionLayerReady()));
        }
        if (listServer) {
            listServer->incFailedCount();
            if (thePrefs.deadServerRetries() > 0
                && listServer->failedCount() >= thePrefs.deadServerRetries()) {
                logInfo(QStringLiteral("Removing dead server %1 (failed %2 times)")
                            .arg(listServer->name()).arg(listServer->failedCount()));
                m_serverList.removeServer(listServer);
                listServer = nullptr;
            }
        }
        break;

    case ServerConnState::Error:
        break;

    case ServerConnState::ServerFull:
        if (cserver) {
            logInfo(QStringLiteral("Server %1 (%2:%3) is full")
                        .arg(cserver->name())
                        .arg(cserver->address())
                        .arg(cserver->port()));
        }
        break;

    default:
        break;
    }

    // Handle the failure based on state
    switch (sender->connectionState()) {
    case ServerConnState::FatalError: {
        bool autoretry = m_connecting && !m_singleConnecting;
        stopConnectionTry();
        if (m_config.reconnectOnDisconnect && autoretry && !m_retryTimer.isActive()) {
            logInfo(QStringLiteral("Automatic connection to server will retry in %1 seconds")
                       .arg(kRetryConnectTimeSec));

            m_startAutoConnectPos = 0;
            if (listServer) {
                // Start from the next server to avoid getting stuck
                for (size_t i = 0; i < m_serverList.serverCount(); ++i) {
                    if (m_serverList.serverAt(i) == listServer) {
                        m_startAutoConnectPos = (i + 1) % m_serverList.serverCount();
                        break;
                    }
                }
            }
            m_retryTimer.start(kRetryConnectTimeSec * 1000);
        }
        break;
    }

    case ServerConnState::Disconnected:
        m_connected = false;
        clearServerIdentity();
        if (m_connectedSocket) {
            m_connectedSocket = nullptr;
        }
        emit disconnectedFromServer();

        if (m_config.reconnectOnDisconnect && !m_connecting)
            connectToAnyServer();
        break;

    case ServerConnState::Error:
    case ServerConnState::NotConnected:
    case ServerConnState::ServerDead:
    case ServerConnState::ServerFull:
        if (!m_connecting)
            break;

        if (m_singleConnecting) {
            // For single-connect, try without obfuscation before giving up
            if (listServer && !m_config.cryptLayerRequired
                && listServer->supportsObfuscationTCP() && !listServer->triedCrypt()) {
                // This was a crypt connection attempt — retry without encryption
                listServer->setTriedCrypt(true);
                connectToServer(listServer, false, true /*noCrypt*/);
                break;
            }
            stopConnectionTry();
            break;
        }

        // Remove this socket from connection attempts
        for (auto it = m_connectionAttempts.begin(); it != m_connectionAttempts.end(); ++it) {
            if (it->second == sender) {
                m_connectionAttempts.erase(it);
                break;
            }
        }
        tryAnotherConnectionRequest();
        break;

    default:
        break;
    }

    // Clean up the failed socket
    destroySocket(sender);
    emit stateChanged();
}

// ---------------------------------------------------------------------------
// SendPacket
// ---------------------------------------------------------------------------

bool ServerConnect::sendPacket(std::unique_ptr<Packet> packet, ServerSocket* to)
{
    if (!to) {
        if (!m_connected || !m_connectedSocket) {
            return false;
        }
        m_connectedSocket->sendPacket(std::move(packet), true);
    } else {
        to->sendPacket(std::move(packet), true);
    }
    return true;
}

// ---------------------------------------------------------------------------
// SendUDPPacket
// ---------------------------------------------------------------------------

bool ServerConnect::sendUDPPacket(std::unique_ptr<Packet> packet, const Server& host,
                                  uint16 specialPort)
{
    // MFC: CServerConnect::SendUDPPacket — ServerConnect.cpp:270 gates on
    // theApp.IsConnected() (ED2K *or* Kad), not on our own ED2K connection.
    // Server stat pings and global UDP search keep working in Kad-only mode.
    if (theApp.isConnected() && m_udpSocket)
        m_udpSocket->sendPacket(std::move(packet), host, specialPort);
    return true;
}

bool ServerConnect::sendRawUDPPacket(const Server& host, uint16 specialPort,
                                     const uint8* data, uint32 size)
{
    // Same connectivity gate as sendUDPPacket (ED2K or Kad).
    if (theApp.isConnected() && m_udpSocket)
        m_udpSocket->sendRawPacket(host, specialPort, data, size);
    return true;
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------

bool ServerConnect::disconnect()
{
    if (m_connected && m_connectedSocket) {
        m_connected = false;
        clearServerIdentity();

        destroySocket(m_connectedSocket);
        m_connectedSocket = nullptr;

        emit stateChanged();
        emit disconnectedFromServer();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// RetryTimer
// ---------------------------------------------------------------------------

void ServerConnect::onRetryTimer()
{
    stopConnectionTry();
    if (isConnected())
        return;
    if (m_startAutoConnectPos >= m_serverList.serverCount())
        m_startAutoConnectPos = 0;
    connectToAnyServer(m_startAutoConnectPos, true, true);
}

// ---------------------------------------------------------------------------
// CheckForTimeout
// ---------------------------------------------------------------------------

void ServerConnect::checkForTimeout()
{
    // #22: proactively top up connection-attempt slots while auto-connecting, so a
    // slot freed without a failure/timeout event is refilled promptly. MFC calls
    // this ~every second from CUploadQueue::Process — UploadQueue.cpp:921.
    if (m_connecting && !m_singleConnecting)
        tryAnotherConnectionRequest();

    // #20: behind a proxy the connect handshake is slower — extend the per-attempt
    // timeout to CONNECTION_TIMEOUT. MFC: CServerConnect::ConnectToServer() —
    // ServerConnect.cpp:406-409.
    uint32 timeout = m_config.connectionTimeout;
    if (thePrefs.proxySettings().useProxy)
        timeout = std::max<uint32>(timeout, CONNECTION_TIMEOUT);

    const qint64 curTick = m_elapsedTimer.elapsed();

    // Iterate over a copy because we may modify m_connectionAttempts
    auto attemptsCopy = m_connectionAttempts;
    for (const auto& [startTime, socket] : attemptsCopy) {
        if (!socket) {
            m_connectionAttempts.erase(startTime);
            continue;
        }

        if (curTick >= startTime + timeout) {
            const Server* cserver = socket->currentServer();
            if (cserver) {
                logInfo(QStringLiteral("Connection attempt timed out: %1 (%2:%3)")
                           .arg(cserver->name())
                           .arg(cserver->address())
                           .arg(cserver->port()));
            }

            m_connectionAttempts.erase(startTime);
            destroySocket(socket);

            if (m_singleConnecting)
                stopConnectionTry();
            else
                tryAnotherConnectionRequest();
        }
    }
}

// ---------------------------------------------------------------------------
// KeepConnectionAlive
// ---------------------------------------------------------------------------

void ServerConnect::keepConnectionAlive()
{
    if (m_config.serverKeepAliveTimeout == 0)
        return;

    if (!m_connected || !m_connectedSocket)
        return;

    const qint64 elapsed = m_elapsedTimer.elapsed();
    const qint64 lastTx = m_connectedSocket->lastTransmission();

    if (elapsed >= lastTx + m_config.serverKeepAliveTimeout) {
        // "Ping" the server with an empty publish files packet
        SafeMemFile files;
        files.writeUInt32(0); // nr. of files
        auto packet = std::make_unique<Packet>(files);
        packet->opcode = OP_OFFERFILES;

        logDebug(QStringLiteral("Refreshing server connection (keep-alive)"));
        m_connectedSocket->sendPacket(std::move(packet));
    }
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

bool ServerConnect::isLowID() const
{
    return eMule::isLowID(m_clientID);
}

void ServerConnect::setClientID(uint32 newid)
{
    m_clientID = newid;

    // MFC: CServerConnect::SetClientID() — sockets.cpp:562. A HighID *is* our
    // public IP: the server only issues one after routing a callback to it.
    if (!eMule::isLowID(newid))
        theApp.setPublicIP(newid);

    emit clientIDChanged(newid);
    emit stateChanged();
}

Server* ServerConnect::currentServer() const
{
    if (m_connected && m_connectedSocket)
        return m_connectedSocket->currentServer();
    return nullptr;
}

bool ServerConnect::isLocalServer(uint32 ip, uint16 port) const
{
    if (!m_connected || !m_connectedSocket || !m_connectedSocket->currentServer())
        return false;
    return m_connectedSocket->currentServer()->ipAddress().toNetworkUint32() == ip &&
           m_connectedSocket->currentServer()->port() == port;
}

bool ServerConnect::awaitingTestFromIP(uint32 ip) const
{
    for (const auto& [timestamp, socket] : m_connectionAttempts) {
        if (socket && socket->currentServer() &&
            socket->currentServer()->ipAddress().toNetworkUint32() == ip &&
            socket->connectionState() == ServerConnState::WaitForLogin) {
            return true;
        }
    }
    return false;
}

bool ServerConnect::isConnectedObfuscated() const
{
    return m_connectedSocket != nullptr && m_connectedSocket->isObfuscating();
}

// ---------------------------------------------------------------------------
// DestroySocket
// ---------------------------------------------------------------------------

void ServerConnect::destroySocket(ServerSocket* socket)
{
    if (!socket)
        return;

    // Remove from open sockets list
    auto it = std::find(m_openSockets.begin(), m_openSockets.end(), socket);
    if (it != m_openSockets.end())
        m_openSockets.erase(it);

    // Remove from connection attempts
    for (auto atIt = m_connectionAttempts.begin(); atIt != m_connectionAttempts.end(); ++atIt) {
        if (atIt->second == socket) {
            m_connectionAttempts.erase(atIt);
            break;
        }
    }

    // Disconnect all signals from this socket
    socket->disconnect(this);

    // Close and schedule deletion
    socket->close();
    socket->deleteLater();
}

// ---------------------------------------------------------------------------
// InitLocalIP
// ---------------------------------------------------------------------------

void ServerConnect::initLocalIP()
{
    // #25: MFC derives the local IP from getaddrinfo(gethostname()) (the OS "primary"
    // address). The port keeps the Qt approach below (first non-loopback IPv4) on
    // purpose: it is non-blocking and idiomatic, and getaddrinfo(gethostname()) often
    // returns loopback/.local on macOS. On multi-homed hosts the two can pick
    // different addresses — an accepted, low-impact divergence.
    m_localIP = 0;

    // Use bind address if configured
    if (!m_config.bindAddress.isEmpty()) {
        QHostAddress bindAddr(m_config.bindAddress);
        if (!bindAddr.isNull() && bindAddr.protocol() == QAbstractSocket::IPv4Protocol) {
            m_localIP = htonl(bindAddr.toIPv4Address());
            return;
        }
    }

    // Fall back to first non-loopback IPv4 address
    const auto addresses = QNetworkInterface::allAddresses();
    for (const auto& addr : addresses) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback()) {
            m_localIP = htonl(addr.toIPv4Address());
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Login packet construction
// ---------------------------------------------------------------------------

void ServerConnect::sendLoginPacket(ServerSocket* socket)
{
    SafeMemFile data;

    // User hash
    data.writeHash16(m_config.userHash.data());

    // Client ID
    data.writeUInt32(m_clientID);

    // Listening port
    data.writeUInt16(m_config.listenPort);

    // Tag count = 4
    data.writeUInt32(4);

    // Tag: CT_NAME — user nick
    // Use old-format tags (writeTagToFile) in the login packet. The server hasn't
    // seen our SRVCAP_NEWTAGS flag yet when it begins parsing these tags, so it
    // expects the classic tag format. New-format tags would cause a parse failure
    // and immediate disconnection on real servers.
    Tag tagName(static_cast<uint8>(CT_NAME), m_config.userNick);
    tagName.writeTagToFile(data);

    // Tag: CT_VERSION — ED2K version
    Tag tagVersion(static_cast<uint8>(CT_VERSION), static_cast<uint32>(EDONKEYVERSION));
    tagVersion.writeTagToFile(data);

    // Tag: CT_SERVER_FLAGS — our capabilities
    uint32 cryptFlags = 0;
    if (m_config.cryptLayerEnabled)
        cryptFlags |= SRVCAP_SUPPORTCRYPT;
    if (m_config.cryptLayerPreferred)
        cryptFlags |= SRVCAP_REQUESTCRYPT;
    if (m_config.cryptLayerRequired)
        cryptFlags |= SRVCAP_REQUIRECRYPT;

    uint32 srvCaps = SRVCAP_NEWTAGS | SRVCAP_LARGEFILES | SRVCAP_UNICODE | cryptFlags;
    srvCaps |= SRVCAP_ZLIB;
    Tag tagFlags(static_cast<uint8>(CT_SERVER_FLAGS), srvCaps);
    tagFlags.writeTagToFile(data);

    // Tag: CT_EMULE_VERSION
    Tag tagEmuleVer(static_cast<uint8>(CT_EMULE_VERSION), m_config.emuleVersionTag);
    tagEmuleVer.writeTagToFile(data);

    auto packet = std::make_unique<Packet>(data);
    packet->opcode = OP_LOGINREQUEST;

    logDebug(QStringLiteral(">>> Sending OP_LoginRequest"));
    // forceImmediateSend=true: write the login to the socket NOW rather than
    // deferring via QTimer::singleShot(0).  After a DH handshake the login
    // must reach the server in the same event-loop iteration, otherwise
    // servers with short timeouts close the connection before the deferred
    // send fires.
    socket->sendPacket(std::move(packet), true /*controlPacket*/, 0, true /*forceImmediateSend*/);
}

// ---------------------------------------------------------------------------
// Smart LowID check — onLoginReceived
// ---------------------------------------------------------------------------

void ServerConnect::onLoginReceived(ServerSocket* socket, uint32 clientID, uint32 tcpFlags,
                                    uint32 serverReportedIP)
{
    // #14: the socket applied the server's capability flags only to its throwaway
    // copy; push them (including the obfuscation bit the IDCHANGE handler OR-ed in
    // for an obfuscated connection) to the persistent list entry.
    applyServerFlags(socket, tcpFlags);

    // Smart-LowID retry (#21): if a server hands us a LowID but we already saw a
    // HighID from another server this run, disconnect and try the next one — up to
    // twice, re-mapping our UPnP ports once on the first retry. A LowID that triggers
    // a retry must NOT be committed as our client ID (a later server may give HighID).
    // MFC: CServerSocket::ProcessPacket() OP_IDCHANGE — ServerSocket.cpp:326-350.
    if (m_config.smartLowIdCheck) {
        if (!eMule::isLowID(clientID)) {
            m_smartIdState = 1;                       // saw a HighID this run
        } else if (m_smartIdState > 0) {
            const bool firstRetry = (m_smartIdState == 1);
            ++m_smartIdState;
            m_smartIdState = (m_smartIdState > 2) ? 0 : m_smartIdState;  // 1→2 retry, 2→0 retry+reset
            if (firstRetry && theApp.upnpManager)
                theApp.upnpManager->checkAndRefresh();  // one-shot UPnP re-map
            if (!m_singleConnecting) {
                logInfo(QStringLiteral("Smart LowID: got LowID from server, trying another"));
                destroySocket(socket);
                if (socket == m_connectedSocket) {
                    m_connectedSocket = nullptr;
                    m_connected = false;
                }
                m_connecting = true;
                tryAnotherConnectionRequest();
                return;                                // do NOT commit this LowID
            }
        }
    }

    // Commit the assigned ID (HighID, a LowID we are keeping, or a manual connect).
    setClientID(clientID);
    if (eMule::isLowID(clientID) && serverReportedIP != 0)
        theApp.setPublicIP(serverReportedIP);
}

void ServerConnect::clearServerIdentity()
{
    // MFC: CServerConnect::Disconnect() — sockets.cpp:506. Both the client ID and
    // the public IP were this server's claim about us; with it gone they are no
    // longer ours to assert. Kad still backs theApp.publicIP() if it is running,
    // which is what makes clearing safe here.
    setClientID(0);
    theApp.setPublicIP(0);
}

// ---------------------------------------------------------------------------
// Learned-metadata handlers (#14–#18) — push socket-copy state to the real entry
// ---------------------------------------------------------------------------

Server* ServerConnect::resolveListEntry(ServerSocket* socket)
{
    if (!socket || !theApp.serverList)
        return nullptr;
    Server* connected = socket->currentServer();   // the socket's throwaway copy
    if (!connected)
        return nullptr;
    // Prefer an exact IP+TCP-port match; fall back to the address string (which
    // handles a dynIP server whose numeric IP may have changed). MFC uses
    // GetServerByAddress() (a DN+port lookup) for the same purpose.
    Server* entry = theApp.serverList->findByIPTcp(
        connected->ipAddress().toNetworkUint32(), connected->port());
    if (!entry)
        entry = theApp.serverList->findByAddress(connected->address(), connected->port());
    return entry;
}

void ServerConnect::applyServerFlags(ServerSocket* socket, uint32 tcpFlags)
{
    Server* entry = resolveListEntry(socket);
    if (!entry)
        return;
    entry->setTCPFlags(tcpFlags);
    // On an obfuscated connection reset the tried-crypt marker and default the TCP
    // obfuscation port to the server's own port when unset. MFC: ServerSocket.cpp:296-301.
    if ((tcpFlags & SrvTcpFlag::TcpObfuscation) != 0) {
        entry->setTriedCrypt(false);
        if (entry->obfuscationPortTCP() == 0)
            entry->setObfuscationPortTCP(entry->port());
    }
    theApp.serverList->notifyServerUpdated(entry);
}

void ServerConnect::onServerIdent(ServerSocket* socket, const uint8* serverHash,
                                  const QString& name, const QString& description)
{
    Server* entry = resolveListEntry(socket);
    if (!entry)
        return;
    if (!name.isEmpty())
        entry->setName(name);
    entry->setDescription(description);
    // A hash of "****" (0x2A2A2A2A) marks an eFarm server. MFC: ServerSocket.cpp:463-470.
    if (serverHash != nullptr
        && serverHash[0] == 0x2A && serverHash[1] == 0x2A
        && serverHash[2] == 0x2A && serverHash[3] == 0x2A
        && !entry->version().startsWith(QLatin1String("eFarm"))) {
        entry->setVersion(QStringLiteral("eFarm ") + entry->version());
    }
    theApp.serverList->notifyServerUpdated(entry);
}

void ServerConnect::onServerStatus(ServerSocket* socket, uint32 users, uint32 files)
{
    Server* entry = resolveListEntry(socket);
    if (!entry)
        return;
    entry->setUsers(users);
    entry->setFiles(files);
    theApp.serverList->notifyServerUpdated(entry);
}

void ServerConnect::onServerMessage(ServerSocket* socket, const QString& message)
{
    // 16.40+ servers batch several lines into one OP_SERVERMESSAGE separated by
    // CRLF. Parse each for control markers before display. MFC: CServerSocket::
    // ProcessPacket() OP_SERVERMESSAGE — ServerSocket.cpp:176-231.
    const QStringList lines = message.split(QLatin1String("\r\n"), Qt::SkipEmptyParts);
    const QStringList& iter = lines.isEmpty() ? QStringList{message} : lines;

    QStringList display;
    for (const QString& line : iter) {
        // "[emDynIP: host]" — refresh the server's dynamic DN and collapse any
        // duplicate entries that now share it. Only accept a real DN, not an IP.
        const qsizetype dynStart = line.indexOf(QLatin1String("[emDynIP:"));
        if (dynStart >= 0) {
            const qsizetype dynEnd = line.indexOf(QLatin1Char(']'), dynStart);
            if (dynEnd > dynStart) {
                QString dn = line.mid(dynStart + 9, dynEnd - (dynStart + 9)).trimmed();
                const qsizetype colon = dn.indexOf(QLatin1Char(':'));
                if (colon >= 0)
                    dn = dn.left(colon);
                if (!dn.isEmpty() && QHostAddress(dn).isNull()) {
                    if (Server* entry = resolveListEntry(socket)) {
                        if (entry->dynIP() != dn) {
                            entry->setDynIP(dn);
                            theApp.serverList->removeDuplicatesByAddress(entry);
                            theApp.serverList->notifyServerUpdated(entry);
                        }
                    }
                }
            }
            continue;   // control marker — do not echo
        }

        // "server version X.Y" — record the version, and still show the line.
        const qsizetype verIdx = line.indexOf(QLatin1String("server version"), 0, Qt::CaseInsensitive);
        if (verIdx >= 0) {
            const QString ver = line.mid(verIdx + 14).trimmed();
            if (!ver.isEmpty()) {
                if (Server* entry = resolveListEntry(socket)) {
                    entry->setVersion(ver);
                    theApp.serverList->notifyServerUpdated(entry);
                }
            }
            display << line;
            continue;
        }

        // ERROR / WARNING — route to the proper log level and suppress the echo.
        if (line.startsWith(QLatin1String("ERROR"), Qt::CaseInsensitive)) {
            logError(QStringLiteral("Server message: %1").arg(line));
            continue;
        }
        if (line.startsWith(QLatin1String("WARNING"), Qt::CaseInsensitive)) {
            logWarning(QStringLiteral("Server message: %1").arg(line));
            continue;
        }

        display << line;
    }

    if (!display.isEmpty())
        emit serverMessageReceived(display.join(QLatin1String("\r\n")));
}

} // namespace eMule
