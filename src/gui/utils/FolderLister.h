#pragma once

/// @file FolderLister.h
/// @brief The core's folders for pickers and share trees: read here for a local core,
/// asked over IPC (ListFolders) for one on another host.

#include "utils/FolderList.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QObject;
class QWidget;

namespace eMule {
class IpcClient;
}

namespace eMule::FolderLister {

/// The core runs on another host: its disk is not ours to read
[[nodiscard]] bool remote(const IpcClient* ipc);

using Rows = std::function<void(bool ok, const QList<FolderEntry>& rows, const QString& home)>;

/// Folders right below @p path ("" = drives). Always answers later: a local core's
/// disk is read on a worker thread, a remote one over IPC; @p done runs on the GUI
/// thread and is dropped when @p ctx is gone.
void list(IpcClient* ipc, const QString& path, QObject* ctx, Rows done);

/// @p root and every folder below it (Ctrl+click share), as list()
void tree(IpcClient* ipc, const QString& root, QObject* ctx, std::function<void(const QStringList&)> done);

/// Asks for a UNC folder (MFC OnBnClickedAddUNC). "//server/share/..." with '/'
/// separators, "" when cancelled or refused.
[[nodiscard]] QString askUncShare(QWidget* parent);

} // namespace eMule::FolderLister
