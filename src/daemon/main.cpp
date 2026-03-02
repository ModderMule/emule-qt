/// @file main.cpp
/// @brief Entry point for the headless eMule core daemon.

#include "DaemonApp.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QLoggingCategory>
#include <QStandardPaths>

#ifndef Q_OS_WIN
#include <csignal>
#endif

int main(int argc, char* argv[])
{
#ifndef Q_OS_WIN
    // Ignore SIGPIPE — writing to a disconnected client socket must not kill the daemon.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("eMule Qt"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("eMule"));

    // Load preferences
#ifdef Q_OS_MACOS
    const QString configDir = QDir::homePath() + QStringLiteral("/eMuleQt/Config");
#else
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#endif
    QDir().mkpath(configDir);
    const QString prefsPath = configDir + QStringLiteral("/preferences.yml");
    eMule::thePrefs.load(prefsPath);

    // Enable debug-level output for all emule.* categories so logDebug()
    // messages reach the message handler and are forwarded to the GUI.
    if (eMule::thePrefs.verbose())
        QLoggingCategory::setFilterRules(QStringLiteral("emule.*.debug=true"));

    eMule::logInfo(QStringLiteral("eMule Core Daemon starting..."));

    // Create and start daemon
    eMule::DaemonApp daemon;
    if (!daemon.start()) {
        eMule::logError(QStringLiteral("Failed to start daemon"));
        return 1;
    }

    eMule::logInfo(QStringLiteral("Daemon running. IPC listening on %1:%2")
                       .arg(eMule::thePrefs.ipcListenAddress())
                       .arg(eMule::thePrefs.ipcPort()));

    const int result = QCoreApplication::exec();

    daemon.stop();
    eMule::thePrefs.save();

    return result;
}
