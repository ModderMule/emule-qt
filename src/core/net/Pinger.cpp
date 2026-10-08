#include "pch.h"
/// @file Pinger.cpp
/// @brief Cross-platform ICMP/UDP ping — replaces Windows ICMP.DLL Pinger.

#include "net/Pinger.h"
#include "net/BindAddress.h"
#include "net/InterfacePin.h"
#include "utils/Log.h"

#include <QElapsedTimer>


#ifndef Q_OS_WIN

// POSIX networking
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <linux/errqueue.h>
#endif

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
#ifdef Q_OS_LINUX
    // A ping socket hands out ICMP errors (TTL exceeded, unreachable) only on its
    // error queue; without them a traceroute hop is a timeout.
    if (m_icmpSocket >= 0) {
        int on = 1;
        ::setsockopt(m_icmpSocket, SOL_IP, IP_RECVERR, &on, sizeof(on));
    }
#endif

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
    if (!applyInterfacePin()) {
        PingStatus blocked;
        blocked.delay = static_cast<float>(kPingTimeoutMs);
        blocked.error = static_cast<uint32>(ENETDOWN);
        return blocked;
    }
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

#ifdef Q_OS_LINUX
        if (pfd.revents & POLLERR) {
            // The offending packet comes back as payload, the router as a control message
            uint8 echoed[64];
            char control[512];
            sockaddr_in target{};
            iovec iov{echoed, sizeof(echoed)};
            msghdr msg{};
            msg.msg_name = &target;
            msg.msg_namelen = sizeof(target);
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = control;
            msg.msg_controllen = sizeof(control);
            const auto got = ::recvmsg(m_icmpSocket, &msg, MSG_ERRQUEUE);
            if (got < 0)
                break;
            const float errRtt = static_cast<float>(timer.nsecsElapsed()) / 1'000'000.0f;
            const bool ours = got >= static_cast<ssize_t>(sizeof(ICMPHeader))
                && ntohs(reinterpret_cast<const ICMPHeader*>(echoed)->sequence) == seq;
            for (cmsghdr* c = CMSG_FIRSTHDR(&msg); ours && c; c = CMSG_NXTHDR(&msg, c)) {
                if (c->cmsg_level != SOL_IP || c->cmsg_type != IP_RECVERR)
                    continue;
                const auto* ee = reinterpret_cast<const sock_extended_err*>(CMSG_DATA(c));
                if (ee->ee_origin != SO_EE_ORIGIN_ICMP)
                    continue;
                const auto* offender = reinterpret_cast<const sockaddr_in*>(SO_EE_OFFENDER(ee));
                result.delay = errRtt;
                result.destinationAddress = offender->sin_addr.s_addr;
                result.status = ee->ee_type == kIcmpTTLExpired ? kPingTTLExpired
                                                               : kPingDestUnreachable;
                result.error = 0;
                result.ttl = ttl;
                result.success = true;
                return result;
            }
            continue;
        }
#endif

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

        // A router or the target refusing our echo (macOS delivers these in-band):
        // the error quotes our IP header and the first 8 bytes, i.e. the echo header.
        if ((icmp->type == kIcmpTTLExpired || icmp->type == kIcmpDestUnreachable)
            && n - icmpOffset >= static_cast<ssize_t>(sizeof(ICMPErrorBody))) {
            const auto* err = reinterpret_cast<const ICMPErrorBody*>(recvBuf + icmpOffset);
            const auto* quoted = reinterpret_cast<const ICMPHeader*>(err->data);
            if (err->originalIP.proto == IPPROTO_ICMP && quoted->type == kIcmpEchoRequest
                && ntohs(quoted->sequence) == seq) {
                result.delay = rtt;
                result.destinationAddress = from.sin_addr.s_addr;
                result.status = icmp->type == kIcmpTTLExpired ? kPingTTLExpired
                                                              : kPingDestUnreachable;
                result.error = 0;
                result.ttl = responseTTL > 0 ? responseTTL : ttl;
                result.success = true;
                return result;
            }
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

// Probes follow the bound interface; false = blocked, send nothing.
bool Pinger::applyInterfacePin()
{
    const BindAddress::Resolution r = BindAddress::current();
    if (r.state == BindAddress::State::Blocked)
        return false;
    const int wanted = r.state == BindAddress::State::Bound ? r.index : 0;
    if (wanted == m_pinnedIndex)
        return true;
    for (const int fd : {m_icmpSocket, m_rawSocket, m_udpSocket}) {
        if (fd < 0)
            continue;
        if (wanted == 0) {
            // Back to "any": index 0 clears the option.
#ifdef Q_OS_DARWIN
            const unsigned none = 0;
            ::setsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &none, sizeof(none));
#elif defined(SO_BINDTODEVICE)
            ::setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, "", 0);
#endif
        } else if (!InterfacePin::pinToInterface(fd, wanted, r.name)) {
            return false;
        }
    }
    m_pinnedIndex = wanted;
    return true;
}

} // namespace eMule

#else // Q_OS_WIN

// Windows implementation using IcmpSendEcho from the IP Helper API.

#include <winsock2.h>
#include <iphlpapi.h>
#include <icmpapi.h>

namespace eMule {

static constexpr DWORD kWinPingTimeout = 3000;  // 3 seconds
static constexpr WORD  kReplyBufSize   = sizeof(ICMP_ECHO_REPLY) + 32;

Pinger::Pinger()
{
    HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) {
        logWarning(QStringLiteral("Pinger: IcmpCreateFile() failed, error %1")
                       .arg(::GetLastError()));
    } else {
        m_icmpHandle = h;
    }
}

Pinger::~Pinger()
{
    if (m_icmpHandle) {
        IcmpCloseHandle(static_cast<HANDLE>(m_icmpHandle));
        m_icmpHandle = nullptr;
    }
}

PingStatus Pinger::ping(uint32 addr, uint8 ttl, bool /*useUdp*/)
{
    // IcmpSendEcho cannot be pinned to an interface: at least send nothing while the
    // selected one is missing.
    if (!BindAddress::outboundAllowed()) {
        PingStatus blocked;
        blocked.error = static_cast<uint32>(ERROR_NETWORK_UNREACHABLE);
        return blocked;
    }
    // Windows always uses ICMP via IcmpSendEcho (UDP traceroute not implemented)
    return pingICMP(addr, ttl);
}

PingStatus Pinger::pingICMP(uint32 addr, uint8 ttl)
{
    PingStatus result;
    result.delay = static_cast<float>(kWinPingTimeout);
    result.status = kPingTimedOut;
    result.error = kPingTimedOut;

    if (!m_icmpHandle) {
        result.error = 1;
        return result;
    }

    char replyBuf[kReplyBufSize]{};

    IP_OPTION_INFORMATION ipInfo{};
    ipInfo.Ttl = ttl;

    QElapsedTimer timer;
    timer.start();

    DWORD replyCount = IcmpSendEcho(
        static_cast<HANDLE>(m_icmpHandle),
        addr,
        nullptr, 0,      // no payload
        &ipInfo,
        replyBuf, kReplyBufSize,
        kWinPingTimeout);

    const float elapsed = static_cast<float>(timer.nsecsElapsed()) / 1'000'000.0f;

    if (replyCount > 0) {
        const auto& reply = *reinterpret_cast<const ICMP_ECHO_REPLY*>(replyBuf);
        const auto rtt = static_cast<float>(reply.RoundTripTime);

        // Use high-res timer when Windows RoundTripTime is coarse (<=20ms or multiple of 10)
        result.delay = (rtt <= 20.0f || static_cast<long>(rtt) % 10 == 0)
            ? elapsed : rtt;
        result.destinationAddress = reply.Address;
        result.status = reply.Status;
        result.error = 0;
        result.ttl = (reply.Status == IP_SUCCESS) ? reply.Options.Ttl : ttl;
        result.success = true;
    } else {
        result.error = ::GetLastError();
        result.success = false;
    }

    return result;
}

PingStatus Pinger::pingUDP(uint32 addr, uint8 ttl)
{
    // UDP traceroute not implemented on Windows — fall back to ICMP
    return pingICMP(addr, ttl);
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
