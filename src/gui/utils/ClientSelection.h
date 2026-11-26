#pragma once

/// @file ClientSelection.h
/// @brief Keep a client list's selection across a poll refresh, keyed by user hash.
///
/// MFC deletes a gone client's row and selects nothing else (UploadListCtrl.cpp:447
/// RemoveClient). Qt's SingleSelection view instead selects a neighbour when the
/// current row is removed, even an unselected current row (issue #253), so restore()
/// clears whatever no saved key backs.

#include "controls/ClientListModel.h"

#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QSortFilterProxyModel>
#include <QStringList>

namespace eMule::ClientSelection {

/// User hashes of the selected rows, current one first. The view's model must be
/// a sort proxy over @p model.
[[nodiscard]] inline QStringList save(const QAbstractItemView* view, const ClientListModel* model)
{
    // Key on the selected rows, not the current one: a deselected row stays current,
    // and restoring it would undo the deselect on the next refresh (issue #27)
    const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(view->model());
    if (!proxy)
        return {};

    QStringList keys;
    const auto keyOf = [&](const QModelIndex& proxyIdx) {
        const auto* client = model->clientAt(proxy->mapToSource(proxyIdx).row());
        if (client && !client->userHash.isEmpty() && !keys.contains(client->userHash))
            keys << client->userHash;
    };
    const auto* sel = view->selectionModel();
    if (const QModelIndex cur = sel->currentIndex(); cur.isValid() && sel->isSelected(cur))
        keyOf(cur);
    for (const QModelIndex& idx : sel->selectedRows(0))
        keyOf(idx);
    return keys;
}

/// Reselect the rows of @p keys still in @p model; select nothing else.
inline void restore(QAbstractItemView* view, const ClientListModel* model, const QStringList& keys)
{
    const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(view->model());
    if (!proxy)
        return;
    auto* sel = view->selectionModel();

    // setCurrentIndex() on the view would ClearAndSelect, so set current without updating and
    // select every column of each row (selectedRows() wants whole rows)
    QModelIndex current;
    QItemSelection selection;
    const int lastCol = model->columnCount() - 1;
    for (int row = 0; !keys.isEmpty() && row < model->clientCount(); ++row) {
        const auto* client = model->clientAt(row);
        if (!client)
            continue;
        const qsizetype pos = keys.indexOf(client->userHash);
        if (pos < 0)
            continue;
        const QModelIndex first = proxy->mapFromSource(model->index(row, 0));
        const QModelIndex last = proxy->mapFromSource(model->index(row, lastCol));
        if (!first.isValid())
            continue;
        selection.select(first, last.isValid() ? last : first);
        if (pos == 0)
            current = first;
    }
    if (selection.isEmpty()) {
        // drop the neighbour Qt picked when the current row left; current itself stays
        if (sel->hasSelection())
            sel->clearSelection();
        return;
    }

    if (!current.isValid())
        current = selection.first().topLeft();
    sel->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
    sel->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    // no scrollTo(current): it jumps the horizontal bar back to column 0 on every refresh
}

} // namespace eMule::ClientSelection
