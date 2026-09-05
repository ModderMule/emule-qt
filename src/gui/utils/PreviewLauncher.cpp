#include "pch.h"
/// @file PreviewLauncher.cpp
/// @brief Launch a media player, a browser or the file manager for what the
///        daemon holds.

#include "utils/PreviewLauncher.h"

#include "app/IpcClient.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QUrlQuery>

namespace eMule {

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

    return QStringLiteral("http://%1:%2/api/v1/downloads/%3/preview?token=%4")
        .arg(ipc->daemonHost())
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

    QString url = QStringLiteral("http://%1:%2/api/v1/usenet/%3/%4/preview?token=%5")
                      .arg(ipc->daemonHost())
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
    url.setScheme(QStringLiteral("http"));
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

bool openIncomingFolder(const IpcClient* ipc, const QString& streamToken)
{
    // Local core: same filesystem, so the real file manager wins over anything
    // we could render.
    if (ipc && ipc->isLocalConnection())
        return QDesktopServices::openUrl(QUrl::fromLocalFile(thePrefs.incomingDir()));

    // Remote, and worth checking before building a URL: with both web surfaces
    // off the daemon pins its HTTP listener to loopback (DaemonApp), so the page
    // would be unreachable and the browser would open on a dead tab.
    if (!thePrefs.webServerEnabled() && !thePrefs.webServerRestApiEnabled()) {
        logWarning(QStringLiteral(
            "Cannot show the core's Incoming folder: the core is remote and its web "
            "server only listens on localhost. Enable Web Interface or REST API in "
            "Options -> Web Interface."));
        return false;
    }

    const QString url = daemonIncomingUrl(ipc, streamToken);
    if (url.isEmpty()) {
        logWarning(QStringLiteral(
            "Cannot show the core's Incoming folder: no stream token yet. It arrives "
            "with the core's next status update."));
        return false;
    }

    return QDesktopServices::openUrl(QUrl(url));
}

} // namespace eMule
