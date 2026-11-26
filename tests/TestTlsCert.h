#pragma once

/// @file TestTlsCert.h
/// @brief Short-lived localhost TLS certificate for loopback test servers.
///
/// Made at run time: macOS rejects server certs over 825 days or without serverAuth,
/// so a committed one would expire. Signed with tests/data/tls/localhost-key.pem.

#include "TestHelpers.h"

#include <QFile>
#include <QSslCertificate>
#include <QSslKey>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>

namespace eMule::testing {

[[nodiscard]] inline QByteArray localhostKeyPem()
{
    QFile file(testDataDir() + QStringLiteral("/tls/localhost-key.pem"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

[[nodiscard]] inline QSslKey localhostKey()
{
    return QSslKey(localhostKeyPem(), QSsl::Rsa);
}

/// Self-signed, names localhost and 127.0.0.1; one per process. Null on failure.
[[nodiscard]] inline QSslCertificate localhostCertificate()
{
    static const QSslCertificate cert = [] {
        const QByteArray keyPem = localhostKeyPem();
        const std::unique_ptr<BIO, decltype(&BIO_free)> keyBio(BIO_new_mem_buf(keyPem.constData(), int(keyPem.size())), BIO_free);
        const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
            keyBio ? PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free);
        const std::unique_ptr<X509, decltype(&X509_free)> x(X509_new(), X509_free);
        if (!key || !x)
            return QSslCertificate();

        X509_set_version(x.get(), 2);
        ASN1_INTEGER_set(X509_get_serialNumber(x.get()), 1);
        X509_gmtime_adj(X509_getm_notBefore(x.get()), -3600);
        X509_gmtime_adj(X509_getm_notAfter(x.get()), 30L * 86400);
        X509_set_pubkey(x.get(), key.get());
        auto* name = X509_get_subject_name(x.get());
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
        X509_set_issuer_name(x.get(), name);

        X509V3_CTX ctx;
        X509V3_set_ctx_nodb(&ctx);
        X509V3_set_ctx(&ctx, x.get(), x.get(), nullptr, nullptr, 0);
        const std::pair<int, const char*> extensions[] = {
            {NID_basic_constraints, "critical,CA:TRUE"},
            {NID_key_usage, "digitalSignature,keyEncipherment,keyCertSign"},
            {NID_ext_key_usage, "serverAuth"},
            {NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1"},
        };
        for (const auto& [nid, value] : extensions) {
            X509_EXTENSION* ext = X509V3_EXT_nconf_nid(nullptr, &ctx, nid, value);
            if (!ext || !X509_add_ext(x.get(), ext, -1)) {
                X509_EXTENSION_free(ext);
                return QSslCertificate();
            }
            X509_EXTENSION_free(ext);
        }
        if (!X509_sign(x.get(), key.get(), EVP_sha256()))
            return QSslCertificate();

        const std::unique_ptr<BIO, decltype(&BIO_free)> out(BIO_new(BIO_s_mem()), BIO_free);
        if (!out || !PEM_write_bio_X509(out.get(), x.get()))
            return QSslCertificate();
        char* data = nullptr;
        const long size = BIO_get_mem_data(out.get(), &data);
        return QSslCertificate(QByteArray(data, size));
    }();
    return cert;
}

} // namespace eMule::testing
