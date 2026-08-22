#pragma once

/// @file AesCbc.h
/// @brief AES-256-CBC, both one-shot and streaming, over OpenSSL's EVP layer.
///
/// Two callers with very different shapes share this:
///
///  - Preferences stores the SMTP password as one short base64 blob and wants a
///    single call (encryptToBase64 / decryptFromBase64).
///  - HTTP Cache encrypts a whole 9,728,000-byte part and decrypts it again as
///    it arrives off a socket, so it must feed the cipher in slices and never
///    hold both plaintext and ciphertext in memory at once.
///
/// The streaming classes are the primitive; the one-shot helpers are written on
/// top of them so there is exactly one place where the cipher is configured.
///
/// CBC and resuming: a ciphertext block at offset O (a multiple of kBlockSize)
/// decrypts using the block at O - kBlockSize as its IV. AesCbcDecryptor::reset()
/// exists for that — a downloader that lost its connection re-requests from
/// O - kBlockSize and seeds the decryptor with the first block it receives.

#include "utils/Types.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace eMule {

/// AES-256-CBC parameters. Both sizes are fixed by the cipher, not by policy.
inline constexpr int kAesKeySize = 32;    ///< AES-256
inline constexpr int kAesIvSize = 16;     ///< = AES block size
inline constexpr int kAesBlockSize = 16;

/// Cryptographically strong random bytes. Returns an empty array if the RNG
/// fails, which callers must treat as fatal — never fall back to a weak source.
[[nodiscard]] QByteArray aesRandomBytes(int count);

/// Convenience: a fresh 32-byte key and 16-byte IV.
[[nodiscard]] QByteArray aesRandomKey();
[[nodiscard]] QByteArray aesRandomIv();

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------

/// Incremental AES-256-CBC encryption with PKCS#7 padding.
///
/// Because of the padding, the ciphertext is always longer than the plaintext:
/// for a plaintext that is an exact multiple of the block size (an eMule part
/// is — 9,728,000 = 608,000 * 16) a whole extra block is appended. Use
/// cipherLengthFor() rather than assuming.
class AesCbcEncryptor {
public:
    AesCbcEncryptor();
    ~AesCbcEncryptor();

    AesCbcEncryptor(const AesCbcEncryptor&) = delete;
    AesCbcEncryptor& operator=(const AesCbcEncryptor&) = delete;
    AesCbcEncryptor(AesCbcEncryptor&&) noexcept;
    AesCbcEncryptor& operator=(AesCbcEncryptor&&) noexcept;

    /// Begin a new message. @p key must be kAesKeySize bytes, @p iv kAesIvSize.
    /// Returns false on a bad size or an OpenSSL failure.
    bool begin(const QByteArray& key, const QByteArray& iv);

    /// Feed the next slice of plaintext. Returns the ciphertext produced so far
    /// by this call, which may be empty (CBC only emits whole blocks).
    [[nodiscard]] QByteArray update(const uint8* data, qsizetype size);
    [[nodiscard]] QByteArray update(const QByteArray& data);

    /// Emit the final padded block. The encryptor is unusable until begin() is
    /// called again.
    [[nodiscard]] QByteArray finish();

    [[nodiscard]] bool isValid() const { return m_ctx != nullptr; }

    /// Ciphertext length PKCS#7 produces for @p plainLength bytes.
    [[nodiscard]] static uint64 cipherLengthFor(uint64 plainLength)
    {
        return plainLength + (kAesBlockSize - (plainLength % kAesBlockSize));
    }

private:
    struct Ctx;
    std::unique_ptr<Ctx> m_ctx;
};

/// Incremental AES-256-CBC decryption with PKCS#7 unpadding.
class AesCbcDecryptor {
public:
    AesCbcDecryptor();
    ~AesCbcDecryptor();

    AesCbcDecryptor(const AesCbcDecryptor&) = delete;
    AesCbcDecryptor& operator=(const AesCbcDecryptor&) = delete;
    AesCbcDecryptor(AesCbcDecryptor&&) noexcept;
    AesCbcDecryptor& operator=(AesCbcDecryptor&&) noexcept;

    /// Begin decrypting a message from its start.
    bool begin(const QByteArray& key, const QByteArray& iv);

    /// Begin decrypting from the middle of a message.
    ///
    /// @p precedingCipherBlock is the ciphertext block immediately before the
    /// first byte to be fed in — CBC's chaining value. @p expectPadding must be
    /// false when the caller will stop before the last block, so finish() does
    /// not try to strip padding that was never fed in.
    bool beginAt(const QByteArray& key, const QByteArray& precedingCipherBlock, bool expectPadding);

    [[nodiscard]] QByteArray update(const uint8* data, qsizetype size);
    [[nodiscard]] QByteArray update(const QByteArray& data);

    /// Verify and strip the PKCS#7 padding.
    ///
    /// @param ok set to false when the padding is wrong, which for a fixed-size
    ///           chunk means a wrong key or a corrupted stream. Callers must
    ///           check it: a bad-padding result is not merely empty, it means
    ///           everything already emitted is suspect.
    [[nodiscard]] QByteArray finish(bool* ok = nullptr);

    [[nodiscard]] bool isValid() const { return m_ctx != nullptr; }

private:
    struct Ctx;
    std::unique_ptr<Ctx> m_ctx;
};

// ---------------------------------------------------------------------------
// One-shot
// ---------------------------------------------------------------------------

/// Encrypt @p plaintext with a random IV; returns base64 of `IV || ciphertext`.
/// Empty on failure or when @p key is not kAesKeySize bytes.
[[nodiscard]] QString aesEncryptToBase64(const QString& plaintext, const QByteArray& key);

/// Inverse of aesEncryptToBase64. Empty on any failure, including a wrong key —
/// the caller cannot distinguish "was empty" from "could not decrypt", which is
/// fine for a stored password and wrong for anything that must fail loudly.
[[nodiscard]] QString aesDecryptFromBase64(const QString& base64, const QByteArray& key);

/// Raw one-shot over byte buffers, IV supplied by the caller.
[[nodiscard]] QByteArray aesEncrypt(const QByteArray& plaintext, const QByteArray& key,
                                    const QByteArray& iv);
[[nodiscard]] QByteArray aesDecrypt(const QByteArray& ciphertext, const QByteArray& key,
                                    const QByteArray& iv, bool* ok = nullptr);

} // namespace eMule
