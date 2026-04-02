#pragma once

/// @file CollectionKeys.h
/// @brief RSA key management for collection signing/verification.
///
/// Generates or loads a 1024-bit RSA key pair used to sign .emulecollection
/// files, matching MFC eMule's CryptoPP-based collection signing.

#include <QByteArray>
#include <QString>

#include <memory>

struct evp_pkey_st;

namespace eMule {

class CollectionKeys {
public:
    explicit CollectionKeys(const QString& configDir);
    ~CollectionKeys();

    CollectionKeys(const CollectionKeys&) = delete;
    CollectionKeys& operator=(const CollectionKeys&) = delete;

    /// Load or generate the collection signing key.
    /// @return true if key is ready for use.
    bool initialize();

    /// @return The private signing key, or nullptr if not initialized.
    [[nodiscard]] evp_pkey_st* signKey() const;

    /// @return DER-encoded RSA public key (for embedding in collection headers).
    [[nodiscard]] const QByteArray& publicKeyDer() const { return m_publicKeyDer; }

    /// Verify an RSA-SHA256 signature against a DER-encoded public key.
    static bool verifySignature(const QByteArray& message,
                                const QByteArray& signature,
                                const QByteArray& publicKeyDer);

private:
    bool createKeyPair();
    bool loadKeyPair();

    struct EvpKeyDeleter { void operator()(evp_pkey_st* p) const; };
    std::unique_ptr<evp_pkey_st, EvpKeyDeleter> m_signKey;
    QByteArray m_publicKeyDer;
    QString m_keyPath;
};

} // namespace eMule
