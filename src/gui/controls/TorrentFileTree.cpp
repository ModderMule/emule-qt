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
#include <QTreeWidgetItemIterator>

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

namespace {

int fillTree(QTreeWidget* tree, const QCborArray& files, int nameColumn, int sizeColumn, bool checkable,
             const QList<int>& checked)
{
    tree->setSortingEnabled(false);
    tree->clear();
    QHash<QString, QTreeWidgetItem*> folders;
    QHash<QTreeWidgetItem*, qint64> folderSizes;
    const Qt::ItemFlags folderFlags =
        checkable ? Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate : Qt::ItemFlags{};
    int count = 0;
    for (const auto& v : files) {
        const QCborMap f = v.toMap();
        if (f.value(QStringLiteral("pad")).toBool())
            continue;   // alignment padding, not a real file
        const QString path = f.value(QStringLiteral("path")).toString();
        const qint64 size = f.value(QStringLiteral("size")).toInteger();
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem* parent = slash > 0 ? folderItem(tree, path.left(slash), folders, folderFlags) : nullptr;
        for (auto* p = parent; p; p = p->parent())
            folderSizes[p] += size;
        auto* item = parent ? new SortableTreeItem(parent) : new SortableTreeItem(tree);
        const int index = int(f.value(QStringLiteral("index")).toInteger());
        item->setData(0, kFileIndexRole, index);
        if (checkable) {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(nameColumn, checked.isEmpty() || checked.contains(index) ? Qt::Checked
                                                                                        : Qt::Unchecked);
        }
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
    // only some files picked: open their folders, so the pick is in sight
    if (checkable && !checked.isEmpty()) {
        QTreeWidgetItem* first = nullptr;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->data(0, kFileIndexRole).toInt() < 0 || (*it)->checkState(nameColumn) != Qt::Checked)
                continue;
            for (auto* p = (*it)->parent(); p; p = p->parent())
                p->setExpanded(true);
            if (!first)
                first = *it;
        }
        if (first)
            tree->scrollToItem(first);
    }
    return count;
}

} // namespace

int fill(QTreeWidget* tree, const QCborArray& files, int nameColumn, int sizeColumn)
{
    return fillTree(tree, files, nameColumn, sizeColumn, false, {});
}

int fillCheckable(QTreeWidget* tree, const QCborArray& files, int nameColumn, int sizeColumn,
                  const QList<int>& checked)
{
    return fillTree(tree, files, nameColumn, sizeColumn, true, checked);
}

QList<int> checkedFiles(QTreeWidget* tree, int column)
{
    QList<int> out;
    bool all = true;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        const int index = (*it)->data(0, kFileIndexRole).toInt();
        if (index < 0)
            continue;   // a folder
        if ((*it)->checkState(column) == Qt::Checked)
            out.append(index);
        else
            all = false;
    }
    return all ? QList<int>{} : out;
}

bool anyChecked(QTreeWidget* tree, int column)
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, kFileIndexRole).toInt() >= 0 && (*it)->checkState(column) == Qt::Checked)
            return true;
    return false;
}

} // namespace eMule::TorrentFileTree
