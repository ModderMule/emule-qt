#pragma once
/// @file QuicNatProtocol.h
/// @brief Wire constants and codecs of the QUIC NAT-T dialect (docs/protocol/natt-quic.md).
///
/// Pure functions, no ngtcp2: shared by the transport and the tests.

#include "utils/Types.h"

#include <QByteArray>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string_view>

namespace eMule::quic {

using Hash16 = std::array<uint8, 16>;

/// TLS ALPN, mandatory on both sides.
inline constexpr std::string_view kAlpn = "ed2k-ai-natt-quic-v1";
/// SNI the client sends; nobody checks it.
inline constexpr const char* kSni = "emuleai-natt";

// --- datagram sizes ---
/// QUIC datagram we send, without the 2-byte 0xB2 envelope (also the Initial minimum).
inline constexpr size_t kMaxSendDatagram = 1200;
/// Largest QUIC datagram we accept: peers may probe the path MTU up to here.
inline constexpr size_t kMaxRecvDatagram = 1452;
inline constexpr size_t kCidLen = 8;

// --- transport parameters ---
inline constexpr uint64 kStreamWindow = 512 * 1024;
inline constexpr uint64 kConnWindow = 4 * 1024 * 1024;
inline constexpr uint64 kIdleTimeoutMs = 60'000;
inline constexpr uint64 kKeepAliveMs = 20'000;
inline constexpr uint64 kHandshakeTimeoutMs = 10'000;

// --- handshake punching / obfuscation ---
/// The last handshake datagram is sent again this often while nothing comes back.
inline constexpr uint64 kHandshakeResendMs = 250;
inline constexpr int kHandshakeResendMax = 12;
/// Obfuscated datagrams unanswered this long: go plain (unless strict).
inline constexpr uint64 kPlainFallbackMs = 2000;
/// One key frame per endpoint per this.
inline constexpr uint64 kKeyFrameIntervalMs = 10'000;
/// A key frame's hash is kept this long after last use.
inline constexpr uint64 kKeyFrameTtlMs = 10 * 60'000;
/// A passive expectation lives this long.
inline constexpr uint64 kExpectTtlMs = 20'000;
inline constexpr size_t kMaxConnections = 64;

// --- identity proof: first bytes of each side's stream ---
inline constexpr std::string_view kProofMagic = "EAQN1";
inline constexpr size_t kProofSize = 5 + 16 + 16;

/// "EAQN1" | own hash | peer hash (zeros when not known).
[[nodiscard]] inline std::array<uint8, kProofSize> encodeProof(const Hash16& own, const Hash16& peer)
{
    std::array<uint8, kProofSize> out{};
    std::memcpy(out.data(), kProofMagic.data(), kProofMagic.size());
    std::copy(own.begin(), own.end(), out.begin() + 5);
    std::copy(peer.begin(), peer.end(), out.begin() + 21);
    return out;
}

[[nodiscard]] inline bool isZero(const Hash16& h)
{
    return std::all_of(h.begin(), h.end(), [](uint8 b) { return b == 0; });
}

struct ProofCheck {
    bool ok = false;
    Hash16 sender{};
    const char* reason = "";
};

/// Magic, and the target is us or zero. @p expected non-zero must equal the sender.
[[nodiscard]] inline ProofCheck checkProof(const uint8* data, size_t len, const Hash16& ourHash,
                                           const Hash16& expected)
{
    ProofCheck r;
    if (len < kProofSize || std::memcmp(data, kProofMagic.data(), kProofMagic.size()) != 0) {
        r.reason = "bad proof magic";
        return r;
    }
    Hash16 target{};
    std::copy_n(data + 5, 16, r.sender.begin());
    std::copy_n(data + 21, 16, target.begin());
    if (!isZero(target) && target != ourHash) {
        r.reason = "proof names another client";
        return r;
    }
    if (!isZero(expected) && r.sender != expected) {
        r.reason = "proof from another client";
        return r;
    }
    r.ok = true;
    return r;
}

// --- capability frame (0xB2 0x02 / 0x03) ---
inline constexpr uint32 kCapsMagic = 0x43514145;   // "EAQC" little endian
inline constexpr uint8 kCapsVersion = 1;
inline constexpr size_t kCapsSize = 54;
/// Options bits as in CONNECT_OPT_*; 0x40 = QUIC NAT-T, 0x80 = uTP NAT-T.
inline constexpr uint8 kCapsQuic = 0x40;
inline constexpr uint8 kCapsUtp = 0x80;

struct Caps {
    uint8 options = 0;
    Hash16 sender{};
    Hash16 expected{};
    Hash16 file{};
};

[[nodiscard]] inline QByteArray encodeCaps(const Caps& c)
{
    QByteArray out(static_cast<qsizetype>(kCapsSize), '\0');
    auto* p = reinterpret_cast<uint8*>(out.data());
    p[0] = 0x45; p[1] = 0x41; p[2] = 0x51; p[3] = 0x43;
    p[4] = kCapsVersion;
    p[5] = c.options;
    std::copy(c.sender.begin(), c.sender.end(), p + 6);
    std::copy(c.expected.begin(), c.expected.end(), p + 22);
    std::copy(c.file.begin(), c.file.end(), p + 38);
    return out;
}

/// nullopt for a wrong size, magic or version, or a frame meant for someone else.
[[nodiscard]] inline std::optional<Caps> decodeCaps(const uint8* data, size_t len, const Hash16& ourHash)
{
    if (len < kCapsSize || data[0] != 0x45 || data[1] != 0x41 || data[2] != 0x51 || data[3] != 0x43
        || data[4] != kCapsVersion)
        return std::nullopt;
    Caps c;
    c.options = data[5];
    std::copy_n(data + 6, 16, c.sender.begin());
    std::copy_n(data + 22, 16, c.expected.begin());
    std::copy_n(data + 38, 16, c.file.begin());
    if (!isZero(c.expected) && c.expected != ourHash)
        return std::nullopt;
    return c;
}

/// A long-header QUIC packet (Initial, Handshake, 0-RTT, Retry).
[[nodiscard]] inline bool isLongHeader(const uint8* data, size_t len)
{
    return len > 0 && (data[0] & 0x80) != 0;
}

} // namespace eMule::quic
