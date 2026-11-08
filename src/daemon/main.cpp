/// @file main.cpp
/// @brief Entry point for the headless eMule core daemon.
///
/// With no command flags, starts the daemon normally.
/// With a command flag (--add-link, --connect, etc.), connects to a running
/// daemon via IPC, sends the command, prints the result, and exits.

#include "CommandLineExec.h"
#include "DaemonApp.h"
#include "WinShutdownHandler.h"

#include "app/AppConfig.h"
#include "app/InstallerCopy.h"
#ifdef EMULE_HAVE_ED3K
#include "Ed3kBootstrap.h"
#include "Ed3kStore.h"

#include <QSaveFile>
#endif
#include "app/TranslationRouter.h"
#include "files/PartFileConvert.h"
#include "prefs/LegacyConfigImport.h"
#include "prefs/LegacyIdentityImport.h"
#include "prefs/Preferences.h"
#include "utils/BundleEnvironment.h"
#include "utils/CrashHandler.h"
#include "utils/Log.h"
#include "utils/SidecarProcess.h"
#include "utils/StallWatchdog.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QLockFile>
#include <QTcpServer>
#include <QProcess>
#include <QLoggingCategory>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>
#include <vector>

#ifndef Q_OS_WIN
#include <cerrno>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

static int s_sigFd[2];
/// A signal came before the event loop ran: quit() would be lost then.
static std::atomic<bool> s_signalQuit{false};

static void unixSignalHandler(int)
{
    // Async-signal-safe: just write a byte to wake the signal thread
    char a = 1;
    ::write(s_sigFd[1], &a, sizeof(a));
}
#endif

namespace {

/// Quit asked, by a signal or once the event loop ended: past this the process ends anyway.
/// BT waits 5 s for resume data at most; the rest is quick.
constexpr std::chrono::seconds kQuitDeadline{90};

/// main() has returned: statics may be gone, no more logging.
std::atomic<bool> s_mainReturned{false};

/// Ends the process kQuitDeadline after the first call, whatever hangs: the teardown, a
/// stuck event loop, QCoreApplication's wait for the thread pool. A core stuck there kept
/// running with no GUI, burning CPU, and its i2pd with it (#129). Never disarmed: a clean
/// exit simply comes first. Owned sidecars (i2pd, java) are ended before.
void armQuitDeadline()
{
    static std::atomic<bool> armed{false};
    if (armed.exchange(true))
        return;
    std::thread([] {
        std::this_thread::sleep_for(kQuitDeadline);
        const auto limit = static_cast<long long>(kQuitDeadline.count());
        // Log from a thread of its own: the hung one may hold the log's lock
        if (!s_mainReturned.load())
            std::thread([] {
                eMule::logError(QStringLiteral("Shutdown hung for %1 s, exiting now")
                                    .arg(static_cast<long long>(kQuitDeadline.count())));
            }).detach();
        std::fprintf(stderr, "emulecored: shutdown hung for %lld s, exiting now\n", limit);
        eMule::SidecarProcess::signalAll(false);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        eMule::SidecarProcess::signalAll(true);
        std::fflush(nullptr);
        std::_Exit(3);
    }).detach();
}

/// Nothing listens on the IPC port: a bind probe, as the GUI's.
bool ipcPortFree(const QString& host, uint16_t port)
{
    const QHostAddress addr(host);
    QTcpServer probe;
    return probe.listen(addr.isNull() ? QHostAddress(QHostAddress::LocalHost) : addr, port);
}

/// Take @p lock. A holder that serves no IPC on @p ipcPort is a core still shutting
/// down: the GUI starts the next one right after asking the last to quit, and that
/// one may take a while to save. Waited out past its watchdog. 0 = no wait.
bool lockAfterStop(QLockFile& lock, const QString& ipcHost, uint16_t ipcPort)
{
    if (lock.tryLock(2000))
        return true;
    // Past kQuitDeadline, so a hung core is gone by then
    const QDeadlineTimer deadline(std::chrono::seconds(100));
    bool told = false;
    while (lock.error() == QLockFile::LockFailedError) {
        qint64 holderPid = 0;
        const bool known = lock.getLockInfo(&holderPid, nullptr, nullptr);
        // Our own pid in it: a container's PID 1 again after a restart
        if (known && holderPid == QCoreApplication::applicationPid()) {
            lock.removeStaleLockFile();
            return lock.tryLock(0);
        }
        if (ipcPort == 0 || deadline.hasExpired() || !ipcPortFree(ipcHost, ipcPort))
            return false;
        if (!told) {
            eMule::logInfo(QStringLiteral("Waiting for the previous core (pid %1) to finish "
                                          "shutting down...").arg(holderPid));
            told = true;
        }
        if (lock.tryLock(1000))
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
#ifndef Q_OS_WIN
    // Ignore SIGPIPE — writing to a disconnected client socket must not kill the daemon.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // Before anything is started from here
    eMule::restoreSystemLibraryPath();

    QCoreApplication app(argc, argv);

    // Before anything can log: scripts/debug-gui.sh runs the daemon and the GUI
    // into one terminal, so every console line needs a time and a process tag.
    eMule::installConsoleMessagePattern(QStringLiteral("core"));
    // --log-level / EMULE_LOG_LEVEL: quiets the console only (#139), wraps Qt's own
    // console output, so it goes before the file and log-tab handlers
    eMule::installConsoleLogFilter(eMule::consoleLogLevelFromArgs(app.arguments()));
    // Installed here, not in installLogForwarder(), so the file also captures
    // the lines emitted before DaemonApp exists. The sink stays closed until
    // applyLogFileSettings() below opens it.
    eMule::installLogFileMessageHandler();

#ifndef Q_OS_WIN
    // Self-pipe trick: SIGTERM/SIGINT/SIGHUP become a graceful quit. Read on a thread of
    // its own, not by the event loop: a stuck loop never saw them, and the deadline must
    // start even then (#129).
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, s_sigFd);
    std::thread([fd = s_sigFd[0]] {
        char tmp;
        for (;;) {
            const ssize_t n = ::read(fd, &tmp, sizeof(tmp));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return;
            if (s_signalQuit.exchange(true))
                continue;
            armQuitDeadline();
            QCoreApplication* const core = QCoreApplication::instance();
            if (!core)
                continue;
            QMetaObject::invokeMethod(core, [] {
                eMule::logInfo(QStringLiteral("Signal received — shutting down gracefully..."));
                QCoreApplication::quit();
            }, Qt::QueuedConnection);
        }
    }).detach();
    std::signal(SIGTERM, unixSignalHandler);
    std::signal(SIGINT,  unixSignalHandler);
    std::signal(SIGHUP,  unixSignalHandler);
#endif
#ifdef Q_OS_LINUX
    QCoreApplication::setApplicationName(QStringLiteral("Core"));
#else
    QCoreApplication::setApplicationName(QStringLiteral("eMule Qt Core"));
#endif
    QCoreApplication::setApplicationVersion(eMule::kAppVersion);
    QCoreApplication::setOrganizationName(QStringLiteral("eMule"));

    eMule::CommandLineExec cli;
    cli.parse(app);

    // -- Load preferences (needed for IPC port in both modes) -----------------

    if (!cli.configOverride().isEmpty())
        eMule::AppConfig::setConfigDirOverride(cli.configOverride());
    const QString configDir = eMule::AppConfig::configDir();
    eMule::CrashHandler::install(configDir + QStringLiteral("/crashes"));

    const QString prefsPath = configDir + QStringLiteral("/preferences.yml");
#ifdef EMULE_HAVE_ED3K
    // a new identity's user hash says "also ed3k" (ed3k Protocol.md §14.2)
    eMule::Preferences::setUserHashSource([configDir] {
        QString error;
        auto hash = eMule::ed3k::derivedUserHash(configDir, &error);
        if (!hash)
            eMule::logWarning(QStringLiteral("ed3k key file: %1; random user hash").arg(error));
        return hash;
    });
#endif
    eMule::thePrefs.load(prefsPath);

    // -- offline export of the saved ed3k contacts (bootstrap.md §3), then exit --
    if (const QString out = cli.exportEd3kNodes(); !out.isEmpty()) {
#ifdef EMULE_HAVE_ED3K
        const auto file = eMule::ed3k::Ed3kBootstrap::exportSaved(configDir, cli.exportComment());
        if (!file) {
            std::fprintf(stderr, "export failed: %s\n", qPrintable(file.error()));
            return 1;
        }
        QSaveFile f(out);
        if (!f.open(QIODevice::WriteOnly) || f.write(*file) != file->size() || !f.commit()) {
            std::fprintf(stderr, "cannot write %s\n", qPrintable(out));
            return 1;
        }
        std::printf("wrote %s (%lld bytes)\n", qPrintable(out), static_cast<long long>(file->size()));
        return 0;
#else
        std::fprintf(stderr, "built without ed3k\n");
        return 1;
#endif
    }

    // -- CLI command mode: send to running daemon and exit --------------------

    if (cli.hasCommand())
        return cli.execCommand(app);

    // -- Normal daemon startup ------------------------------------------------

    // One core per profile. The GUI reads this lock to tell our core from another
    // profile's on the same IPC port (issue #49).
    // A reason left by an earlier failed start must not outlive it
    eMule::AppConfig::clearCoreStartFailure(configDir);
    const auto failStart = [&configDir](const QString& reason) {
        eMule::logError(reason);
        eMule::AppConfig::writeCoreStartFailure(configDir, reason);
        return 1;
    };

    // Determine IPC target, also for the lock wait below
    QString ipcHost = eMule::thePrefs.ipcListenAddress();
    if (ipcHost.isEmpty())
        ipcHost = QStringLiteral("127.0.0.1");
    uint16_t ipcPort = eMule::thePrefs.ipcPort();
    if (cli.portOverride() != 0)
        ipcPort = cli.portOverride();

    QLockFile coreLock(eMule::AppConfig::coreLockPath(configDir));
    coreLock.setStaleLockTime(0);   // only a dead holder makes it stale, never age
    if (!lockAfterStop(coreLock, ipcHost, ipcPort)) {
        // Only LockFailedError means another core; the rest is the dir itself
        const QString lockPath = eMule::AppConfig::coreLockPath(configDir);
        switch (coreLock.error()) {
        case QLockFile::LockFailedError: {
            qint64 holderPid = 0;
            QString holderHost;
            (void)coreLock.getLockInfo(&holderPid, &holderHost, nullptr);
            return failStart(QStringLiteral("Another core (pid %1) is already running with the "
                                            "config at %2").arg(holderPid).arg(configDir));
        }
        case QLockFile::PermissionError:
            return failStart(QStringLiteral("Cannot create the lock file %1: permission denied. "
                                            "The config dir is not writable (antivirus or "
                                            "Controlled Folder Access may block it).")
                                 .arg(QDir::toNativeSeparators(lockPath)));
        case QLockFile::UnknownError:
        case QLockFile::NoError:
            break;
        }
        return failStart(QStringLiteral("Cannot create the lock file %1 (unknown error).")
                             .arg(QDir::toNativeSeparators(lockPath)));
    }

    // One default core per user, wherever it was installed: the setup's per-user
    // config and a portable copy each have a profile lock of their own. A --config
    // profile stays separate, unless it names the default one.
    // Only built in here: its path creates ~/eMuleQt, which a --config profile never needs (#153)
    std::optional<QLockFile> userLockHolder;
    const eMule::AppConfig::UserCore lastUserCore = eMule::AppConfig::lastUserCore();
    if (cli.configOverride().isEmpty() || eMule::AppConfig::sameDir(configDir, lastUserCore.configDir)) {
        QLockFile& userLock = userLockHolder.emplace(eMule::AppConfig::userCoreLockPath());
        userLock.setStaleLockTime(0);
        // No wait: a holder with our profile was gone once the lock above was ours
        if (lockAfterStop(userLock, ipcHost, 0)) {
            eMule::AppConfig::writeUserCore({configDir, QCoreApplication::applicationFilePath(),
                                             QCoreApplication::applicationPid()});
        } else if (userLock.error() == QLockFile::LockFailedError) {
            qint64 holderPid = 0;
            (void)userLock.getLockInfo(&holderPid, nullptr, nullptr);
            return failStart(QStringLiteral("Another core (pid %1) is already running for this user "
                                            "with the config at %2. Close it first, or start this "
                                            "one with --config <dir>.")
                                 .arg(holderPid)
                                 .arg(QDir::toNativeSeparators(lastUserCore.configDir)));
        } else {
            eMule::logWarning(QStringLiteral("Cannot create the lock file %1; other installs "
                                             "may start a core of their own")
                                  .arg(QDir::toNativeSeparators(userLock.fileName())));
        }
    }

    // Seed bundled config data (webserver assets, template, nodes.dat)
    if (const auto seed = eMule::AppConfig::seedBundledData(configDir);
        seed.seeded || seed.refreshed || seed.conflicts || seed.pruned) {
        eMule::logInfo(QStringLiteral("Bundled data: %1 seeded, %2 refreshed, %3 kept with local "
                               "edits, %4 removed")
                    .arg(seed.seeded).arg(seed.refreshed).arg(seed.conflicts).arg(seed.pruned));
    }

    // Enable debug-level output per logging pref (verbose / kadVerboseLog /
    // serverVerboseLog / bt-, hoster-, ed3k-, usenet-, sharedFilesVerboseLog) so the corresponding logDebug()/logKad()/logServerVerbose()
    // lines reach the message handler and are forwarded to the GUI.
    eMule::DaemonApp::applyLogFilterRules();
    eMule::DaemonApp::applyLogFileSettings();

    eMule::logInfo(QStringLiteral("eMule Core Daemon starting..."));

    // Installer asked for an import of an MFC eMule config dir: copy it in first,
    // so the identity import below finds its preferences.dat. Once per user.
    (void)eMule::LegacyConfigImport::run(eMule::LegacyConfigImport::requestFilePath(),
                                         configDir, eMule::thePrefs);

    // MFC preferences.dat dropped into the config dir: take over its user hash
    // before anything reads ours. Logs its own outcome; a rejected file changes nothing.
    (void)eMule::LegacyIdentityImport::run(configDir, eMule::thePrefs);

    // Windows setup left a copy of itself next to the exe: share it from Incoming.
    // After the MFC import, which may have changed the Incoming dir.
    (void)eMule::InstallerCopy::run(eMule::InstallerCopy::stagedDir(), configDir,
                                    eMule::thePrefs.incomingDir());

    // The web UI answers each session in its own language through this. It
    // translates nothing outside a web request, so logs and IPC stay as they are.
    // Declared before the daemon so it outlives the web server.
    eMule::TranslationRouter translations(
        eMule::AppConfig::langCandidates(QCoreApplication::applicationDirPath()));
    QCoreApplication::installTranslator(&translations);

    // Create and start daemon
    eMule::DaemonApp daemon;
    daemon.setTranslationRouter(&translations);
    daemon.setConnectHold(cli.holdConnect());
    daemon.setIpcPortOverride(ipcPort);
    daemon.setAutoUpdate(cli.autoUpdate(), cli.autoUpdateHours());
    if (!daemon.start()) {
        eMule::logError(QStringLiteral("Failed to start daemon"));
        // IpcServer logged the reason already
        eMule::AppConfig::writeCoreStartFailure(
            configDir, daemon.startError().isEmpty() ? QStringLiteral("Failed to start daemon")
                                                     : daemon.startError());
        return 1;
    }

    eMule::logInfo(QStringLiteral("Daemon running. IPC listening on %1:%2")
                       .arg(ipcHost)
                       .arg(ipcPort));

    // A stuck event loop leaves its stack in crashes/ (report 20). After start():
    // loading the share may take long before the loop runs at all
    std::optional<eMule::StallWatchdog> stallWatchdog;
    stallWatchdog.emplace(eMule::CrashHandler::crashDir());

    // Second half of the MFC import: unfinished downloads of the old temp dirs.
    // Copied, not moved -- the old install keeps its part files.
    if (const QStringList oldTemps = eMule::LegacyConfigImport::takePendingTempDirs(configDir);
        !oldTemps.isEmpty()) {
        for (const QString& dir : oldTemps) {
            eMule::logInfo(QStringLiteral("Importing unfinished eMule downloads from %1").arg(dir));
            eMule::PartFileConvert::scanFolderToAdd(dir, /*recursive=*/false, /*removeSource=*/false);
        }
        eMule::PartFileConvert::processQueue();
    }

    // Once, whoever gets there first: the event loop ending, or Windows ending the
    // session from inside it.
    bool stopped = false;
    const auto shutdownNow = [&daemon, &stopped, &stallWatchdog] {
        if (stopped)
            return;
        stopped = true;
        stallWatchdog.reset();   // the teardown runs outside the loop
        armQuitDeadline();
        daemon.stop();
        eMule::thePrefs.save();
    };
    eMule::WinShutdownHandler::install(shutdownNow);

#ifndef Q_OS_WIN
    // quit() before exec() does nothing
    const int result = s_signalQuit.load() ? 0 : QCoreApplication::exec();
#else
    const int result = QCoreApplication::exec();
#endif

    shutdownNow();
    eMule::WinShutdownHandler::finished();

    if (eMule::DaemonApp::restartRequested()) {
        // The restarted core takes the locks again: same pid, or a new process
        if (userLockHolder && userLockHolder->isLocked())
            userLockHolder->unlock();
        coreLock.unlock();
        // The first-start hold belongs to the first start only
        QStringList args = QCoreApplication::arguments().mid(1);
        args.removeAll(QStringLiteral("--hold-connect"));
        const QString program = QCoreApplication::applicationFilePath();
        eMule::logInfo(QStringLiteral("Restarting daemon..."));
#ifdef Q_OS_WIN
        if (!QProcess::startDetached(program, args))
            eMule::logError(QStringLiteral("Restart failed: could not start %1").arg(program));
#else
        // Same process: keeps the PID, the terminal and a container's PID 1
        ::close(s_sigFd[0]);
        ::close(s_sigFd[1]);
        const QByteArray programBytes = QFile::encodeName(program);
        QList<QByteArray> argBytes{programBytes};
        for (const QString& arg : args)
            argBytes.append(arg.toLocal8Bit());
        std::vector<char*> argvNew;
        for (QByteArray& arg : argBytes)
            argvNew.push_back(arg.data());
        argvNew.push_back(nullptr);
        // The binary itself, not the launcher: it needs the bundled libs again
        eMule::reapplyBundleLibraryPath();
        ::execv(programBytes.constData(), argvNew.data());
        eMule::logError(QStringLiteral("Restart failed: could not start %1").arg(program));
        return 1;
#endif
    }

    s_mainReturned.store(true);
    return result;
}
