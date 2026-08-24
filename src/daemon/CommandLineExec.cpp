/// @file CommandLineExec.cpp
/// @brief Command-line parsing and one-shot CLI command execution for the daemon.

#include "CommandLineExec.h"
#include "CliIpcClient.h"

#include "IpcMessage.h"
#include "prefs/Preferences.h"
#include "protocol/ED2KLink.h"

#include <QCoreApplication>


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
    m_parser.addOption(m_configOption);

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
    int timeoutMs = 0;   // 0 = leave CliIpcClient's default

    if (m_parser.isSet(m_addLinkOption)) {
        const QString link = m_parser.value(m_addLinkOption);
        auto parsed = parseED2KLink(link);
        if (!parsed) {
            // Redacted: an HTTP Cache link is a credential, and this message is
            // the one place a malformed one would otherwise reach a terminal
            // scrollback or a CI log.
            std::fprintf(stderr, "Invalid ed2k link: %s\n",
                         qPrintable(redactLinkSecret(link)));
            return 1;
        }

        if (auto* cfg = std::get_if<ED2KHttpCacheLink>(&*parsed)) {
            // No prompt: running this command is the consent the link format asks
            // for. The daemon still handshakes with the server before storing
            // anything, so a link pointing somewhere that is not a cache is
            // refused there rather than trusted here.
            msg = Ipc::IpcMessage(Ipc::IpcMsgType::ApplyHttpCacheConfig, 4);
            msg.append(cfg->baseUrl);
            msg.append(cfg->secret);
            // Display only, and the reason a list of several servers is readable.
            msg.append(cfg->name);
            msg.append(cfg->keyId);
            // The daemon has to reach a third machine before it can answer.
            timeoutMs = 20'000;

        } else if (auto* fl = std::get_if<ED2KFileLink>(&*parsed)) {
            QString hashHex;
            for (uint8_t b : fl->hash)
                hashHex += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));

            msg = Ipc::IpcMessage(Ipc::IpcMsgType::DownloadSearchFile, 2);
            msg.append(hashHex);
            msg.append(fl->name);
            msg.append(static_cast<qint64>(fl->size));
            msg.append(link.trimmed());   // the daemon prefers the raw link (AICH, sources)

        } else {
            std::fprintf(stderr,
                         "Only ed2k file links and HTTP Cache configuration links "
                         "are supported.\n");
            return 1;
        }

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
    if (timeoutMs > 0)
        client->setTimeoutMs(timeoutMs);
    client->sendCommand(ipcHost, ipcPort, std::move(msg));
    return QCoreApplication::exec();
}

uint16_t CommandLineExec::portOverride() const
{
    if (m_parser.isSet(m_portOption))
        return m_parser.value(m_portOption).toUShort();
    return 0;
}

QString CommandLineExec::configOverride() const
{
    if (m_parser.isSet(m_configOption))
        return m_parser.value(m_configOption);
    return {};
}

} // namespace eMule
