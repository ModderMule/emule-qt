#pragma once

/// @file UDPSocket.h
/// @brief Server UDP communication socket — replaces MFC CUDPSocket.
///
/// Uses QUdpSocket + ThrottledControlSocket for bandwidth-controlled
/// UDP communication with ED2K servers. The CUDPSocketWnd DNS helper window is
/// replaced by the shared HostResolver, which resolves a dynIP server's hostname
/// for both address families.

#include "net/Address.h"
#include "net/Packet.h"
#include "net/ThrottledSocket.h"
#include "utils/Types.h"

#include <QUdpSocket>

#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace eMule {

class HostResolver;
class Server;

// ---------------------------------------------------------------------------
// Queued UDP packet
// ---------------------------------------------------------------------------

struct ServerUDPPacket {
    std::vector<uint8> data;
    Endpoint destination;   ///< Destination address + port.
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

    /// The OS-assigned local UDP port (0 if unbound).
    [[nodiscard]] quint16 localPort() const { return m_socket.localPort(); }

    /// Send a packet to a server. Takes ownership of packet.
    /// @param packet     Packet to send.
    /// @param server     Target server (for IP, port, encryption keys).
    /// @param specialPort Override port (0 = use server's UDP port).
    void sendPacket(std::unique_ptr<Packet> packet, const Server& server,
                    uint16 specialPort = 0);

    /// Send raw, pre-built bytes to a server unencrypted (used for the obfuscated
    /// server-stat crypt-ping, whose challenge is sent in the clear).
    /// @param server      Target server (for IP / dynIP resolution).
    /// @param specialPort Destination port (0 = server's UDP port = TCP+4).
    /// @param data        Raw bytes to send.
    /// @param size        Byte count.
    void sendRawPacket(const Server& server, uint16 specialPort,
                       const uint8* data, uint32 size);

    /// ThrottledControlSocket: send queued control data up to bandwidth limit.
    SocketSentBytes sendControlData(uint32 maxNumberOfBytesToSend, uint32 minFragSize) override;

signals:
    /// Global search results received from server.
    void globalSearchResult(const uint8* data, uint32 size, const Endpoint& server);

    /// Global found sources received from server.
    void globalFoundSources(const uint8* data, uint32 size, const Endpoint& server);

    /// Server status response received.
    void serverStatusResult(const uint8* data, uint32 size, const Endpoint& server);

    /// Server description response received.
    void serverDescResult(const uint8* data, uint32 size, const Endpoint& server);

private slots:
    void onReadyRead();

private:
    bool processPacket(const uint8* packet, uint32 size, uint8 opcode,
                       const Endpoint& sender);

    // Endpoint-typed: an IPv6 server's address does not fit a uint32, and projecting it
    // through toNetworkUint32() addressed the datagram to 0.0.0.0 — the server then
    // never answered a stat ping and was reaped as dead.
    void sendBuffer(const Endpoint& dest, const uint8* data, uint32 size);

    /// Resolve @p server's dynIP hostname, then send @p data to the resolved address.
    /// IPv4-preferred by default, IPv6-preferred when serverPreferIPv6 is set; a single
    /// query covers both families, so an AAAA-only hostname is reached either way.
    void queueDNSRequest(const Server& server, uint16 port, const uint8* data, uint32 size);

    QUdpSocket m_socket;
    std::deque<ServerUDPPacket> m_controlQueue;
    HostResolver* m_hostResolver = nullptr;   // created on first dynIP send
    mutable std::mutex m_sendLock;
    bool m_wouldBlock = false;
};

} // namespace eMule
