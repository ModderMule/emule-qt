/// @file CliIpcClient.cpp
/// @brief One-shot IPC client for CLI commands to a running daemon.

#include "CliIpcClient.h"
#include "IpcProtocol.h"

#include <QCoreApplication>

#include <cstdio>

namespace eMule {

CliIpcClient::CliIpcClient(QObject* parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(5000);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        fail(QStringLiteral("Timed out waiting for daemon response."));
    });
}

void CliIpcClient::sendCommand(const QString& host, uint16_t port,
                               Ipc::IpcMessage command)
{
    m_command = std::move(command);
    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected, this, [this]() {
        m_connection = new Ipc::IpcConnection(m_socket, this);
        // Re-parent socket to connection (IpcConnection takes ownership)

        connect(m_connection, &Ipc::IpcConnection::messageReceived,
                this, [this](const Ipc::IpcMessage& msg) {
            if (!m_handshaked)
                onHandshakeResponse(msg);
            else
                onCommandResponse(msg);
        });
        connect(m_connection, &Ipc::IpcConnection::disconnected,
                this, [this]() {
            fail(QStringLiteral("Daemon closed the connection."));
        });

        // Send handshake
        Ipc::IpcMessage hs(Ipc::IpcMsgType::Handshake, 1);
        hs.append(QString::fromLatin1(Ipc::ProtocolVersion));
        m_connection->sendMessage(hs);
        m_timeout.start();
    });

    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this, host, port](QAbstractSocket::SocketError) {
        fail(QStringLiteral("Cannot connect to emulecored at %1:%2. Is the daemon running?")
                 .arg(host).arg(port));
    });

    m_socket->connectToHost(host, port);
}

void CliIpcClient::onHandshakeResponse(const Ipc::IpcMessage& msg)
{
    if (msg.type() == Ipc::IpcMsgType::Error) {
        fail(QStringLiteral("Handshake failed: %1").arg(msg.fieldString(1)));
        return;
    }
    if (msg.type() != Ipc::IpcMsgType::HandshakeOk) {
        fail(QStringLiteral("Unexpected response from daemon."));
        return;
    }

    m_handshaked = true;
    m_timeout.start(); // restart timeout for command
    m_connection->sendMessage(m_command);
}

void CliIpcClient::onCommandResponse(const Ipc::IpcMessage& msg)
{
    m_timeout.stop();

    if (msg.type() == Ipc::IpcMsgType::Error) {
        std::fprintf(stderr, "Error: %s\n", qPrintable(msg.fieldString(1)));
        QCoreApplication::exit(1);
        return;
    }

    if (msg.type() == Ipc::IpcMsgType::Result) {
        if (msg.fieldBool(0)) {
            std::fprintf(stdout, "OK\n");
            QCoreApplication::exit(0);
        } else {
            const QString detail = msg.fieldString(1);
            if (!detail.isEmpty())
                std::fprintf(stderr, "Failed: %s\n", qPrintable(detail));
            else
                std::fprintf(stderr, "Command failed.\n");
            QCoreApplication::exit(1);
        }
        return;
    }

    // Unexpected message type — treat as success
    std::fprintf(stdout, "OK\n");
    QCoreApplication::exit(0);
}

void CliIpcClient::fail(const QString& message)
{
    m_timeout.stop();
    std::fprintf(stderr, "%s\n", qPrintable(message));
    QCoreApplication::exit(1);
}

} // namespace eMule
