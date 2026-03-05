/// @file Pinger.cpp
/// @brief Cross-platform ICMP/UDP ping — replaces Windows ICMP.DLL Pinger.

#include "net/Pinger.h"
#include "utils/Log.h"

#include <QElapsedTimer>

#include <cerrno>
#include <cstring>

#ifndef Q_OS_WIN

// POSIX networking
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Pinger::Pinger()
{
    // Unprivileged ICMP socket for echo request/reply (macOS 10.x+, Linux 3.x+).
    m_icmpSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (m_icmpSocket < 0)
        logWarning(QStringLiteral("Pinger: could not create ICMP socket (errno %1)").arg(errno));

    // Raw ICMP socket for reading TTL_EXPIRED responses (requires root/admin).
    m_rawSocket = ::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (m_rawSocket >= 0) {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = INADDR_ANY;
        if (::bind(m_rawSocket, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
            ::close(m_rawSocket);
            m_rawSocket = -1;
        } else {
            // UDP socket for sending traceroute probes.
            m_udpSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (m_udpSocket < 0) {
                ::close(m_rawSocket);
                m_rawSocket = -1;
            } else {
                m_udpStarted = true;
            }
        }
    }
}

Pinger::~Pinger()
{
    if (m_icmpSocket >= 0)
        ::close(m_icmpSocket);
    if (m_udpStarted) {
        ::close(m_rawSocket);
        ::close(m_udpSocket);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

PingStatus Pinger::ping(uint32 addr, uint8 ttl, bool useUdp)
{
    if (useUdp && m_udpStarted)
        return pingUDP(addr, ttl);
    return pingICMP(addr, ttl);
}

// ---------------------------------------------------------------------------
// ICMP echo ping (unprivileged)
// ---------------------------------------------------------------------------

PingStatus Pinger::pingICMP(uint32 addr, uint8 ttl)
{
    PingStatus result;
    result.delay = static_cast<float>(kPingTimeoutMs);

    if (m_icmpSocket < 0) {
        result.error = static_cast<uint32>(EACCES);
        return result;
    }

    // Set TTL
    int ttlVal = ttl;
    if (::setsockopt(m_icmpSocket, IPPROTO_IP, IP_TTL, &ttlVal, sizeof(ttlVal)) < 0) {
        result.error = static_cast<uint32>(errno);
        return result;
    }

    // Build ICMP echo request
    struct {
        ICMPHeader hdr;
        uint8      payload[8]{};
    } request{};

    uint16 seq = ++m_icmpSeq;
    uint16 id  = static_cast<uint16>(::getpid() & 0xFFFF);

    request.hdr.type     = kIcmpEchoRequest;
    request.hdr.code     = 0;
    request.hdr.checksum = 0;
    request.hdr.id       = htons(id);
    request.hdr.sequence = htons(seq);
    // Fill payload with pattern
    for (int i = 0; i < 8; ++i)
        request.payload[i] = static_cast<uint8>(i + 0x30);
    request.hdr.checksum = icmpChecksum(&request, sizeof(request));

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = addr;

    QElapsedTimer timer;
    timer.start();

    auto sent = ::sendto(m_icmpSocket, &request, sizeof(request), 0,
                         reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    if (sent < 0) {
        result.error = static_cast<uint32>(errno);
        return result;
    }

    // Wait for reply
    uint8 recvBuf[1500];
    while (true) {
        int elapsed = static_cast<int>(timer.elapsed());
        int remaining = kPingTimeoutMs - elapsed;
        if (remaining <= 0)
            break;

        pollfd pfd{};
        pfd.fd = m_icmpSocket;
        pfd.events = POLLIN;

        int ready = ::poll(&pfd, 1, remaining);
        if (ready <= 0)
            break;

        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        auto n = ::recvfrom(m_icmpSocket, recvBuf, sizeof(recvBuf), 0,
                            reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n < 0)
            break;

        float rtt = static_cast<float>(timer.nsecsElapsed()) / 1'000'000.0f;

        // Determine where the ICMP header starts.
        // macOS: SOCK_DGRAM + IPPROTO_ICMP includes the IP header in received data.
        // Linux: SOCK_DGRAM + IPPROTO_ICMP strips the IP header (ICMP at offset 0).
        int icmpOffset = 0;
        uint8 responseTTL = 0;
        if (n >= static_cast<ssize_t>(sizeof(IPHeader))) {
            auto* ip = reinterpret_cast<const IPHeader*>(recvBuf);
            if (ip->version == 4 && ip->headerLen >= 5) {
                // IP header present — skip it
                icmpOffset = ip->headerLen * 4;
                responseTTL = ip->ttl;
            }
        }

        if (n - icmpOffset < static_cast<ssize_t>(sizeof(ICMPHeader)))
            continue;

        auto* icmp = reinterpret_cast<const ICMPHeader*>(recvBuf + icmpOffset);

        // On SOCK_DGRAM + IPPROTO_ICMP, the kernel manages the ICMP ID and
        // already filters replies for this socket. Match on type + sequence only.
        if (icmp->type == kIcmpEchoReply &&
            ntohs(icmp->sequence) == seq) {
            result.delay = rtt;
            result.destinationAddress = from.sin_addr.s_addr;
            result.status = kPingSuccess;
            result.error = 0;
            result.ttl = responseTTL > 0 ? responseTTL : ttl;
            result.success = true;
            return result;
        }
        // Not our reply; loop and try again
    }

    result.status = kPingTimedOut;
    result.error = kPingTimedOut;
    return result;
}

// ---------------------------------------------------------------------------
// UDP traceroute ping (requires elevated privileges)
// ---------------------------------------------------------------------------

PingStatus Pinger::pingUDP(uint32 addr, uint8 ttl)
{
    PingStatus result;
    result.delay = static_cast<float>(kPingTimeoutMs);

    // Drain any stale ICMP responses from the raw socket
    {
        pollfd pfd{};
        pfd.fd = m_rawSocket;
        pfd.events = POLLIN;
        uint8 drain[1500];
        while (::poll(&pfd, 1, 0) > 0) {
            if (::recv(m_rawSocket, drain, sizeof(drain), 0) <= 0)
                break;
        }
    }

    // Set TTL on UDP socket
    int ttlVal = ttl;
    if (::setsockopt(m_udpSocket, IPPROTO_IP, IP_TTL, &ttlVal, sizeof(ttlVal)) < 0) {
        result.error = static_cast<uint32>(errno);
        return result;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = addr;
    dest.sin_port = htons(kUDPTracePort);

    // Send a small UDP packet
    uint8 probe[4]{};
    std::memcpy(probe, &ttl, 1);

    QElapsedTimer timer;
    timer.start();

    auto sent = ::sendto(m_udpSocket, probe, sizeof(probe), 0,
                         reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    if (sent < 0) {
        result.error = static_cast<uint32>(errno);
        return result;
    }

    // Wait for ICMP TTL_EXPIRED or DEST_UNREACH on raw socket
    uint8 recvBuf[1500];
    while (true) {
        int elapsed = static_cast<int>(timer.elapsed());
        int remaining = kPingTimeoutMs - elapsed;
        if (remaining <= 0)
            break;

        pollfd pfd{};
        pfd.fd = m_rawSocket;
        pfd.events = POLLIN;

        int ready = ::poll(&pfd, 1, remaining);
        if (ready <= 0)
            break;

        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        auto n = ::recvfrom(m_rawSocket, recvBuf, sizeof(recvBuf), 0,
                            reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n < 0)
            break;

        float rtt = static_cast<float>(timer.nsecsElapsed()) / 1'000'000.0f;

        // Raw socket includes IP header
        if (n < static_cast<ssize_t>(sizeof(IPHeader)))
            continue;

        auto* ip = reinterpret_cast<const IPHeader*>(recvBuf);
        int ipHeaderLen = ip->headerLen * 4;

        if (n < ipHeaderLen + static_cast<ssize_t>(sizeof(ICMPErrorBody)))
            continue;

        auto* icmpErr = reinterpret_cast<const ICMPErrorBody*>(recvBuf + ipHeaderLen);

        bool isTTLExpired = (icmpErr->type == kIcmpTTLExpired);
        bool isDestUnreach = (icmpErr->type == kIcmpDestUnreachable);

        if ((isTTLExpired || isDestUnreach) &&
            icmpErr->udp().dstPort == htons(kUDPTracePort) &&
            icmpErr->originalIP.destIP == addr) {
            result.delay = rtt;
            result.destinationAddress = ip->sourceIP;
            result.status = isTTLExpired ? kPingTTLExpired : kPingDestUnreachable;
            result.error = 0;
            result.ttl = isTTLExpired ? ttl : static_cast<uint8>(kDefaultTTL - (ip->ttl & 0x3F));
            result.success = true;
            return result;
        }
        // Not our response; keep waiting
    }

    result.status = kPingTimedOut;
    result.error = kPingTimedOut;
    return result;
}

// ---------------------------------------------------------------------------
// ICMP checksum (RFC 1071)
// ---------------------------------------------------------------------------

uint16 Pinger::icmpChecksum(const void* data, int len)
{
    uint32 sum = 0;
    auto* p = static_cast<const uint16*>(data);
    int remaining = len;

    while (remaining > 1) {
        sum += *p++;
        remaining -= 2;
    }
    if (remaining == 1)
        sum += *reinterpret_cast<const uint8*>(p);

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return static_cast<uint16>(~sum);
}

} // namespace eMule

#else // Q_OS_WIN

// TODO: Windows implementation using IcmpSendEcho / IcmpSendEcho2
namespace eMule {

Pinger::Pinger()
{
    // Raw ICMP sockets not available on Windows without IcmpSendEcho.
    // All sockets remain at -1 (disabled).
    logWarning(QStringLiteral("Pinger: not yet implemented on Windows"));
}

Pinger::~Pinger() = default;

PingStatus Pinger::ping(uint32 /*addr*/, uint8 /*ttl*/, bool /*useUdp*/)
{
    PingStatus result;
    result.delay = static_cast<float>(kPingTimeoutMs);
    result.status = kPingTimedOut;
    result.error = kPingTimedOut;
    return result;
}

PingStatus Pinger::pingICMP(uint32 addr, uint8 ttl)
{
    return ping(addr, ttl, false);
}

PingStatus Pinger::pingUDP(uint32 addr, uint8 ttl)
{
    return ping(addr, ttl, true);
}

uint16 Pinger::icmpChecksum(const void* data, int len)
{
    uint32 sum = 0;
    auto* p = static_cast<const uint16*>(data);
    int remaining = len;

    while (remaining > 1) {
        sum += *p++;
        remaining -= 2;
    }
    if (remaining == 1)
        sum += *reinterpret_cast<const uint8*>(p);

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return static_cast<uint16>(~sum);
}

} // namespace eMule

#endif // Q_OS_WIN
