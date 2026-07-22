#include "pch.h"
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
#include "utils/SafeFile.h"


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
    if (auto* throttler = theApp.uploadBandwidthThrottler)
        throttler->removeFromAllQueues(static_cast<ThrottledControlSocket*>(this));
    m_socket.close();
}

// ---------------------------------------------------------------------------
// Create and bind
// ---------------------------------------------------------------------------

bool UDPSocket::create()
{
    // Bind the server UDP socket to the configured server UDP port, matching MFC
    // CUDPSocket::Create() (srchybrid/UDPSocket.cpp:130): 0 = server UDP disabled,
    // 65535 (_UI16_MAX) = OS-assigned random port (backward compat), else bind the
    // configured port. Binding the configured port lets global-search replies land
    // on a forwardable port instead of a random one the router won't forward.
    const uint16 serverUDPPort = thePrefs.serverUDPPort();
    if (serverUDPPort == 0)
        return false;
    const uint16 bindPort = (serverUDPPort == 65535) ? uint16{0} : serverUDPPort;
    if (!m_socket.bind(QHostAddress::AnyIPv4, bindPort)) {
        logError(QStringLiteral("UDPSocket: Failed to bind server UDP port %1: %2")
                     .arg(bindPort).arg(m_socket.errorString()));
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

    if (auto* stats = theApp.statistics)
        stats->addUpDataOverheadServer(packet->size);


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
    uint32 ip = server.ipAddress().toNetworkUint32();

    // Encrypt if the crypt layer is enabled and the server supports it. MFC gates
    // this on IsCryptLayerEnabled() too (UDPSocket.cpp:750) — without it we would
    // obfuscate outbound server UDP even with the crypt layer switched off.
    // encryptSendServer writes a crypto header at buf[0..overhead-1]
    // and expects the plaintext payload at buf[overhead..overhead+len-1].
    // We placed the plaintext at buf.data()+offset, so pass
    // buf.data()+offset-overhead so the header goes into the reserved area.
    const bool encrypt = thePrefs.cryptLayerSupported()
        && server.serverKeyUDP() != 0 && server.supportsObfuscationUDP();
    if (encrypt) {
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
        pkt.destination = Endpoint(Address(), port); // IP resolved later via DNS
        req->pendingPackets.push_back(std::move(pkt));

        req->lookup = std::make_unique<QDnsLookup>(this);
        req->lookup->setType(QDnsLookup::A);
        req->lookup->setName(server.dynIP());
        QObject::connect(req->lookup.get(), &QDnsLookup::finished, this, &UDPSocket::onDnsFinished);
        req->lookup->lookup();

        m_dnsRequests.push_back(std::move(req));
        return;
    }

    logDebug(QStringLiteral("UDPSocket::sendPacket opcode=0x%1 payload=%2 bytes -> %3:%4 encrypted=%5")
                 .arg(packet->opcode, 2, 16, QLatin1Char('0'))
                 .arg(packet->size)
                 .arg(ipstr(ip))
                 .arg(port)
                 .arg(encrypt ? QStringLiteral("yes") : QStringLiteral("no")));

    // Send directly
    sendBuffer(ip, port, buf.data() + offset, rawSize);
}

// ---------------------------------------------------------------------------
// Send raw (unencrypted, pre-built) bytes to a server
// ---------------------------------------------------------------------------

void UDPSocket::sendRawPacket(const Server& server, uint16 specialPort,
                              const uint8* data, uint32 size)
{
    // The obfuscated server-stat handshake (ServerList::serverStats) sends its
    // challenge as raw bytes in the clear — the server encrypts the *reply* with
    // that challenge. MFC does the same: CUDPSocket::SendPacket() does not encrypt
    // raw packets (UDPSocket.cpp:762-766).
    if (!data || size == 0)
        return;

    if (auto* stats = theApp.statistics)
        stats->addUpDataOverheadServer(size);

    cleanupStaleDNSRequests();

    uint16 port = specialPort ? specialPort : static_cast<uint16>(server.port() + 4);
    uint32 ip = server.ipAddress().toNetworkUint32();

    if (server.hasDynIP() && ip == 0) {
        auto req = std::make_unique<ServerDNSRequest>();
        req->createdTime = static_cast<uint32>(m_elapsedTimer.elapsed());
        req->serverPort = port;

        ServerUDPPacket pkt;
        pkt.data.assign(data, data + size);
        pkt.destination = Endpoint(Address(), port); // IP resolved later via DNS
        req->pendingPackets.push_back(std::move(pkt));

        req->lookup = std::make_unique<QDnsLookup>(this);
        req->lookup->setType(QDnsLookup::A);
        req->lookup->setName(server.dynIP());
        QObject::connect(req->lookup.get(), &QDnsLookup::finished, this, &UDPSocket::onDnsFinished);
        req->lookup->lookup();

        m_dnsRequests.push_back(std::move(req));
        return;
    }

    logDebug(QStringLiteral("UDPSocket::sendRawPacket %1 bytes -> %2:%3 (raw)")
                 .arg(size).arg(ipstr(ip)).arg(port));

    sendBuffer(ip, port, data, size);
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
            pkt.destination.address().toQHostAddress(),
            pkt.destination.port());

        if (sent < 0) {
            // #32: MFC increments a server's failed-count on WSAECONNRESET (an ICMP
            // port-unreachable from an earlier send). Not portable here — this is an
            // *unconnected* QUdpSocket, so ICMP errors are neither delivered
            // synchronously nor attributable to a prior destination. Dead servers are
            // still reaped via the TCP-connect path (#27) and stat-ping failedCount.
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
        qsizetype bufLen = data.size();

        // Resolve the answering server so we can pick the right decryption key.
        // An obfuscated crypt-ping *reply* is encrypted with the challenge we sent
        // (ServerList::serverStats, port+12); an ordinary obfuscated reply uses the
        // server's stored UDP key. Only attempt decryption when the crypt layer is
        // enabled and one of those keys applies, and only accept the result if it
        // decodes to a real ED2K packet — otherwise drop, never re-parse ciphertext
        // as plaintext. MFC: CUDPSocket::OnReceive() — UDPSocket.cpp:159-181.
        Server* srv = theApp.serverList ? theApp.serverList->findByIPUdp(senderIP, senderPort)
                                        : nullptr;
        const bool cryptPending = srv && srv->cryptPingReplyPending() && srv->challenge() != 0;
        const bool tryDecrypt = srv && thePrefs.cryptLayerSupported()
            && ((srv->serverKeyUDP() != 0 && srv->supportsObfuscationUDP()) || cryptPending);
        const uint32 dwKey = cryptPending ? srv->challenge()
                                          : (srv ? srv->serverKeyUDP() : 0);

        uint8 protoByte = buf[0];

        if (protoByte == OP_EDONKEYPROT) {
            // Unencrypted ED2K packet
            processPacket(buf + 2, static_cast<uint32>(bufLen - 2), buf[1], senderIP, senderPort);
        } else if (tryDecrypt) {
            DecryptResult dr = EncryptedDatagramSocket::decryptReceivedServer(
                buf, static_cast<int>(bufLen), dwKey);
            // decryptReceivedServer passes the buffer through unchanged on failure,
            // so success is signalled by the decrypted first byte being OP_EDONKEYPROT.
            if (dr.data != nullptr && dr.length >= 2 && dr.data[0] == OP_EDONKEYPROT) {
                processPacket(dr.data + 2, static_cast<uint32>(dr.length - 2), dr.data[1],
                              senderIP, senderPort);
            } else {
                logDebug(QStringLiteral("UDPSocket: dropped undecryptable server datagram from %1:%2")
                             .arg(ipstr(senderIP)).arg(senderPort));
            }
        } else {
            // Unknown protocol byte and no server key applies — drop it (MFC does
            // not fall back to parsing it as plaintext).
            logDebug(QStringLiteral("UDPSocket: dropped non-ed2k datagram (proto=0x%1) from %2:%3")
                         .arg(protoByte, 2, 16, QLatin1Char('0'))
                         .arg(ipstr(senderIP)).arg(senderPort));
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

    const Endpoint senderEP = Endpoint::fromNetworkOrder(senderIP, senderPort);

    // A server that answers ANY UDP opcode is alive — clear its failure count so
    // one that only ever replies to searches/sources is never reaped as dead. MFC
    // resets this at the top of ProcessPacket for every opcode (UDPSocket.cpp:216).
    if (auto* sl = theApp.serverList) {
        if (auto* srv = sl->findByIPUdp(senderIP, senderPort, true))
            srv->resetFailedCount();
    }

    // The result/desc handlers below are invoked synchronously (direct signal
    // connections) and parse untrusted server payloads via SafeMemFile, which
    // throws FileException on any over-read. Without this boundary a truncated
    // packet would unwind out of the Qt slot and kill the daemon. MFC guards the
    // whole dispatch the same way — CUDPSocket::ProcessPacket() (UDPSocket.cpp:216)
    // — swallowing the error (returns true) for the two multi-result opcodes.
    try {
        switch (opcode) {
        case OP_GLOBSEARCHRES:
            logDebug(QStringLiteral("UDPSocket: received OP_GLOBSEARCHRES from %1 size=%2")
                         .arg(senderEP.toString())
                         .arg(size));
            emit globalSearchResult(packet, size, senderEP);
            break;

        case OP_GLOBFOUNDSOURCES:
            emit globalFoundSources(packet, size, senderEP);
            break;

        case OP_GLOBSERVSTATRES:
            emit serverStatusResult(packet, size, senderEP);
            break;

        case OP_SERVER_DESC_RES:
            emit serverDescResult(packet, size, senderEP);
            break;

        default:
            logDebug(QStringLiteral("UDPSocket: Unknown server opcode 0x%1 from %2:%3")
                         .arg(opcode, 2, 16, QLatin1Char('0'))
                         .arg(ipstr(senderIP))
                         .arg(senderPort));
            break;
        }
        return true;
    } catch (const FileException& ex) {
        logWarning(QStringLiteral("UDPSocket: bad server packet opcode=0x%1 from %2: %3")
                       .arg(opcode, 2, 16, QLatin1Char('0'))
                       .arg(senderEP.toString())
                       .arg(QLatin1StringView(ex.what())));
        // Swallow for the multi-result opcodes exactly as MFC does.
        return (opcode == OP_GLOBSEARCHRES || opcode == OP_GLOBFOUNDSOURCES);
    } catch (...) {
        logWarning(QStringLiteral("UDPSocket: unknown exception processing opcode 0x%1 from %2")
                       .arg(opcode, 2, 16, QLatin1Char('0'))
                       .arg(senderEP.toString()));
        return false;
    }
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
        pkt.destination = Endpoint::fromNetworkOrder(ip, port);
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
                Address resolved = Address::fromNetworkOrder(ip);

                // Send all pending packets
                for (auto& pkt : (*it)->pendingPackets) {
                    pkt.destination = Endpoint(resolved, pkt.destination.port());
                    sendBuffer(ip, pkt.destination.port(),
                               pkt.data.data(), static_cast<uint32>(pkt.data.size()));
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
