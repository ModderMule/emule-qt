#pragma once

/// @file SafeRemove.h
/// @brief The one way to delete a folder tree: QDir(path).removeRecursively() with a guard.
///
/// QDir("") is the working directory, and a relative path is resolved against it: an unset
/// scratch-folder field once wiped the daemon's working directory on shutdown. Every
/// recursive delete goes through removeDirTree(), which refuses such paths.
/// Header-only: core, daemon, ed3k, usenet and GUI all use it.

#include "utils/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace eMule {

/// Why @p path must not be removed recursively; empty: it may.
[[nodiscard]] inline QString unsafeDirTreeReason(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QStringLiteral("empty path");
    if (QDir::isRelativePath(path))
        return QStringLiteral("relative path");
    const QString clean = QDir::cleanPath(QDir(path).absolutePath());
    if (QDir(clean).isRoot())
        return QStringLiteral("filesystem root");
    // "/Users", "C:/Users", "/home": too close to the root to be a folder of ours
    QStringList parts = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!parts.isEmpty() && parts.first().endsWith(QLatin1Char(':')))
        parts.removeFirst();   // the drive letter
    if (parts.size() < 2)
        return QStringLiteral("top-level folder");
    // symlinks resolved: macOS /var is /private/var
    const auto canon = [](const QString& p) {
        const QString c = QFileInfo(p).canonicalFilePath();
        return c.isEmpty() ? QDir::cleanPath(p) : c;
    };
    const QString real = canon(clean);
    const auto same = [&](const QString& other) {
        return !other.isEmpty() && canon(other).compare(real, Qt::CaseInsensitive) == 0;
    };
    if (same(QDir::homePath()))
        return QStringLiteral("home folder");
    if (same(QDir::currentPath()))
        return QStringLiteral("working directory");
    return {};
}

/// QDir(@p path).removeRecursively(), refused (false, logged) for an unsafe path.
inline bool removeDirTree(const QString& path)
{
    if (const QString why = unsafeDirTreeReason(path); !why.isEmpty()) {
        logWarning(QStringLiteral("Refused to delete folder tree \"%1\": %2").arg(path, why));
        return false;
    }
    return QDir(path).removeRecursively();
}

} // namespace eMule
