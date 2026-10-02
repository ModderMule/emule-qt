#include "pch.h"
/// @file ViewSelection.cpp
/// @brief Save/restore a multi-row selection across a model reset, by stable key.

#include "utils/ViewSelection.h"

#include <QAbstractItemView>
#include <QHash>
#include <QItemSelectionModel>
#include <QScrollBar>

#include <algorithm>

namespace eMule {

ViewSelection captureViewSelection(const QAbstractItemView* view, const RowKeyFn& keyOf)
{
    ViewSelection state;
    if (!view || !view->selectionModel())
        return state;

    state.scrollValue = view->verticalScrollBar()->value();

    // selection order is click order; keep view order
    auto rows = view->selectionModel()->selectedRows(0);
    std::ranges::sort(rows, {}, &QModelIndex::row);
    state.keys.reserve(rows.size());
    for (const QModelIndex& idx : std::as_const(rows)) {
        const QString key = keyOf(idx.row());
        if (!key.isEmpty() && !state.keys.contains(key))
            state.keys.append(key);
    }

    if (const QModelIndex current = view->selectionModel()->currentIndex(); current.isValid())
        state.currentKey = keyOf(current.row());
    return state;
}

void restoreViewSelection(QAbstractItemView* view, const ViewSelection& state, const RowKeyFn& keyOf)
{
    if (!view || !view->model() || !view->selectionModel())
        return;
    view->verticalScrollBar()->setValue(state.scrollValue);
    if (state.keys.isEmpty())
        return;

    const QAbstractItemModel* model = view->model();
    const int rowCount = model->rowCount();
    const int lastCol = model->columnCount() - 1;

    // one pass instead of a scan per key
    QHash<QString, int> rowByKey;
    rowByKey.reserve(rowCount);
    for (int row = 0; row < rowCount; ++row)
        rowByKey.insert(keyOf(row), row);

    QItemSelection selection;
    QModelIndex currentIdx;
    for (const QString& key : state.keys) {
        const auto it = rowByKey.constFind(key);
        if (it == rowByKey.cend())
            continue;   // gone since the save
        const QModelIndex first = model->index(it.value(), 0);
        // whole row, hidden columns included, or selectedRows() won't report it
        selection.select(first, model->index(it.value(), lastCol));
        if (key == state.currentKey)
            currentIdx = first;
    }

    if (selection.isEmpty())
        return;
    if (!currentIdx.isValid())
        currentIdx = selection.indexes().constFirst();   // anchor vanished — take a survivor

    // view->setCurrentIndex() would ClearAndSelect the current row only
    view->selectionModel()->setCurrentIndex(currentIdx, QItemSelectionModel::NoUpdate);
    view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);

    view->verticalScrollBar()->setValue(state.scrollValue);
}

} // namespace eMule
