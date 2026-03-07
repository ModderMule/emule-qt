#include "pch.h"
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
#include <QSocketNotifier>
#include <QSplashScreen>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>

#ifndef Q_OS_WIN
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

static int s_sigFd[2];

static void unixSignalHandler(int)
{
    char a = 1;
    ::write(s_sigFd[1], &a, sizeof(a));
}
#endif

#include "app/AppConfig.h"
#include "app/CommandLineExec.h"
#include "app/IpcClient.h"
#include "app/MainWindow.h"
#include "app/PowerManager.h"
#include "app/VersionChecker.h"
#include "app/UiState.h"
#include "dialogs/CoreConnectDialog.h"
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
#include "controls/DownloadListModel.h"
#include "controls/SharedFilesModel.h"
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
/// Priority: 1) "local" = assume daemon is running  2) explicit path  3) next to GUI binary.
QString resolveDaemonPath()
{
    QString path = eMule::thePrefs.ipcDaemonPath();

    // "local" means connect to localhost without binary discovery
    if (path == QStringLiteral("local"))
        return path;

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
    auto* dlModel = mainWindow.transferPanel()->downloadModel();
    auto* sfModel = mainWindow.sharedFilesPanel()->sharedFilesModel();
    for (const QString& line : lines) {
        auto parsed = parseED2KLink(line.trimmed());
        if (!parsed) continue;
        if (auto* fl = std::get_if<ED2KFileLink>(&*parsed)) {
            QString hashHex;
            for (uint8_t b : fl->hash)
                hashHex += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));
            if ((dlModel && dlModel->containsHash(hashHex))
                || (sfModel && sfModel->containsHash(hashHex)))
                continue;
            fileNames << fl->name;
        }
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
    QApplication::setApplicationVersion(eMule::kAppVersion);
    QApplication::setOrganizationName(QStringLiteral("eMule"));

#ifndef Q_OS_WIN
    // Self-pipe trick: convert SIGTERM/SIGINT/SIGHUP into Qt events for graceful shutdown
    std::signal(SIGPIPE, SIG_IGN);
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

    // -- Command-line parsing -------------------------------------------------

    eMule::CommandLineExec cli;
    cli.parse(app);

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

    // Apply --tab and --subtab arguments
    cli.applyTabArgs(mainWindow);

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
                // No daemon path configured — show connect dialog immediately
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
            } else if (daemonPath == QStringLiteral("local")) {
                // "local" mode — assume daemon is already running on localhost
                eMule::logInfo(QStringLiteral("Local mode — connecting to localhost:%1").arg(port));
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
                         &mainWindow, [&ipcClient, &mainWindow, prefsPath]() {
            eMule::logInfo(QStringLiteral("Connected to daemon via IPC."));

            // Reload preferences from disk so every reconnect starts from a clean
            // YAML baseline — matching the first-connect flow where thePrefs.load()
            // runs before the async GetPreferences/updateFromCbor overlay.
            eMule::thePrefs.load(prefsPath);

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

            // Sync GUI thePrefs with daemon's live values so OptionsDialog
            // always has correct data even if opened before the async
            // GetPreferences response inside the dialog arrives.
            eMule::Ipc::IpcMessage reqPrefs(eMule::Ipc::IpcMsgType::GetPreferences);
            ipcClient.sendRequest(std::move(reqPrefs),
                                  [](const eMule::Ipc::IpcMessage& resp) {
                eMule::thePrefs.updateFromCbor(resp.fieldMap(1));
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
        if (!isRemote && daemonPath == QStringLiteral("local")) {
            // "local" mode: daemon should already be running; show dialog on failure
            auto dialogShown = std::make_shared<bool>(false);
            QObject::connect(&ipcClient, &eMule::IpcClient::connectionFailed,
                             &mainWindow, [&mainWindow, &ipcClient, dialogShown](const QString& error) {
                eMule::logWarning(QStringLiteral("Daemon not reachable (%1)").arg(error));
                if (*dialogShown)
                    return;
                *dialogShown = true;

                eMule::CoreConnectDialog dlg(&mainWindow);
                if (dlg.exec() == QDialog::Accepted) {
                    eMule::thePrefs.setIpcListenAddress(dlg.address());
                    eMule::thePrefs.setIpcPort(dlg.port());
                    ipcClient.setAuthToken(dlg.token());
                    if (dlg.saveToken())
                        eMule::thePrefs.setIpcTokens({dlg.token()});
                    ipcClient.connectToDaemon(QHostAddress(dlg.address()), dlg.port());
                }
            });
        } else if (!isRemote && !daemonPath.isEmpty()) {
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

                // Update stream token for preview streaming
                if (auto st = stats.value(QStringLiteral("streamToken")); st.isString())
                    mainWindow.transferPanel()->setStreamToken(st.toString());

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

    // Handle ed2k:// positional args and --screenshot/--options
    cli.handleEd2kLinks(mainWindow, ipcClient);
    cli.setupScreenshotTimer(app, mainWindow);

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
