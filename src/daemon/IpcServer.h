#pragma once

/// @file IpcServer.h
/// @brief TCP server accepting GUI client connections for IPC.
///
/// Manages IpcClientHandler instances for each connected GUI client.
/// Provides broadcast() to push events to all connected clients.

#include "IpcMessage.h"
#include "IpcTls.h"

#include <QHostAddress>
#include <QObject>

#include <functional>
#include <memory>
#include <vector>

namespace eMule {

class IpcClientHandler;

class IpcServer : public QObject {
    Q_OBJECT

public:
    explicit IpcServer(QObject* parent = nullptr);
    ~IpcServer() override;

    /// TLS identity for remote GUIs. Without one only loopback clients get in.
    void setTlsIdentity(const QSslCertificate& cert, const QSslKey& key);

    /// Start listening on the given address and port.
    bool start(const QHostAddress& address, uint16_t port);
    /// Why the last start() failed, with a hint where one helps.
    [[nodiscard]] QString errorText() const { return m_errorText; }

    /// Stop listening and disconnect all clients.
    void stop();

    /// Returns true if the server is listening.
    [[nodiscard]] bool isListening() const;

    /// Broadcast a push event to all connected (handshaked) clients.
    void broadcast(const Ipc::IpcMessage& msg);

    /// The same without the push tap: for news the tap has already seen.
    void sendToClients(const Ipc::IpcMessage& msg);

    /// The core has finished loading: every client gets the answers it was kept
    /// waiting for.
    void replayDeferred();

    /// True when a handshaked client is there to receive a push.
    [[nodiscard]] bool hasReadyClient() const;

    /// Number of currently connected clients.
    [[nodiscard]] int clientCount() const;

    /// Sees every broadcast before the clients do, connected or not. For the REST
    /// event stream, which carries the same news.
    void setPushTap(std::function<void(const Ipc::IpcMessage&)> tap) { m_pushTap = std::move(tap); }

    /// Tells every GUI the category list changed.
    void notifyCategoriesChanged();

signals:
    /// A client finished its handshake.
    void clientReady();

    /// Forwarded from any client's IpcClientHandler::webServerConfigChanged.
    void webServerConfigChanged();

    /// Forwarded from any client's IpcClientHandler::webTemplateReloadRequested.
    void webTemplateReloadRequested();

    /// Forwarded from any client's IpcClientHandler::usenetConfigChanged.
    void usenetConfigChanged();

    /// Forwarded from any client's IpcClientHandler::btConfigChanged.
    void btConfigChanged();

    /// Forwarded from any client's IpcClientHandler::ed3kConfigChanged.
    void ed3kConfigChanged();

    /// Forwarded from any client's IpcClientHandler::standbyConfigChanged.
    void standbyConfigChanged();

    /// Forwarded from any client's IpcClientHandler::indexerConfigChanged.
    void indexerConfigChanged();

private slots:
    void onSocketReady(QSslSocket* socket, bool isLocal);
    void onClientDisconnected(IpcClientHandler* handler);

private:
    Ipc::IpcTlsServer m_tcpServer;
    std::vector<std::unique_ptr<IpcClientHandler>> m_clients;
    std::function<void(const Ipc::IpcMessage&)> m_pushTap;
    QString m_errorText;
};

} // namespace eMule
