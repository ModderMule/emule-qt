#include <QApplication>
#include <QFile>
#include <QFileOpenEvent>
#include <QFileInfo>
#include <QFont>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QSplashScreen>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>

#include "app/IpcClient.h"
#include "app/MainWindow.h"
#include "app/PowerManager.h"
#include "app/VersionChecker.h"
#include "app/UiState.h"
#include "dialogs/CoreConnectDialog.h"
#include "dialogs/OptionsDialog.h"
#include "controls/LogWidget.h"
#include "panels/IrcPanel.h"
#include "panels/KadPanel.h"
#include "panels/MessagesPanel.h"
#include "panels/SearchPanel.h"
#include "panels/ServerPanel.h"
#include "panels/SharedFilesPanel.h"
#include "panels/StatisticsPanel.h"
#include "panels/TransferPanel.h"
#include "app/AppConfig.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/Types.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "protocol/ED2KLink.h"

using eMule::ED2KFileLink;
using eMule::parseED2KLink;

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


/// Process an ed2k:// link string — prompts user and downloads via IPC.
void handleEd2kUrl(const QString& urlStr, eMule::MainWindow& mainWindow, eMule::IpcClient& ipcClient)
{
    if (!urlStr.startsWith(QStringLiteral("ed2k://"), Qt::CaseInsensitive))
        return;
    if (!ipcClient.isConnected())
        return;

    const QStringList lines = urlStr.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QStringList fileNames;
    for (const QString& line : lines) {
        auto parsed = parseED2KLink(line.trimmed());
        if (!parsed) continue;
        if (auto* fl = std::get_if<ED2KFileLink>(&*parsed))
            fileNames << fl->name;
    }
    if (fileNames.isEmpty())
        return;

    if (eMule::thePrefs.bringToFrontOnLinkClick()) {
        mainWindow.raise();
        mainWindow.activateWindow();
    }

    const QString preview = fileNames.join(QLatin1Char('\n'));
    const auto result = QMessageBox::question(
        &mainWindow, QObject::tr("eD2K Link"),
        QObject::tr("Do you want to download the following file(s)?\n\n%1").arg(preview),
        QMessageBox::Yes | QMessageBox::No);
    if (result != QMessageBox::Yes)
        return;

    for (const QString& line : lines) {
        auto parsed = parseED2KLink(line.trimmed());
        if (!parsed) continue;
        auto* fl = std::get_if<ED2KFileLink>(&*parsed);
        if (!fl) continue;

        QString hashHex;
        for (uint8_t b : fl->hash)
            hashHex += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));

        eMule::Ipc::IpcMessage msg(eMule::Ipc::IpcMsgType::DownloadSearchFile);
        msg.append(hashHex);
        msg.append(fl->name);
        msg.append(static_cast<int64_t>(fl->size));
        ipcClient.sendRequest(std::move(msg));
    }
    mainWindow.switchToTab(eMule::MainWindow::TabTransfers);
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("eMule Qt"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("eMule"));

    // Load preferences early so language setting is available for translators
    const QString configDir = eMule::AppConfig::configDir();
    const QString prefsPath = configDir + QStringLiteral("/preferences.yml");
    eMule::thePrefs.load(prefsPath);

    // Determine locale: user-selected language or system default
    QLocale appLocale;
    if (!eMule::thePrefs.language().isEmpty())
        appLocale = QLocale(eMule::thePrefs.language());

    // Load Qt's own translations (dialogs, standard buttons, etc.)
    QTranslator qtTranslator;
    if (qtTranslator.load(appLocale, QStringLiteral("qt"), QStringLiteral("_"),
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
        if (appTranslator.load(appLocale, QStringLiteral("emuleqt"), QStringLiteral("_"), path)) {
            app.installTranslator(&appTranslator);
            break;
        }
    }

    // Seed bundled config data (webserver assets, template, nodes.dat)
    eMule::AppConfig::seedBundledData(configDir);
    eMule::theUiState.load();

    // Splash screen
    QSplashScreen* splash = nullptr;
    if (eMule::thePrefs.showSplashScreen()) {
        splash = new QSplashScreen(QPixmap(QStringLiteral(":/images/Logo.jpg")));
        splash->show();
        QApplication::processEvents();
    }

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

    if (splash) {
        splash->finish(&mainWindow);
        delete splash;
        splash = nullptr;
    }

    // Apply custom font on startup
    if (!eMule::thePrefs.logFont().isEmpty()) {
        QFont f;
        f.fromString(eMule::thePrefs.logFont());
        mainWindow.serverPanel()->logWidget()->setCustomFont(f);
        mainWindow.messagesPanel()->setCustomFont(f);
        mainWindow.ircPanel()->setCustomFont(f);
    }

    // Power management — prevent idle sleep if enabled
    eMule::PowerManager powerManager;
    if (eMule::thePrefs.preventStandby())
        powerManager.setPreventStandby(true);

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
        ipcClient.setRemotePollingMs(eMule::thePrefs.ipcRemotePollingMs());
        if (!eMule::thePrefs.ipcTokens().isEmpty())
            ipcClient.setAuthToken(eMule::thePrefs.ipcTokens().first());

        // Determine connection target and whether we should manage a local daemon
        QString connectHost = eMule::thePrefs.ipcListenAddress();
        uint16_t port = eMule::thePrefs.ipcPort();
        QString daemonPath;
        bool isRemote = false;

        const QHostAddress configAddr(connectHost);
        if (!configAddr.isNull() && !configAddr.isLoopback()
            && connectHost != QStringLiteral("localhost") && !connectHost.isEmpty()) {
            // FAST PATH: remote address configured — skip daemon discovery
            isRemote = true;
            eMule::logInfo(QStringLiteral("Remote core configured at %1:%2 — skipping local daemon discovery.")
                               .arg(connectHost).arg(port));
        } else {
            // Local path — try to find the daemon binary
            daemonPath = resolveDaemonPath();
            if (daemonPath.isEmpty()) {
                // LAST FALLBACK: no local daemon binary found — show dialog
                eMule::logWarning(QStringLiteral("No local emulecored binary found."));
                eMule::CoreConnectDialog dlg(&mainWindow);
                if (dlg.exec() == QDialog::Rejected)
                    return 0;  // User chose Exit

                connectHost = dlg.address();
                port = dlg.port();
                ipcClient.setAuthToken(dlg.token());
                isRemote = true;

                // Persist to prefs so next launch uses the remote fast-path
                eMule::thePrefs.setIpcListenAddress(connectHost);
                eMule::thePrefs.setIpcPort(port);
                if (dlg.saveToken())
                    eMule::thePrefs.setIpcTokens({dlg.token()});
            }
        }

        eMule::logInfo(QStringLiteral("Connecting to daemon at %1:%2...")
                           .arg(connectHost).arg(port));

        // Wire IPC client to main window and panels
        mainWindow.setIpcClient(&ipcClient);
        mainWindow.kadPanel()->setIpcClient(&ipcClient);
        mainWindow.serverPanel()->setIpcClient(&ipcClient);
        mainWindow.transferPanel()->setIpcClient(&ipcClient);
        mainWindow.searchPanel()->setIpcClient(&ipcClient);
        mainWindow.sharedFilesPanel()->setIpcClient(&ipcClient);
        mainWindow.messagesPanel()->setIpcClient(&ipcClient);
        mainWindow.statisticsPanel()->setIpcClient(&ipcClient);

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
            else if (severity == QtCriticalMsg || severity == QtFatalMsg)
                colored = QStringLiteral("<font color='red'><b>%1</b></font>").arg(text.toHtmlEscaped());
            else
                colored = QStringLiteral("<font color='#3399FF'>%1</font>").arg(text.toHtmlEscaped());

            // Route to the correct tab
            if (cat == QStringLiteral("emule.kad"))
                logWidget->appendKad(colored, timestamp);
            else if (cat == QStringLiteral("emule.server"))
                logWidget->appendServerInfo(colored);
            else if (severity == QtDebugMsg || severity == QtWarningMsg)
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
                    info.value(QStringLiteral("connected")).toBool(),
                    info.value(QStringLiteral("firewalled")).toBool());
            });

            // Auto version check after connection is established
            QTimer::singleShot(2000, &mainWindow, [&mainWindow]() {
                auto* vc = mainWindow.findChild<eMule::VersionChecker*>();
                if (vc) vc->check(false);
            });
        });

        // On first connection failure, try launching the daemon binary (local path only).
        // IpcClient auto-reconnects with exponential backoff regardless.
        auto weOwnDaemon = std::make_shared<bool>(false);
        if (!isRemote && !daemonPath.isEmpty()) {
            QObject::connect(&ipcClient, &eMule::IpcClient::connectionFailed,
                             &app, [daemonPath, weOwnDaemon](const QString& error) {
                eMule::logWarning(QStringLiteral("Daemon not reachable (%1)").arg(error));
                if (!*weOwnDaemon && launchDaemon(daemonPath))
                    *weOwnDaemon = true;
            });
        } else {
            QObject::connect(&ipcClient, &eMule::IpcClient::connectionFailed,
                             &app, [](const QString& error) {
                eMule::logWarning(QStringLiteral("Daemon not reachable (%1)").arg(error));
            });
        }

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
                info.value(QStringLiteral("connected")).toBool(),
                info.value(QStringLiteral("firewalled")).toBool());
            mainWindow.setNetworkStats(
                static_cast<quint32>(info.value(QStringLiteral("users")).toInteger()),
                static_cast<quint32>(info.value(QStringLiteral("files")).toInteger()));
        });

        // Wire notification push events to system tray popups
        QObject::connect(&ipcClient, &eMule::IpcClient::downloadAdded,
                         &mainWindow, [&mainWindow](const eMule::Ipc::IpcMessage&) {
            if (eMule::thePrefs.notifyOnDownloadAdded())
                mainWindow.showNotification(
                    QObject::tr("Download Added"),
                    QObject::tr("A new download has been added."));
        });
        QObject::connect(&ipcClient, &eMule::IpcClient::chatMessageReceived,
                         &mainWindow, [&mainWindow](const eMule::Ipc::IpcMessage& msg) {
            if (eMule::thePrefs.notifyOnChat()) {
                const QString user = msg.fieldString(1);
                const QString text = msg.fieldString(2);
                mainWindow.showNotification(
                    QObject::tr("Chat Message from %1").arg(user), text);
            }
        });
        QObject::connect(&ipcClient, &eMule::IpcClient::logMessageReceived,
                         &mainWindow, [&mainWindow](const eMule::Ipc::IpcMessage& msg) {
            if (eMule::thePrefs.notifyOnLog()) {
                const auto severity = static_cast<QtMsgType>(msg.fieldInt(2));
                // Only notify on warnings and above to avoid spamming
                if (severity >= QtWarningMsg) {
                    const QString text = msg.fieldString(3);
                    mainWindow.showNotification(
                        QObject::tr("Log Entry"), text);
                }
            }
        });
        QObject::connect(&ipcClient, &eMule::IpcClient::serverStateChanged,
                         &mainWindow, [&mainWindow](const eMule::Ipc::IpcMessage& msg) {
            if (eMule::thePrefs.notifyOnUrgent()) {
                const QCborMap info = msg.fieldMap(0);
                if (!info.value(QStringLiteral("connected")).toBool()
                    && !info.value(QStringLiteral("connecting")).toBool()) {
                    mainWindow.showNotification(
                        QObject::tr("Connection Lost"),
                        QObject::tr("Server connection has been lost."));
                }
            }
        });

        // Reset status when IPC connection to daemon is lost
        QObject::connect(&ipcClient, &eMule::IpcClient::disconnected,
                         &mainWindow, [&mainWindow]() {
            mainWindow.setEd2kStatus(false, false, false);
            mainWindow.setKadStatus(false, false, false);
            mainWindow.setNetworkStats(0, 0);
            mainWindow.updateTransferRates(0.0, 0.0, 0.0, 0.0);
        });

        // Poll transfer rates for the status bar (1 second interval)
        auto* rateTimer = new QTimer(&app);
        rateTimer->setInterval(1000);
        QObject::connect(rateTimer, &QTimer::timeout, &app,
                         [&ipcClient, &mainWindow, &configDir]() {
            if (!ipcClient.isConnected())
                return;
            eMule::Ipc::IpcMessage req(eMule::Ipc::IpcMsgType::GetStats);
            ipcClient.sendRequest(std::move(req),
                                  [&mainWindow, &configDir](const eMule::Ipc::IpcMessage& resp) {
                if (resp.type() != eMule::Ipc::IpcMsgType::Result || !resp.fieldBool(0))
                    return;
                const QCborMap stats = resp.fieldMap(1);
                auto val = [&](QLatin1StringView k) {
                    return stats.value(QString(k)).toDouble();
                };
                const double upRate = val(QLatin1StringView("rateUp"));
                const double downRate = val(QLatin1StringView("rateDown"));
                mainWindow.updateTransferRates(
                    upRate, downRate,
                    val(QLatin1StringView("upOverheadRate")),
                    val(QLatin1StringView("downOverheadRate")));

                // Update MiniMule popup stats
                const int completedDl = static_cast<int>(
                    stats.value(QStringLiteral("completedDownloads")).toInteger());
                const qint64 freeSpace =
                    stats.value(QStringLiteral("freeTempSpace")).toInteger();
                mainWindow.updateMiniMule(completedDl, freeSpace);

                // Write online signature file if enabled
                if (eMule::thePrefs.enableOnlineSignature()) {
                    int connStatus = 0;
                    if (mainWindow.isEd2kConnected()) connStatus |= 1;
                    if (mainWindow.isKadConnected()) connStatus |= 2;
                    QFile sigFile(configDir + QStringLiteral("/onlinesig.dat"));
                    if (sigFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        QTextStream ts(&sigFile);
                        ts << connStatus << '\n'
                           << QString::number(upRate, 'f', 1) << '\n'
                           << QString::number(downRate, 'f', 1) << '\n'
                           << "0\n";
                    }
                }
            });
        });
        QObject::connect(&ipcClient, &eMule::IpcClient::connected,
                         rateTimer, [rateTimer]() { rateTimer->start(); });
        QObject::connect(&ipcClient, &eMule::IpcClient::disconnected,
                         rateTimer, [rateTimer]() { rateTimer->stop(); });

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

        // Connect using hostname overload (handles both IPs and hostnames)
        ipcClient.connectToDaemon(connectHost, port);
    } else {
        eMule::logInfo(QStringLiteral("IPC disabled in settings."));
    }

    // macOS: handle ed2k:// URLs sent via Apple Events (QFileOpenEvent)
    // Install event filter on QApplication to catch FileOpen events.
    struct Ed2kEventFilter : public QObject {
        eMule::MainWindow& mw;
        eMule::IpcClient& ipc;
        Ed2kEventFilter(eMule::MainWindow& w, eMule::IpcClient& i, QObject* p)
            : QObject(p), mw(w), ipc(i) {}
        bool eventFilter(QObject* obj, QEvent* event) override {
            if (event->type() == QEvent::FileOpen) {
                auto* foe = static_cast<QFileOpenEvent*>(event);
                const QString url = foe->url().toString();
                if (url.startsWith(QStringLiteral("ed2k://"), Qt::CaseInsensitive)) {
                    QTimer::singleShot(0, &mw, [this, url]() {
                        handleEd2kUrl(url, mw, ipc);
                    });
                    return true;
                }
            }
            return QObject::eventFilter(obj, event);
        }
    };
    app.installEventFilter(new Ed2kEventFilter(mainWindow, ipcClient, &app));

    // Check command-line args for ed2k:// URLs (Linux/Windows: browser passes URL as arg)
    for (const QString& arg : app.arguments()) {
        if (arg.startsWith(QStringLiteral("ed2k://"), Qt::CaseInsensitive)) {
            // Delay so IPC connection has time to establish
            QTimer::singleShot(3000, &mainWindow, [arg, &mainWindow, &ipcClient]() {
                handleEd2kUrl(arg, mainWindow, ipcClient);
            });
            break;
        }
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
                eMule::OptionsDialog dlg(nullptr, mainWindow.statisticsPanel(), &mainWindow);
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

    // Write disconnected online signature on exit
    if (eMule::thePrefs.enableOnlineSignature()) {
        QFile sigFile(configDir + QStringLiteral("/onlinesig.dat"));
        if (sigFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&sigFile);
            ts << "0\n0.0\n0.0\n0\n";
        }
    }

    // Save UI state (splitter positions, etc.) to preferences
    eMule::theUiState.save();
    eMule::thePrefs.save();

    return result;
}
