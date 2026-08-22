#include "crypto/AesCbc.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace eMule {

namespace {

const unsigned char* uc(const QByteArray& b)
{
    return reinterpret_cast<const unsigned char*>(b.constData());
}

unsigned char* uc(QByteArray& b)
{
    return reinterpret_cast<unsigned char*>(b.data());
}

} // namespace

// ---------------------------------------------------------------------------
// Random
// ---------------------------------------------------------------------------

QByteArray aesRandomBytes(int count)
{
    if (count <= 0)
        return {};

    QByteArray out(count, Qt::Uninitialized);
    if (RAND_bytes(uc(out), count) != 1)
        return {}; // RNG failure is fatal — never substitute a weak source

    return out;
}

QByteArray aesRandomKey()
{
    return aesRandomBytes(kAesKeySize);
}

QByteArray aesRandomIv()
{
    return aesRandomBytes(kAesIvSize);
}

// ---------------------------------------------------------------------------
// AesCbcEncryptor
// ---------------------------------------------------------------------------

struct AesCbcEncryptor::Ctx {
    EVP_CIPHER_CTX* ctx = nullptr;

    ~Ctx()
    {
        if (ctx)
            EVP_CIPHER_CTX_free(ctx);
    }
};

AesCbcEncryptor::AesCbcEncryptor() = default;
AesCbcEncryptor::~AesCbcEncryptor() = default;
AesCbcEncryptor::AesCbcEncryptor(AesCbcEncryptor&&) noexcept = default;
AesCbcEncryptor& AesCbcEncryptor::operator=(AesCbcEncryptor&&) noexcept = default;

bool AesCbcEncryptor::begin(const QByteArray& key, const QByteArray& iv)
{
    m_ctx.reset();

    if (key.size() != kAesKeySize || iv.size() != kAesIvSize)
        return false;

    auto ctx = std::make_unique<Ctx>();
    ctx->ctx = EVP_CIPHER_CTX_new();
    if (!ctx->ctx)
        return false;

    if (EVP_EncryptInit_ex(ctx->ctx, EVP_aes_256_cbc(), nullptr, uc(key), uc(iv)) != 1)
        return false;

    m_ctx = std::move(ctx);
    return true;
}

QByteArray AesCbcEncryptor::update(const uint8* data, qsizetype size)
{
    if (!m_ctx || size <= 0)
        return {};

    // EVP may hold back up to one block and emit it later, so the output buffer
    // has to allow for a full extra block.
    QByteArray out(size + kAesBlockSize, Qt::Uninitialized);
    int written = 0;

    if (EVP_EncryptUpdate(m_ctx->ctx, uc(out), &written,
                          reinterpret_cast<const unsigned char*>(data),
                          static_cast<int>(size)) != 1) {
        m_ctx.reset();
        return {};
    }

    out.resize(written);
    return out;
}

QByteArray AesCbcEncryptor::update(const QByteArray& data)
{
    return update(reinterpret_cast<const uint8*>(data.constData()), data.size());
}

QByteArray AesCbcEncryptor::finish()
{
    if (!m_ctx)
        return {};

    QByteArray out(kAesBlockSize, Qt::Uninitialized);
    int written = 0;

    const bool good = EVP_EncryptFinal_ex(m_ctx->ctx, uc(out), &written) == 1;
    m_ctx.reset();

    if (!good)
        return {};

    out.resize(written);
    return out;
}

// ---------------------------------------------------------------------------
// AesCbcDecryptor
// ---------------------------------------------------------------------------

struct AesCbcDecryptor::Ctx {
    EVP_CIPHER_CTX* ctx = nullptr;
    bool expectPadding = true;

    ~Ctx()
    {
        if (ctx)
            EVP_CIPHER_CTX_free(ctx);
    }
};

AesCbcDecryptor::AesCbcDecryptor() = default;
AesCbcDecryptor::~AesCbcDecryptor() = default;
AesCbcDecryptor::AesCbcDecryptor(AesCbcDecryptor&&) noexcept = default;
AesCbcDecryptor& AesCbcDecryptor::operator=(AesCbcDecryptor&&) noexcept = default;

bool AesCbcDecryptor::begin(const QByteArray& key, const QByteArray& iv)
{
    return beginAt(key, iv, true);
}

bool AesCbcDecryptor::beginAt(const QByteArray& key, const QByteArray& precedingCipherBlock,
                              bool expectPadding)
{
    m_ctx.reset();

    if (key.size() != kAesKeySize || precedingCipherBlock.size() != kAesIvSize)
        return false;

    auto ctx = std::make_unique<Ctx>();
    ctx->ctx = EVP_CIPHER_CTX_new();
    if (!ctx->ctx)
        return false;

    if (EVP_DecryptInit_ex(ctx->ctx, EVP_aes_256_cbc(), nullptr, key.isEmpty() ? nullptr : uc(key),
                           uc(precedingCipherBlock)) != 1)
        return false;

    // Stopping short of the last block means there is no padding to strip; asking
    // OpenSSL to strip it anyway makes the final partial block disappear.
    ctx->expectPadding = expectPadding;
    if (!expectPadding)
        EVP_CIPHER_CTX_set_padding(ctx->ctx, 0);

    m_ctx = std::move(ctx);
    return true;
}

QByteArray AesCbcDecryptor::update(const uint8* data, qsizetype size)
{
    if (!m_ctx || size <= 0)
        return {};

    QByteArray out(size + kAesBlockSize, Qt::Uninitialized);
    int written = 0;

    if (EVP_DecryptUpdate(m_ctx->ctx, uc(out), &written,
                          reinterpret_cast<const unsigned char*>(data),
                          static_cast<int>(size)) != 1) {
        m_ctx.reset();
        return {};
    }

    out.resize(written);
    return out;
}

QByteArray AesCbcDecryptor::update(const QByteArray& data)
{
    return update(reinterpret_cast<const uint8*>(data.constData()), data.size());
}

QByteArray AesCbcDecryptor::finish(bool* ok)
{
    if (!m_ctx) {
        if (ok)
            *ok = false;
        return {};
    }

    QByteArray out(kAesBlockSize, Qt::Uninitialized);
    int written = 0;

    const bool good = EVP_DecryptFinal_ex(m_ctx->ctx, uc(out), &written) == 1;
    m_ctx.reset();

    if (ok)
        *ok = good;

    if (!good)
        return {};

    out.resize(written);
    return out;
}

// ---------------------------------------------------------------------------
// One-shot
// ---------------------------------------------------------------------------

QByteArray aesEncrypt(const QByteArray& plaintext, const QByteArray& key, const QByteArray& iv)
{
    AesCbcEncryptor enc;
    if (!enc.begin(key, iv))
        return {};

    QByteArray out = enc.update(plaintext);
    out.append(enc.finish());
    return out;
}

QByteArray aesDecrypt(const QByteArray& ciphertext, const QByteArray& key, const QByteArray& iv,
                      bool* ok)
{
    AesCbcDecryptor dec;
    if (!dec.begin(key, iv)) {
        if (ok)
            *ok = false;
        return {};
    }

    QByteArray out = dec.update(ciphertext);

    bool finalOk = false;
    out.append(dec.finish(&finalOk));

    if (ok)
        *ok = finalOk;

    return finalOk ? out : QByteArray{};
}

QString aesEncryptToBase64(const QString& plaintext, const QByteArray& key)
{
    if (plaintext.isEmpty() || key.size() != kAesKeySize)
        return {};

    const QByteArray iv = aesRandomIv();
    if (iv.isEmpty())
        return {};

    const QByteArray ct = aesEncrypt(plaintext.toUtf8(), key, iv);
    if (ct.isEmpty())
        return {};

    QByteArray combined = iv;
    combined.append(ct);

    return QString::fromLatin1(combined.toBase64());
}

QString aesDecryptFromBase64(const QString& base64, const QByteArray& key)
{
    if (base64.isEmpty() || key.size() != kAesKeySize)
        return {};

    const QByteArray raw = QByteArray::fromBase64(base64.toLatin1());
    if (raw.size() < kAesIvSize + kAesBlockSize)
        return {};

    bool ok = false;
    const QByteArray pt = aesDecrypt(raw.mid(kAesIvSize), key, raw.left(kAesIvSize), &ok);

    return ok ? QString::fromUtf8(pt) : QString{};
}

} // namespace eMule
