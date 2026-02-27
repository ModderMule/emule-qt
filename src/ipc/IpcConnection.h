#pragma once

/// @file IpcConnection.h
/// @brief QObject wrapping QTcpSocket with length-prefixed CBOR framing.
///
/// Handles read buffering and frame assembly. Emits messageReceived()
/// for each complete IPC message decoded from the stream.

#include "IpcMessage.h"

#include <QObject>
#include <QTcpSocket>

#include <memory>

namespace eMule::Ipc {

class IpcConnection : public QObject {
    Q_OBJECT

public:
    /// Take ownership of an existing connected socket.
    explicit IpcConnection(QTcpSocket* socket, QObject* parent = nullptr);
    ~IpcConnection() override;

    /// Send a message over the connection.
    void sendMessage(const IpcMessage& msg);

    /// Returns true if the underlying socket is connected.
    [[nodiscard]] bool isConnected() const;

    /// Close the connection.
    void close();

    /// Access the underlying socket (e.g. for peer address).
    [[nodiscard]] QTcpSocket* socket() const;

signals:
    /// Emitted for each complete IPC message received.
    void messageReceived(const eMule::Ipc::IpcMessage& msg);

    /// Emitted when the connection is closed or an error occurs.
    void disconnected();

    /// Emitted on protocol errors (e.g. oversized frame).
    void protocolError(const QString& reason);

private slots:
    void onReadyRead();
    void onDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError error);

private:
    QTcpSocket* m_socket;  // Owned by this object (parented)
    QByteArray m_readBuffer;
    bool m_disconnectEmitted = false;
};

} // namespace eMule::Ipc
