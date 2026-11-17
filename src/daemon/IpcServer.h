#pragma once

/// @file IpcServer.h
/// @brief TCP server accepting GUI client connections for IPC.
///
/// Manages IpcClientHandler instances for each connected GUI client.
/// Provides broadcast() to push events to all connected clients.

#include "IpcMessage.h"
#include "IpcTls.h"

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QStringList>

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
    [[nodiscard]] QSslCertificate tlsCertificate() const { return m_cert; }

    /// Start listening on each of @p addresses at @p port. Fails only when none
    /// listens; the others are logged.
    bool start(const QList<QHostAddress>& addresses, uint16_t port);
    bool start(const QHostAddress& address, uint16_t port) { return start(QList{address}, port); }
    /// Listen on @p addresses from now on, same port: clients and the listeners
    /// still wanted stay.
    void relisten(const QList<QHostAddress>& addresses);
    /// Move every listener to @p addresses now (Options › Create Connect Script).
    /// Clients stay. When none opens, the old listeners come back and @p error says why.
    bool switchListen(const QList<QHostAddress>& addresses, QString* error);
    /// Where it listens now.
    [[nodiscard]] QList<QHostAddress> listenAddresses() const;
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

    /// A local GUI connected right now, with its pid and relaunch command.
    struct LocalGui {
        qint64 pid = 0;
        QStringList relaunch;
    };
    [[nodiscard]] std::vector<LocalGui> localGuis() const;

    /// PushCoreUpdating to each client: [version, reopened]. Only a GUI the
    /// updater starts again quits; the others wait for the core to come back.
    void notifyCoreUpdating(const QString& version);

    /// Sees every broadcast before the clients do, connected or not. For the REST
    /// event stream, which carries the same news.
    void setPushTap(std::function<void(const Ipc::IpcMessage&)> tap) { m_pushTap = std::move(tap); }

    /// Tells every GUI the category list changed.
    void notifyCategoriesChanged();

    /// Takes a plain socket that carries a paired phone's IPC from the ed3k tunnel
    /// (Ed3kTunnelBridge): a remote client that needs the token. Re-parented here.
    void adoptTunnel(QTcpSocket* socket, const QString& peer);

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

    /// ipc.listenAddress changed at runtime (ListenIpcAddress)
    void listenHostChanged(const QString& value);

private slots:
    void onSocketReady(QSslSocket* socket, bool isLocal);
    void onClientDisconnected(IpcClientHandler* handler);

private:
    /// A handler for @p socket with its signals wired up.
    IpcClientHandler* addClient(QTcpSocket* socket, bool isLocal);
    /// Opens and keeps a listener on @p address at m_port; else why not in @p error.
    bool openListener(const QHostAddress& address, QString* error);

    std::vector<std::unique_ptr<Ipc::IpcTlsServer>> m_tcpServers;   ///< one per address
    QSslCertificate m_cert;
    QSslKey m_key;
    uint16_t m_port = 0;
    std::vector<std::unique_ptr<IpcClientHandler>> m_clients;
    std::function<void(const Ipc::IpcMessage&)> m_pushTap;
    QString m_errorText;
};

} // namespace eMule
