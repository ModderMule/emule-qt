/// @file ClientCredits.cpp
/// @brief Credit system + clients.met persistence — replaces MFC CClientCredits + CClientCreditsList.

#include "client/ClientCredits.h"
#include "prefs/Preferences.h"
#include "server/ServerConnect.h"
#include "app/AppContext.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"
#include "utils/Opcodes.h"
#include "utils/Log.h"

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#include <QDir>
#include <QFile>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace eMule {

// ---------------------------------------------------------------------------
// ClientCredits — construction
// ---------------------------------------------------------------------------

ClientCredits::ClientCredits(const CreditStruct& credits, const ClientCreditsList* owner)
    : m_credits(credits)
    , m_creditsList(owner)
{
    initializeIdent();
    clearWaitStartTime();
    m_waitTimeIP = 0;
}

ClientCredits::ClientCredits(const uint8* userHash, const ClientCreditsList* owner)
    : m_creditsList(owner)
{
    md4cpy(m_credits.key.data(), userHash);
    initializeIdent();
    // Initialize wait times to "now" — using a simple counter since these are
    // relative comparison values (matching MFC GetTickCount() usage)
    auto now = static_cast<uint32>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() & 0xFFFFFFFF);
    m_secureWaitTime = now;
    m_unsecureWaitTime = now;
    m_waitTimeIP = 0;
}

// ---------------------------------------------------------------------------
// Credit accumulation
// ---------------------------------------------------------------------------

void ClientCredits::addDownloaded(uint32 bytes, uint32 forIP)
{
    // When crypto is available and identity is bad, refuse credits
    switch (currentIdentState(forIP)) {
    case IdentState::IdFailed:
    case IdentState::IdBadGuy:
    case IdentState::IdNeeded:
        if (m_creditsList && m_creditsList->cryptoAvailable())
            return;
        break;
    default:
        break;
    }

    uint64 current = downloadedTotal() + bytes;
    m_credits.downloadedLo = static_cast<uint32>(current & 0xFFFFFFFF);
    m_credits.downloadedHi = static_cast<uint32>(current >> 32);
}

void ClientCredits::addUploaded(uint32 bytes, uint32 forIP)
{
    switch (currentIdentState(forIP)) {
    case IdentState::IdFailed:
    case IdentState::IdBadGuy:
    case IdentState::IdNeeded:
        if (m_creditsList && m_creditsList->cryptoAvailable())
            return;
        break;
    default:
        break;
    }

    uint64 current = uploadedTotal() + bytes;
    m_credits.uploadedLo = static_cast<uint32>(current & 0xFFFFFFFF);
    m_credits.uploadedHi = static_cast<uint32>(current >> 32);
}

uint64 ClientCredits::uploadedTotal() const
{
    return (static_cast<uint64>(m_credits.uploadedHi) << 32) | m_credits.uploadedLo;
}

uint64 ClientCredits::downloadedTotal() const
{
    return (static_cast<uint64>(m_credits.downloadedHi) << 32) | m_credits.downloadedLo;
}

// ---------------------------------------------------------------------------
// Score ratio — the credit formula
// ---------------------------------------------------------------------------

float ClientCredits::scoreRatio(uint32 forIP) const
{
    // Identity check (when crypto is available, bad-ident clients get no credits)
    switch (currentIdentState(forIP)) {
    case IdentState::IdFailed:
    case IdentState::IdBadGuy:
    case IdentState::IdNeeded:
        if (m_creditsList && m_creditsList->cryptoAvailable())
            return 1.0f;
        break;
    default:
        break;
    }

    if (downloadedTotal() < 1048576)
        return 1.0f;

    float result;
    if (uploadedTotal() != 0)
        result = static_cast<float>(downloadedTotal() * 2) / static_cast<float>(uploadedTotal());
    else
        result = 10.0f;

    // Exponential max based on downloaded data (9.2MB → 3.34, 100MB → 10.0)
    float result2 = std::sqrt(static_cast<float>(downloadedTotal()) / 1048576.0f + 2.0f);

    // Linear ramp for the first chunk (1MB → 1.01, 9.2MB → 3.34)
    float result3;
    if (downloadedTotal() < 9646899)
        result3 = static_cast<float>(downloadedTotal() - 1048576) / 8598323.0f * 2.34f + 1.0f;
    else
        result3 = 10.0f;

    result = std::min(result, std::min(result2, result3));

    if (result < 1.0f)
        return 1.0f;
    return std::min(result, 10.0f);
}

// ---------------------------------------------------------------------------
// Identity state machine
// ---------------------------------------------------------------------------

void ClientCredits::initializeIdent()
{
    if (m_credits.keySize == 0) {
        m_publicKey.fill(0);
        m_publicKeyLen = 0;
        m_identState = IdentState::NotAvailable;
    } else {
        m_publicKeyLen = m_credits.keySize;
        std::memcpy(m_publicKey.data(), m_credits.secureIdent.data(), m_publicKeyLen);
        m_identState = IdentState::IdNeeded;
    }
    cryptRndChallengeFor = 0;
    cryptRndChallengeFrom = 0;
    m_identIP = 0;
}

void ClientCredits::verified(uint32 forIP)
{
    m_identIP = forIP;
    // Copy key to persistent struct if not already done
    if (m_credits.keySize == 0) {
        m_credits.keySize = m_publicKeyLen;
        std::memcpy(m_credits.secureIdent.data(), m_publicKey.data(), m_publicKeyLen);
        if (downloadedTotal() > 0) {
            // For security: delete all prior credits
            m_credits.downloadedHi = 0;
            m_credits.downloadedLo = 1;
            m_credits.uploadedHi = 0;
            m_credits.uploadedLo = 1;
        }
    }
    m_identState = IdentState::Identified;
}

bool ClientCredits::setSecureIdent(const uint8* ident, uint8 identLen)
{
    if (identLen > kMaxPubKeySize || m_credits.keySize != 0)
        return false;
    std::memcpy(m_publicKey.data(), ident, identLen);
    m_publicKeyLen = identLen;
    m_identState = IdentState::IdNeeded;
    return true;
}

IdentState ClientCredits::currentIdentState(uint32 forIP) const
{
    if (m_identState != IdentState::Identified)
        return m_identState;
    if (forIP == m_identIP)
        return IdentState::Identified;
    return IdentState::IdBadGuy;
}

// ---------------------------------------------------------------------------
// Wait time management
// ---------------------------------------------------------------------------

uint32 ClientCredits::secureWaitStartTime(uint32 forIP)
{
    if (m_unsecureWaitTime == 0 || m_secureWaitTime == 0)
        setSecWaitStartTime(forIP);

    if (m_credits.keySize != 0) {
        if (currentIdentState(forIP) == IdentState::Identified)
            return m_secureWaitTime;

        if (forIP == m_waitTimeIP)
            return m_unsecureWaitTime;

        // IP changed — reset wait time
        auto now = static_cast<uint32>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() & 0xFFFFFFFF);
        m_unsecureWaitTime = now;
        m_waitTimeIP = forIP;
    }
    return m_unsecureWaitTime;
}

void ClientCredits::setSecWaitStartTime(uint32 forIP)
{
    auto now = static_cast<uint32>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() & 0xFFFFFFFF);
    m_unsecureWaitTime = now - 1;
    m_secureWaitTime = m_unsecureWaitTime;
    m_waitTimeIP = forIP;
}

void ClientCredits::clearWaitStartTime()
{
    m_unsecureWaitTime = 0;
    m_secureWaitTime = 0;
}

// ---------------------------------------------------------------------------
// ClientCreditsList — persistence
// ---------------------------------------------------------------------------

bool ClientCreditsList::loadList(const QString& filePath)
{
    SafeFile file;
    if (!file.open(filePath, QIODevice::ReadOnly))
        return false;

    try {
        uint8 version = file.readUInt8();
        if (version != CREDITFILE_VERSION && version != CREDITFILE_VERSION_29) {
            logWarning(QStringLiteral("Credit file has unsupported version: %1").arg(version));
            return false;
        }

        uint32 count = file.readUInt32();
        const auto expired = static_cast<uint32>(std::time(nullptr) - DAY2S(150));
        uint32 deleted = 0;

        for (uint32 i = 0; i < count; ++i) {
            CreditStruct cs{};
            std::size_t readSize = (version == CREDITFILE_VERSION_29)
                ? sizeof(CreditStruct_29a)
                : sizeof(CreditStruct);
            file.read(&cs, static_cast<qint64>(readSize));

            if (cs.lastSeen < expired) {
                ++deleted;
            } else {
                auto credits = std::make_unique<ClientCredits>(cs, this);
                HashKeyOwn hk(credits->key());
                m_clients[hk] = std::move(credits);
            }
        }

        if (deleted > 0)
            logInfo(QStringLiteral("Loaded %1 credits, %2 expired entries skipped")
                        .arg(count - deleted).arg(deleted));
        else
            logInfo(QStringLiteral("Loaded %1 credits").arg(count));

        return true;
    } catch (const FileException& e) {
        logError(QStringLiteral("Error reading credit file: %1").arg(e.what()));
        return false;
    }
}

bool ClientCreditsList::saveList(const QString& filePath) const
{
    SafeFile file;
    if (!file.open(filePath, QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    try {
        file.writeUInt8(CREDITFILE_VERSION);

        // Count entries with actual transfer data
        uint32 count = 0;
        for (auto& [key, credit] : m_clients) {
            if (credit->uploadedTotal() || credit->downloadedTotal())
                ++count;
        }

        file.writeUInt32(count);

        for (auto& [key, credit] : m_clients) {
            if (credit->uploadedTotal() || credit->downloadedTotal())
                file.write(&credit->m_credits, sizeof(CreditStruct));
        }

        return true;
    } catch (const FileException& e) {
        logError(QStringLiteral("Error saving credit file: %1").arg(e.what()));
        return false;
    }
}

ClientCredits* ClientCreditsList::getCredit(const uint8* userHash)
{
    HashKeyOwn hk(userHash);
    auto it = m_clients.find(hk);
    if (it == m_clients.end()) {
        auto credits = std::make_unique<ClientCredits>(userHash, this);
        auto* ptr = credits.get();
        m_clients[HashKeyOwn(ptr->key())] = std::move(credits);
        ptr->setLastSeen();
        return ptr;
    }
    it->second->setLastSeen();
    return it->second.get();
}

void ClientCreditsList::process(const QString& filePath)
{
    auto now = std::chrono::steady_clock::now();
    if (now >= m_lastSaved + std::chrono::minutes(13)) {
        saveList(filePath);
        m_lastSaved = now;
    }
}

// ---------------------------------------------------------------------------
// ClientCreditsList — RSA secure identity
// ---------------------------------------------------------------------------

void ClientCreditsList::EvpKeyDeleter::operator()(EVP_PKEY* p) const
{
    EVP_PKEY_free(p);
}

ClientCreditsList::ClientCreditsList()
{
    initializeCrypting();
}

ClientCreditsList::~ClientCreditsList() = default;

bool ClientCreditsList::createKeyPair(const QString& keyPath)
{
    // Original eMule uses RSA-384 (RSAKEYSIZE) via Crypto++. OpenSSL 3.x has a
    // hard-coded minimum of 512 bits (RSA_MIN_MODULUS_BITS) that cannot be
    // overridden. We try 384 first for Crypto++-built peers, then fall back to
    // 512 which still fits within kMaxPubKeySize (80 bytes, RSA-512 DER ≈ 74).
    // Peers verify with generic RSA, so either key size is wire-compatible.
    static constexpr int kFallbackKeySize = 512;
    int keyBits = RSAKEYSIZE;
    EVP_PKEY* rawKey = nullptr;

    for (int attempt = 0; attempt < 2; ++attempt) {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        if (!ctx)
            return false;

        bool ok = EVP_PKEY_keygen_init(ctx) > 0
               && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, keyBits) > 0
               && EVP_PKEY_keygen(ctx, &rawKey) > 0;
        EVP_PKEY_CTX_free(ctx);

        if (ok && rawKey)
            break;

        EVP_PKEY_free(rawKey);
        rawKey = nullptr;

        if (keyBits == kFallbackKeySize) {
            logError(QStringLiteral("Failed to generate RSA key pair"));
            return false;
        }
        keyBits = kFallbackKeySize;
    }

    // DER-encode the private key
    int derLen = i2d_PrivateKey(rawKey, nullptr);
    if (derLen <= 0) {
        EVP_PKEY_free(rawKey);
        return false;
    }

    std::vector<uint8> derBuf(static_cast<std::size_t>(derLen));
    uint8* derPtr = derBuf.data();
    i2d_PrivateKey(rawKey, &derPtr);
    EVP_PKEY_free(rawKey);

    // Base64-encode and write to file (matching Crypto++ format)
    QByteArray derData(reinterpret_cast<const char*>(derBuf.data()), derLen);
    QByteArray base64 = derData.toBase64();

    QFile file(keyPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logError(QStringLiteral("Failed to create key file: %1").arg(keyPath));
        return false;
    }
    file.write(base64);
    file.close();

    logInfo(QStringLiteral("RSA key pair generated and saved to %1").arg(keyPath));
    return true;
}

void ClientCreditsList::initializeCrypting()
{
    if (!thePrefs.useSecureIdent()) {
        logInfo(QStringLiteral("Secure identification disabled in preferences"));
        return;
    }

    const QString configDir = thePrefs.configDir();
    if (configDir.isEmpty())
        return;
    const QString keyPath = QDir(configDir).filePath(QStringLiteral("cryptkey.dat"));

    // Generate key pair if file doesn't exist or is empty
    QFileInfo fi(keyPath);
    if (!fi.exists() || fi.size() == 0) {
        logInfo(QStringLiteral("No RSA key found, generating new key pair..."));
        if (!createKeyPair(keyPath)) {
            logError(QStringLiteral("RSA key pair generation failed — secure ident disabled"));
            return;
        }
    }

    // Load the private key from base64-DER file
    QFile file(keyPath);
    if (!file.open(QIODevice::ReadOnly)) {
        logError(QStringLiteral("Cannot open key file: %1").arg(keyPath));
        return;
    }

    QByteArray base64Data = file.readAll();
    file.close();

    QByteArray derData = QByteArray::fromBase64(base64Data);
    if (derData.isEmpty()) {
        logError(QStringLiteral("Key file is empty or invalid base64"));
        return;
    }

    // Parse the private key from DER
    const uint8* derPtr = reinterpret_cast<const uint8*>(derData.constData());
    EVP_PKEY* rawKey = d2i_PrivateKey(EVP_PKEY_RSA, nullptr, &derPtr, derData.size());
    if (!rawKey) {
        logError(QStringLiteral("Failed to load RSA private key from %1").arg(keyPath));
        return;
    }

    m_signKey.reset(rawKey);

    // Extract the public key in DER format
    int pubLen = i2d_PublicKey(m_signKey.get(), nullptr);
    if (pubLen <= 0 || pubLen > kMaxPubKeySize) {
        logError(QStringLiteral("RSA public key size %1 exceeds maximum %2")
                     .arg(pubLen).arg(kMaxPubKeySize));
        m_signKey.reset();
        return;
    }

    uint8* pubPtr = m_myPublicKey.data();
    i2d_PublicKey(m_signKey.get(), &pubPtr);
    m_myPublicKeyLen = static_cast<uint8>(pubLen);

    logInfo(QStringLiteral("RSA secure identification initialized (public key: %1 bytes)")
                .arg(m_myPublicKeyLen));
}

uint8 ClientCreditsList::createSignature(ClientCredits* target, uint8* output, uint8 maxSize,
                                         uint32 challengeIP, uint8 chaIPKind) const
{
    if (!m_signKey || !target)
        return 0;

    // Build message buffer: [target_pubkey][4-byte challenge_from][optional 5-byte IP+kind]
    // Maximum: kMaxPubKeySize + 4 + 4 + 1 = 89 bytes
    std::array<uint8, kMaxPubKeySize + 4 + 4 + 1> msgBuf{};
    uint32 msgLen = 0;

    // Append target's public key
    std::memcpy(msgBuf.data(), target->secureIdent(), target->secIDKeyLen());
    msgLen += target->secIDKeyLen();

    // Append challenge_from (the challenge the target gave us)
    pokeUInt32(msgBuf.data() + msgLen, target->cryptRndChallengeFrom);
    msgLen += 4;

    // Append IP + kind for v2 signatures
    if (chaIPKind != kCryptCipNoneClient) {
        pokeUInt32(msgBuf.data() + msgLen, challengeIP);
        msgLen += 4;
        msgBuf[msgLen] = chaIPKind;
        msgLen += 1;
    }

    // Sign with SHA-1 + RSA PKCS1 v1.5 (matching original RSASSA_PKCS1v15_SHA)
    EVP_MD_CTX* mdCtx = EVP_MD_CTX_new();
    if (!mdCtx)
        return 0;

    std::size_t sigLen = 0;
    bool ok = EVP_DigestSignInit(mdCtx, nullptr, EVP_sha1(), nullptr, m_signKey.get()) > 0
           && EVP_DigestSignUpdate(mdCtx, msgBuf.data(), msgLen) > 0
           && EVP_DigestSignFinal(mdCtx, nullptr, &sigLen) > 0;

    if (!ok || sigLen > maxSize) {
        EVP_MD_CTX_free(mdCtx);
        return 0;
    }

    std::size_t actualSigLen = sigLen;
    ok = EVP_DigestSignFinal(mdCtx, output, &actualSigLen) > 0;
    EVP_MD_CTX_free(mdCtx);

    return ok ? static_cast<uint8>(actualSigLen) : 0;
}

bool ClientCreditsList::verifyIdent(ClientCredits* target, const uint8* signature, uint8 sigSize,
                                    uint32 forIP, uint8 chaIPKind)
{
    if (!target || target->secIDKeyLen() == 0)
        return false;

    // Load target's public key from their stored identity
    const uint8* pubKeyData = target->secureIdent();
    const uint8* pubPtr = pubKeyData;
    EVP_PKEY* peerKey = d2i_PublicKey(EVP_PKEY_RSA, nullptr, &pubPtr, target->secIDKeyLen());
    if (!peerKey)
        return false;

    // Build message buffer: [our_pubkey][4-byte challenge_for][optional 5-byte IP+kind]
    std::array<uint8, kMaxPubKeySize + 4 + 4 + 1> msgBuf{};
    uint32 msgLen = 0;

    // Our public key
    std::memcpy(msgBuf.data(), m_myPublicKey.data(), m_myPublicKeyLen);
    msgLen += m_myPublicKeyLen;

    // challenge_for (the challenge we gave them)
    pokeUInt32(msgBuf.data() + msgLen, target->cryptRndChallengeFor);
    msgLen += 4;

    // IP + kind for v2 signatures
    if (chaIPKind != kCryptCipNoneClient) {
        uint32 ip = 0;
        if (chaIPKind == kCryptCipLocalClient) {
            ip = forIP;
        } else if (chaIPKind == kCryptCipRemoteClient) {
            if (theApp.serverConnect) {
                ip = theApp.serverConnect->isLowID()
                         ? theApp.serverConnect->localIP()
                         : theApp.serverConnect->clientID();
            }
        }
        pokeUInt32(msgBuf.data() + msgLen, ip);
        msgLen += 4;
        msgBuf[msgLen] = chaIPKind;
        msgLen += 1;
    }

    // Verify with SHA-1 + RSA PKCS1 v1.5
    EVP_MD_CTX* mdCtx = EVP_MD_CTX_new();
    if (!mdCtx) {
        EVP_PKEY_free(peerKey);
        return false;
    }

    bool ok = EVP_DigestVerifyInit(mdCtx, nullptr, EVP_sha1(), nullptr, peerKey) > 0
           && EVP_DigestVerifyUpdate(mdCtx, msgBuf.data(), msgLen) > 0
           && EVP_DigestVerifyFinal(mdCtx, signature, sigSize) > 0;

    EVP_MD_CTX_free(mdCtx);
    EVP_PKEY_free(peerKey);

    if (ok) {
        target->verified(forIP);
    } else {
        target->m_identState = IdentState::IdFailed;
    }

    return ok;
}

} // namespace eMule
