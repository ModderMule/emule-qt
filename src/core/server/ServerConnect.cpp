/// @file ServerConnect.cpp
/// @brief ED2K server connection state machine — replaces MFC CServerConnect.

#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "server/Server.h"
#include "app/AppContext.h"
#include "net/ServerSocket.h"
#include "net/UDPSocket.h"
#include "net/Packet.h"
#include "protocol/Tag.h"
#include "transfer/DownloadQueue.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkInterface>

#include <algorithm>

#if __has_include(<zlib.h>)
#define HAVE_ZLIB 1
#else
#define HAVE_ZLIB 0
#endif

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
        Server* nextServer = m_serverList.nextServer(m_tryObfuscated);
        if (!nextServer) {
            if (m_connectionAttempts.empty()) {
                if (m_tryObfuscated && !m_config.cryptLayerRequired) {
                    // Retry all servers on the non-obfuscated port
                    m_tryObfuscated = false;
                    connectToAnyServer(0, true, true, true);
                } else if (!m_retryTimer.isActive()) {
                    emit logMessage(QStringLiteral("No more servers to try."),
                                    LogWarning | LogStatusBar);
                    emit logMessage(QStringLiteral("Retrying in %1 seconds...")
                                        .arg(kRetryConnectTimeSec),
                                    LogInfo);
                    m_startAutoConnectPos = 0;
                    m_retryTimer.start(kRetryConnectTimeSec * 1000);
                }
            }
        } else {
            // Only auto-connect to static servers if configured
            if (!m_config.autoConnectStaticOnly || nextServer->isStaticMember())
                connectToServer(nextServer, true, !m_tryObfuscated);
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
            emit logMessage(QStringLiteral("No valid servers found (static-only mode)."),
                            LogError | LogStatusBar);
            return;
        }
    }

    m_serverList.setServerPosition(startAt);
    if (m_config.useServerPriorities && prioSort)
        m_serverList.sortByPreference();

    if (m_serverList.serverCount() == 0) {
        m_connecting = false;
        emit stateChanged();
        emit logMessage(QStringLiteral("No valid servers found."),
                        LogError | LogStatusBar);
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

    connect(socket, &ServerSocket::serverMessage, this,
            &ServerConnect::serverMessageReceived);

    connect(socket, &ServerSocket::foundSourcesReceived, this,
            [](const uint8* data, uint32 size, bool obfuscated) {
                if (theApp.downloadQueue)
                    theApp.downloadQueue->addServerSourceResult(data, size, obfuscated);
            });

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
            emit logMessage(QStringLiteral("Connected to %1 (%2:%3), requesting login...")
                                .arg(cserver->name())
                                .arg(cserver->address())
                                .arg(cserver->port()),
                            LogInfo);

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
            emit logMessage(QStringLiteral("Connected to %1 (%2:%3)")
                                .arg(cserver->name())
                                .arg(cserver->address())
                                .arg(cserver->port()),
                            LogSuccess | LogStatusBar);
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
        emit logMessage(QStringLiteral("Fatal connection error"), LogError | LogStatusBar);
        break;

    case ServerConnState::Disconnected:
        if (cserver) {
            emit logMessage(QStringLiteral("Lost connection to %1 (%2:%3)")
                                .arg(cserver->name())
                                .arg(cserver->address())
                                .arg(cserver->port()),
                            LogError | LogStatusBar);
        }
        break;

    case ServerConnState::ServerDead:
        if (cserver) {
            emit logMessage(QStringLiteral("Server %1 (%2:%3) is dead")
                                .arg(cserver->name())
                                .arg(cserver->address())
                                .arg(cserver->port()),
                            LogError | LogStatusBar);
        }
        if (listServer)
            listServer->incFailedCount();
        break;

    case ServerConnState::Error:
        break;

    case ServerConnState::ServerFull:
        if (cserver) {
            emit logMessage(QStringLiteral("Server %1 (%2:%3) is full")
                                .arg(cserver->name())
                                .arg(cserver->address())
                                .arg(cserver->port()),
                            LogError | LogStatusBar);
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
            emit logMessage(QStringLiteral("Retrying in %1 seconds...")
                                .arg(kRetryConnectTimeSec), LogWarning);

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
    if (m_connected && m_udpSocket)
        m_udpSocket->sendPacket(std::move(packet), host, specialPort);
    return true;
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------

bool ServerConnect::disconnect()
{
    if (m_connected && m_connectedSocket) {
        m_connected = false;

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
    uint32 timeout = m_config.connectionTimeout;

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
                emit logMessage(QStringLiteral("Connection attempt timed out: %1 (%2:%3)")
                                    .arg(cserver->name())
                                    .arg(cserver->address())
                                    .arg(cserver->port()),
                                LogWarning);
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
    return m_connectedSocket->currentServer()->ip() == ip &&
           m_connectedSocket->currentServer()->port() == port;
}

bool ServerConnect::awaitingTestFromIP(uint32 ip) const
{
    for (const auto& [timestamp, socket] : m_connectionAttempts) {
        if (socket && socket->currentServer() &&
            socket->currentServer()->ip() == ip &&
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
    // Use writeNewEd2kTag (compact format) since we advertise SRVCAP_NEWTAGS.
    // Sending old-format tags while advertising new-tag support triggers anti-leecher
    // fingerprinting on some servers (incorrectly identified as Shareaza).
    Tag tagName(static_cast<uint8>(CT_NAME), m_config.userNick);
    tagName.writeNewEd2kTag(data, UTF8Mode::OptBOM);

    // Tag: CT_VERSION — ED2K version
    Tag tagVersion(static_cast<uint8>(CT_VERSION), static_cast<uint32>(EDONKEYVERSION));
    tagVersion.writeNewEd2kTag(data);

    // Tag: CT_SERVER_FLAGS — our capabilities
    uint32 cryptFlags = 0;
    if (m_config.cryptLayerEnabled)
        cryptFlags |= SRVCAP_SUPPORTCRYPT;
    if (m_config.cryptLayerPreferred)
        cryptFlags |= SRVCAP_REQUESTCRYPT;
    if (m_config.cryptLayerRequired)
        cryptFlags |= SRVCAP_REQUIRECRYPT;

    uint32 srvCaps = SRVCAP_NEWTAGS | SRVCAP_LARGEFILES | SRVCAP_UNICODE | cryptFlags;
#if HAVE_ZLIB
    srvCaps |= SRVCAP_ZLIB;
#endif
    Tag tagFlags(static_cast<uint8>(CT_SERVER_FLAGS), srvCaps);
    tagFlags.writeNewEd2kTag(data);

    // Tag: CT_EMULE_VERSION
    Tag tagEmuleVer(static_cast<uint8>(CT_EMULE_VERSION), m_config.emuleVersionTag);
    tagEmuleVer.writeNewEd2kTag(data);

    auto packet = std::make_unique<Packet>(data);
    packet->opcode = OP_LOGINREQUEST;

    logDebug(QStringLiteral(">>> Sending OP_LoginRequest"));
    sendPacket(std::move(packet), socket);
}

} // namespace eMule
