#pragma once

/// @file ServerSocket.h
/// @brief TCP connection to an ED2K server — replaces MFC CServerSocket.
///
/// Inherits EMSocket for packet framing and encryption. Replaces tight
/// `friend class CServerConnect` coupling with Qt signals. When ServerConnect
/// is ported, it connects to these signals.

#include "net/EMSocket.h"
#include "net/Address.h"
#include "server/ServerFailure.h"

#include <QDnsLookup>

#include <memory>
#include <optional>

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
    /// @param dialAddress One of the server's addresses to dial (dual-stack family
    ///        fallback); null = the server's preferred-family address.
    void connectTo(const Server& server, bool noCrypt = false, const Address& dialAddress = {});

    /// The address this session dialed — decides the session family, which a
    /// dual-stack Server copy can no longer tell by itself.
    [[nodiscard]] const Address& sessionAddress() const { return m_sessionAddress; }

    /// True when this attempt is the dual-stack retry on the other family.
    [[nodiscard]] bool isFamilyFallback() const { return m_familyFallback; }

    /// True once the TCP connect succeeded (before any login).
    [[nodiscard]] bool tcpConnected() const { return m_tcpConnected; }

    /// Phase and reason of the failure that ended this attempt (unset until one did).
    [[nodiscard]] const ServerFailure& lastFailure() const { return m_lastFailure; }

    /// Get the current connection state.
    [[nodiscard]] ServerConnState connectionState() const { return m_connectionState; }

    /// getTickCount() of the last packet sent or received.
    [[nodiscard]] uint64 lastTransmission() const { return m_lastTransmission; }

    /// Whether this is a manually-initiated single-server connection.
    [[nodiscard]] bool isManualSingleConnect() const { return m_manualSingleConnect; }

    /// Smart-LowID bounce flag. During loginReceived handling ServerConnect calls
    /// requestLowIDBounce() to abandon a LowID and try another server; the
    /// OP_IDCHANGE handler then skips promotion to Connected, mirroring the
    /// reference's `break` (srchybrid CServerSocket::ProcessPacket:336-337).
    [[nodiscard]] bool lowIDBounced() const { return m_lowIDBounced; }
    void requestLowIDBounce() { m_lowIDBounced = true; }

    /// Get a copy of the connected server's data (may be null if not connected).
    [[nodiscard]] Server* currentServer() const { return m_curServer.get(); }

    /// True until the first server message of this connection has been displayed.
    /// ServerConnect uses it to emit the "Connection established on:" header into the
    /// Server Info pane exactly once per connection, mirroring the reference's
    /// m_bStartNewMessageLog (srchybrid CServerSocket::ProcessPacket:234-245).
    [[nodiscard]] bool startNewMessageLog() const { return m_startNewMessageLog; }
    void clearStartNewMessageLog() { m_startNewMessageLog = false; }

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
    /// @param serverReportedIP The IP the server sees us on, from the extended
    ///        OP_IDCHANGE answer; 0 when absent or when the server reported a
    ///        LowID there. Our only public-IP source on a LowID connection.
    void loginReceived(uint32 clientID, uint32 tcpFlags, uint32 serverReportedIP);

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

    /// IPv6 LowID callback (OP_CALLBACKREQUESTED_IPV6): the requester's public IPv6
    /// endpoint. We connect back to it over IPv6 (no crypt trailer in this variant).
    void callbackRequestedIPv6(const eMule::Endpoint& requester);

    /// Server sent reject.
    void rejectReceived();

    /// DNS resolution completed for a dynamic-IP server. Address-typed: an AAAA-only
    /// hostname resolves to an IPv6, which a uint32 cannot carry.
    void dynIPResolved(const eMule::Address& addr, const QString& hostname);

    /// OP_SERVERIDENT named the server's address of the other family: its IPv6
    /// (CT_MOD_SVR_IP_V6) over an IPv4 session, its IPv4 over an IPv6 session.
    void serverAddressLearned(const eMule::Address& addr);

    /// A dynIP server resolved to an IP-filtered address; the list entry must go.
    void dynIPFiltered(const eMule::Address& addr);

    /// Connection failed or broken.
    void connectionFailed(eMule::ServerConnState reason);

public:
    /// What a socket error in @p current means. Only a refusal, a timeout or a close
    /// by the server says anything about the server (ServerDead, which counts against
    /// it); a local or network failure is FatalError and counts against nobody.
    [[nodiscard]] static ServerConnState stateForSocketError(ServerConnState current,
                                                             QAbstractSocket::SocketError error);

    /// The error to judge a failed dial by. Some systems report a refused connect as a
    /// plain NetworkError whose text is the OS's "Connection refused"; taken at its
    /// code that reads as trouble on our side and never counts against the server.
    [[nodiscard]] static QAbstractSocket::SocketError refinedSocketError(
        QAbstractSocket::SocketError error, const QString& errorText, bool tcpConnected);

    /// Names a failure: @p socketError is empty for a protocol error or a plain close.
    [[nodiscard]] static ServerFailure failureFor(ServerConnState current, bool tcpConnected,
                                                  std::optional<QAbstractSocket::SocketError> socketError);

protected:
    bool packetReceived(Packet* packet) override;
    void onError(int errorCode) override;
    void onEncryptionHandshakeComplete() override;

private:
    bool processPacket(const uint8* packet, uint32 size, uint8 opcode);
    void setConnectionState(ServerConnState newState);
    /// Record the failure, then move to @p newState (which reports it).
    void failWith(ServerConnState newState, ServerFailure failure);

    // --- Slots ---
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onDnsLookupFinished();

    /// Start (or restart) the dynIP lookup with the given record type. Replaces any
    /// in-flight lookup safely, so it can be called from the finished handler.
    void startDnsLookup(QDnsLookup::Type type);

    // --- State ---
    std::unique_ptr<Server> m_curServer;
    Address m_sessionAddress;
    std::unique_ptr<QDnsLookup> m_dnsLookup;
    ServerConnState m_connectionState = ServerConnState::NotConnected;
    ServerFailure m_lastFailure;
    uint64 m_lastTransmission = 0;
    bool m_manualSingleConnect = false;
    bool m_startNewMessageLog = true;
    bool m_isDeleting = false;
    bool m_noCrypt = false;
    bool m_pendingLogin = false;
    bool m_lowIDBounced = false;
    bool m_dnsTriedFallback = false;   // the other-family retry has been used
    bool m_familyFallback = false;     // dual-stack retry on the other family
    bool m_tcpConnected = false;

};

} // namespace eMule
