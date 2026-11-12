/// @file IpcServer.cpp
/// @brief TCP server for IPC — implementation.

#include "IpcServer.h"
#include "app/AppContext.h"
#include "IpcClientHandler.h"

#include "net/ListenConflict.h"
#include "utils/Log.h"

#include <QSslSocket>


namespace eMule {

IpcServer::IpcServer(QObject* parent)
    : QObject(parent)
{
}

void IpcServer::setTlsIdentity(const QSslCertificate& cert, const QSslKey& key)
{
    m_cert = cert;
    m_key = key;
    for (const auto& server : m_tcpServers)
        server->setIdentity(cert, key);
}

IpcServer::~IpcServer()
{
    stop();
}

bool IpcServer::start(const QList<QHostAddress>& addresses, uint16_t port)
{
    stop();
    m_errorText.clear();
    m_port = port;

    QStringList failed;
    for (const QHostAddress& address : addresses) {
        QString error;
        if (openListener(address, &error))
            continue;
        if (m_errorText.isEmpty())
            m_errorText = error;
        failed.append(error);
    }
    if (m_tcpServers.empty()) {
        logError(QStringLiteral("IPC server: ") + m_errorText);
        return false;
    }
    for (const QString& error : std::as_const(failed))
        logWarning(QStringLiteral("IPC server: ") + error);
    m_errorText.clear();
    return true;
}

void IpcServer::relisten(const QList<QHostAddress>& addresses)
{
    // Listeners no longer wanted go; clients stay (theirs is gone with the address anyway)
    for (auto it = m_tcpServers.begin(); it != m_tcpServers.end();) {
        if (addresses.contains((*it)->serverAddress())) {
            ++it;
            continue;
        }
        logInfo(QStringLiteral("IPC server: no longer listening on %1")
                    .arg((*it)->serverAddress().toString()));
        (*it)->close();
        it->release()->deleteLater();   // may hold sockets mid-handshake
        it = m_tcpServers.erase(it);
    }
    const QList<QHostAddress> have = listenAddresses();
    for (const QHostAddress& address : addresses) {
        if (have.contains(address))
            continue;
        QString error;
        if (openListener(address, &error))
            logInfo(QStringLiteral("IPC server: now listening on %1").arg(address.toString()));
        else
            logWarning(QStringLiteral("IPC server: ") + error);
    }
}

QList<QHostAddress> IpcServer::listenAddresses() const
{
    QList<QHostAddress> result;
    for (const auto& server : m_tcpServers)
        result.append(server->serverAddress());
    return result;
}

void IpcServer::stop()
{
    m_tcpServers.clear();
    m_clients.clear();
}

bool IpcServer::isListening() const
{
    return !m_tcpServers.empty();
}

void IpcServer::broadcast(const Ipc::IpcMessage& msg)
{
    if (m_pushTap)
        m_pushTap(msg);
    sendToClients(msg);
}

void IpcServer::sendToClients(const Ipc::IpcMessage& msg)
{
    // A failed write drops its client synchronously (errorOccurred ->
    // onClientDisconnected), which erases it from m_clients mid-loop. Walk a
    // snapshot: the dropped handler lives until its deleteLater() and reads as
    // unhandshaked.
    std::vector<IpcClientHandler*> clients;
    clients.reserve(m_clients.size());
    for (const auto& handler : m_clients)
        clients.push_back(handler.get());

    for (auto* handler : clients) {
        if (handler->isHandshaked())
            handler->sendPush(msg);
    }
}

void IpcServer::replayDeferred()
{
    // A snapshot, as in sendToClients(): a replayed request may drop its client
    std::vector<IpcClientHandler*> clients;
    clients.reserve(m_clients.size());
    for (const auto& handler : m_clients)
        clients.push_back(handler.get());

    for (auto* handler : clients)
        handler->replayDeferred();
}

bool IpcServer::hasReadyClient() const
{
    return std::ranges::any_of(m_clients, [](const auto& h) { return h->isHandshaked(); });
}

int IpcServer::clientCount() const
{
    return static_cast<int>(m_clients.size());
}

std::vector<IpcServer::LocalGui> IpcServer::localGuis() const
{
    std::vector<LocalGui> out;
    for (const auto& h : m_clients) {
        if (h->isReopenableGui())
            out.push_back({h->guiPid(), h->guiRelaunch()});
    }
    return out;
}

void IpcServer::notifyCoreUpdating(const QString& version)
{
    const auto make = [&version](bool reopened) {
        Ipc::IpcMessage msg(Ipc::IpcMsgType::PushCoreUpdating, 0);
        msg.append(version);
        msg.append(reopened);
        return msg;
    };
    if (m_pushTap)
        m_pushTap(make(false));
    // A snapshot, as in sendToClients()
    std::vector<IpcClientHandler*> clients;
    clients.reserve(m_clients.size());
    for (const auto& handler : m_clients)
        clients.push_back(handler.get());
    for (auto* handler : clients) {
        if (handler->isHandshaked())
            handler->sendPush(make(handler->isReopenableGui()));
    }
}

void IpcServer::notifyCategoriesChanged()
{
    broadcast(Ipc::IpcMessage(Ipc::IpcMsgType::PushCategoriesChanged));
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void IpcServer::onSocketReady(QSslSocket* socket, bool isLocal)
{
    {
        logInfo(QStringLiteral("IPC client connected from %1:%2%3%4")
                    .arg(socket->peerAddress().toString())
                    .arg(socket->peerPort())
                    .arg(isLocal ? QString() : QStringLiteral(" (remote)"))
                    .arg(socket->isEncrypted() ? QStringLiteral(", TLS") : QString()));

        auto handler = std::make_unique<IpcClientHandler>(socket, isLocal, this);

        connect(handler.get(), &IpcClientHandler::disconnected,
                this, &IpcServer::onClientDisconnected);
        connect(handler.get(), &IpcClientHandler::handshakeCompleted,
                this, &IpcServer::clientReady);
        connect(handler.get(), &IpcClientHandler::webServerConfigChanged,
                this, &IpcServer::webServerConfigChanged);
        connect(handler.get(), &IpcClientHandler::webTemplateReloadRequested,
                this, &IpcServer::webTemplateReloadRequested);
        connect(handler.get(), &IpcClientHandler::usenetConfigChanged,
                this, &IpcServer::usenetConfigChanged);
        connect(handler.get(), &IpcClientHandler::btConfigChanged,
                this, &IpcServer::btConfigChanged);
        connect(handler.get(), &IpcClientHandler::ed3kConfigChanged,
                this, &IpcServer::ed3kConfigChanged);
        connect(handler.get(), &IpcClientHandler::standbyConfigChanged,
                this, &IpcServer::standbyConfigChanged);
        connect(handler.get(), &IpcClientHandler::indexerConfigChanged,
                this, &IpcServer::indexerConfigChanged);
        // Broadcast rather than forward to the daemon: nothing outside the GUIs
        // cares that the category list changed, and this is the object that
        // knows every connected client.
        connect(handler.get(), &IpcClientHandler::categoriesChanged,
                this, &IpcServer::notifyCategoriesChanged);

        m_clients.push_back(std::move(handler));
    }
}

void IpcServer::onClientDisconnected(IpcClientHandler* handler)
{
    logInfo(QStringLiteral("IPC client disconnected"));

    // A port scan or a refused login is not the GUI leaving
    const bool wasClient = handler && handler->isHandshaked();

    // Release ownership and defer destruction — the handler's socket is still
    // inside Qt's signal delivery chain, so deleting it now would crash.
    auto it = std::find_if(m_clients.begin(), m_clients.end(),
                           [handler](const auto& h) { return h.get() == handler; });
    if (it != m_clients.end()) {
        (*it).release()->deleteLater();
        m_clients.erase(it);
    }
    // A GUI that quit mid-wizard must not leave the core held offline
    if (wasClient && m_clients.empty() && theApp.releaseConnectHold)
        theApp.releaseConnectHold();
}

bool IpcServer::openListener(const QHostAddress& address, QString* error)
{
    // A wildcard listen succeeds even if another program holds the port on one
    // local address; a local GUI would then talk to that program.
    if (const auto held = heldLocalAddresses(address, m_port); !held.isEmpty()) {
        *error = QStringLiteral("IPC port %1 is already used by another program on %2. "
                                "Change the IPC port in preferences.yml.")
                     .arg(m_port).arg(addressListText(held));
        return false;
    }

    auto server = std::make_unique<Ipc::IpcTlsServer>();
    server->setIdentity(m_cert, m_key);
    connect(server.get(), &Ipc::IpcTlsServer::ready, this, &IpcServer::onSocketReady);
    connect(server.get(), &Ipc::IpcTlsServer::rejected, this,
            [](const QString& peer, const QString& reason) {
                logWarning(QStringLiteral("IPC client %1 dropped: %2").arg(peer, reason));
            });
    if (server->listen(address, m_port)) {
        m_tcpServers.push_back(std::move(server));
        return true;
    }

    *error = QStringLiteral("Cannot listen on IPC port %1:%2: %3")
                 .arg(address.toString()).arg(m_port).arg(server->errorString());
    switch (server->serverError()) {
    case QAbstractSocket::AddressInUseError:
        *error += QStringLiteral(". Another program uses the port; "
                                 "change the IPC port in preferences.yml.");
        break;
    case QAbstractSocket::SocketAccessError:
#ifdef Q_OS_WIN
        // WSAEACCES: Hyper-V, WSL2 or Docker reserved a range holding the port
        *error += QStringLiteral(". Windows may reserve this port (check "
                                 "'netsh int ipv4 show excludedportrange protocol=tcp'); "
                                 "change the IPC port in preferences.yml.");
#endif
        break;
    default:
        break;
    }
    return false;
}

} // namespace eMule
