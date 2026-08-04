#include "pch.h"
/// @file PortMapWire.cpp
/// @brief Primitives shared by the PCP and NAT-PMP codecs.

#include "portmap/PortMapWire.h"

#include <algorithm>
#include <cmath>

namespace eMule::portmap {

namespace {

/// The 96-bit prefix of an IPv4-mapped IPv6 address.
constexpr std::array<uint8, 12> kV4MappedPrefix = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};

[[nodiscard]] bool allZero(std::span<const uint8> bytes) noexcept
{
    return std::all_of(bytes.begin(), bytes.end(), [](uint8 b) { return b == 0; });
}

} // namespace

// ---------------------------------------------------------------------------
// 128-bit addresses
// ---------------------------------------------------------------------------

std::array<uint8, 16> encodeAddr128(const Address& addr, bool wantIPv4Family)
{
    if (addr.isIPv6())
        return addr.ipv6Bytes();

    if (addr.isIPv4()) {
        std::array<uint8, 16> out{};
        std::copy(kV4MappedPrefix.begin(), kV4MappedPrefix.end(), out.begin());
        const uint32 host = addr.toUint32();
        out[12] = static_cast<uint8>((host >> 24) & 0xFF);
        out[13] = static_cast<uint8>((host >> 16) & 0xFF);
        out[14] = static_cast<uint8>((host >> 8) & 0xFF);
        out[15] = static_cast<uint8>(host & 0xFF);
        return out;
    }

    return wantIPv4Family ? kAnyIPv4 : kAnyIPv6;
}

Address decodeAddr128(std::span<const uint8, 16> bytes)
{
    // Either all-zeros form means "unspecified"; collapsing both to a null
    // Address gives callers one test instead of two.
    if (allZero(bytes))
        return {};

    const bool mapped = std::equal(kV4MappedPrefix.begin(), kV4MappedPrefix.end(), bytes.begin());
    if (mapped) {
        const uint32 host = (uint32(bytes[12]) << 24) | (uint32(bytes[13]) << 16)
                            | (uint32(bytes[14]) << 8) | uint32(bytes[15]);
        return Address::fromHostOrder(host);   // ::ffff:0.0.0.0 -> null, as intended
    }

    return Address::fromIPv6Bytes(bytes.data());
}

// ---------------------------------------------------------------------------
// Big-endian field access
// ---------------------------------------------------------------------------

uint16 readU16(std::span<const uint8> data, usize offset) noexcept
{
    if (offset + 2 > data.size())
        return 0;
    return static_cast<uint16>((uint16(data[offset]) << 8) | uint16(data[offset + 1]));
}

uint32 readU32(std::span<const uint8> data, usize offset) noexcept
{
    if (offset + 4 > data.size())
        return 0;
    return (uint32(data[offset]) << 24) | (uint32(data[offset + 1]) << 16)
           | (uint32(data[offset + 2]) << 8) | uint32(data[offset + 3]);
}

void appendU16(QByteArray& out, uint16 value)
{
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

void appendU32(QByteArray& out, uint32 value)
{
    out.append(static_cast<char>((value >> 24) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

void appendBytes(QByteArray& out, std::span<const uint8> bytes)
{
    out.append(reinterpret_cast<const char*>(bytes.data()),
               static_cast<qsizetype>(bytes.size()));
}

void appendZeros(QByteArray& out, usize count)
{
    out.append(static_cast<qsizetype>(count), '\0');
}

std::span<const uint8> asBytes(const QByteArray& data) noexcept
{
    return {reinterpret_cast<const uint8*>(data.constData()),
            static_cast<usize>(data.size())};
}

// ---------------------------------------------------------------------------
// Cross-protocol version handshake
// ---------------------------------------------------------------------------

ProbeVerdict classifyDatagram(std::span<const uint8> datagram) noexcept
{
    if (datagram.empty())
        return ProbeVerdict::Ignore;

    switch (datagram[0]) {
    case 0: return ProbeVerdict::SpeaksNatPmp;
    case 2: return ProbeVerdict::SpeaksPcp;
    default: return ProbeVerdict::UnknownVersion;
    }
}

// ---------------------------------------------------------------------------
// Lifetimes
// ---------------------------------------------------------------------------

uint32 renewalDelaySecs(uint32 lifetimeSecs, int attempt, double rand01) noexcept
{
    if (attempt < 0)
        attempt = 0;
    // Cap the exponent: past ~30 the fractions are indistinguishable from 1.0
    // and ldexp would denormalize.
    const int clamped = std::min(attempt, 30);

    const double base = 1.0 - std::ldexp(1.0, -(clamped + 1));   // 1/2, 3/4, 7/8, ...
    const double width = std::ldexp(0.125, -clamped);            // 1/8, 1/16, 1/32, ...
    const double fraction = base + std::clamp(rand01, 0.0, 1.0) * width;

    const double delay = static_cast<double>(lifetimeSecs) * fraction;
    if (delay <= static_cast<double>(kMinRenewGapSecs))
        return kMinRenewGapSecs;
    return static_cast<uint32>(delay);
}

uint32 natPmpRenewalDelaySecs(uint32 lifetimeSecs) noexcept
{
    const uint32 half = lifetimeSecs / 2;
    return half < kMinRenewGapSecs ? kMinRenewGapSecs : half;
}

qint64 nextRetransmitMs(qint64 prevMs, double rand01, qint64 irtMs, qint64 mrtMs) noexcept
{
    const double jitter = (std::clamp(rand01, 0.0, 1.0) * 0.2) - 0.1;   // [-0.1, +0.1]
    const double base = prevMs <= 0 ? static_cast<double>(irtMs)
                                    : static_cast<double>(std::min(2 * prevMs, mrtMs));
    const auto result = static_cast<qint64>((1.0 + jitter) * base);
    return result > 0 ? result : 1;
}

// ---------------------------------------------------------------------------
// Server-reboot detection
// ---------------------------------------------------------------------------

bool EpochTracker::validate(uint32 serverEpochSecs, qint64 clientNowSecs) noexcept
{
    if (!m_seen) {
        m_seen = true;
        m_prevServer = serverEpochSecs;
        m_prevClient = clientNowSecs;
        return true;   // the first response is necessarily valid
    }

    bool valid = true;
    if (static_cast<uint64>(serverEpochSecs) + 1 < static_cast<uint64>(m_prevServer)) {
        // Server time went backwards by more than the tolerated one second of
        // reordering.
        valid = false;
    } else {
        const auto clientDelta = static_cast<uint64>(std::max<qint64>(0, clientNowSecs - m_prevClient));
        // Clamp at zero. Reaching here still allows a one-second backward step
        // (the reordering tolerance above), and RFC 6887 section 8.5's pseudocode
        // subtracts unsigned — so a -1 delta would wrap to 2^64-1 and report a
        // reboot on every reordered packet.
        const qint64 rawServerDelta = static_cast<qint64>(serverEpochSecs)
                                      - static_cast<qint64>(m_prevServer);
        const uint64 serverDelta = rawServerDelta > 0 ? static_cast<uint64>(rawServerDelta) : 0;
        // The +2 absorbs one second of quantization at each end; the /16 allows
        // 6.25% drift, which cheap CPE crystals routinely exhibit.
        if (clientDelta + 2 < serverDelta - serverDelta / 16
            || serverDelta + 2 < clientDelta - clientDelta / 16)
            valid = false;
    }

    m_prevServer = serverEpochSecs;
    m_prevClient = clientNowSecs;
    return valid;
}

bool natPmpEpochIndicatesReboot(uint32 prevSssoe, uint32 currSssoe, qint64 elapsedSecs) noexcept
{
    if (elapsedSecs < 0)
        elapsedSecs = 0;
    const uint64 expected = static_cast<uint64>(prevSssoe)
                            + (static_cast<uint64>(elapsedSecs) * 7) / 8;
    return static_cast<uint64>(currSssoe) + 2 < expected;
}

} // namespace eMule::portmap
