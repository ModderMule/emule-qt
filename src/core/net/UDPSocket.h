#pragma once

/// @file UDPSocket.h
/// @brief Server UDP communication socket — replaces MFC CUDPSocket.
///
/// Uses QUdpSocket + ThrottledControlSocket for bandwidth-controlled
/// UDP communication with ED2K servers. Replaces the CUDPSocketWnd DNS
/// helper window with QDnsLookup.

#include "net/Packet.h"
#include "net/ThrottledSocket.h"
#include "utils/Types.h"

#include <QDnsLookup>
#include <QElapsedTimer>
#include <QUdpSocket>

#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace eMule {

class Server;

// ---------------------------------------------------------------------------
// Queued UDP packet
// ---------------------------------------------------------------------------

struct ServerUDPPacket {
    std::vector<uint8> data;
    uint32 ip = 0;      ///< Destination IP (network byte order).
    uint16 port = 0;    ///< Destination port (host byte order).
};

// ---------------------------------------------------------------------------
// Pending DNS request for dynamic-IP servers
// ---------------------------------------------------------------------------

struct ServerDNSRequest {
    std::unique_ptr<QDnsLookup> lookup;
    uint32 createdTime = 0;
    std::vector<ServerUDPPacket> pendingPackets;
    uint32 serverIP = 0;
    uint16 serverPort = 0;
};

// ---------------------------------------------------------------------------
// UDPSocket
// ---------------------------------------------------------------------------

/// UDP socket for server communication (ping, search, sources).
///
/// Handles OP_GLOBSEARCHRES, OP_GLOBFOUNDSOURCES, OP_GLOBSERVSTATRES,
/// OP_SERVER_DESC_RES. Uses EncryptedDatagramSocket static methods for
/// server obfuscation when supported.
class UDPSocket : public QObject, public ThrottledControlSocket {
    Q_OBJECT

public:
    explicit UDPSocket(QObject* parent = nullptr);
    ~UDPSocket() override;

    UDPSocket(const UDPSocket&) = delete;
    UDPSocket& operator=(const UDPSocket&) = delete;

    /// Create and bind the UDP socket.
    bool create();

    /// Send a packet to a server. Takes ownership of packet.
    /// @param packet     Packet to send.
    /// @param server     Target server (for IP, port, encryption keys).
    /// @param specialPort Override port (0 = use server's UDP port).
    void sendPacket(std::unique_ptr<Packet> packet, const Server& server,
                    uint16 specialPort = 0);

    /// ThrottledControlSocket: send queued control data up to bandwidth limit.
    SocketSentBytes sendControlData(uint32 maxNumberOfBytesToSend, uint32 minFragSize) override;

signals:
    /// Global search results received from server.
    void globalSearchResult(const uint8* data, uint32 size, uint32 serverIP, uint16 serverPort);

    /// Global found sources received from server.
    void globalFoundSources(const uint8* data, uint32 size, uint32 serverIP, uint16 serverPort);

    /// Server status response received.
    void serverStatusResult(const uint8* data, uint32 size, uint32 serverIP, uint16 serverPort);

    /// Server description response received.
    void serverDescResult(const uint8* data, uint32 size, uint32 serverIP, uint16 serverPort);

private slots:
    void onReadyRead();
    void onDnsFinished();

private:
    bool processPacket(const uint8* packet, uint32 size, uint8 opcode,
                       uint32 senderIP, uint16 senderPort);

    void sendBuffer(uint32 ip, uint16 port, const uint8* data, uint32 size);
    void cleanupStaleDNSRequests();

    QUdpSocket m_socket;
    std::deque<ServerUDPPacket> m_controlQueue;
    std::vector<std::unique_ptr<ServerDNSRequest>> m_dnsRequests;
    mutable std::mutex m_sendLock;
    bool m_wouldBlock = false;

    QElapsedTimer m_elapsedTimer;
};

} // namespace eMule
