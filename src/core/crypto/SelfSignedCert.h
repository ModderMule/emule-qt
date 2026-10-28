#pragma once

/// @file SelfSignedCert.h
/// @brief Self-signed ECDSA P-256 / SHA-256 certificates (libcrypto only).
///
/// Shared by QUIC NAT-T (in-memory, short-lived) and the IPC TLS identity
/// (persisted as PEM, pinned by the GUI).

#include <QByteArray>

#include <openssl/types.h>

#include <memory>
#include <optional>
#include <string_view>

namespace eMule {

struct EvpPkeyDeleter { void operator()(EVP_PKEY* p) const noexcept; };
struct X509Deleter { void operator()(X509* p) const noexcept; };

/// A key and its certificate as OpenSSL objects.
struct SelfSignedIdentity {
    std::unique_ptr<EVP_PKEY, EvpPkeyDeleter> key;
    std::unique_ptr<X509, X509Deleter> cert;
};

/// Same, PEM encoded (PKCS#8 key).
struct SelfSignedPem {
    QByteArray certPem;
    QByteArray keyPem;
};

/// New EC P-256 key + self-signed cert for @p commonName, backdated a day, valid
/// @p validDays. nullopt on any OpenSSL failure.
[[nodiscard]] std::optional<SelfSignedIdentity> generateSelfSigned(std::string_view commonName,
                                                                   long validDays);

/// generateSelfSigned(), PEM encoded.
[[nodiscard]] std::optional<SelfSignedPem> generateSelfSignedPem(std::string_view commonName,
                                                                 long validDays);

} // namespace eMule
