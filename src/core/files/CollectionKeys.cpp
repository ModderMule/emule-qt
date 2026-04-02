#include "pch.h"
/// @file CollectionKeys.cpp
/// @brief RSA key management for collection signing/verification.

#include "files/CollectionKeys.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

namespace eMule {

static constexpr int kCollectionKeyBits = 1024;
static constexpr QLatin1StringView kKeyFileName("collectioncryptkey.dat");

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

CollectionKeys::CollectionKeys(const QString& configDir)
    : m_keyPath(QDir(configDir).filePath(kKeyFileName))
{
}

CollectionKeys::~CollectionKeys() = default;

void CollectionKeys::EvpKeyDeleter::operator()(EVP_PKEY* p) const
{
    EVP_PKEY_free(p);
}

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------

bool CollectionKeys::initialize()
{
    QFileInfo fi(m_keyPath);
    if (!fi.exists() || fi.size() == 0) {
        logInfo(QStringLiteral("No collection signing key found — generating new 1024-bit RSA key..."));
        if (!createKeyPair()) {
            logError(QStringLiteral("Collection signing key generation failed"));
            return false;
        }
    }

    if (!loadKeyPair()) {
        logError(QStringLiteral("Failed to load collection signing key from %1").arg(m_keyPath));
        return false;
    }

    logInfo(QStringLiteral("Collection signing key loaded (public key: %1 bytes)")
                .arg(m_publicKeyDer.size()));
    return true;
}

// ---------------------------------------------------------------------------
// signKey
// ---------------------------------------------------------------------------

EVP_PKEY* CollectionKeys::signKey() const
{
    return m_signKey.get();
}

// ---------------------------------------------------------------------------
// createKeyPair
// ---------------------------------------------------------------------------

bool CollectionKeys::createKeyPair()
{
    EVP_PKEY* rawKey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx)
        return false;

    bool ok = EVP_PKEY_keygen_init(ctx) > 0
           && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, kCollectionKeyBits) > 0
           && EVP_PKEY_keygen(ctx, &rawKey) > 0;
    EVP_PKEY_CTX_free(ctx);

    if (!ok || !rawKey) {
        EVP_PKEY_free(rawKey);
        return false;
    }

    // DER-encode the private key
    int derLen = i2d_PrivateKey(rawKey, nullptr);
    if (derLen <= 0) {
        EVP_PKEY_free(rawKey);
        return false;
    }

    QByteArray derBuf(derLen, '\0');
    auto* derPtr = reinterpret_cast<unsigned char*>(derBuf.data());
    i2d_PrivateKey(rawKey, &derPtr);
    EVP_PKEY_free(rawKey);

    // Base64-encode and write to file (matching CryptoPP base64 format)
    QByteArray base64 = derBuf.toBase64();

    QFile file(m_keyPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logError(QStringLiteral("Failed to create collection key file: %1").arg(m_keyPath));
        return false;
    }
    file.write(base64);
    file.close();

    logInfo(QStringLiteral("Collection RSA key pair generated and saved to %1").arg(m_keyPath));
    return true;
}

// ---------------------------------------------------------------------------
// loadKeyPair
// ---------------------------------------------------------------------------

bool CollectionKeys::loadKeyPair()
{
    QFile file(m_keyPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QByteArray base64Data = file.readAll();
    file.close();

    QByteArray derData = QByteArray::fromBase64(base64Data);
    if (derData.isEmpty())
        return false;

    // Parse the private key from DER
    const auto* derPtr = reinterpret_cast<const unsigned char*>(derData.constData());
    EVP_PKEY* rawKey = d2i_PrivateKey(EVP_PKEY_RSA, nullptr, &derPtr, derData.size());
    if (!rawKey)
        return false;

    m_signKey.reset(rawKey);

    // Extract the public key in DER format
    int pubLen = i2d_PUBKEY(m_signKey.get(), nullptr);
    if (pubLen <= 0) {
        m_signKey.reset();
        return false;
    }

    m_publicKeyDer.resize(pubLen);
    auto* pubPtr = reinterpret_cast<unsigned char*>(m_publicKeyDer.data());
    i2d_PUBKEY(m_signKey.get(), &pubPtr);

    return true;
}

// ---------------------------------------------------------------------------
// verifySignature (static)
// ---------------------------------------------------------------------------

bool CollectionKeys::verifySignature(const QByteArray& message,
                                     const QByteArray& signature,
                                     const QByteArray& publicKeyDer)
{
    if (publicKeyDer.isEmpty() || signature.isEmpty())
        return false;

    const auto* keyData = reinterpret_cast<const unsigned char*>(publicKeyDer.constData());
    EVP_PKEY* pubKey = d2i_PUBKEY(nullptr, &keyData, publicKeyDer.size());
    if (!pubKey)
        return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = false;
    if (ctx) {
        if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pubKey) > 0
            && EVP_DigestVerifyUpdate(ctx, message.constData(),
                                     static_cast<size_t>(message.size())) > 0
            && EVP_DigestVerifyFinal(ctx,
                                    reinterpret_cast<const unsigned char*>(signature.constData()),
                                    static_cast<size_t>(signature.size())) == 1)
        {
            ok = true;
        }
        EVP_MD_CTX_free(ctx);
    }
    EVP_PKEY_free(pubKey);
    return ok;
}

} // namespace eMule
