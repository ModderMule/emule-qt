#include "utils/Ed2kLinkImporter.h"
#include "utils/TooltipDelayStyle.h"
#include "pch.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHostAddress>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QSocketNotifier>
#include <QSplashScreen>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#ifndef Q_OS_WIN
#include <cerrno>
#include <csignal>
#include <optional>
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
#include "app/CoreStartWatch.h"
#include "app/Ed2kSchemeHandler.h"
#include "app/ExternalLinkHandler.h"
#include "app/IpcClient.h"
#include "app/MainWindow.h"
#include "app/SingleInstance.h"
#include "app/UiState.h"
#include "utils/Appearance.h"
#include "utils/ColumnTooltip.h"
#include "utils/CountryFlags.h"
#include "utils/FileAssociation.h"
#include "utils/StatusBarNotifier.h"
#include "dialogs/CoreConnectDialog.h"
#include "controls/LogWidget.h"
#include "panels/IrcPanel.h"
#include "panels/KadPanel.h"
#include "panels/MessagesPanel.h"
#include "utils/NotifierText.h"
#include "panels/SearchPanel.h"
#include "panels/ServerPanel.h"
#include "panels/SharedFilesPanel.h"
#include "panels/StatisticsPanel.h"
#include "panels/TransferPanel.h"
#ifdef EMULE_HAVE_BITTORRENT
#include "panels/TorrentPanel.h"
#endif
#include "panels/UsenetPanel.h"
#include "prefs/Preferences.h"
#include "utils/BundleEnvironment.h"
#include "utils/CrashHandler.h"
#include "utils/Log.h"

#include "IpcMessage.h"

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

/// The configured core address is another machine: nothing to discover or launch here.
bool isRemoteCoreAddress(const QString& host)
{
    const QHostAddress addr(host);   // "" and "localhost" parse as null
    return !addr.isNull() && !addr.isLoopback();
}

/// Nothing listens on the local IPC port, so no core is running. A bind probe, as
/// the daemon's own: a connect would show up there as a client.
bool ipcPortFree(const QString& host, uint16_t port)
{
    const QHostAddress addr(host);
    QTcpServer probe;
    return probe.listen(addr.isNull() ? QHostAddress(QHostAddress::LocalHost) : addr, port);
}

/// This profile's core is not running, but another program holds its ports. A new
/// --config starts on the default IPC port, so connecting would show the other
/// profile's core (issue #49); a copied profile shares the TCP/UDP ports too. Moves
/// each taken one to a free port and saves it, so the core started next reads the same.
/// TCP/UDP/BitTorrent only on @p firstStart: later they may be forwarded on the router.
void claimFreePorts(const QString& configDir, bool firstStart)
{
    using eMule::Preferences;
    if (eMule::AppConfig::coreRunningFor(configDir))
        return;   // ours holds them
    bool changed = false;

    const QString host = eMule::thePrefs.ipcListenAddress();
    if (const uint16_t taken = eMule::thePrefs.ipcPort(); !ipcPortFree(host, taken)) {
        const QList<uint16_t> ownPorts{eMule::thePrefs.port(), eMule::thePrefs.udpPort(),
                                       eMule::thePrefs.webServerPort(), eMule::thePrefs.btListenPort()};
        uint16_t freePort = 0;
        for (uint32_t port = taken + 1u; port <= 65535u && port <= taken + 100u; ++port) {
            if (!ownPorts.contains(static_cast<uint16_t>(port))
                && ipcPortFree(host, static_cast<uint16_t>(port))) {
                freePort = static_cast<uint16_t>(port);
                break;
            }
        }
        if (freePort != 0) {
            eMule::logInfo(QStringLiteral("IPC port %1 is taken by another program, using %2")
                               .arg(taken).arg(freePort));
            eMule::thePrefs.setIpcPort(freePort);
            changed = true;
        } else {
            eMule::logWarning(QStringLiteral("IPC port %1 is taken by another program, "
                                             "and no free port follows it").arg(taken));
        }
    }

    if (const uint16_t taken = eMule::thePrefs.port();
        firstStart && !Preferences::tcpPortFree(taken)) {
        const uint16_t freePort = Preferences::randomTCPPort();
        eMule::logWarning(QStringLiteral("TCP port %1 is taken by another program, using %2")
                              .arg(taken).arg(freePort));
        eMule::thePrefs.setPort(freePort);
        changed = true;
    }
    // 0 = UDP off
    if (const uint16_t taken = eMule::thePrefs.udpPort();
        firstStart && taken != 0 && !Preferences::udpPortFree(taken)) {
        const uint16_t freePort = Preferences::randomUDPPort();
        eMule::logWarning(QStringLiteral("UDP port %1 is taken by another program, using %2")
                              .arg(taken).arg(freePort));
        eMule::thePrefs.setUdpPort(freePort);
        changed = true;
    }
    // BT listens on TCP and UDP alike; stay off the ports above
    if (const uint16_t taken = eMule::thePrefs.btListenPort();
        firstStart && (!Preferences::tcpPortFree(taken) || !Preferences::udpPortFree(taken))) {
        const QList<uint16_t> ownPorts{eMule::thePrefs.port(), eMule::thePrefs.udpPort(),
                                       eMule::thePrefs.webServerPort(), eMule::thePrefs.ipcPort()};
        const uint16_t freePort = Preferences::randomPort([&ownPorts](uint16_t port) {
            return !ownPorts.contains(port) && Preferences::tcpPortFree(port)
                   && Preferences::udpPortFree(port);
        });
        eMule::logWarning(QStringLiteral("BitTorrent port %1 is taken by another program, using %2")
                              .arg(taken).arg(freePort));
        eMule::thePrefs.setBtListenPort(freePort);
        changed = true;
    }

    if (changed)
        eMule::thePrefs.save();
}

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
/// A terminal emulator and the arguments that make it run the command after them.
struct Terminal {
    QString program;
    QStringList args;
};

/// The user's terminal: $TERMINAL, then Debian's x-terminal-emulator, then the
/// common desktop ones. Each takes the command its own way.
std::optional<Terminal> findTerminal()
{
    static const QList<std::pair<QString, QStringList>> known = {
        {QStringLiteral("x-terminal-emulator"), {QStringLiteral("-e")}},
        {QStringLiteral("gnome-terminal"),      {QStringLiteral("--")}},
        {QStringLiteral("kgx"),                 {QStringLiteral("--")}},
        {QStringLiteral("ptyxis"),              {QStringLiteral("--")}},
        {QStringLiteral("konsole"),             {QStringLiteral("-e")}},
        {QStringLiteral("xfce4-terminal"),      {QStringLiteral("-x")}},
        {QStringLiteral("mate-terminal"),       {QStringLiteral("-x")}},
        {QStringLiteral("lxterminal"),          {QStringLiteral("-e")}},
        {QStringLiteral("tilix"),               {QStringLiteral("-e")}},
        {QStringLiteral("terminator"),          {QStringLiteral("-x")}},
        {QStringLiteral("kitty"),               {}},
        {QStringLiteral("wezterm"),             {QStringLiteral("start"), QStringLiteral("--")}},
        {QStringLiteral("alacritty"),           {QStringLiteral("-e")}},
        {QStringLiteral("foot"),                {}},
        {QStringLiteral("xterm"),               {QStringLiteral("-e")}},
    };

    // $TERMINAL is a bare program name or path; -e is the common convention
    if (const QString env = qEnvironmentVariable("TERMINAL").trimmed(); !env.isEmpty()) {
        if (const QString path = QStandardPaths::findExecutable(env); !path.isEmpty()) {
            const QString name = QFileInfo(path).fileName();
            for (const auto& [program, args] : known) {
                if (program == name)
                    return Terminal{path, args};
            }
            return Terminal{path, {QStringLiteral("-e")}};
        }
    }
    for (const auto& [program, args] : known) {
        if (const QString path = QStandardPaths::findExecutable(program); !path.isEmpty())
            return Terminal{path, args};
    }
    return std::nullopt;
}
#endif

/// Try to start the daemon process. Returns true if launched.
/// @param holdConnect the first start wizard is about to run: the core must not
///        auto-connect before the ports are chosen.
/// @param pid the core's process id, 0 where it is not known (started in a console).
bool launchDaemon(const QString& daemonPath, bool holdConnect, qint64& pid)
{
    pid = 0;
    if (daemonPath.isEmpty())
        return false;

    eMule::logInfo(QStringLiteral("Launching daemon: %1").arg(daemonPath));

    QStringList daemonArgs;
    // Our --config goes along, or the two would read different preferences.yml
    // files -- different ipc ports included. Absolute: a terminal starts elsewhere.
    if (const QString dir = eMule::AppConfig::configDirOverride(); !dir.isEmpty())
        daemonArgs << QStringLiteral("--config") << QDir(dir).absolutePath();
    if (holdConnect)
        daemonArgs << QStringLiteral("--hold-connect");

    if (eMule::thePrefs.startCoreWithConsole()) {
#if defined(Q_OS_MACOS)
        // Quoted for the shell, then escaped for the AppleScript string around it
        QStringList words;
        for (const QString& word : QStringList{daemonPath} + daemonArgs)
            words << QLatin1Char('\'') + QString(word).replace(QLatin1Char('\''), QStringLiteral("'\\''"))
                         + QLatin1Char('\'');
        QString command = words.join(QLatin1Char(' '));
        command.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        command.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        const QString script = QStringLiteral("tell application \"Terminal\" to do script \"%1\"")
                                   .arg(command);
        return QProcess::startDetached(QStringLiteral("osascript"), {QStringLiteral("-e"), script});
#elif defined(Q_OS_WIN)
        return QProcess::startDetached(QStringLiteral("cmd"),
                                       QStringList{QStringLiteral("/k"), daemonPath} + daemonArgs);
#else
        // No xterm on most desktops (issue #32): the desktop's own terminal, and
        // a plain start when there is none or it will not run
        if (const auto terminal = findTerminal()) {
            if (QProcess::startDetached(terminal->program,
                                        terminal->args + QStringList{daemonPath} + daemonArgs))
                return true;
            eMule::logWarning(QStringLiteral("Could not start the core in %1, starting it without console")
                                  .arg(terminal->program));
        } else {
            eMule::logWarning(QStringLiteral("No terminal found for \"Start core with console\", "
                                             "starting the core without console"));
        }
#endif
    }

    // The tarball launcher exec's the binary, so this is the core's own pid
    return QProcess::startDetached(daemonPath, daemonArgs, QString(), &pid);
}

/// The process still runs. A core that died on start never opens its port, and
/// waiting out the start grace for it tells the user nothing.
bool processAlive(qint64 pid)
{
#ifdef Q_OS_WIN
    const HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                         static_cast<DWORD>(pid));
    if (!process)
        return false;
    DWORD exitCode = 0;
    const bool alive = ::GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    ::CloseHandle(process);
    return alive;
#else
    // Signal 0 only checks; EPERM means it exists but is not ours to signal
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

/// Stop a core we started and cannot reach over IPC: busy enough to miss a
/// keepalive, it would otherwise outlive the GUI and greet the next one already
/// connected. The port still being held tells it from a core that is gone.
void terminateUnreachableDaemon(qint64 pid)
{
    if (pid <= 0 || ipcPortFree(eMule::thePrefs.ipcListenAddress(), eMule::thePrefs.ipcPort()))
        return;
#ifndef Q_OS_WIN
    // The core turns SIGTERM into a graceful stop
    eMule::logInfo(QStringLiteral("Core not reachable, stopping it by signal (pid %1)").arg(pid));
    ::kill(static_cast<pid_t>(pid), SIGTERM);
#else
    // ToDo: Windows has no signal for this; the core needs a named event to wait on
    eMule::logWarning(QStringLiteral("Core not reachable at exit, left running (pid %1)").arg(pid));
#endif
}

/// The donkey at every size we have art for. One file per size rather than
/// Mule.ico: the taskbar and Alt-Tab ask for 48 and up, which that one lacks.
QIcon appIcon()
{
    QIcon icon;
    for (const int size : {16, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/app/emuleqt-%1.png").arg(size), QSize(size, size));
    return icon;
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    // Before anything is started from here: the core, xdg-open, a video player
    eMule::restoreSystemLibraryPath();

    QApplication app(argc, argv);

    // Before anything can log: scripts/debug-gui.sh runs the daemon and the GUI
    // into one terminal, so every console line needs a time and a process tag.
    eMule::installConsoleMessagePattern(QStringLiteral("gui"));
    // Installed here, not in LogWidget, so the file also captures the lines
    // emitted before the window exists. The sink stays closed until
    // LogWidget::applyLogFileSettings() below opens it.
    eMule::installLogFileMessageHandler();

#ifdef Q_OS_LINUX
    // Shown as the tray item's name (StatusNotifierItem Id/Title, issue #38)
    QApplication::setApplicationName(QStringLiteral("eMuleQt"));
#else
    QApplication::setApplicationName(QStringLiteral("eMule Qt"));
#endif
    QApplication::setApplicationVersion(eMule::kAppVersion);
    QApplication::setOrganizationName(QStringLiteral("eMule"));
    app.setWindowIcon(appIcon());
#ifdef Q_OS_LINUX
    // Wayland has no per-window icon: the compositor looks up <app id>.desktop.
    QGuiApplication::setDesktopFileName(QLatin1String(eMule::gui::FileAssociation::kDesktopId));
#endif

    // Before anything spins the event loop. On macOS a browser click on an ed2k:// link
    // launches us and the Apple Event follows within moments, so it can land during the
    // splash screen's processEvents() below — long before there is a MainWindow, an
    // IpcClient or a daemon. The handler queues what it cannot act on yet; installing it
    // any later means a cold start from a link click silently does nothing.
    eMule::ExternalLinkHandler linkHandler;
    app.installEventFilter(&linkHandler);

#ifndef Q_OS_WIN
    // Self-pipe trick: convert SIGTERM/SIGINT/SIGHUP into Qt events for graceful shutdown
    std::signal(SIGPIPE, SIG_IGN);
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, s_sigFd);
    QSocketNotifier sigNotifier(s_sigFd[0], QSocketNotifier::Read);
    QObject::connect(&sigNotifier, &QSocketNotifier::activated, &app, [&]() {
        char tmp;
        ::read(s_sigFd[0], &tmp, sizeof(tmp));
        static bool quitting = false;
        if (quitting)
            return;   // a repeat while we wind down
        quitting = true;
        eMule::logInfo(QStringLiteral("Signal received — shutting down gracefully..."));
        eMule::MainWindow::quitWithoutPrompt();
        // Quitting from inside a modal loop crashes AppKit: close the boxes, then
        // quit once their loops have returned
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* dlg = qobject_cast<QDialog*>(w); dlg && dlg->isVisible())
                dlg->reject();
        }
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    });
    std::signal(SIGTERM, unixSignalHandler);
    std::signal(SIGINT,  unixSignalHandler);
    std::signal(SIGHUP,  unixSignalHandler);
#endif

    // -- Command-line parsing -------------------------------------------------

    eMule::CommandLineExec cli;
    cli.parse(app);

    // Icons into the hicolor theme, and a launcher entry if there is none: the
    // only things a Linux desktop takes a taskbar icon from.
    if (!cli.screenshotMode())
        eMule::gui::FileAssociation::installLauncher();

    // Load preferences early so language setting is available for translators
    if (!cli.configOverride().isEmpty())
        eMule::AppConfig::setConfigDirOverride(cli.configOverride());
    const QString configDir = eMule::AppConfig::configDir();
    eMule::CrashHandler::install(configDir + QStringLiteral("/crashes"));

    // --register-file-types / --unregister-file-types: do the one thing and go.
    // Before the splash screen and before any IPC, because a packager's
    // post-install script has no display and no daemon.
    if (cli.handleFileTypeRegistration())
        return 0;

    const QString prefsPath = configDir + QStringLiteral("/preferences.yml");
    eMule::thePrefs.load(prefsPath);

    // Opens emuleqt.log / emuleqt_Verbose.log if the GUI switch is on. Re-run
    // once the daemon's GetPreferences answer lands, and after the Options dialog.
    eMule::LogWidget::applyLogFileSettings();

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
    const QStringList translationPaths =
        eMule::AppConfig::langCandidates(QCoreApplication::applicationDirPath());
    for (const auto& path : translationPaths) {
        if (appTranslator.load(appLocale, QStringLiteral("emuleqt"), QStringLiteral("_"), path)) {
            app.installTranslator(&appTranslator);
            break;
        }
    }

    // One GUI per install dir and profile when it starts its own core (issue #29).
    // Another --config, another install, a remote or "local" core, or --new-instance
    // each get a window of their own.
    std::unique_ptr<eMule::SingleInstance> singleInstance;
    if (!cli.screenshotMode() && !cli.newInstance() && eMule::thePrefs.ipcEnabled()
        && !isRemoteCoreAddress(eMule::thePrefs.ipcListenAddress())) {
        const QString ownedPath = resolveDaemonPath();
        if (!ownedPath.isEmpty() && ownedPath != QStringLiteral("local")) {
            singleInstance = std::make_unique<eMule::SingleInstance>(
                QCoreApplication::applicationDirPath(), configDir);
            if (!singleInstance->tryBecomePrimary()) {
                const QStringList args =
                    eMule::SingleInstance::absoluteArgs(cli.positionalArgs());
                if (singleInstance->forwardToPrimary(args)) {
                    eMule::logInfo(QStringLiteral("Already running for this profile, "
                                                  "handed over to it"));
                    return 0;
                }
                eMule::logWarning(QStringLiteral("Already running for this profile, "
                                                 "but it does not answer"));
                QMessageBox::warning(nullptr, QObject::tr("eMule Qt"),
                    QObject::tr("eMule Qt is already running with this configuration "
                                "but does not respond.\n\nStart it with --new-instance "
                                "or --config <dir> to open another window."));
                return 1;
            }
        }
    }

    // Seed bundled config data (webserver assets, template, nodes.dat)
    if (const auto seed = eMule::AppConfig::seedBundledData(configDir);
        seed.seeded || seed.refreshed || seed.conflicts || seed.pruned) {
        eMule::logInfo(QStringLiteral("Bundled data: %1 seeded, %2 refreshed, %3 kept with local "
                               "edits, %4 removed")
                    .arg(seed.seeded).arg(seed.refreshed).arg(seed.conflicts).arg(seed.pruned));
    }
    eMule::theUiState.load(configDir);
    if (!cli.screenshotMode())
        eMule::theUiState.countStart();
    eMule::CountryFlags::setSettings(eMule::theUiState.showCountryFlags());

    // Start the core now, so it loads while the window is built instead of after.
    // After the seeding above: the daemon seeds the same files. A core that is
    // already there, or cannot be judged, is left to the connect path below.
    auto weOwnDaemon = std::make_shared<bool>(false);
    // The connection to the core dropped and has not come back yet
    auto daemonLost = std::make_shared<bool>(false);
    // Of the core we launched; 0 = none, or not known
    auto daemonPid = std::make_shared<qint64>(0);
    if (eMule::thePrefs.ipcEnabled() && !isRemoteCoreAddress(eMule::thePrefs.ipcListenAddress())) {
        const QString earlyPath = resolveDaemonPath();
        const bool ownsCore = !earlyPath.isEmpty() && earlyPath != QStringLiteral("local");
        if (ownsCore)
            claimFreePorts(configDir, !eMule::theUiState.firstStartWizardDone());
        if (ownsCore
            && ipcPortFree(eMule::thePrefs.ipcListenAddress(), eMule::thePrefs.ipcPort())) {
            const bool hold = !cli.screenshotMode() && !eMule::theUiState.firstStartWizardDone();
            *weOwnDaemon = launchDaemon(earlyPath, hold, *daemonPid);
        }
    }

    // Applied at every start, not only the first: idempotent, so it takes the
    // association back from an application that took it away, and removes it
    // promptly when the user turns the setting off.
    if (eMule::gui::FileAssociation::isRuntimeRegistration()) {
        QString assocError;
        const bool assocOk = eMule::gui::FileAssociation::applyFileTypes(
            {eMule::theUiState.associateNzbFiles(), eMule::theUiState.associateTorrentFiles(),
             eMule::isEd2kSchemeRegistered()},
            assocError);
        if (!assocOk) {
            // Never fatal, and never a dialog: a desktop that will not take the
            // association is not a reason to refuse to start.
            eMule::logWarning(QStringLiteral("Could not update the file associations: %1")
                                  .arg(assocError));
        }
    }

    // "Tooltip delay" (Options > Display)
    app.setStyle(new eMule::TooltipDelayStyle);

    // "Label:\tvalue" list tooltips in two aligned columns, as MFC draws them
    eMule::ColumnTooltipFilter columnTooltips;
    app.installEventFilter(&columnTooltips);

    // Theme and font size (Options > Display), before any widget picks up the defaults
    eMule::applyColorScheme(eMule::theUiState.colorScheme(), true);
    eMule::applyFontScale(eMule::theUiState.fontScalePercent());

    // MFC CemuleDlg::OnInitDialog (srchybrid/EmuleDlg.cpp:395-398): no splash and no
    // window when starting minimized — but never on the very first start, where the
    // wizard has to be seen.
    const bool startMinimized = eMule::thePrefs.startMinimized() && !cli.screenshotMode()
                                && eMule::theUiState.firstStartWizardDone();

    // Splash screen
    QSplashScreen* splash = nullptr;
    if (eMule::thePrefs.showSplashScreen() && !startMinimized) {
        splash = new QSplashScreen(QPixmap(QStringLiteral(":/images/Logo.jpg")));
        splash->show();
        QApplication::processEvents();
    }

    // Create and show main window
    eMule::MainWindow mainWindow;
    mainWindow.setFirstStartWizardAllowed(!cli.screenshotMode());
    linkHandler.setMainWindow(&mainWindow);
    if (startMinimized)
        mainWindow.startMinimized(eMule::theUiState.isWindowMaximized());
    else if (eMule::theUiState.isWindowMaximized())
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

    // Apply --tab and --subtab arguments
    cli.applyTabArgs(mainWindow);

    // IPC client — always used to connect to the daemon
    eMule::IpcClient ipcClient;
    bool isRemote = false;  // true when connected to a non-localhost daemon

    // Releases anything the link handler queued while the daemon was still starting.
    linkHandler.setIpcClient(&ipcClient);

    if (eMule::thePrefs.ipcEnabled()) {
        ipcClient.setRemotePollingMs(eMule::thePrefs.ipcRemotePollingMs());
        // Determine connection target and whether we should manage a local daemon.
        //
        // Connection modes (isRemote / daemonPath):
        //   isRemote=true,  daemonPath=""       — remote address configured or chosen via dialog
        //   isRemote=false, daemonPath="local"  — daemon already running on localhost (e.g. Docker)
        //   isRemote=false, daemonPath="/path"  — GUI discovers and launches local emulecored
        //   isRemote=false, daemonPath=""        — no binary found → connect dialog (becomes remote)
        //
        // Auth token is sent only when we don't own the daemon (isRemote or "local").
        QString connectHost = eMule::thePrefs.ipcListenAddress();
        uint16_t port = eMule::thePrefs.ipcPort();
        QString daemonPath;

        if (isRemoteCoreAddress(connectHost)) {
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

        // Send auth token only when we don't own the daemon process
        if (isRemote || daemonPath == QStringLiteral("local")) {
            if (!eMule::thePrefs.ipcTokens().isEmpty())
                ipcClient.setAuthToken(eMule::thePrefs.ipcTokens().first());
        }

        eMule::logInfo(QStringLiteral("Connecting to daemon at %1:%2...")
                           .arg(connectHost).arg(port));

        // Wire IPC client to main window and panels
        mainWindow.setIpcClient(&ipcClient);
        mainWindow.kadPanel()->setIpcClient(&ipcClient);
        mainWindow.serverPanel()->setIpcClient(&ipcClient);
        mainWindow.transferPanel()->setIpcClient(&ipcClient);
        mainWindow.searchPanel()->setIpcClient(&ipcClient);
        mainWindow.searchPanel()->setDownloadModel(mainWindow.transferPanel()->downloadModel());
        mainWindow.sharedFilesPanel()->setIpcClient(&ipcClient);
        mainWindow.messagesPanel()->setIpcClient(&ipcClient);
        mainWindow.ircPanel()->setIpcClient(&ipcClient);
        mainWindow.statisticsPanel()->setIpcClient(&ipcClient);
        mainWindow.usenetPanel()->setIpcClient(&ipcClient);
#ifdef EMULE_HAVE_BITTORRENT
        mainWindow.torrentPanel()->setIpcClient(&ipcClient);
#endif

        // Wire daemon log messages to the LogWidget
        // PushLogMessage format: [logId(0), category(1), severity(2), message(3), timestamp(4)]
        auto* logWidget = mainWindow.serverPanel()->logWidget();
        QObject::connect(&ipcClient, &eMule::IpcClient::ipcLogMessage,
                         logWidget, &eMule::LogWidget::appendIpcMessage);
        // Server Info pane. Deliberately NOT the log path: the daemon delivers these
        // over a dedicated push event carrying the reference's LOG_INFO/LOG_SUCCESS
        // distinction, and appendServerInfo takes plain text (it escapes internally).
        QObject::connect(&ipcClient, &eMule::IpcClient::serverMessageReceived,
                         logWidget, [logWidget](const eMule::Ipc::IpcMessage& msg) {
            logWidget->appendServerInfo(
                msg.fieldString(2),
                static_cast<eMule::ServerMsgType>(msg.fieldInt(1)));
        });

        // Every pane that shows text somebody else sent us routes its links in-app
        // rather than handing them to the OS. They arrive as plain text — see
        // TextLinks.h for why they cannot be QUrls — and join the same queue as a link
        // handed to us by the OS or the command line.
        const auto onLink = [&linkHandler](const QString& link) { linkHandler.open(link); };
        QObject::connect(logWidget, &eMule::LogWidget::linkActivated, &mainWindow, onLink);
        QObject::connect(mainWindow.messagesPanel(), &eMule::MessagesPanel::linkActivated,
                         &mainWindow, onLink);
        QObject::connect(mainWindow.ircPanel(), &eMule::IrcPanel::linkActivated,
                         &mainWindow, onLink);
        // A link an IRC peer sent and the options accepted: started without asking,
        // as MFC CIrcMain::ProcessLink
        QObject::connect(mainWindow.ircPanel(), &eMule::IrcPanel::linkReceived, &mainWindow,
                         [&ipcClient, &mainWindow](const QString& link) {
            eMule::Ed2kLinkImporter::importLinks(link, &ipcClient, &mainWindow,
                eMule::Ed2kLinkImporter::Source::Automatic,
                eMule::Ed2kLinkImporter::Prompt::Silent);
        });

        // A burst of log lines arrives as one batch: one repaint for the lot
        QObject::connect(&ipcClient, &eMule::IpcClient::logBatchStarted,
                         logWidget, &eMule::LogWidget::beginBatch);
        QObject::connect(&ipcClient, &eMule::IpcClient::logBatchFinished,
                         logWidget, &eMule::LogWidget::endBatch);
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
                colored = QStringLiteral("<font color='%1'>%2</font>")
                              .arg(eMule::roleColorName(eMule::TextRole::Caution), text.toHtmlEscaped());
            else if (severity == QtCriticalMsg || severity == QtFatalMsg)
                colored = QStringLiteral("<font color='%1'><b>%2</b></font>")
                              .arg(eMule::roleColorName(eMule::TextRole::Error), text.toHtmlEscaped());
            else
                colored = QStringLiteral("<font color='%1'>%2</font>")
                              .arg(eMule::roleColorName(eMule::TextRole::Info), text.toHtmlEscaped());

            // Route to the correct tab (pass epoch timestamp as seqId for sorted insertion)
            const qint64 seqId = ts > 0 ? static_cast<qint64>(ts) : 0;
            if (cat == QStringLiteral("emule.kad"))
                logWidget->appendKad(colored, timestamp, seqId);
            else if (cat == QStringLiteral("emule.leecher"))
                logWidget->routeLeecher(colored, timestamp, seqId);
            else if (cat == QStringLiteral("emule.usenet"))
                logWidget->routeUsenet(severity, colored, timestamp, seqId);
            else if (cat == QStringLiteral("emule.ed3k") || cat == QStringLiteral("emule.ed3k.kad"))
                logWidget->routeEd3k(cat == QStringLiteral("emule.ed3k.kad"), severity, colored, timestamp, seqId);
            else if (cat == QStringLiteral("emule.server"))
                // Raw text, not `colored` — appendServerInfo escapes internally.
                // The pane's real feed is PushServerMessage; this only catches
                // anything the daemon happens to log to emule.server.
                logWidget->appendServerInfo(
                    text, severity == QtCriticalMsg || severity == QtFatalMsg
                              ? eMule::ServerMsgType::Error
                          : severity == QtWarningMsg ? eMule::ServerMsgType::Warning
                                                     : eMule::ServerMsgType::Info);
            else if (cat == QStringLiteral("emule.status")) {
                // MFC LOG_STATUSBAR: main Log tab plus the status bar, warnings included
                logWidget->appendLog(colored, timestamp, seqId);
                eMule::StatusBarNotifier::post(text);
            }
            else if (cat == QStringLiteral("emule.net"))
                logWidget->appendVerbose(colored, timestamp, seqId);
            else if (severity == QtDebugMsg || severity == QtWarningMsg)
                logWidget->appendVerbose(colored, timestamp, seqId);
            else
                logWidget->appendLog(colored, timestamp, seqId);
        });

        QObject::connect(&ipcClient, &eMule::IpcClient::connected,
                         &mainWindow, [&ipcClient, &mainWindow, prefsPath]() {
            eMule::logInfo(QStringLiteral("Connected to daemon via IPC."));

            // Reload preferences from disk so every reconnect starts from a clean
            // YAML baseline — matching the first-connect flow where thePrefs.load()
            // runs before the async GetPreferences/updateFromCbor overlay.
            const bool ipcLog = eMule::thePrefs.enableIpcLog();
            const bool usenetLog = eMule::thePrefs.showUsenetLog();
            eMule::thePrefs.load(prefsPath);
            eMule::thePrefs.setEnableIpcLog(ipcLog);
            eMule::thePrefs.setShowUsenetLog(usenetLog);

            // Request initial eD2K connection state
            eMule::Ipc::IpcMessage reqConn(eMule::Ipc::IpcMsgType::GetConnection);
            ipcClient.sendRequest(std::move(reqConn),
                                  [&mainWindow](const eMule::Ipc::IpcMessage& resp) {
                const QCborMap info = resp.fieldMap(1);
                mainWindow.setNetworkBlocked(info.value(QStringLiteral("netBlocked")).toBool(),
                                             info.value(QStringLiteral("netBlockReason")).toString());
                mainWindow.setEd2kServer(info.value(QStringLiteral("serverName")).toString(),
                                         info.value(QStringLiteral("serverUsers")).toInteger());
                mainWindow.setEd2kStatus(
                    info.value(QStringLiteral("connected")).toBool(),
                    info.value(QStringLiteral("connecting")).toBool(),
                    info.value(QStringLiteral("firewalled")).toBool(),
                    info.value(QStringLiteral("lowID")).toBool());
                mainWindow.setRelatedSearchSupported(
                    info.value(QStringLiteral("connected")).toBool()
                    && info.value(QStringLiteral("serverRelatedSearch")).toBool());
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
                                  [&mainWindow, &ipcClient](const eMule::Ipc::IpcMessage& resp) {
                // A failed reply (the connection dropped first) has no map, and
                // applying one would zero every daemon-owned setting.
                if (!resp.fieldBool(0))
                    return;
                eMule::thePrefs.updateFromCbor(resp.fieldMap(1));
                // Pushes only report changes, so the starting value comes from here.
                ipcClient.setUsenetEnginePaused(
                    resp.fieldMap(1).value(QStringLiteral("usenetPaused")).toBool());
                // logToDiskGui is stored by the daemon (it owns preferences.yml)
                // but acted on here, so the sink follows the value that just landed.
                eMule::LogWidget::applyLogFileSettings();
                // The graph maxima just landed, and the rate scopes are pinned to them.
                if (auto* stats = mainWindow.statisticsPanel())
                    stats->applySettings();
                // The update-URL fields show daemon-owned prefs.
                mainWindow.serverPanel()->refreshUpdateUrl();
                mainWindow.kadPanel()->refreshNodesUrl();
                // The folder tree marks what sharedDirs just named.
                mainWindow.sharedFilesPanel()->refreshSharedDirs();
                // The lists were built on the local prefs file's showExtControls
                mainWindow.sharedFilesPanel()->applyDisplayOptions();
                mainWindow.transferPanel()->applyExtColumns();
            });

            // Automatic version check: one now if the interval has elapsed, then
            // hourly for as long as the app runs. Started here rather than at
            // construction so the preferences reload just above has landed first;
            // idempotent, because connected() fires again on every reconnect.
            QTimer::singleShot(2000, &mainWindow, [&mainWindow]() {
                mainWindow.startVersionChecks();
            });
        });

        // On first connection failure, try launching the daemon binary (local path only).
        // IpcClient auto-reconnects with exponential backoff regardless.
        if (*weOwnDaemon)
            ipcClient.expectDaemonStart();
        if (!isRemote && daemonPath == QStringLiteral("local")) {
            // "local" mode: daemon should already be running; show dialog on failure
            auto dialogShown = std::make_shared<bool>(false);
            QObject::connect(&ipcClient, &eMule::IpcClient::connectionFailed,
                             &mainWindow, [&mainWindow, &ipcClient, dialogShown](const QString& error) {
                eMule::logWarning(QStringLiteral("Daemon not reachable (%1)").arg(error));
                if (*dialogShown)
                    return;
                *dialogShown = true;

                // open(), not exec(): this runs inside the socket's error signal, and
                // a nested event loop there keeps the retry timer waiting on the dialog
                auto* dlg = new eMule::CoreConnectDialog(&mainWindow);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                QObject::connect(dlg, &QDialog::accepted, &mainWindow, [dlg, &ipcClient] {
                    eMule::thePrefs.setIpcListenAddress(dlg->address());
                    eMule::thePrefs.setIpcPort(dlg->port());
                    ipcClient.setAuthToken(dlg->token());
                    if (dlg->saveToken())
                        eMule::thePrefs.setIpcTokens({dlg->token()});
                    ipcClient.connectToDaemon(QHostAddress(dlg->address()), dlg->port());
                });
                QObject::connect(dlg, &QDialog::rejected, &mainWindow, &eMule::MainWindow::forceQuit);
                dlg->open();
            });
            QObject::connect(&ipcClient, &eMule::IpcClient::connected,
                             &mainWindow, [dialogShown]() {
                *dialogShown = false;
            });
        } else if (!isRemote && !daemonPath.isEmpty()) {
            QObject::connect(&ipcClient, &eMule::IpcClient::connectionFailed,
                             &app, [&ipcClient, daemonPath, weOwnDaemon, daemonLost, daemonPid, wizardAllowed = !cli.screenshotMode()](const QString& error) {
                if (ipcClient.daemonStarting())
                    return;   // launched, still loading
                eMule::logWarning(QStringLiteral("Daemon not reachable (%1)").arg(error));
                if (ipcClient.daemonRestarting())
                    return;   // it starts itself again
                // Ours and never lost: it was launched and has not come up. Not twice.
                if (*weOwnDaemon && !*daemonLost)
                    return;
                // Something still holds the port: the core is alive, only too busy
                // to answer. A second one could not bind anyway.
                if (!ipcPortFree(eMule::thePrefs.ipcListenAddress(), eMule::thePrefs.ipcPort()))
                    return;
                // Read at launch time: a relaunch after the wizard must connect
                const bool hold = wizardAllowed && !eMule::theUiState.firstStartWizardDone();
                if (launchDaemon(daemonPath, hold, *daemonPid)) {
                    *weOwnDaemon = true;
                    *daemonLost = false;
                    ipcClient.expectDaemonStart();
                }
            });
            // A lost connection only allows the relaunch above; it is not the end of
            // the daemon. Giving up ownership here left a core that was merely slow
            // to answer a keepalive running after quit, for every later session too.
            QObject::connect(&ipcClient, &eMule::IpcClient::disconnected,
                             &app, [&ipcClient, daemonLost]() {
                // A requested restart brings back the daemon by itself
                if (!ipcClient.daemonRestarting())
                    *daemonLost = true;
            });
            QObject::connect(&ipcClient, &eMule::IpcClient::connected,
                             &app, [&ipcClient, weOwnDaemon, daemonLost, daemonPid]() {
                *daemonLost = false;
                // The pid we launched goes stale on a Windows restart (a new
                // process), and is unknown from a console start: take the core's own
                if (*weOwnDaemon && ipcClient.corePid() > 0)
                    *daemonPid = ipcClient.corePid();
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
            mainWindow.setNetworkBlocked(info.value(QStringLiteral("netBlocked")).toBool(),
                                         info.value(QStringLiteral("netBlockReason")).toString());
            mainWindow.setEd2kServer(info.value(QStringLiteral("serverName")).toString(),
                                     info.value(QStringLiteral("serverUsers")).toInteger());
            mainWindow.setEd2kStatus(
                info.value(QStringLiteral("connected")).toBool(),
                info.value(QStringLiteral("connecting")).toBool(),
                info.value(QStringLiteral("firewalled")).toBool(),
                info.value(QStringLiteral("lowID")).toBool());
            mainWindow.setRelatedSearchSupported(
                info.value(QStringLiteral("connected")).toBool()
                && info.value(QStringLiteral("serverRelatedSearch")).toBool());
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
        // The chat panel knows whether the message opened a session and whether it is
        // being read; a session that is closed counts as new again (MFC
        // ChatSelector.cpp:227-248).
        QObject::connect(mainWindow.messagesPanel(), &eMule::MessagesPanel::unseenChatMessage,
                         &mainWindow, [&mainWindow](const QString& user, const QString& text, bool newChat) {
            if (!eMule::thePrefs.notifyOnChat())
                return;
            if (!newChat && !eMule::thePrefs.notifyOnChatMsg())
                return;
            mainWindow.showNotification(QObject::tr("Message from %1").arg(user),
                                        QStringLiteral("'%1'").arg(text));
        });
        QObject::connect(&ipcClient, &eMule::IpcClient::notifierEvent,
                         &mainWindow, [&mainWindow](const eMule::Ipc::IpcMessage& msg) {
            const eMule::NotifierText note = eMule::notifierEventText(
                static_cast<eMule::Ipc::NotifierEvent>(msg.fieldInt(0)), msg.fieldString(1));
            if (note.urgent ? eMule::thePrefs.notifyOnUrgent()
                            : eMule::thePrefs.notifyOnDownloadFinished())
                mainWindow.showNotification(note.title, note.text);
        });
        QObject::connect(&ipcClient, &eMule::IpcClient::logMessageReceived,
                         &mainWindow, [&mainWindow](const eMule::Ipc::IpcMessage& msg) {
            if (eMule::thePrefs.notifyOnLog()) {
                const auto severity = static_cast<QtMsgType>(msg.fieldInt(2));
                // Only notify on warnings and above to avoid spamming
                if (severity >= QtWarningMsg) {
                    mainWindow.notifyLogEntry(msg.fieldString(3));
                }
            }
        });

        // Reset status when IPC connection to daemon is lost
        QObject::connect(&ipcClient, &eMule::IpcClient::disconnected,
                         &mainWindow, [&mainWindow]() {
            mainWindow.setEd2kStatus(false, false, false, false);
            mainWindow.setRelatedSearchSupported(false);
            mainWindow.setKadStatus(false, false, false);
            mainWindow.setNetworkStats(0, 0);
            mainWindow.updateTransferRates(0.0, 0.0, 0.0, 0.0);
            mainWindow.clearTaskbarProgress();
        });

        // Poll transfer rates for the status bar (1 second interval)
        auto* rateTimer = new QTimer(&app);
        rateTimer->setInterval(1000);
        QObject::connect(rateTimer, &QTimer::timeout, &app,
                         [&ipcClient, &mainWindow, &configDir]() {
            if (!ipcClient.isConnected())
                return;
            eMule::Ipc::IpcMessage req(eMule::Ipc::IpcMsgType::GetStats);
            ipcClient.sendPoll(&mainWindow, std::move(req),
                                  [&mainWindow, &configDir](const eMule::Ipc::IpcMessage& resp) {
                if (resp.type() != eMule::Ipc::IpcMsgType::Result || !resp.fieldBool(0))
                    return;
                const QCborMap stats = resp.fieldMap(1);
                auto val = [&](QLatin1StringView k) {
                    return stats.value(QString(k)).toDouble();
                };
                // Totals of every network: status bar, title, tray and MiniMule
                const double upRate = val(QLatin1StringView("rateUp"))
                                    + val(QLatin1StringView("rateUpBt"));
                const double downRate = val(QLatin1StringView("rateDown"))
                                      + val(QLatin1StringView("rateDownUsenet"))
                                      + val(QLatin1StringView("rateDownBt"));
                mainWindow.updateTransferRates(
                    upRate, downRate,
                    val(QLatin1StringView("upOverheadRate")),
                    val(QLatin1StringView("downOverheadRate")));

                // Upload SpeedSense pane; absent while the option is off
                const QCborValue uss = stats.value(QStringLiteral("uss"));
                if (uss.isMap()) {
                    const QCborMap u = uss.toMap();
                    mainWindow.setUssStatus(true, static_cast<int>(u.value(QStringLiteral("state")).toInteger()),
                        u.value(QStringLiteral("limit")).toInteger(),
                        u.value(QStringLiteral("latency")).toInteger(),
                        u.value(QStringLiteral("lowest")).toInteger(),
                        u.value(QStringLiteral("msTolerance")).toBool());
                } else {
                    mainWindow.setUssStatus(false, 0, 0, 0, 0, false);
                }

                // Update stream token for preview streaming
                if (auto st = stats.value(QStringLiteral("streamToken")); st.isString())
                    mainWindow.setStreamToken(st.toString());

                // Update MiniMule popup stats
                const int completedDl = static_cast<int>(
                    stats.value(QStringLiteral("completedDownloads")).toInteger());
                const qint64 freeSpace =
                    stats.value(QStringLiteral("freeTempSpace")).toInteger();
                mainWindow.updateMiniMule(completedDl, freeSpace);

                // Taskbar / Dock progress over the whole download queue
                auto num = [&](QLatin1StringView k) {
                    return stats.value(QString(k)).toInteger();
                };
                mainWindow.updateTaskbarProgress(num(QLatin1StringView("totalDownSize")),
                                                 num(QLatin1StringView("totalDownDone")),
                                                 num(QLatin1StringView("downTransferring")),
                                                 num(QLatin1StringView("downErrors")));

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
                         &ipcClient, [&ipcClient, weOwnDaemon, daemonPid]() {
            if (*weOwnDaemon && ipcClient.isConnected()) {
                eMule::logInfo(QStringLiteral("Shutting down daemon (we launched it)..."));
                ipcClient.sendShutdown();
            } else {
                ipcClient.disconnectFromDaemon();
                // Ours, but the connection is down: there is nobody to ask
                if (*weOwnDaemon)
                    terminateUnreachableDaemon(*daemonPid);
            }
        });

        // Connect using hostname overload (handles both IPs and hostnames)
        // Says so when the core does not come up, instead of retrying in silence.
        // Not for a screenshot run: a box would end up in the picture.
        if (!cli.screenshotMode()) {
            auto* startWatch = new eMule::CoreStartWatch(&ipcClient, &app);
            const bool launchesCore = !isRemote && !daemonPath.isEmpty()
                                   && daemonPath != QStringLiteral("local");
            eMule::CoreStartWatch::Probes probes;
            if (launchesCore) {
                probes.coreAlive = [&ipcClient, weOwnDaemon, daemonPid] {
                    // Unknown pid (console start), not ours, or restarting itself
                    if (!*weOwnDaemon || *daemonPid <= 0 || ipcClient.daemonRestarting())
                        return true;
                    return processAlive(*daemonPid);
                };
            }
            if (!isRemote) {
                probes.portFree = [] {
                    return ipcPortFree(eMule::thePrefs.ipcListenAddress(), eMule::thePrefs.ipcPort());
                };
            }
            startWatch->setProbes(std::move(probes));

            // Start the core again if it is gone and nothing else took the port,
            // then wait for it afresh. Shared by Retry and the one automatic relaunch.
            const auto relaunchCore = [&ipcClient, startWatch, launchesCore, daemonPath,
                                       weOwnDaemon, daemonLost, daemonPid] {
                if (launchesCore
                    && ipcPortFree(eMule::thePrefs.ipcListenAddress(), eMule::thePrefs.ipcPort())
                    && launchDaemon(daemonPath, false, *daemonPid)) {
                    *weOwnDaemon = true;
                    *daemonLost = false;
                    ipcClient.expectDaemonStart();
                }
                ipcClient.connectToDaemon(ipcClient.daemonHost(), ipcClient.daemonPort());
                startWatch->restart();
            };

            const bool localMode = daemonPath == QStringLiteral("local");
            // A core that dies on start is started again once, silently. Only once:
            // one that dies every time would loop; the box and its Retry take over.
            auto autoRelaunched = std::make_shared<bool>(false);
            QObject::connect(startWatch, &eMule::CoreStartWatch::problemChanged, &mainWindow,
                             [&mainWindow, localMode, launchesCore, autoRelaunched,
                              relaunchCore](eMule::CoreStartWatch::Problem problem) {
                // "local" mode already asks for another address when nothing listens
                if (localMode && problem == eMule::CoreStartWatch::Problem::Unreachable)
                    return;
                if (problem == eMule::CoreStartWatch::Problem::CoreExited && launchesCore
                    && !*autoRelaunched) {
                    *autoRelaunched = true;
                    eMule::logWarning(QStringLiteral("The core stopped right after start, starting it again"));
                    // Queued: restarting the watch from inside its own signal re-enters it
                    QTimer::singleShot(0, &mainWindow, relaunchCore);
                    return;
                }
                mainWindow.showCoreStartProblem(problem);
            });
            QObject::connect(&mainWindow, &eMule::MainWindow::coreRetryRequested, &app,
                             [relaunchCore] {
                eMule::logInfo(QStringLiteral("Retrying the core connection"));
                relaunchCore();
            });
            startWatch->restart();
        }

        ipcClient.connectToDaemon(connectHost, port);
    } else {
        eMule::logInfo(QStringLiteral("IPC disabled in settings."));
    }

    // A later start of this profile: come to the front and take over its links.
    // Queued: an import asks in a dialog, and this runs inside a socket signal.
    if (singleInstance) {
        QObject::connect(singleInstance.get(), &eMule::SingleInstance::activationRequested,
                         &mainWindow, [&mainWindow, &linkHandler](const QStringList& args) {
            QTimer::singleShot(0, &mainWindow, [&mainWindow, &linkHandler, args] {
                mainWindow.restoreWindow();
                eMule::CommandLineExec::openArguments(args, linkHandler);
            });
        });
    }

    // Handle ed2k:// positional args and --screenshot/--options. The macOS Apple Event
    // route is already live — linkHandler went up before the splash screen.
    cli.handleOpenArguments(linkHandler);
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

    // Save UI state to its own file (not preferences.yml)
    eMule::theUiState.save(configDir);

    // Only save preferences when connected to a remote daemon — the GUI owns
    // its local copy in that scenario.  When connected to a local daemon we
    // launched, the daemon is the sole writer of preferences.yml.
    if (isRemote)
        eMule::thePrefs.save();

    return result;
}
