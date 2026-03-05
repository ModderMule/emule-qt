#pragma once

/// @file CliIpcClient.h
/// @brief One-shot IPC client for CLI commands to a running daemon.

#include "IpcConnection.h"
#include "IpcMessage.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

namespace eMule {

class CliIpcClient : public QObject {
    Q_OBJECT

public:
    explicit CliIpcClient(QObject* parent = nullptr);

    /// Connect to daemon and send a command. Calls QCoreApplication::exit()
    /// with 0 on success or 1 on failure.
    void sendCommand(const QString& host, uint16_t port,
                     Ipc::IpcMessage command);

private:
    void onHandshakeResponse(const Ipc::IpcMessage& msg);
    void onCommandResponse(const Ipc::IpcMessage& msg);
    void fail(const QString& message);

    QTcpSocket* m_socket = nullptr;
    Ipc::IpcConnection* m_connection = nullptr;
    QTimer m_timeout;
    Ipc::IpcMessage m_command;
    bool m_handshaked = false;
};

} // namespace eMule
