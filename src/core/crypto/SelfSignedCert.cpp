#include "pch.h"

/// @file SelfSignedCert.cpp
/// @brief Self-signed ECDSA P-256 certificates.

#include "crypto/SelfSignedCert.h"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

#include <string>

namespace eMule {

void EvpPkeyDeleter::operator()(EVP_PKEY* p) const noexcept { EVP_PKEY_free(p); }
void X509Deleter::operator()(X509* p) const noexcept { X509_free(p); }

std::optional<SelfSignedIdentity> generateSelfSigned(std::string_view commonName, long validDays)
{
    SelfSignedIdentity id;
    id.key.reset(EVP_EC_gen("P-256"));
    id.cert.reset(X509_new());
    if (!id.key || !id.cert)
        return std::nullopt;

    unsigned char serial[8];
    if (RAND_bytes(serial, sizeof(serial)) != 1)
        return std::nullopt;
    serial[0] &= 0x7F;   // positive
    std::unique_ptr<BIGNUM, decltype(&BN_free)> bn(BN_bin2bn(serial, sizeof(serial), nullptr),
                                                   BN_free);
    if (!bn)
        return std::nullopt;

    X509* cert = id.cert.get();
    X509_set_version(cert, 2);
    BN_to_ASN1_INTEGER(bn.get(), X509_get_serialNumber(cert));
    X509_gmtime_adj(X509_getm_notBefore(cert), -86400);
    X509_gmtime_adj(X509_getm_notAfter(cert), validDays * 86400L);
    X509_NAME* name = X509_get_subject_name(cert);
    const std::string cn(commonName);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
    X509_set_issuer_name(cert, name);
    X509_set_pubkey(cert, id.key.get());
    if (X509_sign(cert, id.key.get(), EVP_sha256()) <= 0)
        return std::nullopt;
    return id;
}

std::optional<SelfSignedPem> generateSelfSignedPem(std::string_view commonName, long validDays)
{
    auto id = generateSelfSigned(commonName, validDays);
    if (!id)
        return std::nullopt;

    const auto toBytes = [](BIO* bio) {
        char* data = nullptr;
        const long len = BIO_get_mem_data(bio, &data);
        return QByteArray(data, static_cast<qsizetype>(len));
    };
    std::unique_ptr<BIO, decltype(&BIO_free)> certBio(BIO_new(BIO_s_mem()), BIO_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> keyBio(BIO_new(BIO_s_mem()), BIO_free);
    if (!certBio || !keyBio
        || PEM_write_bio_X509(certBio.get(), id->cert.get()) != 1
        || PEM_write_bio_PrivateKey(keyBio.get(), id->key.get(), nullptr, nullptr, 0, nullptr,
                                    nullptr) != 1)
        return std::nullopt;

    SelfSignedPem pem;
    pem.certPem = toBytes(certBio.get());
    pem.keyPem = toBytes(keyBio.get());
    return pem;
}

} // namespace eMule
