#include "pch.h"
/// @file ClientCredits.cpp
/// @brief Credit system + clients.met persistence — replaces MFC CClientCredits + CClientCreditsList.

#include "client/ClientCredits.h"
#include "prefs/Preferences.h"
#include "server/ServerConnect.h"
#include "app/AppContext.h"
#include "utils/SafeFile.h"
#include "utils/Log.h"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/err.h>

#include <QDir>
#include <QFile>
#include <QString>


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

void ClientCredits::restoreWaitStartTime(uint32 forIP, uint32 elapsedMs)
{
    auto now = static_cast<uint32>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() & 0xFFFFFFFF);

    // Deliberate 32-bit wrap: score() computes curTick - waitStartTime, and every other
    // tick comparison in the upload path relies on the same wrapping arithmetic.
    uint32 started = now - elapsedMs;

    // 0 is the "never started" sentinel — secureWaitStartTime() re-initialises on it, which
    // would silently throw away the position we are restoring. One tick either way is noise.
    if (started == 0)
        started = 1;

    m_unsecureWaitTime = started;
    m_secureWaitTime   = started;
    m_waitTimeIP       = forIP;
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
        logError(QStringLiteral("Error reading credit file: %1").arg(QLatin1StringView(e.what())));
        return false;
    }
}

bool ClientCreditsList::saveList(const QString& filePath) const
{
    const QString tmpPath = filePath + QStringLiteral(".tmp");
    const QString bakPath = filePath + QStringLiteral(".bak");

    try {
        QFile::remove(tmpPath);

        {
            SafeFile file;
            if (!file.open(tmpPath, QIODevice::WriteOnly))
                return false;

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
        } // file closed before rename

        // Rotate: current → .bak
        QFile::remove(bakPath);
        if (QFile::exists(filePath)) {
            if (!QFile::rename(filePath, bakPath))
                QFile::remove(filePath);
        }

        // Rename temp → final
        if (!QFile::rename(tmpPath, filePath)) {
            logError(QStringLiteral("clients.met: failed to rename tmp → clients.met"));
            if (QFile::exists(bakPath))
                QFile::rename(bakPath, filePath);
            return false;
        }

        return true;
    } catch (const FileException& e) {
        logError(QStringLiteral("Error saving credit file: %1").arg(QLatin1StringView(e.what())));
        QFile::remove(tmpPath);
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
    // Generate 384-bit RSA key (RSAKEYSIZE) matching original eMule / CryptoPP.
    // OpenSSL 3.x rejects keys < 512 bits via all standard generation APIs
    // (EVP_PKEY_keygen and RSA_generate_key_ex both fail).  We bypass this by
    // generating primes directly with the BIGNUM API and assembling the RSA
    // key manually.
    constexpr int kHalfBits = RSAKEYSIZE / 2; // 192

    BIGNUM* p    = BN_new();
    BIGNUM* q    = BN_new();
    BIGNUM* n    = BN_new();
    BIGNUM* d    = BN_new();
    BIGNUM* e    = BN_new();
    BIGNUM* p1   = BN_new();
    BIGNUM* q1   = BN_new();
    BIGNUM* phi  = BN_new();
    BIGNUM* dp   = BN_new();
    BIGNUM* dq   = BN_new();
    BIGNUM* qinv = BN_new();
    BN_CTX* ctx  = BN_CTX_new();
    BN_set_word(e, RSA_F4); // 65537

    auto cleanup = [&] {
        BN_free(p);  BN_free(q);  BN_free(n);  BN_free(d);  BN_free(e);
        BN_free(p1); BN_free(q1); BN_free(phi);
        BN_free(dp); BN_free(dq); BN_free(qinv);
        BN_CTX_free(ctx);
    };

    bool ok = false;
    for (int attempt = 0; attempt < 100 && !ok; ++attempt) {
        if (!BN_generate_prime_ex(p, kHalfBits, 0, nullptr, nullptr, nullptr))
            continue;
        if (!BN_generate_prime_ex(q, kHalfBits, 0, nullptr, nullptr, nullptr))
            continue;
        if (BN_cmp(p, q) == 0)
            continue;
        BN_mul(n, p, q, ctx);
        if (BN_num_bits(n) < RSAKEYSIZE)
            continue;
        // phi = (p-1)(q-1)
        BN_copy(p1, p); BN_sub_word(p1, 1);
        BN_copy(q1, q); BN_sub_word(q1, 1);
        BN_mul(phi, p1, q1, ctx);
        // d = e^(-1) mod phi
        if (BN_mod_inverse(d, e, phi, ctx))
            ok = true;
    }

    if (!ok) {
        logError(QStringLiteral("Failed to generate %1-bit RSA key pair").arg(RSAKEYSIZE));
        cleanup();
        return false;
    }

    // CRT parameters for efficient private-key operations
    BN_mod(dp, d, p1, ctx);
    BN_mod(dq, d, q1, ctx);
    BN_mod_inverse(qinv, q, p, ctx);

    // Assemble into legacy RSA struct (takes ownership of BN_dup'd values)
    RSA* rsa = RSA_new();
    RSA_set0_key(rsa, BN_dup(n), BN_dup(e), BN_dup(d));
    RSA_set0_factors(rsa, BN_dup(p), BN_dup(q));
    RSA_set0_crt_params(rsa, BN_dup(dp), BN_dup(dq), BN_dup(qinv));
    cleanup();

    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey || EVP_PKEY_assign_RSA(pkey, rsa) <= 0) {
        logError(QStringLiteral("EVP_PKEY_assign_RSA failed"));
        EVP_PKEY_free(pkey);
        RSA_free(rsa);
        return false;
    }
    // rsa now owned by pkey

    // DER-encode the private key
    int derLen = i2d_PrivateKey(pkey, nullptr);
    if (derLen <= 0) {
        EVP_PKEY_free(pkey);
        return false;
    }

    std::vector<uint8> derBuf(static_cast<std::size_t>(derLen));
    uint8* derPtr = derBuf.data();
    i2d_PrivateKey(pkey, &derPtr);
    EVP_PKEY_free(pkey);

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

    logInfo(QStringLiteral("RSA %1-bit key pair generated and saved to %2")
                .arg(RSAKEYSIZE).arg(keyPath));
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
        // OpenSSL 3.x rejects RSA keys < 512 bits via the provider layer.
        // Fall back to legacy decoder for existing 384-bit cryptkey.dat files.
        ERR_clear_error();
        derPtr = reinterpret_cast<const uint8*>(derData.constData());
        RSA* rsa = d2i_RSAPrivateKey(nullptr, &derPtr, derData.size());
        if (rsa) {
            rawKey = EVP_PKEY_new();
            if (rawKey && EVP_PKEY_assign_RSA(rawKey, rsa) > 0) {
                // rsa now owned by rawKey
            } else {
                EVP_PKEY_free(rawKey);
                RSA_free(rsa);
                rawKey = nullptr;
            }
        }
        if (!rawKey) {
            logError(QStringLiteral("Failed to load RSA private key from %1").arg(keyPath));
            return;
        }
    }

    m_signKey.reset(rawKey);

    // Extract the public key in X.509 SubjectPublicKeyInfo format, matching
    // CryptoPP's X509PublicKey::Save() used by MFC eMule.  For 384-bit RSA
    // this is ~78 bytes, well within kMaxPubKeySize (80).
    int pubLen = i2d_PUBKEY(m_signKey.get(), nullptr);
    if (pubLen <= 0 || pubLen > kMaxPubKeySize) {
        logError(QStringLiteral("RSA public key size %1 exceeds maximum %2")
                     .arg(pubLen).arg(kMaxPubKeySize));
        m_signKey.reset();
        return;
    }

    uint8* pubPtr = m_myPublicKey.data();
    i2d_PUBKEY(m_signKey.get(), &pubPtr);
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
    // MFC checks byChaIPKind == 0 (literal) for v1; kCryptCipNoneClient (30) is
    // NOT the v1 sentinel — ProcessSignaturePacket sets chaIPKind=0 for v1.
    if (chaIPKind != 0) {
        pokeUInt32(msgBuf.data() + msgLen, challengeIP);
        msgLen += 4;
        msgBuf[msgLen] = chaIPKind;
        msgLen += 1;
    }

    // Sign with SHA-1 + RSA PKCS1 v1.5 (matching original RSASSA_PKCS1v15_SHA).
    // Use legacy RSA_sign API directly — OpenSSL 3.x provider layer rejects
    // EVP_DigestSign operations on RSA keys < 512 bits, but RSA_sign has no
    // such restriction.
    uint8 hash[SHA_DIGEST_LENGTH];
    SHA1(msgBuf.data(), msgLen, hash);

    const RSA* rsa = EVP_PKEY_get0_RSA(m_signKey.get());
    if (!rsa)
        return 0;

    unsigned int rsaSigLen = 0;
    if (RSA_sign(NID_sha1, hash, SHA_DIGEST_LENGTH,
                 output, &rsaSigLen, const_cast<RSA*>(rsa)) != 1) {
        logWarning(QStringLiteral("createSignature: RSA_sign failed — %1")
                       .arg(QLatin1StringView(ERR_reason_error_string(ERR_peek_last_error()))));
        return 0;
    }

    if (rsaSigLen > maxSize)
        return 0;

    return static_cast<uint8>(rsaSigLen);
}

bool ClientCreditsList::verifyIdent(ClientCredits* target, const uint8* signature, uint8 sigSize,
                                    uint32 forIP, uint8 chaIPKind)
{
    if (!target || target->secIDKeyLen() == 0)
        return false;

    // Load target's public key from their stored identity.
    // Both MFC/CryptoPP (X509PublicKey::Save) and our OpenSSL (i2d_PUBKEY) export
    // keys in X.509 SubjectPublicKeyInfo format — d2i_PUBKEY handles both.
    // On OpenSSL 3.x the result may be provider-backed; we extract raw BIGNUMs
    // to guarantee a legacy RSA struct that RSA_verify can use for sub-512-bit keys.
    const uint8* pubPtr = target->secureIdent();
    EVP_PKEY* tmpKey = d2i_PUBKEY(nullptr, &pubPtr, target->secIDKeyLen());

    // Extract raw RSA components (n, e) and rebuild as a guaranteed-legacy key.
    EVP_PKEY* peerKey = nullptr;
    if (tmpKey) {
        BIGNUM* n = nullptr;
        BIGNUM* e = nullptr;
        if (EVP_PKEY_get_bn_param(tmpKey, "n", &n) > 0 &&
            EVP_PKEY_get_bn_param(tmpKey, "e", &e) > 0) {
            RSA* legacyRsa = RSA_new();
            if (legacyRsa && RSA_set0_key(legacyRsa, n, e, nullptr) > 0) {
                // n, e ownership transferred to legacyRsa
                peerKey = EVP_PKEY_new();
                if (!peerKey || EVP_PKEY_assign_RSA(peerKey, legacyRsa) <= 0) {
                    EVP_PKEY_free(peerKey);
                    RSA_free(legacyRsa);
                    peerKey = nullptr;
                }
            } else {
                BN_free(n);
                BN_free(e);
                RSA_free(legacyRsa);
            }
        } else {
            BN_free(n);
            BN_free(e);
        }
        EVP_PKEY_free(tmpKey);
    }

    if (!peerKey) {
        ERR_clear_error();
        logWarning(QStringLiteral("verifyIdent: d2i_PUBKEY failed for %1-byte key")
                       .arg(target->secIDKeyLen()));
        return false;
    }

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
    // MFC checks byChaIPKind == 0 (literal) for v1; must match createSignature.
    if (chaIPKind != 0) {
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

    // Verify with SHA-1 + RSA PKCS1 v1.5.
    // Use legacy RSA_verify API directly — OpenSSL 3.x provider layer rejects
    // EVP_DigestVerify operations on RSA keys < 512 bits.
    uint8 hash[SHA_DIGEST_LENGTH];
    SHA1(msgBuf.data(), msgLen, hash);

    // Hex dump for debugging
    {
        QString hashHex;
        for (int i = 0; i < SHA_DIGEST_LENGTH; ++i)
            hashHex += QStringLiteral("%1").arg(hash[i], 2, 16, QLatin1Char('0'));
        QString sigHex;
        for (uint8 i = 0; i < std::min<uint8>(sigSize, 8); ++i)
            sigHex += QStringLiteral("%1").arg(signature[i], 2, 16, QLatin1Char('0'));
        logDebug(QStringLiteral("verifyIdent: msgLen=%1 challenge=%2 sha1=%3 sig8=%4")
                     .arg(msgLen).arg(target->cryptRndChallengeFor).arg(hashHex).arg(sigHex));
    }

    const RSA* rsa = EVP_PKEY_get0_RSA(peerKey);
    bool ok = rsa && RSA_verify(NID_sha1, hash, SHA_DIGEST_LENGTH,
                                signature, sigSize, const_cast<RSA*>(rsa)) == 1;

    if (!ok) {
        // Peer likely has a stale copy of our public key in their clients.met
        // (from a previous session with a different cryptkey.dat).  MFC eMule's
        // SetSecureIdent rejects key updates once nKeySize > 0, so peers that
        // previously verified us with an old key can never update it.
        // This is self-correcting as stale credit entries expire (~5 months).
        ERR_clear_error();
        if (thePrefs.logSecureIdent())
            logDebug(QStringLiteral("verifyIdent: RSA_verify failed — peer may have stale public key"));
    }

    EVP_PKEY_free(peerKey);

    if (ok) {
        target->verified(forIP);
    } else {
        target->m_identState = IdentState::IdFailed;
    }

    return ok;
}

} // namespace eMule
