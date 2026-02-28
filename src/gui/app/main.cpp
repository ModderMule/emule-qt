#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>

#include "app/IpcClient.h"
#include "app/MainWindow.h"
#include "app/UiState.h"
#include "dialogs/OptionsDialog.h"
#include "controls/LogWidget.h"
#include "panels/KadPanel.h"
#include "panels/ServerPanel.h"
#include "panels/TransferPanel.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/Types.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

namespace {

/// Resolve the daemon binary path.
/// Priority: 1) explicit ipc.daemonPath setting  2) next to the GUI binary.
QString resolveDaemonPath()
{
    QString path = eMule::thePrefs.ipcDaemonPath();
    if (!path.isEmpty() && QFileInfo::exists(path))
        return path;

    // Look next to the GUI executable
    const QString appDir = QCoreApplication::applicationDirPath();
    path = appDir + QStringLiteral("/emulecored");
    if (QFileInfo::exists(path))
        return path;

#ifdef Q_OS_WIN
    path = appDir + QStringLiteral("/emulecored.exe");
    if (QFileInfo::exists(path))
        return path;
#endif

    return {};
}

/// Try to start the daemon process. Returns true if launched.
bool launchDaemon(const QString& daemonPath)
{
    if (daemonPath.isEmpty())
        return false;

    eMule::logInfo(QStringLiteral("Launching daemon: %1").arg(daemonPath));
    return QProcess::startDetached(daemonPath, {});
}

/// Copy bundled nodes.dat into configDir if none exists yet.
void seedNodesDat(const QString& configDir)
{
    const QString dest = configDir + QStringLiteral("/nodes.dat");
    if (QFile::exists(dest))
        return;

    // Look for bundled nodes.dat next to the binary, then in known source path
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/nodes.dat"),
#ifdef EMULE_DEV_BUILD
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../../data/config/nodes.dat"),
#endif
    };

    for (const auto& src : candidates) {
        if (QFile::exists(src) && QFile::copy(src, dest)) {
            eMule::logInfo(QStringLiteral("Seeded nodes.dat from %1").arg(src));
            return;
        }
    }
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("eMule Qt"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("eMule"));

    // Load Qt's own translations (dialogs, standard buttons, etc.)
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qt"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    // Load application translations
    QTranslator appTranslator;
    const QStringList translationPaths = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/lang"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/lang"),
#ifdef EMULE_DEV_BUILD
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../../lang"),
#endif
    };
    for (const auto& path : translationPaths) {
        if (appTranslator.load(QLocale(), QStringLiteral("emuleqt"), QStringLiteral("_"), path)) {
            app.installTranslator(&appTranslator);
            break;
        }
    }

    // Load preferences
#ifdef Q_OS_MACOS
    const QString configDir = QDir::homePath() + QStringLiteral("/eMuleQt/Config");
#else
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#endif
    QDir().mkpath(configDir);
    const QString prefsPath = configDir + QStringLiteral("/preferences.yml");
    eMule::thePrefs.load(prefsPath);
    eMule::theUiState.load();

    // Check for --screenshot mode (development aid)
    const bool screenshotMode = app.arguments().contains(QStringLiteral("--screenshot"));
    QString screenshotPath;
    if (screenshotMode) {
        const auto idx = app.arguments().indexOf(QStringLiteral("--screenshot"));
        screenshotPath = (idx + 1 < app.arguments().size())
                             ? app.arguments().at(idx + 1)
                             : QStringLiteral("/tmp/emuleqt_screenshot.png");
    }

    // Create and show main window
    eMule::MainWindow mainWindow;
    if (eMule::theUiState.isWindowMaximized())
        mainWindow.showMaximized();
    else
        mainWindow.show();

    // Seed bundled nodes.dat into config directory if not present
    seedNodesDat(configDir);

    // Parse --tab argument (used by both screenshot and normal mode)
    auto activeTab = eMule::MainWindow::TabKad;
    const auto tabIdx = app.arguments().indexOf(QStringLiteral("--tab"));
    if (tabIdx >= 0 && tabIdx + 1 < app.arguments().size()) {
        const QString tabArg = app.arguments().at(tabIdx + 1).toLower();
        static const std::pair<QString, eMule::MainWindow::Tab> tabNames[] = {
            {QStringLiteral("kad"),        eMule::MainWindow::TabKad},
            {QStringLiteral("servers"),    eMule::MainWindow::TabServers},
            {QStringLiteral("transfers"),  eMule::MainWindow::TabTransfers},
            {QStringLiteral("search"),     eMule::MainWindow::TabSearch},
            {QStringLiteral("shared"),     eMule::MainWindow::TabSharedFiles},
            {QStringLiteral("messages"),   eMule::MainWindow::TabMessages},
            {QStringLiteral("irc"),        eMule::MainWindow::TabIRC},
            {QStringLiteral("statistics"), eMule::MainWindow::TabStatistics},
        };
        activeTab = static_cast<eMule::MainWindow::Tab>(tabArg.toInt());
        for (const auto& [name, value] : tabNames) {
            if (tabArg == name) {
                activeTab = value;
                break;
            }
        }
        mainWindow.switchToTab(activeTab);
    }

    // Parse --subtab argument: switch to a sub-tab within the active panel
    const auto subTabIdx = app.arguments().indexOf(QStringLiteral("--subtab"));
    if (subTabIdx >= 0 && subTabIdx + 1 < app.arguments().size()) {
        const int subtab = app.arguments().at(subTabIdx + 1).toInt();
        if (activeTab == eMule::MainWindow::TabKad)
            mainWindow.kadPanel()->switchToSubTab(subtab);
        else if (activeTab == eMule::MainWindow::TabTransfers)
            mainWindow.transferPanel()->switchToSubTab(subtab);
    }

    // Parse --delay argument: override screenshot delay (default 3000 ms)
    int screenshotDelay = 3000;
    const auto delayIdx = app.arguments().indexOf(QStringLiteral("--delay"));
    if (delayIdx >= 0 && delayIdx + 1 < app.arguments().size())
        screenshotDelay = app.arguments().at(delayIdx + 1).toInt();

    // IPC client — always used to connect to the daemon
    eMule::IpcClient ipcClient;

    if (eMule::thePrefs.ipcEnabled()) {
        const QHostAddress addr(eMule::thePrefs.ipcListenAddress());
        const uint16_t port = eMule::thePrefs.ipcPort();
        ipcClient.setRemotePollingMs(eMule::thePrefs.ipcRemotePollingMs());
        const QString daemonPath = resolveDaemonPath();

        eMule::logInfo(QStringLiteral("Connecting to daemon at %1:%2...")
                           .arg(addr.toString()).arg(port));

        // Wire IPC client to main window and panels
        mainWindow.setIpcClient(&ipcClient);
        mainWindow.kadPanel()->setIpcClient(&ipcClient);
        mainWindow.transferPanel()->setIpcClient(&ipcClient);

        // Wire daemon log messages to the LogWidget
        // PushLogMessage format: [logId(0), category(1), severity(2), message(3), timestamp(4)]
        auto* logWidget = mainWindow.serverPanel()->logWidget();
        QObject::connect(&ipcClient, &eMule::IpcClient::logMessageReceived,
                         logWidget, [logWidget](const eMule::Ipc::IpcMessage& msg) {
            const QString cat = msg.fieldString(1);
            const auto severity = static_cast<QtMsgType>(msg.fieldInt(2));
            const QString text = msg.fieldString(3);
            const int64_t ts = msg.fieldInt(4);

            // Use the daemon's original timestamp if available, else current time
            const QString timestamp = ts > 0
                ? QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("HH:mm:ss"))
                : QString{};

            // Color based on severity
            QString colored;
            if (severity == QtWarningMsg)
                colored = QStringLiteral("<font color='#CC6600'>%1</font>").arg(text.toHtmlEscaped());
            else if (severity >= QtCriticalMsg)
                colored = QStringLiteral("<font color='red'><b>%1</b></font>").arg(text.toHtmlEscaped());
            else
                colored = QStringLiteral("<font color='#3399FF'>%1</font>").arg(text.toHtmlEscaped());

            // Route to the correct tab
            if (cat == QStringLiteral("emule.kad"))
                logWidget->appendKad(colored, timestamp);
            else if (cat == QStringLiteral("emule.server"))
                logWidget->appendServerInfo(colored);
            else if (severity == QtDebugMsg)
                logWidget->appendVerbose(colored, timestamp);
            else
                logWidget->appendLog(colored, timestamp);
        });

        QObject::connect(&ipcClient, &eMule::IpcClient::connected,
                         &mainWindow, [&ipcClient, &mainWindow]() {
            eMule::logInfo(QStringLiteral("Connected to daemon via IPC."));

            // Request initial eD2K connection state
            eMule::Ipc::IpcMessage reqConn(eMule::Ipc::IpcMsgType::GetConnection);
            ipcClient.sendRequest(std::move(reqConn),
                                  [&mainWindow](const eMule::Ipc::IpcMessage& resp) {
                const QCborMap info = resp.fieldMap(1);
                mainWindow.setEd2kStatus(
                    info.value(QStringLiteral("connected")).toBool(),
                    info.value(QStringLiteral("connecting")).toBool(),
                    info.value(QStringLiteral("firewalled")).toBool());
            });

            // Request initial Kad state
            eMule::Ipc::IpcMessage reqKad(eMule::Ipc::IpcMsgType::GetKadStatus);
            ipcClient.sendRequest(std::move(reqKad),
                                  [&mainWindow](const eMule::Ipc::IpcMessage& resp) {
                const QCborMap info = resp.fieldMap(1);
                mainWindow.setKadStatus(
                    info.value(QStringLiteral("running")).toBool(),
                    info.value(QStringLiteral("connected")).toBool());
            });
        });

        // On first connection failure, try launching the daemon binary.
        // IpcClient auto-reconnects with exponential backoff regardless.
        auto weOwnDaemon = std::make_shared<bool>(false);
        QObject::connect(&ipcClient, &eMule::IpcClient::connectionFailed,
                         &app, [daemonPath, weOwnDaemon](const QString& error) {
            eMule::logWarning(QStringLiteral("Daemon not reachable (%1)").arg(error));
            if (!*weOwnDaemon && !daemonPath.isEmpty() && launchDaemon(daemonPath))
                *weOwnDaemon = true;
        });

        // Wire eD2K push events to the status bar
        QObject::connect(&ipcClient, &eMule::IpcClient::serverStateChanged,
                         &mainWindow, [&mainWindow](const eMule::Ipc::IpcMessage& msg) {
            const QCborMap info = msg.fieldMap(0);
            mainWindow.setEd2kStatus(
                info.value(QStringLiteral("connected")).toBool(),
                info.value(QStringLiteral("connecting")).toBool(),
                info.value(QStringLiteral("firewalled")).toBool());
        });

        // Wire Kad push events to the status bar
        QObject::connect(&ipcClient, &eMule::IpcClient::kadUpdated,
                         &mainWindow, [&mainWindow](const eMule::Ipc::IpcMessage& msg) {
            const QCborMap info = msg.fieldMap(0);
            mainWindow.setKadStatus(
                info.value(QStringLiteral("running")).toBool(),
                info.value(QStringLiteral("connected")).toBool());
        });

        // Reset status when IPC connection to daemon is lost
        QObject::connect(&ipcClient, &eMule::IpcClient::disconnected,
                         &mainWindow, [&mainWindow]() {
            mainWindow.setEd2kStatus(false, false, false);
            mainWindow.setKadStatus(false, false);
        });

        // When the GUI closes, shut down the daemon only if we launched it.
        // If the daemon was already running independently, leave it alone.
        QObject::connect(&app, &QCoreApplication::aboutToQuit,
                         &ipcClient, [&ipcClient, weOwnDaemon]() {
            if (*weOwnDaemon && ipcClient.isConnected()) {
                eMule::logInfo(QStringLiteral("Shutting down daemon (we launched it)..."));
                ipcClient.sendShutdown();
            } else {
                ipcClient.disconnectFromDaemon();
            }
        });

        // connectToDaemon enables auto-reconnect internally
        ipcClient.connectToDaemon(addr, port);
    } else {
        eMule::logInfo(QStringLiteral("IPC disabled in settings."));
    }

    // Parse --options argument: open Options dialog at a specific page
    int optionsPage = -1;
    const auto optIdx = app.arguments().indexOf(QStringLiteral("--options"));
    if (optIdx >= 0 && optIdx + 1 < app.arguments().size()) {
        const QString optArg = app.arguments().at(optIdx + 1).toLower();
        static const std::pair<QString, int> pageNames[] = {
            {QStringLiteral("general"),      eMule::OptionsDialog::PageGeneral},
            {QStringLiteral("display"),      eMule::OptionsDialog::PageDisplay},
            {QStringLiteral("connection"),   eMule::OptionsDialog::PageConnection},
            {QStringLiteral("proxy"),        eMule::OptionsDialog::PageProxy},
            {QStringLiteral("server"),       eMule::OptionsDialog::PageServer},
            {QStringLiteral("directories"),  eMule::OptionsDialog::PageDirectories},
            {QStringLiteral("files"),        eMule::OptionsDialog::PageFiles},
            {QStringLiteral("notifications"),eMule::OptionsDialog::PageNotifications},
            {QStringLiteral("statistics"),   eMule::OptionsDialog::PageStatistics},
            {QStringLiteral("irc"),          eMule::OptionsDialog::PageIRC},
            {QStringLiteral("messages"),     eMule::OptionsDialog::PageMessages},
            {QStringLiteral("security"),     eMule::OptionsDialog::PageSecurity},
            {QStringLiteral("scheduler"),    eMule::OptionsDialog::PageScheduler},
            {QStringLiteral("webinterface"), eMule::OptionsDialog::PageWebInterface},
            {QStringLiteral("extended"),     eMule::OptionsDialog::PageExtended},
        };
        optionsPage = optArg.toInt(); // fallback: numeric index
        for (const auto& [name, value] : pageNames) {
            if (optArg == name) {
                optionsPage = value;
                break;
            }
        }
    }

    // In screenshot mode, grab the window after a delay and exit
    if (screenshotMode) {
        QTimer::singleShot(screenshotDelay, &app, [&mainWindow, &screenshotPath, &app, optionsPage]() {
            // If --options was specified, open the dialog and screenshot it
            if (optionsPage >= 0) {
                eMule::OptionsDialog dlg(nullptr, &mainWindow);
                dlg.selectPage(optionsPage);
                dlg.show();
                dlg.repaint();
                QApplication::processEvents();
                QPixmap pixmap = dlg.grab();
                pixmap.save(screenshotPath);
            } else {
                mainWindow.repaint();
                QApplication::processEvents();
                QPixmap pixmap = mainWindow.grab();
                pixmap.save(screenshotPath);
            }
            eMule::logInfo(QStringLiteral("Screenshot saved to %1").arg(screenshotPath));
            app.quit();
        });
    } else if (optionsPage >= 0) {
        // Non-screenshot mode: just open the options dialog
        mainWindow.showOptionsDialog(optionsPage);
    }

    const int result = QApplication::exec();

    // Save UI state (splitter positions, etc.) to preferences
    eMule::theUiState.save();
    eMule::thePrefs.save();

    return result;
}
