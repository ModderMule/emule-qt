#include "pch.h"
/// @file PreviewLauncher.cpp
/// @brief Launch a media player, a browser or the file manager for what the
///        daemon holds.

#include "utils/PreviewLauncher.h"

#include "app/IpcClient.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QUrlQuery>

namespace eMule {

namespace {

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

} // namespace

void launchPreview(const QString& url)
{
    const QString playerCmd = thePrefs.videoPlayerCommand();
    if (playerCmd.isEmpty()) {
        logWarning(QStringLiteral("No video player configured. Set it in Options → Files."));
        return;
    }

    QString args = thePrefs.videoPlayerArgs();
    QStringList argList;
    if (args.contains(QStringLiteral("%1"))) {
        args.replace(QStringLiteral("%1"), url);
        argList = QProcess::splitCommand(args);
    } else {
        if (!args.isEmpty())
            argList = QProcess::splitCommand(args);
        argList.append(url);
    }

    // Reuse existing VLC instance (matches original eMule ShellExecute behavior).
    // macOS VLC doesn't support --one-instance; use `open -a` which sends the URL
    // to the running app. Linux/Windows VLC supports --one-instance --playlist-replace.
    const QString playerName = QFileInfo(playerCmd).completeBaseName().toLower();
    if (playerName == QStringLiteral("vlc")) {
#ifdef Q_OS_MACOS
        QProcess::startDetached(QStringLiteral("open"),
            QStringList{QStringLiteral("-a"), playerCmd} + argList);
#else
        argList.prepend(QStringLiteral("--playlist-replace"));
        argList.prepend(QStringLiteral("--one-instance"));
        QProcess::startDetached(playerCmd, argList);
#endif
    } else {
        QProcess::startDetached(playerCmd, argList);
    }
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

    return QDesktopServices::openUrl(QUrl(daemonIncomingUrl(ipc, streamToken, relPath)));
}

bool openIncomingFolder(const IpcClient* ipc, const QString& streamToken,
                        const QString& localPath, const QString& relPath)
{
    // Local core: same filesystem, so the real file manager wins over anything
    // we could render.
    if (ipc && ipc->isLocalConnection()) {
        const QString dir = localPath.isEmpty() ? thePrefs.incomingDir() : localPath;
        return QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }

    return openIncomingInBrowser(ipc, streamToken, relPath);
}

} // namespace eMule
