/// @file UDPSocket.cpp
/// @brief Server UDP communication socket — replaces MFC CUDPSocket.

#include "net/UDPSocket.h"
#include "net/EncryptedDatagramSocket.h"
#include "app/AppContext.h"
#include "ipfilter/IPFilter.h"
#include "prefs/Preferences.h"
#include "server/Server.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QHostAddress>
#include <QNetworkDatagram>

namespace eMule {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int kMaxUDPPacketSize = 5000;
static constexpr uint32 kDNSRequestTimeoutMs = 120'000; // 2 minutes

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

UDPSocket::UDPSocket(QObject* parent)
    : QObject(parent)
{
    m_elapsedTimer.start();
    QObject::connect(&m_socket, &QUdpSocket::readyRead, this, &UDPSocket::onReadyRead);
}

UDPSocket::~UDPSocket()
{
    m_socket.close();
}

// ---------------------------------------------------------------------------
// Create and bind
// ---------------------------------------------------------------------------

bool UDPSocket::create()
{
    if (!m_socket.bind(QHostAddress::AnyIPv4, 0)) {
        logError(QStringLiteral("UDPSocket: Failed to bind: %1").arg(m_socket.errorString()));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Send packet to server
// ---------------------------------------------------------------------------

void UDPSocket::sendPacket(std::unique_ptr<Packet> packet, const Server& server,
                           uint16 specialPort)
{
    if (!packet)
        return;

    cleanupStaleDNSRequests();

    // Build raw UDP packet: 2-byte header (prot + opcode) + payload
    uint32 rawSize = 2 + packet->size;

    // Reserve space for potential encryption overhead
    static constexpr uint32 kMaxEncryptOverhead = 32;
    std::vector<uint8> buf(rawSize + kMaxEncryptOverhead, 0);

    // Write the packet at the encryption overhead offset
    uint32 offset = kMaxEncryptOverhead;
    buf[offset] = packet->prot;
    buf[offset + 1] = packet->opcode;
    if (packet->pBuffer && packet->size > 0)
        std::memcpy(buf.data() + offset + 2, packet->pBuffer, packet->size);

    uint16 port = specialPort ? specialPort : (server.port() + 4); // Default UDP port = TCP+4
    uint32 ip = server.ip();

    // Encrypt if server supports it.
    // encryptSendServer writes a crypto header at buf[0..overhead-1]
    // and expects the plaintext payload at buf[overhead..overhead+len-1].
    // We placed the plaintext at buf.data()+offset, so pass
    // buf.data()+offset-overhead so the header goes into the reserved area.
    if (server.serverKeyUDP() != 0 && server.supportsObfuscationUDP()) {
        uint32 cryptOverhead = static_cast<uint32>(
            EncryptedDatagramSocket::encryptOverheadSize(false));
        uint32 encryptedLen = EncryptedDatagramSocket::encryptSendServer(
            buf.data() + offset - cryptOverhead, rawSize, server.serverKeyUDP());

        uint32 actualStart = offset - (encryptedLen - rawSize);
        rawSize = encryptedLen;
        offset = actualStart;

        // Use obfuscation port if available
        if (server.obfuscationPortUDP() != 0)
            port = server.obfuscationPortUDP();
    }

    // Check if server needs DNS resolution
    if (server.hasDynIP() && ip == 0) {
        // Queue for DNS resolution
        auto req = std::make_unique<ServerDNSRequest>();
        req->createdTime = static_cast<uint32>(m_elapsedTimer.elapsed());
        req->serverPort = port;

        ServerUDPPacket pkt;
        pkt.data.assign(buf.begin() + offset, buf.begin() + offset + rawSize);
        pkt.port = port;
        req->pendingPackets.push_back(std::move(pkt));

        req->lookup = std::make_unique<QDnsLookup>(this);
        req->lookup->setType(QDnsLookup::A);
        req->lookup->setName(server.dynIP());
        QObject::connect(req->lookup.get(), &QDnsLookup::finished, this, &UDPSocket::onDnsFinished);
        req->lookup->lookup();

        m_dnsRequests.push_back(std::move(req));
        return;
    }

    // Send directly
    sendBuffer(ip, port, buf.data() + offset, rawSize);
}

// ---------------------------------------------------------------------------
// ThrottledControlSocket: bandwidth-limited send
// ---------------------------------------------------------------------------

SocketSentBytes UDPSocket::sendControlData(uint32 maxNumberOfBytesToSend, uint32 /*minFragSize*/)
{
    std::lock_guard lock(m_sendLock);

    SocketSentBytes result;
    result.success = true;

    while (!m_controlQueue.empty() && result.sentBytesControlPackets < maxNumberOfBytesToSend) {
        auto& pkt = m_controlQueue.front();

        qint64 sent = m_socket.writeDatagram(
            reinterpret_cast<const char*>(pkt.data.data()),
            static_cast<qint64>(pkt.data.size()),
            QHostAddress(ntohl(pkt.ip)),
            pkt.port);

        if (sent < 0) {
            m_wouldBlock = true;
            break;
        }

        result.sentBytesControlPackets += static_cast<uint32>(sent);
        m_controlQueue.pop_front();
    }

    return result;
}

// ---------------------------------------------------------------------------
// Receive datagrams
// ---------------------------------------------------------------------------

void UDPSocket::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_socket.receiveDatagram(kMaxUDPPacketSize);
        if (!datagram.isValid())
            continue;

        QByteArray data = datagram.data();
        if (data.size() < 2)
            continue;

        QHostAddress senderAddr = datagram.senderAddress();
        uint32 senderIP = htonl(senderAddr.toIPv4Address());
        uint16 senderPort = static_cast<uint16>(datagram.senderPort());

        if (auto* filter = theApp.ipFilter) {
            if (filter->isFiltered(senderIP, thePrefs.ipFilterLevel()))
                continue;
        }

        auto* buf = reinterpret_cast<uint8*>(data.data());
        int bufLen = data.size();

        // Look up the server to get its UDP key for decryption
        uint32 serverKeyUDP = 0;
        if (auto* sl = theApp.serverList) {
            if (auto* srv = sl->findByIPUdp(senderIP, senderPort)) {
                serverKeyUDP = srv->serverKeyUDP();
            }
        }

        uint8 protoByte = buf[0];

        if (protoByte == OP_EDONKEYPROT) {
            // Unencrypted ED2K packet
            uint8 opcode = buf[1];
            processPacket(buf + 2, static_cast<uint32>(bufLen - 2), opcode, senderIP, senderPort);
        } else {
            // May be encrypted — try decryption with server key
            DecryptResult dr = EncryptedDatagramSocket::decryptReceivedServer(buf, bufLen, serverKeyUDP);
            if (dr.length > 1 && dr.data != nullptr) {
                uint8 opcode = dr.data[1];
                processPacket(dr.data + 2, static_cast<uint32>(dr.length - 2), opcode, senderIP, senderPort);
            } else {
                // Try as plain ED2K even if first byte is unusual
                if (bufLen >= 2) {
                    uint8 opcode = buf[1];
                    processPacket(buf + 2, static_cast<uint32>(bufLen - 2), opcode, senderIP, senderPort);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Packet processing
// ---------------------------------------------------------------------------

bool UDPSocket::processPacket(const uint8* packet, uint32 size, uint8 opcode,
                              uint32 senderIP, uint16 senderPort)
{
    if (auto* stats = theApp.statistics)
        stats->addDownDataOverheadServer(size);

    switch (opcode) {
    case OP_GLOBSEARCHRES:
        emit globalSearchResult(packet, size, senderIP, senderPort);
        break;

    case OP_GLOBFOUNDSOURCES:
        emit globalFoundSources(packet, size, senderIP, senderPort);
        break;

    case OP_GLOBSERVSTATRES:
        emit serverStatusResult(packet, size, senderIP, senderPort);
        break;

    case OP_SERVER_DESC_RES:
        emit serverDescResult(packet, size, senderIP, senderPort);
        break;

    default:
        logDebug(QStringLiteral("UDPSocket: Unknown server opcode 0x%1 from %2:%3")
                     .arg(opcode, 2, 16, QLatin1Char('0'))
                     .arg(ipstr(senderIP))
                     .arg(senderPort));
        break;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void UDPSocket::sendBuffer(uint32 ip, uint16 port, const uint8* data, uint32 size)
{
    {
        std::lock_guard lock(m_sendLock);

        ServerUDPPacket pkt;
        pkt.data.assign(data, data + size);
        pkt.ip = ip;
        pkt.port = port;
        m_controlQueue.push_back(std::move(pkt));
    }

    if (auto* throttler = theApp.uploadBandwidthThrottler)
        throttler->queueForSendingControlPacket(this);
}

void UDPSocket::cleanupStaleDNSRequests()
{
    uint32 now = static_cast<uint32>(m_elapsedTimer.elapsed());

    std::erase_if(m_dnsRequests, [now](const std::unique_ptr<ServerDNSRequest>& req) {
        return (now - req->createdTime) > kDNSRequestTimeoutMs;
    });
}

void UDPSocket::onDnsFinished()
{
    auto* lookup = qobject_cast<QDnsLookup*>(QObject::sender());
    if (!lookup)
        return;

    // Find the matching request
    for (auto it = m_dnsRequests.begin(); it != m_dnsRequests.end(); ++it) {
        if ((*it)->lookup.get() == lookup) {
            if (lookup->error() == QDnsLookup::NoError && !lookup->hostAddressRecords().isEmpty()) {
                QHostAddress addr = lookup->hostAddressRecords().first().value();
                uint32 ip = htonl(addr.toIPv4Address());

                // Send all pending packets
                for (auto& pkt : (*it)->pendingPackets) {
                    pkt.ip = ip;
                    sendBuffer(ip, pkt.port, pkt.data.data(), static_cast<uint32>(pkt.data.size()));
                }
            } else {
                logWarning(QStringLiteral("UDPSocket: DNS lookup failed: %1")
                               .arg(lookup->errorString()));
            }

            m_dnsRequests.erase(it);
            break;
        }
    }
}

} // namespace eMule
