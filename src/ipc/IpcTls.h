#pragma once

/// @file IpcTls.h
/// @brief TLS for the GUI <-> core link.
///
/// A non-loopback peer must speak TLS. A loopback peer may speak either: the
/// first byte tells them apart (0x16 = TLS record, a plain frame starts 0x00/0x01
/// because of the 16 MiB cap). The certificate is self-signed; the GUI pins its
/// SHA-256 fingerprint on first connect.

#include <QSslCertificate>
#include <QSslKey>
#include <QTcpServer>

class QSslSocket;

namespace eMule::Ipc {

/// SHA-256 of the DER certificate, "AB:CD:..." upper case.
[[nodiscard]] QString certFingerprint(const QSslCertificate& cert);

/// Accepts IPC connections and runs the TLS handshake where needed. Emits
/// ready() once a socket can carry frames.
class IpcTlsServer : public QTcpServer {
    Q_OBJECT

public:
    explicit IpcTlsServer(QObject* parent = nullptr);

    /// Server identity. Without one, non-loopback peers are refused.
    void setIdentity(const QSslCertificate& cert, const QSslKey& key);
    [[nodiscard]] bool hasIdentity() const { return !m_cert.isNull() && !m_key.isNull(); }
    [[nodiscard]] QSslCertificate certificate() const { return m_cert; }

    /// Tests: treat loopback peers as remote (TLS required).
    void setTreatLoopbackAsRemote(bool on) { m_loopbackIsRemote = on; }

    /// Milliseconds a peer gets to start or finish TLS.
    void setHandshakeTimeout(int ms) { m_handshakeTimeoutMs = ms; }

signals:
    /// @p socket is ready for IPC frames (plain loopback, or TLS done). Owned by
    /// this server until the receiver re-parents it.
    void ready(QSslSocket* socket, bool isLocal);
    /// A connection was dropped before it got ready.
    void rejected(const QString& peer, const QString& reason);

protected:
    void incomingConnection(qintptr handle) override;

private:
    void startTls(QSslSocket* socket, bool isLocal);
    void decideLoopback(QSslSocket* socket);
    void finish(QSslSocket* socket, bool isLocal);
    void drop(QSslSocket* socket, const QString& reason);

    QSslCertificate m_cert;
    QSslKey m_key;
    bool m_loopbackIsRemote = false;
    int m_handshakeTimeoutMs = 15000;
};

} // namespace eMule::Ipc
