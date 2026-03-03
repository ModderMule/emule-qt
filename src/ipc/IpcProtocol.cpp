/// @file IpcProtocol.cpp
/// @brief IPC wire protocol framing implementation.

#include "IpcProtocol.h"

#include <QCborStreamReader>
#include <QCborStreamWriter>
#include <QCborValue>
#include <QCryptographicHash>
#include <QtEndian>

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace eMule::Ipc {

// ---------------------------------------------------------------------------
// encodeFrame
// ---------------------------------------------------------------------------

QByteArray encodeFrame(const QByteArray& cborPayload)
{
    QByteArray frame;
    frame.reserve(FrameHeaderSize + cborPayload.size());

    // Write big-endian uint32 length prefix
    const auto len = static_cast<uint32_t>(cborPayload.size());
    uint8_t header[FrameHeaderSize];
    qToBigEndian(len, header);
    frame.append(reinterpret_cast<const char*>(header), FrameHeaderSize);

    frame.append(cborPayload);
    return frame;
}

QByteArray encodeFrame(const QCborArray& message)
{
    return encodeFrame(message.toCborValue().toCbor());
}

// ---------------------------------------------------------------------------
// tryDecodeFrame
// ---------------------------------------------------------------------------

std::optional<DecodeResult> tryDecodeFrame(const QByteArray& buffer)
{
    if (buffer.size() < FrameHeaderSize)
        return std::nullopt;

    // Read big-endian uint32 payload length
    const auto payloadLen = qFromBigEndian<uint32_t>(
        reinterpret_cast<const uint8_t*>(buffer.constData()));

    if (payloadLen > MaxPayloadSize)
        return std::nullopt;  // Oversized — caller should disconnect

    const int totalLen = FrameHeaderSize + static_cast<int>(payloadLen);
    if (buffer.size() < totalLen)
        return std::nullopt;  // Incomplete frame

    // Extract and decode CBOR payload
    const QByteArray payload = buffer.mid(FrameHeaderSize, static_cast<int>(payloadLen));
    QCborValue value = QCborValue::fromCbor(payload);

    if (!value.isArray())
        return std::nullopt;  // Protocol violation

    return DecodeResult{value.toArray(), totalLen};
}

// ---------------------------------------------------------------------------
// tryExtractRawFrame
// ---------------------------------------------------------------------------

std::optional<RawFrameResult> tryExtractRawFrame(const QByteArray& buffer)
{
    if (buffer.size() < FrameHeaderSize)
        return std::nullopt;

    const auto payloadLen = qFromBigEndian<uint32_t>(
        reinterpret_cast<const uint8_t*>(buffer.constData()));

    if (payloadLen > MaxPayloadSize)
        return std::nullopt;

    const int totalLen = FrameHeaderSize + static_cast<int>(payloadLen);
    if (buffer.size() < totalLen)
        return std::nullopt;

    return RawFrameResult{buffer.mid(FrameHeaderSize, static_cast<int>(payloadLen)), totalLen};
}

// ---------------------------------------------------------------------------
// AES-256-CBC helpers
// ---------------------------------------------------------------------------

QByteArray deriveAesKey(const QString& token)
{
    return QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256);
}

QByteArray aesEncryptPayload(const QByteArray& plaintext, const QByteArray& key)
{
    if (key.size() != 32)
        return {};

    // Generate random 16-byte IV
    unsigned char iv[16];
    if (RAND_bytes(iv, sizeof(iv)) != 1)
        return {};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};

    QByteArray result;
    result.append(reinterpret_cast<const char*>(iv), 16);

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    // Output buffer: plaintext size + one block for padding
    const int maxOut = plaintext.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc());
    QByteArray ciphertext(maxOut, Qt::Uninitialized);

    int outLen = 0;
    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()),
                          &outLen, reinterpret_cast<const unsigned char*>(plaintext.constData()),
                          plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()) + outLen,
                            &finalLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    EVP_CIPHER_CTX_free(ctx);
    ciphertext.resize(outLen + finalLen);
    result.append(ciphertext);
    return result;
}

QByteArray aesDecryptPayload(const QByteArray& data, const QByteArray& key)
{
    if (key.size() != 32 || data.size() < 16)
        return {};

    const unsigned char* iv = reinterpret_cast<const unsigned char*>(data.constData());
    const unsigned char* ciphertext = reinterpret_cast<const unsigned char*>(data.constData()) + 16;
    const int ciphertextLen = data.size() - 16;

    if (ciphertextLen <= 0)
        return {};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    QByteArray plaintext(ciphertextLen + EVP_CIPHER_block_size(EVP_aes_256_cbc()), Qt::Uninitialized);

    int outLen = 0;
    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plaintext.data()),
                          &outLen, ciphertext, ciphertextLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    int finalLen = 0;
    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plaintext.data()) + outLen,
                            &finalLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(outLen + finalLen);
    return plaintext;
}

} // namespace eMule::Ipc
