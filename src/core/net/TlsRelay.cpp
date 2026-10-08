#include "pch.h"
/// @file TlsRelay.cpp
/// @brief TLS leg for an https URL source.

#include "net/TlsRelay.h"
#include "net/InterfacePin.h"
#include "utils/Log.h"

#include <QNetworkProxy>
#include <QSslSocket>
#include <QTcpSocket>

namespace eMule {

TlsRelay::TlsRelay(QObject* parent)
    : QObject(parent)
{
    m_listener.setMaxPendingConnections(1);
    connect(&m_listener, &QTcpServer::newConnection, this, &TlsRelay::onLocalConnection);
}

TlsRelay::~TlsRelay() = default;

uint16 TlsRelay::start(const Address& addr, uint16 port, const QString& hostName,
                       const ProxySettings& proxy)
{
    // Loopback by name: port 0 on the wildcard can collide with another loopback bind
    if (!m_listener.listen(QHostAddress::LocalHost, 0)) {
        m_failure = m_listener.errorString();
        return 0;
    }

    m_remote = new QSslSocket(this);
    m_remote->setReadBufferSize(kBufferLimit);

    const QNetworkProxy netProxy = toNetworkProxy(proxy);
    const bool proxied = netProxy.type() != QNetworkProxy::NoProxy;
    if (proxied)
        m_remote->setProxy(netProxy);
    if (!InterfacePin::prepareOutgoing(*m_remote, proxied)) {
        m_failure = QStringLiteral("the selected network interface is not available");
        m_listener.close();
        return 0;
    }

    connect(m_remote, &QSslSocket::encrypted, this, &TlsRelay::pumpToRemote);
    connect(m_remote, &QSslSocket::readyRead, this, &TlsRelay::pumpToLocal);
    // Certificate errors are never ignored: the handshake fails and lands here.
    connect(m_remote, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_remoteClosed)
            fail(m_remote->errorString());
    });
    connect(m_remote, &QSslSocket::disconnected, this, [this] {
        m_remoteClosed = true;
        pumpToLocal();
        closeLocalWhenDrained();
    });

    // By address — it was vetted — with the name for the certificate and SNI
    m_remote->connectToHostEncrypted(addr.toQHostAddress().toString(), port, hostName);
    return m_listener.serverPort();
}

void TlsRelay::expectClientPort(uint16 localPort)
{
    m_expectedPort = localPort;
    if (m_local && m_local->peerPort() != m_expectedPort) {
        fail(QStringLiteral("unexpected local connection"));
        return;
    }
    pumpToRemote();
    pumpToLocal();
}

void TlsRelay::onLocalConnection()
{
    QTcpSocket* sock = m_listener.nextPendingConnection();
    m_listener.close();   // exactly one
    if (!sock)
        return;
    if (m_local || !sock->peerAddress().isLoopback()
        || (m_expectedPort != 0 && sock->peerPort() != m_expectedPort)) {
        sock->abort();
        sock->deleteLater();
        return;
    }

    m_local = sock;
    m_local->setParent(this);
    if (!m_failure.isEmpty()) {
        m_local->abort();
        return;
    }
    connect(m_local, &QTcpSocket::readyRead, this, &TlsRelay::pumpToRemote);
    connect(m_local, &QTcpSocket::bytesWritten, this, [this] {
        pumpToLocal();
        closeLocalWhenDrained();
    });
    connect(m_local, &QTcpSocket::disconnected, this, [this] {
        if (m_remote && !m_remoteClosed) {
            m_remoteClosed = true;
            m_remote->abort();
        }
    });
    pumpToRemote();
    pumpToLocal();
    closeLocalWhenDrained();
}

void TlsRelay::pumpToRemote()
{
    if (!m_local || m_expectedPort == 0 || !m_remote || !m_remote->isEncrypted())
        return;
    if (m_local->bytesAvailable() > 0)
        m_remote->write(m_local->readAll());
}

void TlsRelay::pumpToLocal()
{
    if (!m_local || m_expectedPort == 0 || !m_remote)
        return;
    // Only what the reader has room for: the rest waits in the TLS socket, whose
    // bounded buffer then stops the server (the download limit, end to end).
    while (m_remote->bytesAvailable() > 0 && m_local->bytesToWrite() < kBufferLimit
           && m_local->state() == QAbstractSocket::ConnectedState)
        m_local->write(m_remote->read(64 * 1024));
}

void TlsRelay::closeLocalWhenDrained()
{
    if (m_remoteClosed && m_local && m_remote && m_remote->bytesAvailable() == 0
        && m_local->bytesToWrite() == 0
        && m_local->state() == QAbstractSocket::ConnectedState)
        m_local->disconnectFromHost();
}

void TlsRelay::fail(const QString& reason)
{
    if (!m_failure.isEmpty())
        return;
    m_failure = reason;
    m_remoteClosed = true;
    logDebug(QStringLiteral("TlsRelay: %1").arg(reason));
    m_listener.close();   // a local leg not yet connected is refused
    if (m_remote)
        m_remote->abort();
    if (m_local)
        m_local->abort();
}

} // namespace eMule
