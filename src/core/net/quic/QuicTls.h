#pragma once
/// @file QuicTls.h
/// @brief OpenSSL contexts of the QUIC NAT-T transport: TLS 1.3, fixed ALPN, no verify.

#ifdef EMULE_HAVE_QUIC_NATT

#include <QString>

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

namespace eMule::quic {

/// Process-wide client and server SSL_CTX. The server key is a fresh self-signed
/// ECDSA P-256 certificate per process: nobody verifies it, identity is the proof.
class QuicTls {
public:
    /// Lazily initialises ngtcp2's OpenSSL backend and both contexts. Thread: owner.
    [[nodiscard]] static QuicTls* instance();

    [[nodiscard]] SSL_CTX* clientContext() const { return m_client; }
    [[nodiscard]] SSL_CTX* serverContext() const { return m_server; }
    /// Why instance() returned nullptr.
    [[nodiscard]] static QString lastError();

    /// The ALPN the peer chose matches ours. Call after the handshake.
    [[nodiscard]] static bool alpnMatches(SSL* ssl);
    /// Offer our ALPN on a client SSL.
    static void setClientAlpn(SSL* ssl);

    ~QuicTls();

private:
    QuicTls() = default;
    bool init();

    SSL_CTX* m_client = nullptr;
    SSL_CTX* m_server = nullptr;
};

} // namespace eMule::quic

#endif // EMULE_HAVE_QUIC_NATT
