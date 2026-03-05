#pragma once

/// @file CommandLineExec.h
/// @brief Command-line parsing and one-shot CLI command execution for the daemon.

#include <QCommandLineOption>
#include <QCommandLineParser>

#include <cstdint>

class QCoreApplication;

namespace eMule {

class CommandLineExec {
public:
    /// Parse command-line arguments. Call before anything else.
    void parse(QCoreApplication& app);

    /// True if a CLI command flag (--connect, --add-link, etc.) was set.
    [[nodiscard]] bool hasCommand() const;

    /// Execute the CLI command against the running daemon. Enters event loop.
    /// Returns process exit code (0 = success, 1 = failure).
    int execCommand(QCoreApplication& app);

    /// Port override from --port, or 0 if not set.
    [[nodiscard]] uint16_t portOverride() const;

private:
    QCommandLineParser m_parser;

    QCommandLineOption m_portOption{
        QStringLiteral("port"),
        QStringLiteral("Override IPC port (default: 4712)."),
        QStringLiteral("port")};

    QCommandLineOption m_addLinkOption{
        QStringLiteral("add-link"),
        QStringLiteral("Add a download from an ed2k:// link."),
        QStringLiteral("link")};

    QCommandLineOption m_connectOption{
        {QStringLiteral("c"), QStringLiteral("connect")},
        QStringLiteral("Connect to an eD2K server.")};

    QCommandLineOption m_disconnectOption{
        {QStringLiteral("d"), QStringLiteral("disconnect")},
        QStringLiteral("Disconnect from eD2K server.")};

    QCommandLineOption m_connectKadOption{
        QStringLiteral("connect-kad"),
        QStringLiteral("Start/bootstrap Kademlia.")};

    QCommandLineOption m_disconnectKadOption{
        QStringLiteral("disconnect-kad"),
        QStringLiteral("Stop Kademlia.")};
};

} // namespace eMule
