#pragma once

/// @file OtherFunctions.h
/// @brief Core utility functions ported from MFC OtherFunctions.h.
///
/// This header consolidates the portable, non-GUI utilities from the
/// original monolithic OtherFunctions.h: encoding, IP helpers, hash
/// helpers, random numbers, RC4, file-type enums, comparison helpers,
/// and URL processing.

#include "Types.h"

#include <QString>

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <span>
#include <string_view>

namespace eMule {

// ---------------------------------------------------------------------------
// MD4/MD5 constants
// ---------------------------------------------------------------------------
inline constexpr int kMdxBlockSize  = 64;
inline constexpr int kMdxDigestSize = 16;
inline constexpr int kMaxHashStrSize = kMdxDigestSize * 2 + 1;

// ---------------------------------------------------------------------------
// MD4/MD5 inline helpers
// ---------------------------------------------------------------------------

/// Compare two 16-byte hashes for equality (replaces md4equ).
[[nodiscard]] inline bool md4equ(const void* hash1, const void* hash2) noexcept
{
    return std::memcmp(hash1, hash2, kMdxDigestSize) == 0;
}

/// Check if a 16-byte hash is all zeros (replaces isnulmd4).
[[nodiscard]] inline bool isnulmd4(const void* hash) noexcept
{
    const auto* p = static_cast<const uint8*>(hash);
    for (int i = 0; i < kMdxDigestSize; ++i)
        if (p[i] != 0)
            return false;
    return true;
}

/// Clear a 16-byte hash to all zeros (replaces md4clr).
inline void md4clr(void* hash) noexcept
{
    std::memset(hash, 0, kMdxDigestSize);
}

/// Copy a 16-byte hash (replaces md4cpy).
inline void md4cpy(void* dst, const void* src) noexcept
{
    std::memcpy(dst, src, kMdxDigestSize);
}

/// Convert a 16-byte hash to a hex string.
[[nodiscard]] QString md4str(const uint8* hash);

/// Parse a hex string into a 16-byte hash. Returns false on invalid input.
[[nodiscard]] bool strmd4(const QString& str, uint8* hash);

// ---------------------------------------------------------------------------
// Base16 / Base32 encoding
// ---------------------------------------------------------------------------

/// Encode binary data to uppercase hex (base16) string.
[[nodiscard]] QString encodeBase16(std::span<const uint8> data);

/// Decode a hex (base16) string to binary. Returns bytes written, 0 on error.
[[nodiscard]] std::size_t decodeBase16(const QString& hex, uint8* output, std::size_t outputLen);

/// Encode binary data to base32 string.
[[nodiscard]] QString encodeBase32(std::span<const uint8> data);

/// Decode a base32 string to binary. Returns bytes written, 0 on error.
[[nodiscard]] std::size_t decodeBase32(const QString& input, uint8* output, std::size_t outputLen);

// ---------------------------------------------------------------------------
// URL encoding / decoding
// ---------------------------------------------------------------------------

/// URL-encode a string (percent-encoding).
[[nodiscard]] QString urlEncode(const QString& input);

/// URL-decode a percent-encoded string.
[[nodiscard]] QString urlDecode(const QString& input);

/// Encode a URL query parameter (space → '+', special chars → %XX).
[[nodiscard]] QString encodeUrlQueryParam(const QString& query);

// ---------------------------------------------------------------------------
// IP address helpers
// ---------------------------------------------------------------------------

/// Check if an IP (in network byte order) is a valid public IP.
[[nodiscard]] bool isGoodIP(uint32 nIP, bool forceCheck = false);

/// Check if an IP is a LAN/private IP.
[[nodiscard]] bool isLanIP(uint32 nIP);

/// Check if an ID is a LowID (< 2^24).
[[nodiscard]] constexpr bool isLowID(uint32 id) noexcept
{
    return id < 0x01000000u;
}

/// Format an IP address (network byte order) as "a.b.c.d".
[[nodiscard]] QString ipstr(uint32 nIP);

/// Format an IP:port as "a.b.c.d:port".
[[nodiscard]] QString ipstr(uint32 nIP, uint16 nPort);

// ---------------------------------------------------------------------------
// Random number generation
// ---------------------------------------------------------------------------

/// Thread-local random engine access.
std::mt19937& randomEngine();

/// Generate a random uint16.
[[nodiscard]] uint16 getRandomUInt16();

/// Generate a random uint32.
[[nodiscard]] uint32 getRandomUInt32();

// ---------------------------------------------------------------------------
// RC4 encryption
// ---------------------------------------------------------------------------

struct RC4Key {
    std::array<uint8, 256> state{};
    uint8 x = 0;
    uint8 y = 0;
};

/// Create an RC4 key from key data.
RC4Key rc4CreateKey(std::span<const uint8> keyData, bool skipDiscard = false);

/// RC4 encrypt/decrypt from input to output buffer.
void rc4Crypt(const uint8* input, uint8* output, uint32 len, RC4Key& key);

/// RC4 encrypt/decrypt in-place.
void rc4Crypt(uint8* data, uint32 len, RC4Key& key);

// ---------------------------------------------------------------------------
// File type enums
// ---------------------------------------------------------------------------

enum class FileType : uint8 {
    Unknown,
    Executable,
    ArchiveZip,
    ArchiveRar,
    ArchiveAce,
    Archive7z,
    ImageIso,
    AudioMpeg,
    VideoAvi,
    VideoMpg,
    VideoMp4,
    VideoMkv,
    VideoOgg,
    WindowsMedia,
    PicJpg,
    PicPng,
    PicGif,
    DocumentPdf
};

enum class ED2KFileType : uint8 {
    Any = 0,
    Audio = 1,
    Video = 2,
    Image = 3,
    Program = 4,
    Document = 5,
    Archive = 6,
    CDImage = 7,
    EmuleCollection = 8
};

/// Determine the ED2K file type from a file name/extension.
[[nodiscard]] ED2KFileType getED2KFileTypeID(const QString& fileName);

/// Return the ED2K file type string constant for a filename (e.g. "Audio", "Video").
[[nodiscard]] QString getFileTypeByName(const QString& fileName);

// ---------------------------------------------------------------------------
// Comparison helpers (three-way, for sort callbacks)
// ---------------------------------------------------------------------------

/// Three-way comparison for unsigned types.
template <typename T>
[[nodiscard]] constexpr int compareUnsigned(T v0, T v1) noexcept
{
    if (v0 < v1) return -1;
    if (v0 > v1) return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

/// Strip/clean invalid filename characters (replaces StripInvalidFilenameChars).
[[nodiscard]] QString stripInvalidFilenameChars(const QString& text);

/// Limit a string to a maximum length, appending "..." if truncated.
[[nodiscard]] QString stringLimit(const QString& input, int maxLength);

/// Levenshtein edit distance between two strings.
[[nodiscard]] uint32 levenshteinDistance(const QString& str1, const QString& str2);

// ---------------------------------------------------------------------------
// Peek / Poke — unaligned memory access helpers
// ---------------------------------------------------------------------------

[[nodiscard]] inline uint8 peekUInt8(const void* p) noexcept
{
    uint8 v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

[[nodiscard]] inline uint16 peekUInt16(const void* p) noexcept
{
    uint16 v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

[[nodiscard]] inline uint32 peekUInt32(const void* p) noexcept
{
    uint32 v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

[[nodiscard]] inline uint64 peekUInt64(const void* p) noexcept
{
    uint64 v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

inline void pokeUInt8(void* p, uint8 val) noexcept
{
    std::memcpy(p, &val, sizeof val);
}

inline void pokeUInt16(void* p, uint16 val) noexcept
{
    std::memcpy(p, &val, sizeof val);
}

inline void pokeUInt32(void* p, uint32 val) noexcept
{
    std::memcpy(p, &val, sizeof val);
}

inline void pokeUInt64(void* p, uint64 val) noexcept
{
    std::memcpy(p, &val, sizeof val);
}

} // namespace eMule
