#include "pch.h"
/// @file PreviewLauncher.cpp
/// @brief Launch a media player for preview streaming.

#include "utils/PreviewLauncher.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QFileInfo>
#include <QProcess>

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

} // namespace eMule
