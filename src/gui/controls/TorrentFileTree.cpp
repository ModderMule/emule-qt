#include "pch.h"

/// @file TorrentFileTree.cpp
/// @brief A torrent's files as a folder tree.

#include "controls/TorrentFileTree.h"

#include "controls/SortableItems.h"
#include "utils/MenuUtils.h"
#include "utils/StringUtils.h"

#include <QCborArray>
#include <QCborMap>
#include <QTreeWidget>

namespace eMule::TorrentFileTree {

QTreeWidgetItem* folderItem(QTreeWidget* tree, const QString& path,
                            QHash<QString, QTreeWidgetItem*>& folders, Qt::ItemFlags extraFlags)
{
    if (auto* found = folders.value(path))
        return found;
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    QTreeWidgetItem* parent = slash > 0 ? folderItem(tree, path.left(slash), folders, extraFlags) : nullptr;
    auto* item = parent ? new SortableTreeItem(parent) : new SortableTreeItem(tree);
    item->setData(0, kFileIndexRole, -1);
    item->setData(0, kPathRole, path);
    item->setText(0, slash >= 0 ? path.mid(slash + 1) : path);
    item->setIcon(0, menuIcon("FolderOpen.ico"));
    item->setFlags(item->flags() | extraFlags);
    folders.insert(path, item);
    return item;
}

int fill(QTreeWidget* tree, const QCborArray& files, int nameColumn, int sizeColumn)
{
    tree->setSortingEnabled(false);
    tree->clear();
    QHash<QString, QTreeWidgetItem*> folders;
    QHash<QTreeWidgetItem*, qint64> folderSizes;
    int count = 0;
    for (const auto& v : files) {
        const QCborMap f = v.toMap();
        if (f.value(QStringLiteral("pad")).toBool())
            continue;   // alignment padding, not a real file
        const QString path = f.value(QStringLiteral("path")).toString();
        const qint64 size = f.value(QStringLiteral("size")).toInteger();
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem* parent = slash > 0 ? folderItem(tree, path.left(slash), folders) : nullptr;
        for (auto* p = parent; p; p = p->parent())
            folderSizes[p] += size;
        auto* item = parent ? new SortableTreeItem(parent) : new SortableTreeItem(tree);
        item->setData(0, kFileIndexRole, int(f.value(QStringLiteral("index")).toInteger()));
        item->setText(nameColumn, slash >= 0 ? path.mid(slash + 1) : path);
        item->setToolTip(nameColumn, path);
        item->setText(sizeColumn, formatByteSize(size));
        item->setData(sizeColumn, SortRole, QVariant::fromValue(size));
        item->setTextAlignment(sizeColumn, Qt::AlignRight | Qt::AlignVCenter);
        ++count;
    }
    for (auto it = folderSizes.cbegin(); it != folderSizes.cend(); ++it) {
        it.key()->setText(sizeColumn, formatByteSize(it.value()));
        it.key()->setData(sizeColumn, SortRole, QVariant::fromValue(it.value()));
        it.key()->setTextAlignment(sizeColumn, Qt::AlignRight | Qt::AlignVCenter);
    }
    tree->setSortingEnabled(true);
    if (tree->topLevelItemCount() == 1)
        tree->expandToDepth(0);   // the torrent's own folder
    return count;
}

} // namespace eMule::TorrentFileTree
