#pragma once

/// @file FolderList.h
/// @brief Folder listing for the folder pickers and share trees. A GUI on another
/// host asks the core for these (IPC ListFolders), a local GUI calls them itself.
/// Blocking disk reads: a NAS folder can take a while.

#include <QList>
#include <QString>
#include <QStringList>

namespace eMule {

struct FolderEntry {
    QString label;              ///< shown name ("Data (E:)", a folder's name)
    QString path;               ///< absolute, '/' separators
    bool hasSubfolders = false; ///< draws the expand arrow
};

/// The machine's tops: "/" and readable volumes on macOS, drives elsewhere
[[nodiscard]] QList<FolderEntry> folderRoots();

/// Visible, readable folders right below @p path, by name
[[nodiscard]] QList<FolderEntry> subfolders(const QString& path);

/// Any visible, readable folder below @p path
[[nodiscard]] bool hasSubfolders(const QString& path);

/// @p root and every visible folder below it, symlinks followed once; at most @p cap
[[nodiscard]] QStringList folderTree(const QString& root, int cap = 50000);

/// "//server/share" of a UNC path (either separator), "" for any other path. No
/// drive is listed for these: the trees add them as roots (MFC m_aUNCshares).
[[nodiscard]] QString uncShareRoot(const QString& path);

/// Distinct UNC share roots of @p dirs, sorted, case folded on compare
[[nodiscard]] QStringList uncShareRoots(const QStringList& dirs);

/// "\\server\share": a UNC root as Windows writes it
[[nodiscard]] QString uncLabel(const QString& root);

/// A drive root ("C:/"): the core runs on Windows
[[nodiscard]] bool isDriveRoot(const QString& path);

} // namespace eMule
