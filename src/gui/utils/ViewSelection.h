#pragma once

/// @file ViewSelection.h
/// @brief Save/restore a multi-row selection across a model reset, by stable key.
///
/// A reset (AbstractTableModel::setRows) wipes the view's selection. Restoring
/// it has two traps that both collapse a multi-selection to one row:
/// QAbstractItemView::setCurrentIndex() ClearAndSelects, and selectedRows()
/// only reports a row whose every model column is selected — hidden ones too.

#include <QString>
#include <QStringList>

#include <functional>

class QAbstractItemView;

namespace eMule {

/// A view's selection, keyed so it survives a reset and a re-sort.
struct ViewSelection {
    QStringList keys;      ///< every selected row, in view order
    QString currentKey;    ///< current/anchor row
    int scrollValue = 0;

    [[nodiscard]] bool isEmpty() const { return keys.isEmpty(); }
};

/// Stable key of the row at @p viewRow (a row of the view's own model).
using RowKeyFn = std::function<QString(int viewRow)>;

[[nodiscard]] ViewSelection captureViewSelection(const QAbstractItemView* view, const RowKeyFn& keyOf);

/// Re-select every row of @p state still present; rows that vanished are skipped.
void restoreViewSelection(QAbstractItemView* view, const ViewSelection& state, const RowKeyFn& keyOf);

} // namespace eMule
