#pragma once

/// @file ClientUDPSocket.h
/// @brief Client-to-client UDP socket — replaces MFC CClientUDPSocket.
///
/// Uses QUdpSocket + ThrottledControlSocket for bandwidth-controlled
/// UDP communication with other eMule/Kademlia clients. Uses
/// EncryptedDatagramSocket for client encryption.

#include "net/Packet.h"
#include "net/ThrottledSocket.h"
#include "utils/Types.h"

#include <QElapsedTimer>
#include <QUdpSocket>

#include <array>
#include <deque>
#include <memory>
#include <mutex>

namespace eMule {

// ---------------------------------------------------------------------------
// Queued UDP packet for client communication
// ---------------------------------------------------------------------------

struct UDPPack {
    std::unique_ptr<Packet> packet;
    uint32 ip = 0;                          ///< Destination IP (network byte order).
    uint16 port = 0;                        ///< Destination port.
    uint32 queueTime = 0;                   ///< Tick count when queued.
    bool encrypt = false;                   ///< Use encryption.
    bool kad = false;                       ///< Kademlia packet.
    uint32 receiverVerifyKey = 0;           ///< Kademlia receiver verify key.
    std::array<uint8, 16> targetHash{};     ///< Target client hash or Kad ID.
};

// ---------------------------------------------------------------------------
// ClientUDPSocket
// ---------------------------------------------------------------------------

/// UDP socket for peer-to-peer client communication.
///
/// Handles OP_REASKFILEPING, OP_REASKACK, OP_FILENOTFOUND,
/// OP_QUEUEFULL, OP_REASKCALLBACKUDP, OP_DIRECTCALLBACKREQ,
/// OP_PORTTEST. Kademlia packets forwarded via signal.
class ClientUDPSocket : public QObject, public ThrottledControlSocket {
    Q_OBJECT

public:
    explicit ClientUDPSocket(QObject* parent = nullptr);
    ~ClientUDPSocket() override;

    ClientUDPSocket(const ClientUDPSocket&) = delete;
    ClientUDPSocket& operator=(const ClientUDPSocket&) = delete;

    /// Create and bind the UDP socket.
    bool create();

    /// Rebind to configured port.
    bool rebind(uint16 port);

    /// Get the bound port.
    [[nodiscard]] uint16 connectedPort() const { return m_port; }

    /// Send a packet to a peer. Takes ownership of packet.
    /// @param packet     Packet to send (can be nullptr for raw data).
    /// @param ip         Destination IP (network byte order).
    /// @param port       Destination port.
    /// @param encrypt    Use encryption.
    /// @param targetHash Target client hash or Kad ID (16 bytes), or nullptr.
    /// @param isKad      Kademlia packet.
    /// @param receiverVerifyKey Kad receiver verify key.
    bool sendPacket(std::unique_ptr<Packet> packet, uint32 ip, uint16 port,
                    bool encrypt, const uint8* targetHash, bool isKad,
                    uint32 receiverVerifyKey);

    /// ThrottledControlSocket: send queued data up to bandwidth limit.
    SocketSentBytes sendControlData(uint32 maxNumberOfBytesToSend, uint32 minFragSize) override;

signals:
    /// Reask callback received from firewalled client.
    void reaskCallbackReceived(uint32 senderIP, uint16 senderPort,
                               const uint8* data, uint32 size);

    /// File reask ping received from client.
    void reaskFilePingReceived(uint32 senderIP, uint16 senderPort,
                               const uint8* data, uint32 size);

    /// Reask acknowledged (queue rank response).
    void reaskAckReceived(uint32 senderIP, uint16 senderPort,
                          const uint8* data, uint32 size);

    /// File not found response.
    void fileNotFoundReceived(uint32 senderIP, uint16 senderPort);

    /// Queue full response.
    void queueFullReceived(uint32 senderIP, uint16 senderPort);

    /// Direct callback request.
    void directCallbackReceived(uint32 senderIP, uint16 senderPort,
                                const uint8* data, uint32 size);

    /// Port test packet received.
    void portTestReceived(uint32 senderIP, uint16 senderPort);

    /// Kademlia packet received — forward to Kademlia engine.
    void kadPacketReceived(uint8 protocol, uint8 opcode,
                           const uint8* data, uint32 size,
                           uint32 senderIP, uint16 senderPort);

private slots:
    void onReadyRead();

private:
    bool processPacket(const uint8* packet, uint32 size, uint8 opcode,
                       uint32 senderIP, uint16 senderPort);

    void purgeExpiredPackets();

    QUdpSocket m_socket;
    std::deque<UDPPack> m_controlQueue;
    mutable std::mutex m_sendLock;
    uint16 m_port = 0;
    bool m_wouldBlock = false;

    QElapsedTimer m_elapsedTimer;
};

} // namespace eMule
