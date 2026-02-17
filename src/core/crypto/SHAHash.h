#pragma once

/// @file SHAHash.h
/// @brief SHA-1 hasher wrapping QCryptographicHash::Sha1, implementing AICHHashAlgo.
///
/// Replaces the original CSHA class that used CryptoPP.

#include "AICHData.h"

#include <QCryptographicHash>
#include <QString>

#include <array>
#include <cstring>

namespace eMule {

// ---------------------------------------------------------------------------
// Sha1Digest — raw 20-byte digest
// ---------------------------------------------------------------------------

struct Sha1Digest {
    std::array<uint8, 20> b{};

    friend bool operator==(const Sha1Digest& lhs, const Sha1Digest& rhs) noexcept
    {
        return std::memcmp(lhs.b.data(), rhs.b.data(), 20) == 0;
    }

    friend bool operator!=(const Sha1Digest& lhs, const Sha1Digest& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// ---------------------------------------------------------------------------
// ShaHasher — SHA-1 hasher implementing AICHHashAlgo
// ---------------------------------------------------------------------------

class ShaHasher : public AICHHashAlgo {
public:
    ShaHasher()
        : m_sha(QCryptographicHash::Sha1)
    {
    }

    // AICHHashAlgo interface
    void reset() override
    {
        m_hash = Sha1Digest{};
        m_sha.reset();
    }

    void add(const void* data, uint32 length) override
    {
        m_sha.addData(QByteArrayView(static_cast<const char*>(data), static_cast<qsizetype>(length)));
    }

    void finish(AICHHash& hash) override
    {
        finishInternal();
        getHash(hash);
    }

    void getHash(AICHHash& hash) override
    {
        std::memcpy(hash.getRawHash(), m_hash.b.data(), kAICHHashSize);
    }

    // Extended interface
    void finish()
    {
        finishInternal();
    }

    void getHash(Sha1Digest* outHash) const
    {
        *outHash = m_hash;
    }

    [[nodiscard]] QString getHashString(bool urn = false) const
    {
        return hashToString(&m_hash, urn);
    }

    // Static helpers
    [[nodiscard]] static QString hashToString(const Sha1Digest* hashIn, bool urn = false);
    [[nodiscard]] static QString hashToHexString(const Sha1Digest* hashIn, bool urn = false);
    [[nodiscard]] static bool hashFromString(const QString& str, Sha1Digest* hashOut);
    [[nodiscard]] static bool hashFromURN(const QString& str, Sha1Digest* hashOut);
    [[nodiscard]] static bool isNull(const Sha1Digest* hash);

private:
    void finishInternal()
    {
        const QByteArray result = m_sha.result();
        std::memcpy(m_hash.b.data(), result.constData(), 20);
    }

    QCryptographicHash m_sha;
    Sha1Digest m_hash;
};

} // namespace eMule
