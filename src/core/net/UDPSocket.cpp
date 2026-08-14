#include "pch.h"
/// @file UDPSocket.cpp
/// @brief Server UDP communication socket — replaces MFC CUDPSocket.

#include "net/UDPSocket.h"
#include "net/EncryptedDatagramSocket.h"
#include "net/HostResolver.h"
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

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

UDPSocket::UDPSocket(QObject* parent)
    : QObject(parent)
{
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
    // Dual-stack bind. IPv4 senders (incl. v4-mapped ::ffff: over an AF_INET6 socket)
    // still resolve via toIPv4Address() below, so the server UDP path is unchanged for
    // the realistic case: IPv4 transport to a dual-stack server, with IPv6 sources
    // carried inside the payload. Dialing a v6-only server over UDP is a later step.
    if (!m_socket.bind(QHostAddress::Any, bindPort)) {
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
    if (server.hasDynIP() && server.ipAddress().isNull()) {
        queueDNSRequest(server, port, buf.data() + offset, rawSize);
        return;
    }

    const Endpoint dest(server.ipAddress(), port);

    logServerVerbose(QStringLiteral(">>> UDPSocket::sendPacket opcode=0x%1 payload=%2 bytes -> %3 encrypted=%4")
                 .arg(packet->opcode, 2, 16, QLatin1Char('0'))
                 .arg(packet->size)
                 .arg(dest.toString())
                 .arg(encrypt ? QStringLiteral("yes") : QStringLiteral("no")));

    // Send directly
    sendBuffer(dest, buf.data() + offset, rawSize);
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


    uint16 port = specialPort ? specialPort : static_cast<uint16>(server.port() + 4);

    if (server.hasDynIP() && server.ipAddress().isNull()) {
        queueDNSRequest(server, port, data, size);
        return;
    }

    const Endpoint dest(server.ipAddress(), port);

    logServerVerbose(QStringLiteral(">>> UDPSocket::sendRawPacket %1 bytes -> %2 (raw)")
                 .arg(size).arg(dest.toString()));

    sendBuffer(dest, data, size);
}

// ---------------------------------------------------------------------------
// ThrottledControlSocket: bandwidth-limited send
// ---------------------------------------------------------------------------

bool UDPSocket::hasControlQueue() const
{
    std::lock_guard lock(m_sendLock);
    return !m_controlQueue.empty();
}

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

        // Endpoint-typed so an IPv6 server's reply is attributed to the right entry
        // instead of collapsing to 0 and matching a null-address server.
        const Endpoint sender(Address::fromQHostAddress(datagram.senderAddress()),
                              static_cast<uint16>(datagram.senderPort()));

        if (auto* filter = theApp.ipFilter) {
            if (filter->isFiltered(sender.address(), thePrefs.ipFilterLevel()))
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
        Server* srv = theApp.serverList
                          ? theApp.serverList->findByIPUdp(sender.address(), sender.port())
                          : nullptr;
        const bool cryptPending = srv && srv->cryptPingReplyPending() && srv->challenge() != 0;
        const bool tryDecrypt = srv && thePrefs.cryptLayerSupported()
            && ((srv->serverKeyUDP() != 0 && srv->supportsObfuscationUDP()) || cryptPending);
        const uint32 dwKey = cryptPending ? srv->challenge()
                                          : (srv ? srv->serverKeyUDP() : 0);

        uint8 protoByte = buf[0];

        if (protoByte == OP_EDONKEYPROT) {
            // Unencrypted ED2K packet
            processPacket(buf + 2, static_cast<uint32>(bufLen - 2), buf[1], sender);
        } else if (tryDecrypt) {
            DecryptResult dr = EncryptedDatagramSocket::decryptReceivedServer(
                buf, static_cast<int>(bufLen), dwKey);
            // decryptReceivedServer passes the buffer through unchanged on failure,
            // so success is signalled by the decrypted first byte being OP_EDONKEYPROT.
            if (dr.data != nullptr && dr.length >= 2 && dr.data[0] == OP_EDONKEYPROT) {
                processPacket(dr.data + 2, static_cast<uint32>(dr.length - 2), dr.data[1],
                              sender);
            } else {
                logServerVerbose(QStringLiteral("UDPSocket: dropped undecryptable server datagram from %1")
                             .arg(sender.toString()));
            }
        } else {
            // Unknown protocol byte and no server key applies — drop it (MFC does
            // not fall back to parsing it as plaintext).
            logServerVerbose(QStringLiteral("UDPSocket: dropped non-ed2k datagram (proto=0x%1) from %2")
                         .arg(protoByte, 2, 16, QLatin1Char('0'))
                         .arg(sender.toString()));
        }
    }
}

// ---------------------------------------------------------------------------
// Packet processing
// ---------------------------------------------------------------------------

bool UDPSocket::processPacket(const uint8* packet, uint32 size, uint8 opcode,
                              const Endpoint& senderEP)
{
    if (auto* stats = theApp.statistics)
        stats->addDownDataOverheadServer(size);

    // A server that answers ANY UDP opcode is alive — clear its failure count so
    // one that only ever replies to searches/sources is never reaped as dead. MFC
    // resets this at the top of ProcessPacket for every opcode (UDPSocket.cpp:216).
    if (auto* sl = theApp.serverList) {
        if (auto* srv = sl->findByIPUdp(senderEP.address(), senderEP.port(), true))
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
            logServerVerbose(QStringLiteral("<<< UDP OP_GLOBSEARCHRES from %1 size=%2")
                         .arg(senderEP.toString())
                         .arg(size));
            emit globalSearchResult(packet, size, senderEP);
            break;

        case OP_GLOBFOUNDSOURCES:
            logServerVerbose(QStringLiteral("<<< UDP OP_GLOBFOUNDSOURCES from %1 size=%2")
                         .arg(senderEP.toString()).arg(size));
            emit globalFoundSources(packet, size, senderEP);
            break;

        case OP_GLOBSERVSTATRES:
            logServerVerbose(QStringLiteral("<<< UDP OP_GLOBSERVSTATRES from %1 size=%2")
                         .arg(senderEP.toString()).arg(size));
            emit serverStatusResult(packet, size, senderEP);
            break;

        case OP_SERVER_DESC_RES:
            logServerVerbose(QStringLiteral("<<< UDP OP_SERVER_DESC_RES from %1 size=%2")
                         .arg(senderEP.toString()).arg(size));
            emit serverDescResult(packet, size, senderEP);
            break;

        default:
            logServerVerbose(QStringLiteral("UDPSocket: Unknown server opcode 0x%1 from %2")
                         .arg(opcode, 2, 16, QLatin1Char('0'))
                         .arg(senderEP.toString()));
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

void UDPSocket::sendBuffer(const Endpoint& dest, const uint8* data, uint32 size)
{
    {
        std::lock_guard lock(m_sendLock);

        ServerUDPPacket pkt;
        pkt.data.assign(data, data + size);
        pkt.destination = dest;
        m_controlQueue.push_back(std::move(pkt));
    }

    if (auto* throttler = theApp.uploadBandwidthThrottler)
        throttler->queueForSendingControlPacket(this);
}

void UDPSocket::queueDNSRequest(const Server& server, uint16 port,
                                const uint8* data, uint32 size)
{
    if (!m_hostResolver)
        m_hostResolver = new HostResolver(this);

    // One query returns A and AAAA together; the preference only orders them. IPv4 leads
    // by default because a server reached over IPv6 without a routable IPv4 hands out a
    // LowID unconditionally — but an AAAA-only server hostname now works.
    const auto pref = thePrefs.serverPreferIPv6() ? HostResolver::Preference::PreferIPv6
                                                  : HostResolver::Preference::PreferIPv4;
    const QString host = server.dynIP();
    std::vector<uint8> payload(data, data + size);

    m_hostResolver->resolve(host, pref, this,
        [this, host, port, payload = std::move(payload)](const HostResolver::Result& result) {
            if (!result.ok()) {
                logWarning(QStringLiteral("UDPSocket: DNS lookup failed for %1: %2")
                               .arg(host, result.errorString));
                return;
            }
            sendBuffer(Endpoint(result.first(), port),
                       payload.data(), static_cast<uint32>(payload.size()));
        });
}

} // namespace eMule
