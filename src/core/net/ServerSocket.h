#pragma once

/// @file ServerSocket.h
/// @brief TCP connection to an ED2K server — replaces MFC CServerSocket.
///
/// Inherits EMSocket for packet framing and encryption. Replaces tight
/// `friend class CServerConnect` coupling with Qt signals. When ServerConnect
/// is ported, it connects to these signals.

#include "net/EMSocket.h"

#include <QDnsLookup>

#include <memory>

namespace eMule {

class Server;

// ---------------------------------------------------------------------------
// Connection states (matching original CS_* values)
// ---------------------------------------------------------------------------

enum class ServerConnState : int {
    NotConnected = 0,
    Connecting   = 1,
    WaitForLogin = 2,
    Connected    = 3,
    ServerDead   = 4,
    FatalError   = 5,
    Disconnected = 6,
    ServerFull   = 7,
    Error        = 8
};

// ---------------------------------------------------------------------------
// ServerSocket
// ---------------------------------------------------------------------------

/// TCP connection to a single ED2K server.
///
/// Handles server protocol opcodes: OP_SERVERMESSAGE, OP_IDCHANGE,
/// OP_SEARCHRESULT, OP_FOUNDSOURCES, OP_SERVERSTATUS, OP_SERVERIDENT,
/// OP_SERVERLIST, OP_CALLBACKREQUESTED, OP_REJECT, etc.
///
/// Decoupled from ServerConnect via signals.
class ServerSocket : public EMSocket {
    Q_OBJECT

public:
    /// @param manualSingleConnect  True if connecting to a manually-selected single server.
    explicit ServerSocket(bool manualSingleConnect = false, QObject* parent = nullptr);
    ~ServerSocket() override;

    /// Initiate connection to a server. Takes a copy of the server data.
    /// @param server  Server to connect to.
    /// @param noCrypt Disable encryption for this connection attempt.
    void connectTo(const Server& server, bool noCrypt = false);

    /// Get the current connection state.
    [[nodiscard]] ServerConnState connectionState() const { return m_connectionState; }

    /// Timestamp of last packet transmission (ms from QElapsedTimer).
    [[nodiscard]] uint32 lastTransmission() const { return m_lastTransmission; }

    /// Whether this is a manually-initiated single-server connection.
    [[nodiscard]] bool isManualSingleConnect() const { return m_manualSingleConnect; }

    /// Get a copy of the connected server's data (may be null if not connected).
    [[nodiscard]] Server* currentServer() const { return m_curServer.get(); }

    // Override to track last transmission time
    void sendPacket(std::unique_ptr<Packet> packet, bool controlPacket = true,
                    uint32 actualPayloadSize = 0, bool forceImmediateSend = false) override;

signals:
    /// Connection state changed.
    void connectionStateChanged(eMule::ServerConnState newState);

    /// Server sent a text message.
    void serverMessage(const QString& message);

    /// Server assigned us an ID (login successful).
    /// @param clientID  Our assigned client ID (high or low).
    /// @param tcpFlags  Server capability flags.
    void loginReceived(uint32 clientID, uint32 tcpFlags);

    /// Search results received from server.
    /// @param data    Raw result data.
    /// @param size    Data size in bytes.
    /// @param moreResultsAvailable  Server has more results.
    void searchResultReceived(const uint8* data, uint32 size, bool moreResultsAvailable);

    /// File sources received from server.
    /// @param data  Raw source data.
    /// @param size  Data size.
    /// @param obfuscated  True if OP_FOUNDSOURCES_OBFU.
    void foundSourcesReceived(const uint8* data, uint32 size, bool obfuscated);

    /// Server status update (user/file counts).
    void serverStatusReceived(uint32 users, uint32 files);

    /// Server identification received (name, description, hash, flags).
    void serverIdentReceived(const uint8* serverHash, uint32 ip, uint16 port,
                             const QString& name, const QString& description);

    /// Server list received from server.
    void serverListReceived(const uint8* data, uint32 size);

    /// Callback requested by remote client.
    void callbackRequested(uint32 clientIP, uint16 clientPort,
                           const uint8* cryptOptions, uint32 cryptSize);

    /// Server sent reject.
    void rejectReceived();

    /// DNS resolution completed for a dynamic-IP server.
    void dynIPResolved(uint32 ip, const QString& hostname);

    /// Connection failed or broken.
    void connectionFailed(eMule::ServerConnState reason);

protected:
    bool packetReceived(Packet* packet) override;
    void onError(int errorCode) override;

private:
    bool processPacket(const uint8* packet, uint32 size, uint8 opcode);
    void setConnectionState(ServerConnState newState);

    // --- Slots ---
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onDnsLookupFinished();

    // --- State ---
    std::unique_ptr<Server> m_curServer;
    std::unique_ptr<QDnsLookup> m_dnsLookup;
    ServerConnState m_connectionState = ServerConnState::NotConnected;
    uint32 m_lastTransmission = 0;
    bool m_manualSingleConnect = false;
    bool m_startNewMessageLog = true;
    bool m_isDeleting = false;
    bool m_noCrypt = false;

    QElapsedTimer m_elapsedTimer;
};

} // namespace eMule
