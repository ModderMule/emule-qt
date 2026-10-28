#pragma once

/// @file TorrentFileTree.h
/// @brief A torrent's files as a folder tree: shared by the Torrents tab's Files
///        list (TorrentListsPane) and a search result's Details (MetaResultDetailDialog).
///
/// Input is the `files` array of GetTorrentDetails / inspectTorrent:
/// [{index, path ('/' separated), size, pad}]. Pad files are skipped.

#include <QHash>
#include <QString>
#include <Qt>

class QCborArray;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule::TorrentFileTree {

/// File index on a file row; -1 on a folder row (column 0)
constexpr int kFileIndexRole = Qt::UserRole + 1;
/// Folder path on a folder row (column 0)
constexpr int kPathRole = Qt::UserRole + 2;

/// The folder row of @p path, its parent folders created first. @p folders maps
/// path → row and is filled as rows are made. @p extraFlags are added to new rows.
QTreeWidgetItem* folderItem(QTreeWidget* tree, const QString& path,
                            QHash<QString, QTreeWidgetItem*>& folders,
                            Qt::ItemFlags extraFlags = {});

/// Read-only fill: name and size of each file, folder sizes summed. Clears the
/// tree first. @return the number of files shown.
int fill(QTreeWidget* tree, const QCborArray& files, int nameColumn, int sizeColumn);

} // namespace eMule::TorrentFileTree
