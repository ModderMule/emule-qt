#include "pch.h"
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

#include <QHostAddress>
#include <QNetworkDatagram>


#include <zlib.h>


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
    if (auto* throttler = theApp.uploadBandwidthThrottler)
        throttler->removeFromAllQueues(static_cast<ThrottledControlSocket*>(this));
    m_socket.close();
}

// ---------------------------------------------------------------------------
// Create and bind
// ---------------------------------------------------------------------------

bool ClientUDPSocket::create()
{
    if (!m_socket.bind(QHostAddress::Any, 0)) {   // dual-stack (AF_INET6, IPV6_V6ONLY=0)
        logError(QStringLiteral("ClientUDPSocket: Failed to bind: %1").arg(m_socket.errorString()));
        return false;
    }
    m_port = m_socket.localPort();
    return true;
}

bool ClientUDPSocket::rebind(uint16 port)
{
    m_socket.close();
    if (!m_socket.bind(QHostAddress::Any, port)) {   // dual-stack
        logError(QStringLiteral("ClientUDPSocket: Failed to rebind to port %1: %2")
                     .arg(port).arg(m_socket.errorString()));
        return false;
    }
    m_port = m_socket.localPort();
    return true;
}

// ---------------------------------------------------------------------------
// Send packet
// ---------------------------------------------------------------------------

bool ClientUDPSocket::sendPacket(std::unique_ptr<Packet> packet, uint32 ip, uint16 port,
                                 bool encrypt, const uint8* targetHash, bool isKad,
                                 uint32 receiverVerifyKey)
{
    return sendPacket(std::move(packet), Endpoint::fromHostOrder(ip, port),
                      encrypt, targetHash, isKad, receiverVerifyKey);
}

bool ClientUDPSocket::sendPacket(std::unique_ptr<Packet> packet, const Endpoint& dest,
                                 bool encrypt, const uint8* targetHash, bool isKad,
                                 uint32 receiverVerifyKey)
{
    if (!packet)
        return false;

    if (auto* stats = theApp.statistics) {
        if (isKad)
            stats->addUpDataOverheadKad(packet->size);
        else
            stats->addUpDataOverheadOther(packet->size);
    }

    // Purge expired packets from queue
    purgeExpiredPackets();

    UDPPack pack;
    pack.packet = std::move(packet);
    pack.destination = dest;
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

bool ClientUDPSocket::hasControlQueue() const
{
    std::lock_guard lock(m_sendLock);
    return !m_controlQueue.empty();
}

SocketSentBytes ClientUDPSocket::sendControlData(uint32 maxNumberOfBytesToSend, uint32 /*minFragSize*/)
{
    // Called from the UploadBandwidthThrottler thread.
    // We must NOT call m_socket.writeDatagram() here — QUdpSocket belongs to
    // the main thread and Qt's QSocketNotifier is not thread-safe.
    // Instead, prepare datagrams and queue them for flushSendQueue() which
    // runs on the socket's owning thread.
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

        // Encrypt if requested. The ED2K key mixes in OUR public IP, in the same
        // family as the destination (v6 key for a v6 peer, v4 key otherwise).
        if (pack.encrypt) {
            const Address publicIP = pack.destination.address().isIPv6()
                ? theApp.publicIPv6()
                : Address::fromHostOrder(theApp.publicIP());
            uint32 cryptOverhead = static_cast<uint32>(
                EncryptedDatagramSocket::encryptOverheadSize(pack.kad));

            // Sender verify key: derived from our secret plus the *destination*
            // address, so a peer that receives it can only have received it at that
            // address. It echoes the value back as the receiver key on its next
            // packet, which is what marks a Kad contact IP-verified.
            // MFC ClientUDPSocket.cpp:449 computes this for every Kad packet; we used
            // to send a literal 0, so no contact learned at runtime ever became
            // verified — and RoutingBin::getClosestTo only ever hands out verified
            // contacts, so routing answers could never carry anything beyond the
            // bootstrap set. Non-Kad packets carry no key, exactly as in MFC.
            const uint32 senderVerifyKey =
                (pack.kad && pack.destination.address().isIPv4())
                    ? kad::KadPrefs::getUDPVerifyKey(pack.destination.address().toUint32())
                    : 0u;

            uint32 encryptedLen = EncryptedDatagramSocket::encryptSendClient(
                buf.data() + offset - cryptOverhead, rawSize,
                pack.targetHash.data(), pack.kad,
                pack.receiverVerifyKey, senderVerifyKey, publicIP);

            uint32 actualStart = offset - (encryptedLen - rawSize);
            rawSize = encryptedLen;
            offset = actualStart;
        }

        PreparedDatagram dg;
        dg.data = QByteArray(reinterpret_cast<const char*>(buf.data() + offset),
                             static_cast<qsizetype>(rawSize));
        dg.destination = pack.destination;

        result.sentBytesControlPackets += rawSize;
        m_sendReadyQueue.push_back(std::move(dg));
        m_controlQueue.pop_front();
    }

    // Signal the main thread to flush the prepared datagrams.
    if (!m_sendReadyQueue.empty())
        QMetaObject::invokeMethod(this, &ClientUDPSocket::flushSendQueue, Qt::QueuedConnection);

    return result;
}

// ---------------------------------------------------------------------------
// flushSendQueue — runs on the socket's owning thread
// ---------------------------------------------------------------------------

void ClientUDPSocket::flushSendQueue()
{
    std::deque<PreparedDatagram> toSend;
    {
        std::lock_guard lock(m_sendLock);
        toSend.swap(m_sendReadyQueue);
    }

    for (auto& dg : toSend) {
        qint64 sent = m_socket.writeDatagram(
            dg.data, dg.destination.address().toQHostAddress(), dg.destination.port());
        if (sent < 0) {
            logWarning(QStringLiteral("UDP send failed to %1 — %2")
                .arg(dg.destination.toString()).arg(m_socket.errorString()));
        }
    }
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

        const Address senderAddress = Address::fromQHostAddress(datagram.senderAddress());
        const uint16 senderPort = static_cast<uint16>(datagram.senderPort());
        const Endpoint senderEP(senderAddress, senderPort);
        // Host-order IPv4 for the IPv4-only Kad verify-key store; 0 for IPv6 (no Kad over v6).
        const uint32 senderIPv4Host = senderAddress.isIPv4() ? senderAddress.toUint32() : 0;

        // Address-typed: the filter now holds a per-family range table, so an IPv6 sender
        // is checked against the IPv6 ranges instead of passing unfiltered.
        if (auto* filter = theApp.ipFilter) {
            if (filter->isFiltered(senderAddress, thePrefs.ipFilterLevel())) {
                if (auto* stats = theApp.statistics)
                    stats->addFilteredClient();
                continue;
            }
        }
        if (auto* cl = theApp.clientList) {
            if (cl->isBannedClient(senderAddress))
                continue;
        }

        auto* buf = reinterpret_cast<uint8*>(data.data());
        qsizetype bufLen = data.size();

        uint8 protoByte = buf[0];

        // logDebug(QStringLiteral("UDP recv %1 bytes from %2:%3 proto=0x%4")
        //     .arg(data.size()).arg(senderAddr.toString()).arg(senderPort)
        //     .arg(protoByte, 2, 16, QLatin1Char('0')));

        if (protoByte == OP_EMULEPROT) {
            // Unencrypted eMule client UDP packet
            uint8 opcode = buf[1];
            processPacket(buf + 2, static_cast<uint32>(bufLen - 2), opcode, senderEP);
        } else if (protoByte == OP_KADEMLIAHEADER) {
            // Uncompressed Kademlia packet — forward directly
            if (auto* stats = theApp.statistics)
                stats->addDownDataOverheadKad(static_cast<uint32>(bufLen));
            uint8 opcode = buf[1];
            emit kadPacketReceived(opcode, buf + 2,
                                   static_cast<uint32>(bufLen - 2), senderEP,
                                   false, 0);
        } else if (protoByte == OP_KADEMLIAPACKEDPROT) {
            // Compressed Kademlia packet — decompress before forwarding
            if (auto* stats = theApp.statistics)
                stats->addDownDataOverheadKad(static_cast<uint32>(bufLen));
            uint8 opcode = buf[1];
            QByteArray decompressed = decompressKadPayload(buf + 2, static_cast<int>(bufLen - 2));
            if (!decompressed.isEmpty()) {
                emit kadPacketReceived(opcode,
                                       reinterpret_cast<const uint8*>(decompressed.constData()),
                                       static_cast<uint32>(decompressed.size()),
                                       senderEP, false, 0);
            }
        } else if (protoByte == OP_UDPRESERVEDPROT1 || protoByte == OP_UDPRESERVEDPROT2) {
            // Reserved UDP protocol headers (0xA3 / 0xB2). Obfuscation-transparent
            // (see isProtocolHeader in EncryptedDatagramSocket.cpp), so they always
            // arrive in the clear. No payload semantics are defined yet — dispatch to
            // a named stub instead of dropping them silently.
            processReservedProtPacket(protoByte, buf + 2, static_cast<uint32>(bufLen - 2),
                                      buf[1], senderIPv4Host, senderPort);
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
                kadRecvKey = kadPrefs->getUDPVerifyKey(senderIPv4Host);
            }
            DecryptResult dr = EncryptedDatagramSocket::decryptReceivedClient(
                buf, static_cast<int>(bufLen), senderAddress, userHash.data(), kadIDPtr, kadRecvKey);

            if (dr.length > 1 && dr.data != nullptr) {
                uint8 innerProto = dr.data[0];
                uint8 opcode = dr.data[1];

                if (innerProto == OP_EMULEPROT) {
                    processPacket(dr.data + 2, static_cast<uint32>(dr.length - 2),
                                  opcode, senderEP);
                } else if (innerProto == OP_KADEMLIAHEADER) {
                    if (auto* stats = theApp.statistics)
                        stats->addDownDataOverheadKad(static_cast<uint32>(bufLen));
                    // The two keys are not interchangeable. The *receiver* key is the
                    // one we minted for this peer's IP and handed to it earlier; seeing
                    // it echoed back proves the peer really lives at that address. The
                    // *sender* key is the peer's own key for us, which we keep and echo
                    // back on our next packet. MFC ClientUDPSocket.cpp:121,137.
                    const bool validKey = (dr.receiverVerifyKey != 0) &&
                        (dr.receiverVerifyKey == kad::KadPrefs::getUDPVerifyKey(senderIPv4Host));
                    emit kadPacketReceived(opcode, dr.data + 2,
                                           static_cast<uint32>(dr.length - 2),
                                           senderEP,
                                           validKey, dr.senderVerifyKey);
                } else if (innerProto == OP_KADEMLIAPACKEDPROT) {
                    if (auto* stats = theApp.statistics)
                        stats->addDownDataOverheadKad(static_cast<uint32>(bufLen));
                    const bool validKey = (dr.receiverVerifyKey != 0) &&
                        (dr.receiverVerifyKey == kad::KadPrefs::getUDPVerifyKey(senderIPv4Host));
                    QByteArray decompressed = decompressKadPayload(dr.data + 2, dr.length - 2);
                    if (!decompressed.isEmpty()) {
                        emit kadPacketReceived(opcode,
                                               reinterpret_cast<const uint8*>(decompressed.constData()),
                                               static_cast<uint32>(decompressed.size()),
                                               senderEP,
                                               validKey, dr.senderVerifyKey);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Packet processing
// ---------------------------------------------------------------------------

bool ClientUDPSocket::processPacket(const uint8* packet, uint32 size, uint8 opcode,
                                    const Endpoint& senderEP)
{
    if (auto* stats = theApp.statistics)
        stats->addDownDataOverheadOther(size);

    switch (opcode) {
    case OP_REASKCALLBACKUDP:
        emit reaskCallbackReceived(senderEP, packet, size);
        break;

    case OP_REASKFILEPING:
        emit reaskFilePingReceived(senderEP, packet, size);
        break;

    case OP_REASKACK:
        emit reaskAckReceived(senderEP, packet, size);
        break;

    case OP_FILENOTFOUND:
        emit fileNotFoundReceived(senderEP);
        break;

    case OP_QUEUEFULL:
        emit queueFullReceived(senderEP);
        break;

    case OP_DIRECTCALLBACKREQ:
        emit directCallbackReceived(senderEP, packet, size);
        break;

    case OP_PORTTEST:
        if (size == 1 && packet[0] == 0x12)
            emit portTestReceived(senderEP);
        break;

    default:
        logDebug(QStringLiteral("ClientUDPSocket: Unknown opcode 0x%1 from %2")
                     .arg(opcode, 2, 16, QLatin1Char('0'))
                     .arg(senderEP.toString()));
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

// ---------------------------------------------------------------------------
// Decompression helper
// ---------------------------------------------------------------------------

QByteArray ClientUDPSocket::decompressKadPayload(const uint8* data, int len)
{
    if (len <= 0)
        return {};

    uLongf outSize = static_cast<uLongf>(len) * 10 + 300;
    constexpr uLongf kMaxDecompressed = 250000;

    QByteArray out;
    int result = Z_OK;
    do {
        out.resize(static_cast<qsizetype>(outSize));
        uLongf actualSize = outSize;
        result = uncompress(reinterpret_cast<Bytef*>(out.data()), &actualSize,
                            reinterpret_cast<const Bytef*>(data), static_cast<uLong>(len));
        if (result == Z_OK) {
            out.resize(static_cast<qsizetype>(actualSize));
            return out;
        }
        outSize *= 2;
    } while (result == Z_BUF_ERROR && outSize < kMaxDecompressed);

    return {};
}

// ---------------------------------------------------------------------------
// Reserved UDP protocols (0xA3 / 0xB2) — receive-side dispatch stub
// ---------------------------------------------------------------------------

bool ClientUDPSocket::processReservedProtPacket(uint8 protByte, const uint8* /*packet*/, uint32 size,
                                                uint8 opcode, uint32 senderIP, uint16 senderPort)
{
    if (auto* stats = theApp.statistics)
        stats->addDownDataOverheadOther(size);

    const Endpoint senderEP = Endpoint::fromHostOrder(senderIP, senderPort);
    logDebug(QStringLiteral("ClientUDPSocket: reserved UDP prot (0x%1) opcode 0x%2 size %3 "
                            "from %4 — no channel handler registered")
                 .arg(protByte, 2, 16, QLatin1Char('0'))
                 .arg(opcode, 2, 16, QLatin1Char('0'))
                 .arg(size)
                 .arg(senderEP.toString()));
    return true;
}

} // namespace eMule
