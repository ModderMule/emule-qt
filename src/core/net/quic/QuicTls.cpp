#include "pch.h"

#ifdef EMULE_HAVE_QUIC_NATT

#include "net/quic/QuicTls.h"
#include "crypto/SelfSignedCert.h"
#include "net/quic/QuicNatProtocol.h"

#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <memory>
#include <mutex>

namespace eMule::quic {

namespace {

// TLS 1.3 suites both peer stacks offer
constexpr const char* kCipherSuites =
    "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
// No hybrid ML-KEM share: its 1 KiB+ key share splits the ClientHello over several
// Initials and draws a HelloRetryRequest from peers that do not know it.
constexpr const char* kGroups = "X25519:P-256";

QString g_lastError;

QString opensslError(const char* what)
{
    const unsigned long e = ERR_get_error();
    char buf[256] = {};
    if (e != 0)
        ERR_error_string_n(e, buf, sizeof(buf));
    return QStringLiteral("%1: %2").arg(QLatin1String(what), QLatin1String(buf));
}

/// Wire form of the ALPN list: one length-prefixed entry.
const unsigned char* alpnWire(unsigned int* len)
{
    static const auto wire = [] {
        std::array<unsigned char, 1 + kAlpn.size()> w{};
        w[0] = static_cast<unsigned char>(kAlpn.size());
        std::copy(kAlpn.begin(), kAlpn.end(), w.begin() + 1);
        return w;
    }();
    *len = static_cast<unsigned int>(wire.size());
    return wire.data();
}

int selectAlpn(SSL*, const unsigned char** out, unsigned char* outlen, const unsigned char* in,
               unsigned int inlen, void*)
{
    for (unsigned int i = 0; i < inlen;) {
        const unsigned int n = in[i];
        if (i + 1 + n > inlen)
            break;
        if (n == kAlpn.size() && std::memcmp(in + i + 1, kAlpn.data(), n) == 0) {
            *out = in + i + 1;
            *outlen = static_cast<unsigned char>(n);
            return SSL_TLSEXT_ERR_OK;
        }
        i += 1 + n;
    }
    return SSL_TLSEXT_ERR_ALERT_FATAL;   // mandatory, as on the peers
}

bool commonSetup(SSL_CTX* ctx)
{
    return SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION) == 1
        && SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) == 1
        && SSL_CTX_set_ciphersuites(ctx, kCipherSuites) == 1
        && SSL_CTX_set1_groups_list(ctx, kGroups) == 1;
}

/// Self-signed ECDSA P-256 certificate, valid a year, backdated a day.
bool installSelfSigned(SSL_CTX* ctx)
{
    const auto id = generateSelfSigned("ed2k-natt", 365);
    return id && SSL_CTX_use_certificate(ctx, id->cert.get()) == 1
        && SSL_CTX_use_PrivateKey(ctx, id->key.get()) == 1;
}

} // namespace

QuicTls* QuicTls::instance()
{
    static std::once_flag once;
    static std::unique_ptr<QuicTls> tls;
    std::call_once(once, [] {
        std::unique_ptr<QuicTls> t(new QuicTls);
        if (t->init())
            tls = std::move(t);
    });
    return tls.get();
}

QString QuicTls::lastError()
{
    return g_lastError;
}

bool QuicTls::alpnMatches(SSL* ssl)
{
    const unsigned char* alpn = nullptr;
    unsigned int len = 0;
    SSL_get0_alpn_selected(ssl, &alpn, &len);
    return alpn && len == kAlpn.size() && std::memcmp(alpn, kAlpn.data(), len) == 0;
}

QuicTls::~QuicTls()
{
    SSL_CTX_free(m_client);
    SSL_CTX_free(m_server);
}

bool QuicTls::init()
{
    if (ngtcp2_crypto_ossl_init() != 0) {
        g_lastError = QStringLiteral("ngtcp2_crypto_ossl_init failed");
        return false;
    }

    m_client = SSL_CTX_new(TLS_client_method());
    if (!m_client || !commonSetup(m_client)) {
        g_lastError = opensslError("client context");
        return false;
    }
    SSL_CTX_set_verify(m_client, SSL_VERIFY_NONE, nullptr);   // identity is the EAQN1 proof
    SSL_CTX_set_options(m_client, SSL_OP_NO_TICKET);
    SSL_CTX_set_session_cache_mode(m_client, SSL_SESS_CACHE_OFF);

    m_server = SSL_CTX_new(TLS_server_method());
    if (!m_server || !commonSetup(m_server)) {
        g_lastError = opensslError("server context");
        return false;
    }
    SSL_CTX_set_options(m_server, SSL_OP_NO_TICKET | SSL_OP_CIPHER_SERVER_PREFERENCE);
    SSL_CTX_set_num_tickets(m_server, 0);
    SSL_CTX_set_session_cache_mode(m_server, SSL_SESS_CACHE_OFF);
    SSL_CTX_set_verify(m_server, SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_alpn_select_cb(m_server, selectAlpn, nullptr);
    if (!installSelfSigned(m_server)) {
        g_lastError = opensslError("server certificate");
        return false;
    }
    return true;
}

void QuicTls::setClientAlpn(SSL* ssl)
{
    unsigned int len = 0;
    const unsigned char* wire = alpnWire(&len);
    SSL_set_alpn_protos(ssl, wire, len);
}

} // namespace eMule::quic

#endif // EMULE_HAVE_QUIC_NATT
