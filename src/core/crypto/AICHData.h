#pragma once

/// @file AICHData.h
/// @brief Core AICH types: hash container, hash algorithm interface, status enum.
///
/// Replaces parts of the original SHAHashSet.h (CAICHHash, CAICHHashAlgo, EAICHStatus).

#include "utils/Types.h"
#include "utils/OtherFunctions.h"

#include <QString>

#include <algorithm>
#include <array>
#include <cstring>

namespace eMule {

class FileDataIO;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
inline constexpr uint32 kAICHHashSize = 20;
inline constexpr auto kKnown2MetFilename = u"known2_64.met";
inline constexpr uint8 kKnown2MetVersion = 0x02;

// ---------------------------------------------------------------------------
// EAICHStatus
// ---------------------------------------------------------------------------

enum class EAICHStatus : uint8 {
    Error          = 0,
    Empty          = 1,
    Untrusted      = 2,
    Trusted        = 3,
    Verified       = 4,
    HashSetComplete = 5
};

// ---------------------------------------------------------------------------
// AICHHash — 20-byte SHA-1 hash container
// ---------------------------------------------------------------------------

class AICHHash {
public:
    AICHHash() noexcept
    {
        m_buffer.fill(0);
    }

    explicit AICHHash(FileDataIO& file)
    {
        read(file);
    }

    explicit AICHHash(const uint8* data) noexcept
    {
        std::memcpy(m_buffer.data(), data, kAICHHashSize);
    }

    void read(FileDataIO& file);
    void write(FileDataIO& file) const;
    static void skip(qint64 distance, FileDataIO& file);

    void read(const uint8* data) noexcept
    {
        std::memcpy(m_buffer.data(), data, kAICHHashSize);
    }

    [[nodiscard]] QString getString() const
    {
        return encodeBase32(std::span<const uint8>(m_buffer.data(), kAICHHashSize));
    }

    [[nodiscard]] uint8* getRawHash() noexcept { return m_buffer.data(); }
    [[nodiscard]] const uint8* getRawHash() const noexcept { return m_buffer.data(); }

    [[nodiscard]] static constexpr unsigned getHashSize() noexcept { return kAICHHashSize; }

    friend bool operator==(const AICHHash& a, const AICHHash& b) noexcept
    {
        return std::memcmp(a.m_buffer.data(), b.m_buffer.data(), kAICHHashSize) == 0;
    }

    friend bool operator!=(const AICHHash& a, const AICHHash& b) noexcept
    {
        return !(a == b);
    }

private:
    std::array<uint8, kAICHHashSize> m_buffer;
};

// ---------------------------------------------------------------------------
// AICHHashAlgo — abstract hasher interface
// ---------------------------------------------------------------------------

class AICHHashAlgo {
public:
    virtual ~AICHHashAlgo() = default;

    virtual void reset() = 0;
    virtual void add(const void* data, uint32 length) = 0;
    virtual void finish(AICHHash& hash) = 0;
    virtual void getHash(AICHHash& hash) = 0;
};

} // namespace eMule

// std::hash specialization for use in unordered containers
template <>
struct std::hash<eMule::AICHHash> {
    std::size_t operator()(const eMule::AICHHash& h) const noexcept
    {
        // Use first 8 bytes of the 20-byte hash as size_t (excellent distribution)
        std::size_t result{};
        std::memcpy(&result, h.getRawHash(), sizeof(result));
        return result;
    }
};
