#pragma once

/// @file MD4Hash.h
/// @brief MD4 incremental hasher wrapping QCryptographicHash::Md4.
///
/// Replaces the original CMD4 class that used CryptoPP.

#include "utils/Types.h"

#include <QCryptographicHash>

#include <array>
#include <cstddef>
#include <span>

namespace eMule {

class MD4Hasher {
public:
    static constexpr int kDigestSize = 16;

    MD4Hasher()
        : m_hasher(QCryptographicHash::Md4)
    {
    }

    void reset()
    {
        m_hasher.reset();
        m_digest.fill(0);
    }

    void add(const void* data, std::size_t length)
    {
        m_hasher.addData(QByteArrayView(static_cast<const char*>(data), static_cast<qsizetype>(length)));
    }

    void add(std::span<const uint8> data)
    {
        m_hasher.addData(QByteArrayView(reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size())));
    }

    void finish()
    {
        const QByteArray result = m_hasher.result();
        std::memcpy(m_digest.data(), result.constData(), kDigestSize);
    }

    [[nodiscard]] const uint8* getHash() const noexcept { return m_digest.data(); }

private:
    QCryptographicHash m_hasher;
    std::array<uint8, kDigestSize> m_digest{};
};

} // namespace eMule
