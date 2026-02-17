#pragma once

/// @file ClientCredits.h
/// @brief Credit system + clients.met persistence — replaces MFC CClientCredits + CClientCreditsList.

#include "client/ClientStateDefs.h"
#include "utils/Types.h"
#include "utils/MapKey.h"

#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <unordered_map>

class QString;
struct evp_pkey_st;  // OpenSSL EVP_PKEY forward declaration

namespace eMule {

inline constexpr int kMaxPubKeySize = 80;

// Signature IP-kind constants (matching original eMule)
inline constexpr uint8 kCryptCipRemoteClient = 10;
inline constexpr uint8 kCryptCipLocalClient  = 20;
inline constexpr uint8 kCryptCipNoneClient   = 30;

// ---------------------------------------------------------------------------
// CreditStruct — packed binary layout matching clients.met on disk
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

struct CreditStruct_29a {
    std::array<uint8, 16> key{};     // MD4 user hash
    uint32 uploadedLo   = 0;         // uploaded TO this client (low 32)
    uint32 downloadedLo = 0;         // downloaded FROM this client (low 32)
    uint32 lastSeen     = 0;         // unix timestamp
    uint32 uploadedHi   = 0;         // upload high 32
    uint32 downloadedHi = 0;         // download high 32
    uint16 reserved     = 0;
};

struct CreditStruct {
    std::array<uint8, 16> key{};     // MD4 user hash
    uint32 uploadedLo   = 0;         // uploaded TO this client (low 32)
    uint32 downloadedLo = 0;         // downloaded FROM this client (low 32)
    uint32 lastSeen     = 0;         // unix timestamp
    uint32 uploadedHi   = 0;         // upload high 32
    uint32 downloadedHi = 0;         // download high 32
    uint16 reserved     = 0;
    uint8  keySize      = 0;         // public key length (0 = none)
    std::array<uint8, kMaxPubKeySize> secureIdent{};  // public key
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// ClientCredits — per-client credit tracking
// ---------------------------------------------------------------------------

class ClientCreditsList; // forward declaration for back-pointer

class ClientCredits {
    friend class ClientCreditsList;
public:
    ClientCredits(const CreditStruct& credits, const ClientCreditsList* owner = nullptr);
    ClientCredits(const uint8* userHash, const ClientCreditsList* owner = nullptr);
    ~ClientCredits() = default;

    ClientCredits(const ClientCredits&) = delete;
    ClientCredits& operator=(const ClientCredits&) = delete;

    [[nodiscard]] const uint8* key() const { return m_credits.key.data(); }
    [[nodiscard]] uint8* secureIdent() { return m_publicKey.data(); }
    [[nodiscard]] uint8 secIDKeyLen() const { return m_publicKeyLen; }
    [[nodiscard]] const CreditStruct& dataStruct() const { return m_credits; }

    void addDownloaded(uint32 bytes, uint32 forIP);
    void addUploaded(uint32 bytes, uint32 forIP);
    [[nodiscard]] uint64 uploadedTotal() const;
    [[nodiscard]] uint64 downloadedTotal() const;
    [[nodiscard]] float scoreRatio(uint32 forIP) const;

    void setLastSeen() { m_credits.lastSeen = static_cast<uint32>(std::time(nullptr)); }
    bool setSecureIdent(const uint8* ident, uint8 identLen);

    [[nodiscard]] IdentState currentIdentState(uint32 forIP) const;

    uint32 secureWaitStartTime(uint32 forIP);
    void setSecWaitStartTime(uint32 forIP);
    void clearWaitStartTime();

    uint32 cryptRndChallengeFor  = 0;
    uint32 cryptRndChallengeFrom = 0;

    void verified(uint32 forIP);

private:
    void initializeIdent();

    CreditStruct m_credits{};
    IdentState m_identState = IdentState::NotAvailable;
    uint32 m_identIP = 0;
    uint32 m_secureWaitTime   = 0;
    uint32 m_unsecureWaitTime = 0;
    uint32 m_waitTimeIP = 0;
    std::array<uint8, kMaxPubKeySize> m_publicKey{};
    uint8 m_publicKeyLen = 0;
    const ClientCreditsList* m_creditsList = nullptr;
};

// ---------------------------------------------------------------------------
// ClientCreditsList — credit store + clients.met persistence + RSA crypto
// ---------------------------------------------------------------------------

class ClientCreditsList {
public:
    ClientCreditsList();
    ~ClientCreditsList();

    ClientCreditsList(const ClientCreditsList&) = delete;
    ClientCreditsList& operator=(const ClientCreditsList&) = delete;

    bool loadList(const QString& filePath);
    bool saveList(const QString& filePath) const;

    ClientCredits* getCredit(const uint8* userHash);
    [[nodiscard]] std::size_t creditCount() const { return m_clients.size(); }

    [[nodiscard]] bool cryptoAvailable() const { return m_myPublicKeyLen > 0 && m_signKey != nullptr; }

    void process(const QString& filePath);

    // RSA signature / verification
    uint8 createSignature(ClientCredits* target, uint8* output, uint8 maxSize,
                          uint32 challengeIP, uint8 chaIPKind) const;
    bool verifyIdent(ClientCredits* target, const uint8* signature, uint8 sigSize,
                     uint32 forIP, uint8 chaIPKind);

    [[nodiscard]] uint8 pubKeyLen() const { return m_myPublicKeyLen; }
    [[nodiscard]] const uint8* publicKey() const { return m_myPublicKey.data(); }

private:
    void initializeCrypting();
    static bool createKeyPair(const QString& keyPath);

    struct EvpKeyDeleter { void operator()(evp_pkey_st* p) const; };
    using EvpKeyPtr = std::unique_ptr<evp_pkey_st, EvpKeyDeleter>;

    std::unordered_map<HashKeyOwn, std::unique_ptr<ClientCredits>> m_clients;
    std::chrono::steady_clock::time_point m_lastSaved{};

    EvpKeyPtr m_signKey;
    std::array<uint8, kMaxPubKeySize> m_myPublicKey{};
    uint8 m_myPublicKeyLen = 0;
};

} // namespace eMule
