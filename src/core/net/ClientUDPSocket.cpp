/// @file ClientUDPSocket.cpp
/// @brief Client-to-client UDP socket — replaces MFC CClientUDPSocket.

#include "net/ClientUDPSocket.h"
#include "net/EncryptedDatagramSocket.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "ipfilter/IPFilter.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QHostAddress>
#include <QNetworkDatagram>

#include <cstring>

namespace eMule {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int kMaxClientUDPPacketSize = 6000;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ClientUDPSocket::ClientUDPSocket(QObject* parent)
    : QObject(parent)
{
    m_elapsedTimer.start();
    QObject::connect(&m_socket, &QUdpSocket::readyRead, this, &ClientUDPSocket::onReadyRead);
}

ClientUDPSocket::~ClientUDPSocket()
{
    m_socket.close();
}

// ---------------------------------------------------------------------------
// Create and bind
// ---------------------------------------------------------------------------

bool ClientUDPSocket::create()
{
    if (!m_socket.bind(QHostAddress::AnyIPv4, 0)) {
        logError(QStringLiteral("ClientUDPSocket: Failed to bind: %1").arg(m_socket.errorString()));
        return false;
    }
    m_port = m_socket.localPort();
    return true;
}

bool ClientUDPSocket::rebind(uint16 port)
{
    m_socket.close();
    if (!m_socket.bind(QHostAddress::AnyIPv4, port)) {
        logError(QStringLiteral("ClientUDPSocket: Failed to rebind to port %1: %2")
                     .arg(port).arg(m_socket.errorString()));
        return false;
    }
    m_port = port;
    return true;
}

// ---------------------------------------------------------------------------
// Send packet
// ---------------------------------------------------------------------------

bool ClientUDPSocket::sendPacket(std::unique_ptr<Packet> packet, uint32 ip, uint16 port,
                                 bool encrypt, const uint8* targetHash, bool isKad,
                                 uint32 receiverVerifyKey)
{
    if (!packet)
        return false;

    // Purge expired packets from queue
    purgeExpiredPackets();

    UDPPack pack;
    pack.packet = std::move(packet);
    pack.ip = ip;
    pack.port = port;
    pack.queueTime = static_cast<uint32>(m_elapsedTimer.elapsed());
    pack.encrypt = encrypt;
    pack.kad = isKad;
    pack.receiverVerifyKey = receiverVerifyKey;
    if (targetHash)
        std::memcpy(pack.targetHash.data(), targetHash, 16);

    {
        std::lock_guard lock(m_sendLock);
        m_controlQueue.push_back(std::move(pack));
    }

    if (auto* throttler = theApp.uploadBandwidthThrottler)
        throttler->queueForSendingControlPacket(this);

    return true;
}

// ---------------------------------------------------------------------------
// ThrottledControlSocket: bandwidth-limited send
// ---------------------------------------------------------------------------

SocketSentBytes ClientUDPSocket::sendControlData(uint32 maxNumberOfBytesToSend, uint32 /*minFragSize*/)
{
    std::lock_guard lock(m_sendLock);

    SocketSentBytes result;
    result.success = true;

    while (!m_controlQueue.empty() && result.sentBytesControlPackets < maxNumberOfBytesToSend) {
        auto& pack = m_controlQueue.front();
        Packet* pkt = pack.packet.get();

        if (!pkt) {
            m_controlQueue.pop_front();
            continue;
        }

        // Build raw UDP packet: 2-byte header + payload
        uint32 rawSize = 2 + pkt->size;
        static constexpr uint32 kMaxEncryptOverhead = 32;
        std::vector<uint8> buf(rawSize + kMaxEncryptOverhead, 0);

        uint32 offset = kMaxEncryptOverhead;
        buf[offset] = pkt->prot;
        buf[offset + 1] = pkt->opcode;
        if (pkt->pBuffer && pkt->size > 0)
            std::memcpy(buf.data() + offset + 2, pkt->pBuffer, pkt->size);

        // Encrypt if requested
        if (pack.encrypt) {
            uint32 publicIP = thePrefs.publicIP();
            uint32 encryptedLen = EncryptedDatagramSocket::encryptSendClient(
                buf.data() + offset, rawSize,
                pack.targetHash.data(), pack.kad,
                pack.receiverVerifyKey, 0, publicIP);

            uint32 actualStart = offset - (encryptedLen - rawSize);
            rawSize = encryptedLen;
            offset = actualStart;
        }

        qint64 sent = m_socket.writeDatagram(
            reinterpret_cast<const char*>(buf.data() + offset),
            static_cast<qint64>(rawSize),
            QHostAddress(ntohl(pack.ip)),
            pack.port);

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

void ClientUDPSocket::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_socket.receiveDatagram(kMaxClientUDPPacketSize);
        if (!datagram.isValid())
            continue;

        QByteArray data = datagram.data();
        if (data.size() < 2)
            continue;

        QHostAddress senderAddr = datagram.senderAddress();
        uint32 senderIP = htonl(senderAddr.toIPv4Address());
        uint16 senderPort = static_cast<uint16>(datagram.senderPort());

        if (auto* filter = theApp.ipFilter) {
            if (filter->isFiltered(senderIP, thePrefs.ipFilterLevel())) {
                if (auto* stats = theApp.statistics)
                    stats->addFilteredClient();
                continue;
            }
        }
        if (auto* cl = theApp.clientList) {
            if (cl->isBannedClient(senderIP))
                continue;
        }

        auto* buf = reinterpret_cast<uint8*>(data.data());
        int bufLen = data.size();

        uint8 protoByte = buf[0];

        if (protoByte == OP_EMULEPROT) {
            // Unencrypted eMule client UDP packet
            uint8 opcode = buf[1];
            processPacket(buf + 2, static_cast<uint32>(bufLen - 2), opcode, senderIP, senderPort);
        } else if (protoByte == OP_KADEMLIAHEADER || protoByte == OP_KADEMLIAPACKEDPROT) {
            // Kademlia packet — forward via signal
            uint8 opcode = buf[1];
            emit kadPacketReceived(protoByte, opcode, buf + 2,
                                   static_cast<uint32>(bufLen - 2), senderIP, senderPort);
        } else {
            // May be encrypted — use our userHash and kadID for decryption
            auto userHash = thePrefs.userHash();
            const uint8* kadIDPtr = nullptr;
            uint32 kadRecvKey = 0;
            if (auto* kadPrefs = eMule::kad::Kademlia::getInstancePrefs()) {
                // Use getData() (raw m_data bytes), NOT toByteArray() which
                // byte-swaps.  The wire format uses the raw uint32 representation,
                // so encryption keys must match that byte order.
                kadIDPtr = eMule::kad::RoutingZone::localKadId().getData();
                kadRecvKey = kadPrefs->getUDPVerifyKey(senderIP);
            }
            DecryptResult dr = EncryptedDatagramSocket::decryptReceivedClient(
                buf, bufLen, senderIP, userHash.data(), kadIDPtr, kadRecvKey);

            if (dr.length > 1 && dr.data != nullptr) {
                uint8 innerProto = dr.data[0];
                uint8 opcode = dr.data[1];

                if (innerProto == OP_EMULEPROT) {
                    processPacket(dr.data + 2, static_cast<uint32>(dr.length - 2),
                                  opcode, senderIP, senderPort);
                } else if (innerProto == OP_KADEMLIAHEADER ||
                           innerProto == OP_KADEMLIAPACKEDPROT) {
                    emit kadPacketReceived(innerProto, opcode, dr.data + 2,
                                           static_cast<uint32>(dr.length - 2),
                                           senderIP, senderPort);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Packet processing
// ---------------------------------------------------------------------------

bool ClientUDPSocket::processPacket(const uint8* packet, uint32 size, uint8 opcode,
                                    uint32 senderIP, uint16 senderPort)
{
    if (auto* stats = theApp.statistics)
        stats->addDownDataOverheadOther(size);

    switch (opcode) {
    case OP_REASKCALLBACKUDP:
        emit reaskCallbackReceived(senderIP, senderPort, packet, size);
        break;

    case OP_REASKFILEPING:
        emit reaskFilePingReceived(senderIP, senderPort, packet, size);
        break;

    case OP_REASKACK:
        emit reaskAckReceived(senderIP, senderPort, packet, size);
        break;

    case OP_FILENOTFOUND:
        emit fileNotFoundReceived(senderIP, senderPort);
        break;

    case OP_QUEUEFULL:
        emit queueFullReceived(senderIP, senderPort);
        break;

    case OP_DIRECTCALLBACKREQ:
        emit directCallbackReceived(senderIP, senderPort, packet, size);
        break;

    case OP_PORTTEST:
        emit portTestReceived(senderIP, senderPort);
        break;

    default:
        logDebug(QStringLiteral("ClientUDPSocket: Unknown opcode 0x%1 from %2:%3")
                     .arg(opcode, 2, 16, QLatin1Char('0'))
                     .arg(ipstr(senderIP))
                     .arg(senderPort));
        break;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Queue maintenance
// ---------------------------------------------------------------------------

void ClientUDPSocket::purgeExpiredPackets()
{
    std::lock_guard lock(m_sendLock);

    uint32 now = static_cast<uint32>(m_elapsedTimer.elapsed());

    std::erase_if(m_controlQueue, [now](const UDPPack& pack) {
        return (now - pack.queueTime) > UDPMAXQUEUETIME;
    });
}

} // namespace eMule
