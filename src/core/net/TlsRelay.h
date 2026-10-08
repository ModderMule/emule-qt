#pragma once

/// @file TlsRelay.h
/// @brief TLS leg for an https URL source.
///
/// A URL source downloads through an EMSocket: that is what the throttler, the
/// connection limit and the HTTP parser know. An EMSocket writes to its descriptor
/// from the throttler thread, which rules out a QSslSocket in its place. So the
/// EMSocket connects to this relay on the loopback interface, and the relay carries
/// the bytes to the web server over TLS. Reads are passed on only as fast as the
/// EMSocket takes them, so the download limit still paces the transfer.

#include "net/Address.h"
#include "net/ProxySettings.h"
#include "utils/Types.h"

#include <QObject>
#include <QTcpServer>

class QSslSocket;
class QTcpSocket;

namespace eMule {

class TlsRelay : public QObject {
    Q_OBJECT

public:
    explicit TlsRelay(QObject* parent = nullptr);
    ~TlsRelay() override;

    /// Dial @p addr : @p port with TLS, verifying the certificate for @p hostName, and
    /// wait for the one local connection. @return the loopback port, 0 on failure.
    uint16 start(const Address& addr, uint16 port, const QString& hostName,
                 const ProxySettings& proxy);

    /// The local connection is ours only when it comes from this port. Nothing is
    /// relayed before this is called.
    void expectClientPort(uint16 localPort);

    /// Why the remote leg failed; empty while it has not.
    [[nodiscard]] const QString& failure() const { return m_failure; }

private:
    void onLocalConnection();
    void pumpToRemote();
    void pumpToLocal();
    void fail(const QString& reason);
    void closeLocalWhenDrained();

    QTcpServer m_listener;
    QSslSocket* m_remote = nullptr;
    QTcpSocket* m_local = nullptr;
    QString m_failure;
    uint16 m_expectedPort = 0;
    bool m_remoteClosed = false;

    static constexpr qint64 kBufferLimit = 256 * 1024;
};

} // namespace eMule
