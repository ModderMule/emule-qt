#pragma once

/// @file SharedDirState.h
/// @brief Share state of a directory, worked out from the shared-directory list.
///
/// The folder tree and the Options directory tree both mark shared folders and the
/// folders above them. These are the list questions behind those marks — ports of
/// MFC's FileSystemTreeIsShared / FileSystemTreeHasSharedSubdirectory
/// (srchybrid/SharedDirsTreeCtrl.cpp:774, :847). Header-only, Qt Core only.

#include <QDir>
#include <QHash>
#include <QString>
#include <QStringList>

namespace eMule::SharedDirState {

/// Comparable form of a directory path: no trailing separator, case folded — the same
/// rule the core shares by (SharedFileList::pathKey).
[[nodiscard]] inline QString dirKey(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(path).toCaseFolded();
}

/// Is @p childKey strictly below @p parentKey? Both are dirKey() values. Checked on
/// the separator, so "/a/bc" is not below "/a/b".
[[nodiscard]] inline bool isBelowKey(const QString& childKey, const QString& parentKey)
{
    if (parentKey.isEmpty() || childKey.size() <= parentKey.size()
        || !childKey.startsWith(parentKey))
        return false;
    return parentKey.endsWith(u'/') || childKey.at(parentKey.size()) == u'/';
}

/// Is @p path itself in @p dirs?
[[nodiscard]] inline bool isSharedDir(const QStringList& dirs, const QString& path)
{
    const QString key = dirKey(path);
    if (key.isEmpty())
        return false;
    for (const QString& d : dirs)
        if (dirKey(d) == key)
            return true;
    return false;
}

/// Is any entry of @p dirs strictly below @p path?
[[nodiscard]] inline bool hasSharedSubdir(const QStringList& dirs, const QString& path)
{
    const QString key = dirKey(path);
    for (const QString& d : dirs)
        if (isBelowKey(dirKey(d), key))
            return true;
    return false;
}

/// Is any entry of @p dirs at or below @p path?
[[nodiscard]] inline bool hasDirAtOrBelow(const QStringList& dirs, const QString& path)
{
    return isSharedDir(dirs, path) || hasSharedSubdir(dirs, path);
}

/// @p dirs without @p path and, with @p subdirs, everything below it. Works on the
/// list alone, so a folder that is gone from disk can still be unshared.
[[nodiscard]] inline QStringList withoutDir(const QStringList& dirs, const QString& path,
                                            bool subdirs)
{
    const QString key = dirKey(path);
    QStringList kept;
    for (const QString& d : dirs) {
        const QString k = dirKey(d);
        if (k == key || (subdirs && isBelowKey(k, key)))
            continue;
        kept.append(d);
    }
    return kept;
}

/// For every entry of @p dirs its nearest ancestor that is in @p dirs too, or an empty
/// string for a top-level one. Keyed and valued by the entries as given.
[[nodiscard]] inline QHash<QString, QString> nearestSharedParents(const QStringList& dirs)
{
    QHash<QString, QString> parents;
    for (const QString& d : dirs) {
        const QString key = dirKey(d);
        QString best;
        qsizetype bestLen = -1;
        for (const QString& other : dirs) {
            const QString otherKey = dirKey(other);
            if (isBelowKey(key, otherKey) && otherKey.size() > bestLen) {
                best = other;
                bestLen = otherKey.size();
            }
        }
        parents.insert(d, best);
    }
    return parents;
}

} // namespace eMule::SharedDirState
