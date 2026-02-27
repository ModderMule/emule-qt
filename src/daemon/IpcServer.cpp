/// @file IpcServer.cpp
/// @brief TCP server for IPC — implementation.

#include "IpcServer.h"
#include "IpcClientHandler.h"

#include "utils/Log.h"

#include <algorithm>

namespace eMule {

IpcServer::IpcServer(QObject* parent)
    : QObject(parent)
{
    connect(&m_tcpServer, &QTcpServer::newConnection,
            this, &IpcServer::onNewConnection);
}

IpcServer::~IpcServer()
{
    stop();
}

bool IpcServer::start(const QHostAddress& address, uint16_t port)
{
    if (m_tcpServer.isListening())
        stop();

    if (!m_tcpServer.listen(address, port)) {
        logError(QStringLiteral("IPC server failed to listen: %1")
                     .arg(m_tcpServer.errorString()));
        return false;
    }
    return true;
}

void IpcServer::stop()
{
    m_tcpServer.close();
    m_clients.clear();
}

bool IpcServer::isListening() const
{
    return m_tcpServer.isListening();
}

void IpcServer::broadcast(const Ipc::IpcMessage& msg)
{
    for (auto& handler : m_clients) {
        if (handler->isHandshaked())
            handler->sendMessage(msg);
    }
}

int IpcServer::clientCount() const
{
    return static_cast<int>(m_clients.size());
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void IpcServer::onNewConnection()
{
    while (m_tcpServer.hasPendingConnections()) {
        QTcpSocket* socket = m_tcpServer.nextPendingConnection();
        if (!socket)
            continue;

        logInfo(QStringLiteral("IPC client connected from %1:%2")
                    .arg(socket->peerAddress().toString())
                    .arg(socket->peerPort()));

        auto handler = std::make_unique<IpcClientHandler>(socket, this);

        connect(handler.get(), &IpcClientHandler::disconnected,
                this, &IpcServer::onClientDisconnected);

        m_clients.push_back(std::move(handler));
    }
}

void IpcServer::onClientDisconnected(IpcClientHandler* handler)
{
    logInfo(QStringLiteral("IPC client disconnected"));

    // Release ownership and defer destruction — the handler's socket is still
    // inside Qt's signal delivery chain, so deleting it now would crash.
    auto it = std::find_if(m_clients.begin(), m_clients.end(),
                           [handler](const auto& h) { return h.get() == handler; });
    if (it != m_clients.end()) {
        (*it).release()->deleteLater();
        m_clients.erase(it);
    }
}

} // namespace eMule
