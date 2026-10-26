#include "pch.h"
/// @file PreviewLauncher.cpp
/// @brief Launch a media player, a browser or the file manager for what the
///        daemon holds.

#include "utils/PreviewLauncher.h"

#include "app/IpcClient.h"
#include "media/PreviewApps.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace eMule {

namespace {

/// Pause before a failed player launch is tried again.
constexpr int kLaunchRetryDelayMs = 1000;

/// The scheme the daemon's HTTP surface answers on.
///
/// Mirrors the daemon's own SSL precondition (WebServer::start): HTTPS only with
/// httpsEnabled and both paths set. It still falls back to plain HTTP when the
/// cert or key cannot be opened, and that is not visible from here.
QString daemonWebScheme()
{
    const bool ssl = thePrefs.webServerHttpsEnabled()
                     && !thePrefs.webServerCertPath().isEmpty()
                     && !thePrefs.webServerKeyPath().isEmpty();
    return ssl ? QStringLiteral("https") : QStringLiteral("http");
}

/// Run @p program and log a failure, exit code included.
///
/// startDetached() only says the process was spawned. A launcher like `open` or
/// xdg-open fails *after* that: a busy app that misses the Apple event (-1712),
/// a desktop with no handler. Unparented on purpose: a QProcess destroyed at quit
/// would kill a handler that the launcher runs in the foreground.
/// @p retries reruns a failed launch after a short pause; the second try finds
/// the app awake.
void startChecked(const QString& program, const QStringList& args, const QString& shown,
                  int retries = 0)
{
    auto* proc = new QProcess;
    QObject::connect(proc, &QProcess::errorOccurred, proc,
                     [proc, program, shown](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        logWarning(QStringLiteral("Cannot open %1: %2 could not be started (%3)")
                       .arg(shown, program, proc->errorString()));
        proc->deleteLater();
    });
    QObject::connect(proc, &QProcess::finished, proc,
                     [proc, program, args, shown, retries](int exitCode,
                                                           QProcess::ExitStatus status) {
        proc->deleteLater();
        if (status == QProcess::NormalExit && exitCode == 0)
            return;
        const QString detail = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
        if (retries > 0) {
            logDebug(QStringLiteral("Opening %1 failed (%2 exit %3), retrying")
                         .arg(shown, program).arg(exitCode));
            QTimer::singleShot(kLaunchRetryDelayMs, qApp, [program, args, shown, retries] {
                startChecked(program, args, shown, retries - 1);
            });
            return;
        }
        logWarning(QStringLiteral("Cannot open %1: %2 exited with code %3%4")
                       .arg(shown, program).arg(exitCode)
                       .arg(detail.isEmpty() ? QString{} : QStringLiteral(" - ") + detail));
    });
    proc->start(program, args);
}

/// Hand @p url to the desktop and log a failure.
///
/// @p shown is what the log may name: never the URL itself for a daemon page,
/// whose query carries the stream token.
bool openOnDesktop(const QUrl& url, const QString& shown)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    // QDesktopServices reports success once xdg-open is spawned, so a desktop
    // with no handler fails without a trace. Run it ourselves for the exit code.
    startChecked(QStringLiteral("xdg-open"),
                 {url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded)},
                 shown);
    return true;
#else
    if (QDesktopServices::openUrl(url))
        return true;
    logWarning(QStringLiteral("Cannot open %1: the system found nothing to open it with")
                   .arg(shown));
    return false;
#endif
}

#ifdef Q_OS_MACOS
/// The .app bundle holding @p command, or @p command itself when it is not
/// inside one. `open -a` names an app, not the binary in Contents/MacOS.
QString macAppBundle(const QString& command)
{
    const qsizetype at = command.indexOf(QStringLiteral(".app/Contents/MacOS/"),
                                         0, Qt::CaseInsensitive);
    return at < 0 ? command : command.left(at + 4);
}
#endif

} // namespace

void launchPreview(const QString& url)
{
    const QString playerCmd = thePrefs.videoPlayerCommand();
    if (playerCmd.isEmpty()) {
        logWarning(QStringLiteral("No video player configured. Set it in Options → Files."));
        return;
    }
    if (!launchPlayer(playerCmd, thePrefs.videoPlayerArgs(), url))
        logWarning(QStringLiteral("Cannot start the video player %1").arg(playerCmd));
}

bool launchPlayer(const QString& command, const QString& playerArgs, const QString& target)
{
    if (command.isEmpty() || target.isEmpty())
        return false;

    QString args = playerArgs;
    QStringList argList;
    if (args.contains(QStringLiteral("%1"))) {
        // Split first: a path with spaces must stay one argument
        argList = QProcess::splitCommand(args);
        for (QString& arg : argList)
            arg.replace(QStringLiteral("%1"), target);
    } else {
        if (!args.isEmpty())
            argList = QProcess::splitCommand(args);
        argList.append(target);
    }

    // Reuse existing VLC instance (matches original eMule ShellExecute behavior).
    // macOS VLC doesn't support --one-instance; use `open -a` which sends the URL
    // to the running app. Linux/Windows VLC supports --one-instance; --no-playlist-enqueue
    // makes the running instance play the URL instead of queueing it. There is no
    // --playlist-replace: VLC refuses to start on an unknown option.
    const QString playerName = QFileInfo(command).completeBaseName().toLower();
    if (playerName == QStringLiteral("vlc")) {
#ifdef Q_OS_MACOS
        // Not detached: `open` reports a missed Apple event only in its exit
        // code, and a running VLC that is busy or napping drops the first one.
        // Player options go behind --args, else `open` reads them as its own.
        QStringList openArgs{QStringLiteral("-a"), macAppBundle(command)};
        QStringList playerOpts;
        for (const QString& arg : std::as_const(argList))
            (arg == target ? openArgs : playerOpts).append(arg);
        if (!playerOpts.isEmpty())
            openArgs << QStringLiteral("--args") << playerOpts;
        startChecked(QStringLiteral("open"), openArgs, QStringLiteral("the preview in VLC"),
                     1);
        return true;
#else
        argList.prepend(QStringLiteral("--no-playlist-enqueue"));
        argList.prepend(QStringLiteral("--one-instance"));
        return QProcess::startDetached(command, argList);
#endif
    }
    return QProcess::startDetached(command, argList);
}

const PreviewApps& previewApps()
{
    static PreviewApps apps;
    static QDateTime loadedStamp;

    const QFileInfo file(QDir(thePrefs.configDir()).filePath(QStringLiteral("PreviewApps.dat")));
    if (!file.exists()) {
        if (apps.count() > 0 || loadedStamp.isValid()) {
            apps = PreviewApps{};
            loadedStamp = {};
        }
        return apps;
    }
    if (file.lastModified() != loadedStamp) {
        apps = PreviewApps{};
        (void)apps.loadFromFile(file.absoluteFilePath());
        loadedStamp = file.lastModified();
    }
    return apps;
}

QString daemonStreamUrl(const IpcClient* ipc, const QString& fileHash,
                        const QString& streamToken)
{
    if (!ipc || !ipc->isConnected() || fileHash.isEmpty() || streamToken.isEmpty())
        return {};

    return QStringLiteral("%1://%2:%3/api/v1/downloads/%4/preview?token=%5")
        .arg(daemonWebScheme(), ipc->daemonHost())
        .arg(thePrefs.webServerPort())
        .arg(fileHash, streamToken);
}

QString daemonUsenetStreamUrl(const IpcClient* ipc, const QString& itemId, int fileIndex,
                              const QString& streamToken, int entry)
{
    if (!ipc || !ipc->isConnected() || itemId.isEmpty() || fileIndex < 0
        || streamToken.isEmpty()) {
        return {};
    }

    QString url = QStringLiteral("%1://%2:%3/api/v1/usenet/%4/%5/preview?token=%6")
                      .arg(daemonWebScheme(), ipc->daemonHost())
                      .arg(thePrefs.webServerPort())
                      .arg(itemId)
                      .arg(fileIndex)
                      .arg(streamToken);

    // Appended only when a file was actually chosen, so the default URL stays
    // exactly what it was.
    if (entry >= 0)
        url += QStringLiteral("&entry=%1").arg(entry);

    return url;
}

QString daemonIncomingUrl(const IpcClient* ipc, const QString& streamToken,
                          const QString& relPath)
{
    if (!ipc || !ipc->isConnected() || streamToken.isEmpty())
        return {};

    // Assembled through QUrl rather than by hand: a release folder carries
    // spaces, brackets and ampersands, and only the query encoder gets those
    // back to the daemon intact.
    QUrl url;
    url.setScheme(daemonWebScheme());
    url.setHost(ipc->daemonHost());
    url.setPort(thePrefs.webServerPort());
    url.setPath(QStringLiteral("/api/v1/incoming"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("token"), streamToken);
    if (!relPath.isEmpty())
        query.addQueryItem(QStringLiteral("path"), relPath);
    url.setQuery(query);

    return url.toString(QUrl::FullyEncoded);
}

QString daemonIncomingFileUrl(const IpcClient* ipc, const QString& streamToken,
                              const QString& relPath, bool play)
{
    if (!ipc || !ipc->isConnected() || streamToken.isEmpty() || relPath.isEmpty())
        return {};

    QUrl url;
    url.setScheme(daemonWebScheme());
    url.setHost(ipc->daemonHost());
    url.setPort(thePrefs.webServerPort());

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("token"), streamToken);

    // Two routes, two query keys. The listing route serves the player page under
    // `play=` and refuses a `path=` that names a file; the download route takes
    // `file=` and answers with an attachment.
    if (play) {
        url.setPath(QStringLiteral("/api/v1/incoming"));
        query.addQueryItem(QStringLiteral("play"), relPath);
    } else {
        url.setPath(QStringLiteral("/api/v1/incoming/download"));
        query.addQueryItem(QStringLiteral("file"), relPath);
    }
    url.setQuery(query);

    return url.toString(QUrl::FullyEncoded);
}

bool openIncomingFileInBrowser(const IpcClient* ipc, const QString& streamToken,
                               const QString& relPath, bool play)
{
    const QString reason = incomingBrowseUnavailableReason(ipc, streamToken);
    if (!reason.isEmpty()) {
        logWarning(QStringLiteral("Cannot open the file on the core: ") + reason);
        return false;
    }

    const QString url = daemonIncomingFileUrl(ipc, streamToken, relPath, play);
    if (url.isEmpty())
        return false;

    return QDesktopServices::openUrl(QUrl(url));
}

QString daemonWebUiUrl(const IpcClient* ipc)
{
    if (!ipc || !ipc->isConnected())
        return {};

    QUrl url;
    url.setScheme(daemonWebScheme());
    url.setHost(ipc->daemonHost());
    url.setPort(thePrefs.webServerPort());
    url.setPath(QStringLiteral("/"));
    return url.toString(QUrl::FullyEncoded);
}

QString incomingBrowseUnavailableReason(const IpcClient* ipc, const QString& streamToken)
{
    if (!ipc || !ipc->isConnected())
        return QCoreApplication::translate("PreviewLauncher", "Not connected to the core.");

    if (streamToken.isEmpty()) {
        return QCoreApplication::translate("PreviewLauncher",
            "The core has not sent its stream token yet. It arrives with the next "
            "status update — try again in a moment.");
    }

    // A remote core with both web surfaces off pins its HTTP listener to loopback
    // (DaemonApp::startWebServer), so no URL reaches it. A local core is fine
    // either way: the browse route is registered whatever the surface flags say.
    if (!ipc->isLocalConnection() && !thePrefs.webServerEnabled()
        && !thePrefs.webServerRestApiEnabled()) {
        return QCoreApplication::translate("PreviewLauncher",
            "The core runs on another machine and its web server only listens on "
            "localhost.\n\nEnable Web Interface or REST API under "
            "Options → Web Interface.");
    }

    return {};
}

bool openIncomingInBrowser(const IpcClient* ipc, const QString& streamToken,
                           const QString& relPath)
{
    const QString reason = incomingBrowseUnavailableReason(ipc, streamToken);
    if (!reason.isEmpty()) {
        logWarning(QStringLiteral("Cannot show the core's Incoming folder: ") + reason);
        return false;
    }

    return openOnDesktop(QUrl(daemonIncomingUrl(ipc, streamToken, relPath)),
                         QStringLiteral("the core's Incoming folder in the browser"));
}

bool openIncomingFolder(const IpcClient* ipc, const QString& streamToken,
                        const QString& localPath, const QString& relPath)
{
    // Local core: same filesystem, so the real file manager wins over anything
    // we could render.
    if (ipc && ipc->isLocalConnection()) {
        const QString dir = localPath.isEmpty() ? thePrefs.incomingDir() : localPath;
        if (!QFileInfo(dir).isDir()) {
            logWarning(QStringLiteral("Cannot open the downloads folder: %1 does not exist")
                           .arg(QDir::toNativeSeparators(dir)));
            return false;
        }
        return openOnDesktop(QUrl::fromLocalFile(dir),
                             QStringLiteral("the downloads folder %1")
                                 .arg(QDir::toNativeSeparators(dir)));
    }

    return openIncomingInBrowser(ipc, streamToken, relPath);
}

} // namespace eMule
