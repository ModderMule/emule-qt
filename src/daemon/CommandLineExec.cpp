/// @file CommandLineExec.cpp
/// @brief Command-line parsing and one-shot CLI command execution for the daemon.

#include "CommandLineExec.h"
#include "CliIpcClient.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "prefs/Preferences.h"
#include "protocol/ED2KLink.h"

#include <QCoreApplication>

#include <cstdio>

namespace eMule {

void CommandLineExec::parse(QCoreApplication& app)
{
    m_parser.setApplicationDescription(QStringLiteral(
        "eMule Qt Core Daemon — headless P2P file sharing engine.\n\n"
        "When started without command flags, runs as a daemon.\n"
        "With a command flag, sends the command to a running daemon and exits."));
    m_parser.addHelpOption();
    m_parser.addVersionOption();

    m_parser.addOption(m_portOption);
    m_parser.addOption(m_addLinkOption);
    m_parser.addOption(m_connectOption);
    m_parser.addOption(m_disconnectOption);
    m_parser.addOption(m_connectKadOption);
    m_parser.addOption(m_disconnectKadOption);

    m_parser.process(app);
}

bool CommandLineExec::hasCommand() const
{
    return m_parser.isSet(m_addLinkOption)
        || m_parser.isSet(m_connectOption)
        || m_parser.isSet(m_disconnectOption)
        || m_parser.isSet(m_connectKadOption)
        || m_parser.isSet(m_disconnectKadOption);
}

int CommandLineExec::execCommand(QCoreApplication& app)
{
    // Determine IPC target
    QString ipcHost = thePrefs.ipcListenAddress();
    if (ipcHost.isEmpty())
        ipcHost = QStringLiteral("127.0.0.1");
    uint16_t ipcPort = thePrefs.ipcPort();
    if (portOverride() != 0)
        ipcPort = portOverride();

    // Build the IPC message from the command flag
    Ipc::IpcMessage msg;

    if (m_parser.isSet(m_addLinkOption)) {
        const QString link = m_parser.value(m_addLinkOption);
        auto parsed = parseED2KLink(link);
        if (!parsed) {
            std::fprintf(stderr, "Invalid ed2k link: %s\n", qPrintable(link));
            return 1;
        }
        auto* fl = std::get_if<ED2KFileLink>(&*parsed);
        if (!fl) {
            std::fprintf(stderr, "Only ed2k file links are supported for download.\n");
            return 1;
        }

        QString hashHex;
        for (uint8_t b : fl->hash)
            hashHex += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));

        msg = Ipc::IpcMessage(Ipc::IpcMsgType::DownloadSearchFile, 2);
        msg.append(hashHex);
        msg.append(fl->name);
        msg.append(static_cast<int64_t>(fl->size));

    } else if (m_parser.isSet(m_connectOption)) {
        msg = Ipc::IpcMessage(Ipc::IpcMsgType::ConnectToServer, 2);

    } else if (m_parser.isSet(m_disconnectOption)) {
        msg = Ipc::IpcMessage(Ipc::IpcMsgType::DisconnectFromServer, 2);

    } else if (m_parser.isSet(m_connectKadOption)) {
        msg = Ipc::IpcMessage(Ipc::IpcMsgType::BootstrapKad, 2);

    } else if (m_parser.isSet(m_disconnectKadOption)) {
        msg = Ipc::IpcMessage(Ipc::IpcMsgType::DisconnectKad, 2);
    }

    auto* client = new CliIpcClient(&app);
    client->sendCommand(ipcHost, ipcPort, std::move(msg));
    return QCoreApplication::exec();
}

uint16_t CommandLineExec::portOverride() const
{
    if (m_parser.isSet(m_portOption))
        return m_parser.value(m_portOption).toUShort();
    return 0;
}

} // namespace eMule
