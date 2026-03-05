/// @file main.cpp
/// @brief Entry point for the headless eMule core daemon.
///
/// With no command flags, starts the daemon normally.
/// With a command flag (--add-link, --connect, etc.), connects to a running
/// daemon via IPC, sends the command, prints the result, and exits.

#include "CommandLineExec.h"
#include "DaemonApp.h"

#include "app/AppConfig.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QSocketNotifier>

#ifndef Q_OS_WIN
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

static int s_sigFd[2];

static void unixSignalHandler(int)
{
    // Async-signal-safe: just write a byte to wake the event loop
    char a = 1;
    ::write(s_sigFd[1], &a, sizeof(a));
}
#endif

int main(int argc, char* argv[])
{
#ifndef Q_OS_WIN
    // Ignore SIGPIPE — writing to a disconnected client socket must not kill the daemon.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    QCoreApplication app(argc, argv);

#ifndef Q_OS_WIN
    // Self-pipe trick: convert SIGTERM/SIGINT/SIGHUP into Qt events for graceful shutdown
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, s_sigFd);
    QSocketNotifier sigNotifier(s_sigFd[0], QSocketNotifier::Read);
    QObject::connect(&sigNotifier, &QSocketNotifier::activated, &app, [&]() {
        char tmp;
        ::read(s_sigFd[0], &tmp, sizeof(tmp));
        eMule::logInfo(QStringLiteral("Signal received — shutting down gracefully..."));
        QCoreApplication::quit();
    });
    std::signal(SIGTERM, unixSignalHandler);
    std::signal(SIGINT,  unixSignalHandler);
    std::signal(SIGHUP,  unixSignalHandler);
#endif
    QCoreApplication::setApplicationName(QStringLiteral("eMule Qt Core"));
    QCoreApplication::setApplicationVersion(eMule::kAppVersion);
    QCoreApplication::setOrganizationName(QStringLiteral("eMule"));

    eMule::CommandLineExec cli;
    cli.parse(app);

    // -- Load preferences (needed for IPC port in both modes) -----------------

    const QString configDir = eMule::AppConfig::configDir();
    const QString prefsPath = configDir + QStringLiteral("/preferences.yml");
    eMule::thePrefs.load(prefsPath);

    // -- CLI command mode: send to running daemon and exit --------------------

    if (cli.hasCommand())
        return cli.execCommand(app);

    // -- Normal daemon startup ------------------------------------------------

    // Seed bundled config data (webserver assets, template, nodes.dat)
    eMule::AppConfig::seedBundledData(configDir);

    // Enable debug-level output for all emule.* categories so logDebug()
    // messages reach the message handler and are forwarded to the GUI.
    if (eMule::thePrefs.verbose())
        QLoggingCategory::setFilterRules(QStringLiteral("emule.*.debug=true"));

    eMule::logInfo(QStringLiteral("eMule Core Daemon starting..."));

    // Determine IPC target for log message
    QString ipcHost = eMule::thePrefs.ipcListenAddress();
    if (ipcHost.isEmpty())
        ipcHost = QStringLiteral("127.0.0.1");
    uint16_t ipcPort = eMule::thePrefs.ipcPort();
    if (cli.portOverride() != 0)
        ipcPort = cli.portOverride();

    // Create and start daemon
    eMule::DaemonApp daemon;
    if (!daemon.start()) {
        eMule::logError(QStringLiteral("Failed to start daemon"));
        return 1;
    }

    eMule::logInfo(QStringLiteral("Daemon running. IPC listening on %1:%2")
                       .arg(ipcHost)
                       .arg(ipcPort));

    const int result = QCoreApplication::exec();

    daemon.stop();
    eMule::thePrefs.save();

    return result;
}
