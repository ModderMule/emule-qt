#pragma once

/// @file MD5Hash.h
/// @brief MD5 hasher wrapping QCryptographicHash::Md5.
///
/// Replaces the original MD5Sum class that used CryptoPP.

#include "utils/Types.h"

#include <QCryptographicHash>
#include <QString>

#include <array>
#include <cstddef>

namespace eMule {

class MD5Hasher {
public:
    static constexpr int kDigestSize = 16;

    MD5Hasher() = default;

    explicit MD5Hasher(const QString& source)
    {
        calculate(source);
    }

    MD5Hasher(const uint8* data, std::size_t length)
    {
        calculate(data, length);
    }

    void calculate(const uint8* data, std::size_t length)
    {
        const QByteArray result = QCryptographicHash::hash(
            QByteArrayView(reinterpret_cast<const char*>(data), static_cast<qsizetype>(length)),
            QCryptographicHash::Md5);
        std::memcpy(m_digest.data(), result.constData(), kDigestSize);
    }

    void calculate(const QString& source)
    {
        const QByteArray utf8 = source.toUtf8();
        calculate(reinterpret_cast<const uint8*>(utf8.constData()),
                  static_cast<std::size_t>(utf8.size()));
    }

    [[nodiscard]] QString getHashString() const
    {
        QString result;
        result.reserve(kDigestSize * 2);
        for (std::size_t i = 0; i < kDigestSize; ++i) {
            result += QString::asprintf("%02x", m_digest[i]);
        }
        return result;
    }

    [[nodiscard]] const uint8* getRawHash() const noexcept { return m_digest.data(); }

private:
    std::array<uint8, kDigestSize> m_digest{};
};

} // namespace eMule
