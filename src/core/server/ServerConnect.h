#pragma once

/// @file ServerConnect.h
/// @brief ED2K server connection state machine — modern C++23 replacement for MFC CServerConnect.
///
/// Manages simultaneous connection attempts to ED2K servers, auto-reconnect
/// logic, keep-alive pings, and login packet construction. Decoupled from
/// GUI and global state via Qt signals and a configuration struct.

#include "server/Server.h"
#include "net/ServerSocket.h"
#include "net/UDPSocket.h"
#include "net/Packet.h"
#include "utils/Types.h"
#include "utils/Opcodes.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace eMule {

class ServerList;

// ---------------------------------------------------------------------------
// Retry interval (seconds) — matching original CS_RETRYCONNECTTIME
// ---------------------------------------------------------------------------

inline constexpr int kRetryConnectTimeSec = 30;

// ---------------------------------------------------------------------------
// Configuration struct — replaces thePrefs coupling
// ---------------------------------------------------------------------------

/// Runtime configuration for ServerConnect, bridged from Preferences at the
/// application level.  Keeps the core class testable without a full prefs DB.
struct ServerConnectConfig {
    // -- Connection behaviour --
    bool safeServerConnect = true;       ///< Limit to 1 concurrent attempt.
    bool autoConnectStaticOnly = false;  ///< Only auto-connect to static servers.
    bool useServerPriorities = true;     ///< Sort by priority before connecting.
    bool reconnectOnDisconnect = true;   ///< Auto-reconnect on unexpected disconnect.
    bool addServersFromServer = false;   ///< Request server list after login.

    // -- Encryption --
    bool cryptLayerPreferred = false;    ///< Prefer obfuscated connections.
    bool cryptLayerRequired = false;     ///< Require obfuscation (reject plain).
    bool cryptLayerEnabled = false;      ///< Support encryption layer.

    // -- Keep-alive --
    uint32 serverKeepAliveTimeout = 0;   ///< Keep-alive interval (ms), 0 = disabled.

    // -- Login credentials --
    std::array<uint8, 16> userHash{};    ///< Our user hash for the login packet.
    QString userNick;                    ///< Display name sent to the server.
    uint16 listenPort = 4662;            ///< Our TCP listening port.

    // -- Version encoding --
    /// Pre-built CT_EMULE_VERSION tag value:
    ///   (versionMajor << 17) | (versionMinor << 10) | (versionUpdate << 7)
    uint32 emuleVersionTag = 0;

    // -- Network --
    QString bindAddress;                 ///< Explicit bind address (empty = auto).

    // -- Timeouts --
    uint32 connectionTimeout = CONSERVTIMEOUT; ///< Per-socket timeout (ms).
};

// ---------------------------------------------------------------------------
// ServerConnect
// ---------------------------------------------------------------------------

/// Manages the ED2K server connection lifecycle.
///
/// Creates ServerSocket instances for connection attempts, tracks them,
/// handles login on successful TCP handshake, retry on failure, and
/// periodic keep-alive pings.  All GUI feedback is via Qt signals.
class ServerConnect : public QObject {
    Q_OBJECT

public:
    explicit ServerConnect(ServerList& serverList, QObject* parent = nullptr);
    ~ServerConnect() override;

    ServerConnect(const ServerConnect&) = delete;
    ServerConnect& operator=(const ServerConnect&) = delete;

    // -- Configuration --------------------------------------------------------

    void setConfig(const ServerConnectConfig& config);
    [[nodiscard]] const ServerConnectConfig& config() const { return m_config; }

    // -- UDP socket -----------------------------------------------------------

    void setUDPSocket(UDPSocket* socket);
    [[nodiscard]] bool isUDPSocketAvailable() const { return m_udpSocket != nullptr; }

    // -- Connection management ------------------------------------------------

    /// Auto-connect to the best available server.
    void connectToAnyServer(size_t startAt = 0, bool prioSort = true,
                            bool isAuto = true, bool noCrypt = false);

    /// Connect to a specific server.
    void connectToServer(Server* server, bool multiconnect = false, bool noCrypt = false);

    /// Abort all pending connection attempts.
    void stopConnectionTry();

    /// Disconnect from the current server.
    bool disconnect();

    // -- Packet sending -------------------------------------------------------

    /// Send a TCP packet to the connected server (or a specific socket).
    bool sendPacket(std::unique_ptr<Packet> packet, ServerSocket* to = nullptr);

    /// Send a UDP packet to a server.
    bool sendUDPPacket(std::unique_ptr<Packet> packet, const Server& host,
                       uint16 specialPort = 0);

    // -- State queries --------------------------------------------------------

    [[nodiscard]] bool isConnecting() const    { return m_connecting; }
    [[nodiscard]] bool isConnected() const     { return m_connected; }
    [[nodiscard]] bool isSingleConnect() const { return m_singleConnecting; }
    [[nodiscard]] uint32 clientID() const      { return m_clientID; }
    [[nodiscard]] uint32 curUser() const       { return m_curUser; }

    [[nodiscard]] bool isLowID() const;
    void setClientID(uint32 newid);

    /// Get the server we are currently connected to, or nullptr.
    [[nodiscard]] Server* currentServer() const;

    [[nodiscard]] uint32 localIP() const { return m_localIP; }

    /// True if IP:port matches our currently connected server.
    [[nodiscard]] bool isLocalServer(uint32 ip, uint16 port) const;

    /// True if we are attempting a connection to a server with this IP.
    [[nodiscard]] bool awaitingTestFromIP(uint32 ip) const;

    /// True if the active connection uses obfuscation.
    [[nodiscard]] bool isConnectedObfuscated() const;

    // -- Periodic maintenance -------------------------------------------------

    /// Send keep-alive ping if the connection has been idle.
    void keepConnectionAlive();

    /// Check pending connection attempts for timeouts.
    void checkForTimeout();

signals:
    /// General state change (UI should refresh).
    void stateChanged();

    /// Successfully connected to a server.
    void connectedToServer(Server* server);

    /// Disconnected (either explicit or unexpected).
    void disconnectedFromServer();

    /// Client ID was assigned or changed.
    void clientIDChanged(uint32 newID);

    /// Forwarded server message.
    void serverMessageReceived(const QString& msg);

    /// An error or notable event occurred (for logging/UI).
    void logMessage(const QString& msg, uint32 flags);

private slots:
    void onRetryTimer();

private:
    // -- Internal connection handlers -----------------------------------------
    void onConnectionStateChanged(ServerSocket* socket, ServerConnState newState);
    void onConnectionFailed(ServerSocket* socket, ServerConnState reason);

    void connectionEstablished(ServerSocket* sender);
    void connectionFailed(ServerSocket* sender);

    void tryAnotherConnectionRequest();
    void destroySocket(ServerSocket* socket);
    void initLocalIP();

    // -- Login packet construction -------------------------------------------
    void sendLoginPacket(ServerSocket* socket);

    // -- Data -----------------------------------------------------------------
    ServerList& m_serverList;
    UDPSocket* m_udpSocket = nullptr;

    /// Connection attempts keyed by start time (ms from elapsed timer).
    std::map<qint64, ServerSocket*> m_connectionAttempts;

    /// All currently open sockets.
    std::vector<ServerSocket*> m_openSockets;

    /// The socket we are actively connected through.
    ServerSocket* m_connectedSocket = nullptr;

    QTimer m_retryTimer;
    QElapsedTimer m_elapsedTimer;

    size_t m_startAutoConnectPos = 0;
    uint32 m_clientID = 0;
    uint32 m_curUser = 0;
    uint32 m_localIP = 0;
    uint8 m_maxSimCons = 2;

    bool m_connecting = false;
    bool m_singleConnecting = false;
    bool m_connected = false;
    bool m_tryObfuscated = false;

    ServerConnectConfig m_config;
};

} // namespace eMule
