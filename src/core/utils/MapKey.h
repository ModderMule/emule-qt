#pragma once

/// @file MapKey.h
/// @brief Hash key types for std::unordered_map, replacing MFC CCKey/CSKey.
///
/// CCKey → HashKeyRef  (non-owning reference to a 16-byte hash)
/// CSKey → HashKeyOwn  (owns a copy of the 16-byte hash)
///
/// Both provide operator== and std::hash specialization for use as
/// keys in std::unordered_map.

#include "Types.h"
#include "OtherFunctions.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>

namespace eMule {

/// Non-owning reference to a 16-byte hash (replaces CCKey).
/// The referenced hash must outlive this object.
class HashKeyRef {
public:
    explicit HashKeyRef(const uint8* key = nullptr) noexcept
        : m_key(key) {}

    [[nodiscard]] const uint8* data() const noexcept { return m_key; }

    friend bool operator==(const HashKeyRef& a, const HashKeyRef& b) noexcept
    {
        return md4equ(a.m_key, b.m_key);
    }

private:
    const uint8* m_key;
};

/// Owning copy of a 16-byte hash (replaces CSKey).
class HashKeyOwn {
public:
    HashKeyOwn() noexcept { m_key.fill(0); }

    explicit HashKeyOwn(const uint8* key) noexcept
    {
        if (key)
            std::memcpy(m_key.data(), key, kMdxDigestSize);
        else
            m_key.fill(0);
    }

    [[nodiscard]] const uint8* data() const noexcept { return m_key.data(); }
    [[nodiscard]] uint8* data() noexcept { return m_key.data(); }

    friend bool operator==(const HashKeyOwn& a, const HashKeyOwn& b) noexcept
    {
        return md4equ(a.m_key.data(), b.m_key.data());
    }

private:
    std::array<uint8, kMdxDigestSize> m_key;
};

} // namespace eMule

// Hash specializations for std::unordered_map
template <>
struct std::hash<eMule::HashKeyRef> {
    std::size_t operator()(const eMule::HashKeyRef& key) const noexcept
    {
        std::size_t h = 1;
        const auto* p = key.data();
        for (int i = 0; i < eMule::kMdxDigestSize; ++i)
            h += static_cast<std::size_t>(p[i] + 1) * static_cast<std::size_t>(i * i + 1);
        return h;
    }
};

template <>
struct std::hash<eMule::HashKeyOwn> {
    std::size_t operator()(const eMule::HashKeyOwn& key) const noexcept
    {
        std::size_t h = 1;
        const auto* p = key.data();
        for (int i = 0; i < eMule::kMdxDigestSize; ++i)
            h += static_cast<std::size_t>(p[i] + 1) * static_cast<std::size_t>(i * i + 1);
        return h;
    }
};
