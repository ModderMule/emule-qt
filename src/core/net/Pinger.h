#pragma once

/// @file Pinger.h
/// @brief Cross-platform ICMP/UDP ping utility for latency measurement.
///
/// Replaces MFC Pinger which used Windows ICMP.DLL and Winsock raw sockets.
/// Uses POSIX sockets directly since Qt has no raw ICMP socket support:
///   - ICMP echo: SOCK_DGRAM + IPPROTO_ICMP (unprivileged on macOS/Linux)
///   - UDP traceroute: SOCK_DGRAM + SOCK_RAW for ICMP responses (needs root)
///
/// Used by LastCommonRouteFinder for adaptive upload bandwidth control.

#include "utils/Types.h"

#include <QElapsedTimer>

namespace eMule {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

inline constexpr uint8  kDefaultTTL    = 64;
inline constexpr int    kPingTimeoutMs = 3000;   ///< 3-second timeout.
inline constexpr uint16 kUDPTracePort  = 33434;  ///< IANA traceroute port.

// ICMP packet types
inline constexpr uint8 kIcmpEchoReply       = 0;
inline constexpr uint8 kIcmpDestUnreachable = 3;
inline constexpr uint8 kIcmpEchoRequest     = 8;
inline constexpr uint8 kIcmpTTLExpired      = 11;

// Ping status codes (matching Windows IP_STATUS values for compatibility)
inline constexpr uint32 kPingSuccess          = 0;
inline constexpr uint32 kPingTTLExpired       = 11013;
inline constexpr uint32 kPingDestUnreachable  = 11003;
inline constexpr uint32 kPingTimedOut         = 11010;

// ---------------------------------------------------------------------------
// IP / ICMP header structs (packed, for raw socket parsing)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

struct IPHeader {
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    uint8 headerLen : 4;   ///< Header length in 32-bit words.
    uint8 version   : 4;   ///< IP version (4).
#else
    uint8 version   : 4;
    uint8 headerLen : 4;
#endif
    uint8  tos;
    uint16 totalLen;
    uint16 ident;
    uint16 flags;
    uint8  ttl;
    uint8  proto;
    uint16 checksum;
    uint32 sourceIP;
    uint32 destIP;
};

struct ICMPHeader {
    uint8  type;
    uint8  code;
    uint16 checksum;
    uint16 id;
    uint16 sequence;
};

/// ICMP error response (TTL_EXPIRED / DEST_UNREACH) carries the original IP header + 8 bytes.
struct ICMPErrorBody {
    uint8    type;
    uint8    code;
    uint16   checksum;
    uint8    unused[4];
    IPHeader originalIP;
    uint8 data[8];

    /// Overlay the 8 data bytes as a UDP header for port matching.
    struct UDPOverlay {
        uint16 srcPort;
        uint16 dstPort;
        uint16 length;
        uint16 udpChecksum;
    };
    [[nodiscard]] const UDPOverlay& udp() const {
        return *reinterpret_cast<const UDPOverlay*>(data);
    }
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// PingStatus — result of a single ping
// ---------------------------------------------------------------------------

struct PingStatus {
    float  delay              = 0.0f;  ///< Round-trip time in milliseconds.
    uint32 destinationAddress = 0;     ///< IP that responded.
    uint32 status             = 0;     ///< Status code (kPing* constants).
    uint32 error              = 0;     ///< OS error code, 0 on success.
    uint8  ttl                = 0;     ///< TTL of response.
    bool   success            = false; ///< true if a valid response was received.
};

// ---------------------------------------------------------------------------
// Pinger
// ---------------------------------------------------------------------------

/// Cross-platform ICMP ping / UDP traceroute utility.
///
/// Two modes:
/// - **ICMP echo** (`useUdp = false`): Uses unprivileged ICMP socket
///   (`SOCK_DGRAM` + `IPPROTO_ICMP`). Works without root on macOS 10.x+
///   and Linux 3.x+ (when user is in `ping_group_range`).
/// - **UDP traceroute** (`useUdp = true`): Sends UDP to port 33434 with
///   specific TTL, reads ICMP TTL_EXPIRED on raw socket. Requires elevated
///   privileges (root/admin).
class Pinger {
public:
    Pinger();
    ~Pinger();

    Pinger(const Pinger&) = delete;
    Pinger& operator=(const Pinger&) = delete;

    /// Ping an IPv4 address. Returns result with timing and status.
    /// @param addr   IPv4 address in network byte order.
    /// @param ttl    Time-to-live (hop limit).
    /// @param useUdp Use UDP traceroute mode instead of ICMP echo.
    PingStatus ping(uint32 addr, uint8 ttl = kDefaultTTL, bool useUdp = false);

    /// @return true if the UDP traceroute sockets were successfully opened.
    [[nodiscard]] bool isUdpAvailable() const { return m_udpStarted; }

    /// @return true if the ICMP echo socket was successfully opened.
    [[nodiscard]] bool isIcmpAvailable() const { return m_icmpSocket >= 0; }

private:
    PingStatus pingICMP(uint32 addr, uint8 ttl);
    PingStatus pingUDP(uint32 addr, uint8 ttl);

    static uint16 icmpChecksum(const void* data, int len);

    int  m_icmpSocket = -1;  ///< SOCK_DGRAM + IPPROTO_ICMP (unprivileged ICMP echo).
    int  m_rawSocket  = -1;  ///< SOCK_RAW + IPPROTO_ICMP (for reading TTL_EXPIRED).
    int  m_udpSocket  = -1;  ///< SOCK_DGRAM + IPPROTO_UDP (for sending traceroute probes).
    bool m_udpStarted = false;

    uint16 m_icmpSeq = 0;    ///< Incrementing ICMP sequence number.
};

} // namespace eMule
