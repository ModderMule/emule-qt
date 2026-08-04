#pragma once

/// @file PortMapWire.h
/// @brief Primitives shared by the PCP and NAT-PMP codecs.
///
/// Everything here is pure: no sockets, no Qt objects, and no clock. Anything
/// time-dependent takes `now` (and, where the RFC mandates jitter, a random
/// fraction) as a parameter, so the whole layer is unit-testable without
/// qWait() and behaves identically on every platform.

#include "net/Address.h"
#include "utils/Types.h"

#include <QByteArray>

#include <array>
#include <span>

namespace eMule::portmap {

// ---------------------------------------------------------------------------
// 128-bit addresses (RFC 6887 section 5)
// ---------------------------------------------------------------------------

/// The address-family-specific all-zeros addresses.
///
/// `kAnyIPv4` cannot be produced through `Address`: fromHostOrder(0) returns a
/// *null* Address and toIPv6Mapped() on null returns null, so asking Address for
/// "::ffff:0:0" silently yields "::" instead — which asks the server for the
/// wrong external address family. Hence the hard-coded constants.
inline constexpr std::array<uint8, 16> kAnyIPv4 = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0};
inline constexpr std::array<uint8, 16> kAnyIPv6 = {};

/// Encode as 128 bits. IPv4 becomes IPv4-mapped; a null address becomes the
/// all-zeros value of the family named by @p wantIPv4Family.
[[nodiscard]] std::array<uint8, 16> encodeAddr128(const Address& addr, bool wantIPv4Family);

/// Decode 128 bits. An IPv4-mapped address is demoted to plain IPv4; either
/// all-zeros form decodes to a null Address, i.e. "unspecified".
///
/// All 96 leading bits are checked for the mapped pattern, per section 5 —
/// testing only bits 81-96 would misread e.g. 1::ffff:1.2.3.4 as IPv4.
[[nodiscard]] Address decodeAddr128(std::span<const uint8, 16> bytes);

// ---------------------------------------------------------------------------
// Big-endian field access
// ---------------------------------------------------------------------------

[[nodiscard]] uint16 readU16(std::span<const uint8> data, usize offset) noexcept;
[[nodiscard]] uint32 readU32(std::span<const uint8> data, usize offset) noexcept;

void appendU16(QByteArray& out, uint16 value);
void appendU32(QByteArray& out, uint32 value);
void appendBytes(QByteArray& out, std::span<const uint8> bytes);
void appendZeros(QByteArray& out, usize count);

/// View a QByteArray as bytes, for handing a received datagram to a decoder.
[[nodiscard]] std::span<const uint8> asBytes(const QByteArray& data) noexcept;

// ---------------------------------------------------------------------------
// Cross-protocol version handshake
// ---------------------------------------------------------------------------

/// What a reply on UDP 5351 says about the router, from byte 0 alone.
enum class ProbeVerdict : uint8 {
    Ignore,        ///< unparseable
    SpeaksPcp,     ///< version 2
    SpeaksNatPmp,  ///< version 0
    UnknownVersion,
};

/// Classify a reply without committing to either codec.
///
/// The two protocols were designed so this works: PCP's `reserved:8|result:8` at
/// offsets 2-3 reads as NAT-PMP's 16-bit result code, and UNSUPP_VERSION is 1 in
/// both. Byte 0 always carries the highest version the server supports, so a
/// NAT-PMP box answering our PCP packet says "0" and a PCP box answering our
/// NAT-PMP packet says "2".
///
/// Tolerant of very short datagrams on purpose: a FRITZ!Box rejects NAT-PMP with
/// two bytes, and that reply is the whole signal.
[[nodiscard]] ProbeVerdict classifyDatagram(std::span<const uint8> datagram) noexcept;

// ---------------------------------------------------------------------------
// Lifetimes (RFC 6886 section 3.3, RFC 6887 section 15)
// ---------------------------------------------------------------------------

/// RFC 6886 section 3.3 RECOMMENDED, and comfortably inside RFC 6887's
/// 120s..24h server-side band.
inline constexpr uint32 kDefaultLifetimeSecs = 7200;
inline constexpr uint32 kMaxSaneLifetimeSecs = 24 * 60 * 60;
/// RFC 6887 section 7.4 calls 30 minutes a "long lifetime" error hold-off.
inline constexpr uint32 kMaxErrorHoldoffSecs = 30 * 60;
/// RFC 6887 section 11.2.1: renewals must never be closer together than this.
inline constexpr uint32 kMinRenewGapSecs = 4;

/// Clamp a granted lifetime to something sane.
///
/// This is a security control, not tidiness. RFC 6887 section 8.3 tells a client
/// not to repeat a failed request for the error's lifetime, so one spoofed error
/// carrying lifetime 0xFFFFFFFF would otherwise disable port mapping for 136
/// years. Success values are clamped too, because a pre-existing static mapping
/// legitimately reports 2^32-1 and section 15 says to behave as if it were 24h.
[[nodiscard]] constexpr uint32 clampGrantedLifetime(uint32 granted, bool isError) noexcept
{
    const uint32 ceiling = isError ? kMaxErrorHoldoffSecs : kMaxSaneLifetimeSecs;
    return granted < ceiling ? granted : ceiling;
}

/// RFC 6887 section 11.2.1 renewal schedule, in seconds from now.
///
/// Attempt 0 lands uniformly in [1/2, 5/8] of the lifetime, attempt 1 in
/// [3/4, 3/4+1/16], attempt 2 in [7/8, 7/8+1/32], and so on. @p rand01 in [0,1)
/// is injected so tests are deterministic. Never returns less than 4 seconds.
[[nodiscard]] uint32 renewalDelaySecs(uint32 lifetimeSecs, int attempt, double rand01) noexcept;

/// RFC 6886 section 3.3: "begin trying to renew the mapping halfway to expiry".
[[nodiscard]] uint32 natPmpRenewalDelaySecs(uint32 lifetimeSecs) noexcept;

/// RFC 6887 section 8.1.1 retransmit backoff, with the mandated +/-10% jitter.
/// Pass @p prevMs 0 for the first retransmission.
[[nodiscard]] qint64 nextRetransmitMs(qint64 prevMs, double rand01,
                                      qint64 irtMs = 3000,
                                      qint64 mrtMs = 1'024'000) noexcept;

// ---------------------------------------------------------------------------
// Server-reboot detection
// ---------------------------------------------------------------------------

/// RFC 6887 section 8.5 Epoch Time validation.
///
/// A server that lost state resets its epoch; noticing lets us re-add every
/// mapping immediately instead of waiting out the lease. @p clientNowSecs must
/// come from a monotonic clock — a wall clock stepped by NTP produces false
/// positives.
class EpochTracker {
public:
    /// Returns false when the server appears to have lost state.
    [[nodiscard]] bool validate(uint32 serverEpochSecs, qint64 clientNowSecs) noexcept;

    void reset() noexcept { m_seen = false; }
    [[nodiscard]] bool hasSample() const noexcept { return m_seen; }

private:
    bool   m_seen = false;
    uint32 m_prevServer = 0;
    qint64 m_prevClient = 0;
};

/// RFC 6886 section 3.6, which specifies different arithmetic from PCP's:
/// a conservative estimate is the previous SSSoE plus 7/8 of the locally
/// observed elapsed time, and a reboot is declared if the new value undershoots
/// it by more than two seconds.
[[nodiscard]] bool natPmpEpochIndicatesReboot(uint32 prevSssoe, uint32 currSssoe,
                                              qint64 elapsedSecs) noexcept;

} // namespace eMule::portmap
