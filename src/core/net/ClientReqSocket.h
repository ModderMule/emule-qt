#pragma once

/// @file ClientReqSocket.h
/// @brief Peer-to-peer TCP socket for client communication — replaces MFC CClientReqSocket.
///
/// Inherits EMSocket for ED2K packet framing. Handles all peer protocol
/// opcodes (OP_HELLO, OP_FILEREQUEST, OP_STARTUPLOADREQ, etc.) by
/// delegating to UpDownClient methods.

#include "net/EMSocket.h"

#include <QString>

namespace eMule {

class UpDownClient;

// ---------------------------------------------------------------------------
// Socket connection states
// ---------------------------------------------------------------------------

enum class PeerSocketState : uint8 {
    Other    = 0,   ///< Created or incoming unhandled.
    Half     = 1,   ///< Outgoing; awaiting OnConnect completion.
    Complete = 2    ///< Fully connected.
};

// ---------------------------------------------------------------------------
// ClientReqSocket
// ---------------------------------------------------------------------------

/// TCP socket for peer-to-peer ED2K client communication.
///
/// Each instance is associated with at most one UpDownClient. Handles
/// packet dispatch for the ED2K and eMule extended protocols.
class ClientReqSocket : public EMSocket {
    Q_OBJECT

public:
    explicit ClientReqSocket(UpDownClient* client = nullptr, QObject* parent = nullptr);
    ~ClientReqSocket() override;

    /// Associate a client with this socket.
    void setClient(UpDownClient* client);

    /// Get the associated client (may be nullptr).
    [[nodiscard]] UpDownClient* getClient() const { return m_client; }

    /// Gracefully disconnect and schedule deletion.
    void disconnect(const QString& reason);

    /// Mark that we're waiting for an outgoing connection to complete.
    void waitForOnConnect();

    /// Reset the idle timeout timer.
    void resetTimeOutTimer();

    /// Check if the socket has timed out.
    [[nodiscard]] bool checkTimeOut();

    /// Mark for safe deferred deletion.
    virtual void safeDelete();

    /// Create the underlying socket for outgoing connections.
    bool createSocket();

    /// Whether this socket is a port test connection.
    [[nodiscard]] bool isPortTestConnection() const { return m_portTestCon; }
    void setPortTestConnection(bool val) { m_portTestCon = val; }

    // Override for ThrottledFileSocket + tracking
    void sendPacket(std::unique_ptr<Packet> packet, bool controlPacket = true,
                    uint32 actualPayloadSize = 0, bool forceImmediateSend = false) override;

    /// Debug info about connected client.
    [[nodiscard]] QString debugClientInfo() const;

    /// Current peer socket state (for debug logging).
    [[nodiscard]] PeerSocketState peerSocketState() const { return m_socketState; }

signals:
    /// Socket disconnected (with reason).
    void clientDisconnected(const QString& reason);

    /// TCP connection completed (outgoing connect succeeded).
    void socketConnected();

    /// Hello packet received from peer.
    void helloReceived(const uint8* data, uint32 size, uint8 opcode);

    /// File request received from peer.
    void fileRequestReceived(const uint8* data, uint32 size, uint8 opcode);

    /// Upload request received from peer.
    void uploadRequestReceived(const uint8* data, uint32 size);

    /// Extended protocol packet received.
    void extPacketReceived(const uint8* data, uint32 size, uint8 opcode);

    /// Standard protocol packet that needs client dispatch.
    void packetForClient(const uint8* data, uint32 size, uint8 opcode, uint8 protocol);

protected:
    bool packetReceived(Packet* packet) override;
    void onError(int errorCode) override;
    void onEncryptionHandshakeComplete() override;

    /// Process ED2K standard protocol packet.
    bool processPacket(const uint8* packet, uint32 size, uint8 opcode);

    /// Process eMule extended protocol packet.
    bool processExtPacket(const uint8* packet, uint32 size, uint8 opcode);

    void setPeerSocketState(PeerSocketState val);

    UpDownClient* m_client = nullptr;
    PeerSocketState m_socketState = PeerSocketState::Other;
    uint32 m_timeoutTimer = 0;
    uint32 m_deleteTimer = 0;
    bool m_deleteThis = false;
    bool m_portTestCon = false;

private:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void emitSocketConnected();

    QElapsedTimer m_elapsedTimer;
};

} // namespace eMule
