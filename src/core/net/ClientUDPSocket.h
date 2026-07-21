#pragma once

/// @file ClientUDPSocket.h
/// @brief Client-to-client UDP socket — replaces MFC CClientUDPSocket.
///
/// Uses QUdpSocket + ThrottledControlSocket for bandwidth-controlled
/// UDP communication with other eMule/Kademlia clients. Uses
/// EncryptedDatagramSocket for client encryption.

#include "net/Address.h"
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
    Endpoint destination;                   ///< Destination address + port.
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
    /// @param ip         Destination IP (host byte order).
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
    void reaskCallbackReceived(const Endpoint& sender,
                               const uint8* data, uint32 size);

    /// File reask ping received from client.
    void reaskFilePingReceived(const Endpoint& sender,
                               const uint8* data, uint32 size);

    /// Reask acknowledged (queue rank response).
    void reaskAckReceived(const Endpoint& sender,
                          const uint8* data, uint32 size);

    /// File not found response.
    void fileNotFoundReceived(const Endpoint& sender);

    /// Queue full response.
    void queueFullReceived(const Endpoint& sender);

    /// Direct callback request.
    void directCallbackReceived(const Endpoint& sender,
                                const uint8* data, uint32 size);

    /// Port test packet received.
    void portTestReceived(const Endpoint& sender);

    /// Kademlia packet received — forward to Kademlia engine.
    /// @param opcode  Kad opcode (first byte after protocol).
    /// @param data    Payload after opcode.
    /// @param size    Size of payload.
    /// @param sender  Sender address + port (IP in host byte order convention).
    /// @param validReceiverKey  True if the decrypted receiver key matched.
    /// @param receiverVerifyKey The receiver verify key from decryption (0 if plaintext).
    void kadPacketReceived(uint8 opcode, const uint8* data, uint32 size,
                           const Endpoint& sender,
                           bool validReceiverKey, uint32 receiverVerifyKey);

private slots:
    void onReadyRead();
    void flushSendQueue();

private:
    bool processPacket(const uint8* packet, uint32 size, uint8 opcode,
                       uint32 senderIP, uint16 senderPort);

    /// Receive-side dispatch stub for the reserved UDP protocol headers
    /// OP_UDPRESERVEDPROT1 (0xA3) / OP_UDPRESERVEDPROT2 (0xB2). No payload semantics
    /// defined yet — logs the packet and accounts its overhead instead of dropping
    /// it silently. @param protByte the reserved header byte that was received.
    bool processReservedProtPacket(uint8 protByte, const uint8* packet, uint32 size,
                                   uint8 opcode, uint32 senderIP, uint16 senderPort);

    QByteArray decompressKadPayload(const uint8* data, int len);

    void purgeExpiredPackets();

    struct PreparedDatagram {
        QByteArray data;
        Endpoint destination;
    };

    QUdpSocket m_socket;
    std::deque<UDPPack> m_controlQueue;
    std::deque<PreparedDatagram> m_sendReadyQueue;
    mutable std::mutex m_sendLock;
    uint16 m_port = 0;
    bool m_wouldBlock = false;

    QElapsedTimer m_elapsedTimer;
};

} // namespace eMule
