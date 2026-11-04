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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QProcess>
#include <QLoggingCategory>
#include <QSocketNotifier>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

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

namespace {

/// Ends the process when the teardown hangs. A core stuck in it keeps running
/// with no GUI and burns CPU until killed by hand (#129).
class ShutdownWatchdog {
public:
    explicit ShutdownWatchdog(std::chrono::seconds limit)
        : m_thread([this, limit] { watch(limit); })
    {
    }
    ~ShutdownWatchdog()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_done = true;
        }
        m_cv.notify_one();
        m_thread.join();
    }
    ShutdownWatchdog(const ShutdownWatchdog&) = delete;
    ShutdownWatchdog& operator=(const ShutdownWatchdog&) = delete;

private:
    void watch(std::chrono::seconds limit)
    {
        std::unique_lock lock(m_mutex);
        if (m_cv.wait_for(lock, limit, [this] { return m_done; }))
            return;
        // Log from a thread of its own: the hung one may hold the log's lock
        std::thread([limit] {
            eMule::logError(QStringLiteral("Shutdown hung for %1 s, exiting now").arg(limit.count()));
        }).detach();
        std::fprintf(stderr, "emulecored: shutdown hung for %lld s, exiting now\n",
                     static_cast<long long>(limit.count()));
        if (m_cv.wait_for(lock, std::chrono::seconds(2), [this] { return m_done; }))
            return;
        std::fflush(nullptr);
        std::_Exit(3);
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done = false;
    std::thread m_thread;   ///< last: starts once the members above exist
};

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
    // Installed here, not in installLogForwarder(), so the file also captures
    // the lines emitted before DaemonApp exists. The sink stays closed until
    // applyLogFileSettings() below opens it.
    eMule::installLogFileMessageHandler();

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

    QLockFile coreLock(eMule::AppConfig::coreLockPath(configDir));
    coreLock.setStaleLockTime(0);   // only a dead holder makes it stale, never age
    bool locked = coreLock.tryLock(2000);
    if (!locked && coreLock.error() == QLockFile::LockFailedError) {
        // Our own pid in it: a container's PID 1 again after a restart
        qint64 holderPid = 0;
        if (coreLock.getLockInfo(&holderPid, nullptr, nullptr)
            && holderPid == QCoreApplication::applicationPid()) {
            coreLock.removeStaleLockFile();
            locked = coreLock.tryLock(0);
        }
    }
    if (!locked) {
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

    // Seed bundled config data (webserver assets, template, nodes.dat)
    if (const auto seed = eMule::AppConfig::seedBundledData(configDir);
        seed.seeded || seed.refreshed || seed.conflicts || seed.pruned) {
        eMule::logInfo(QStringLiteral("Bundled data: %1 seeded, %2 refreshed, %3 kept with local "
                               "edits, %4 removed")
                    .arg(seed.seeded).arg(seed.refreshed).arg(seed.conflicts).arg(seed.pruned));
    }

    // Enable debug-level output per logging pref (verbose / kadVerboseLog /
    // serverVerboseLog / bt-, hoster-, ed3kVerboseLog) so the corresponding logDebug()/logKad()/logServerVerbose()
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

    // Determine IPC target for log message
    QString ipcHost = eMule::thePrefs.ipcListenAddress();
    if (ipcHost.isEmpty())
        ipcHost = QStringLiteral("127.0.0.1");
    uint16_t ipcPort = eMule::thePrefs.ipcPort();
    if (cli.portOverride() != 0)
        ipcPort = cli.portOverride();

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
    const auto shutdownNow = [&daemon, &stopped] {
        if (stopped)
            return;
        stopped = true;
        // BT waits 5 s for resume data at most; the rest is quick
        const ShutdownWatchdog watchdog(std::chrono::seconds(90));
        daemon.stop();
        eMule::thePrefs.save();
    };
    eMule::WinShutdownHandler::install(shutdownNow);

    const int result = QCoreApplication::exec();

    shutdownNow();
    eMule::WinShutdownHandler::finished();

    if (eMule::DaemonApp::restartRequested()) {
        // The restarted core takes the lock again: same pid, or a new process
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

    return result;
}
