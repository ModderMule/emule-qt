#pragma once

/// @file IpcServer.h
/// @brief TCP server accepting GUI client connections for IPC.
///
/// Manages IpcClientHandler instances for each connected GUI client.
/// Provides broadcast() to push events to all connected clients.

#include "IpcMessage.h"

#include <QHostAddress>
#include <QObject>
#include <QTcpServer>

#include <memory>
#include <vector>

namespace eMule {

class IpcClientHandler;

class IpcServer : public QObject {
    Q_OBJECT

public:
    explicit IpcServer(QObject* parent = nullptr);
    ~IpcServer() override;

    /// Start listening on the given address and port.
    bool start(const QHostAddress& address, uint16_t port);

    /// Stop listening and disconnect all clients.
    void stop();

    /// Returns true if the server is listening.
    [[nodiscard]] bool isListening() const;

    /// Broadcast a push event to all connected (handshaked) clients.
    void broadcast(const Ipc::IpcMessage& msg);

    /// Number of currently connected clients.
    [[nodiscard]] int clientCount() const;

private slots:
    void onNewConnection();
    void onClientDisconnected(IpcClientHandler* handler);

private:
    QTcpServer m_tcpServer;
    std::vector<std::unique_ptr<IpcClientHandler>> m_clients;
};

} // namespace eMule
