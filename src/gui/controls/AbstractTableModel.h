#pragma once

/// @file AbstractTableModel.h
/// @brief Header-only template base for the full-reset GUI table models.
///
/// Factors out the storage vector and the structural boilerplate
/// (`rowCount`/`columnCount`, `setRows`/`clear`, `rowAt`) that every list model
/// repeats verbatim. Subclasses keep their own `Q_OBJECT` and implement only the
/// parts that differ: `columnCountValue()`, `data()` and `headerData()`.
///
/// @note The template intentionally carries no `Q_OBJECT` (templates can't, and
/// it adds no new meta members). moc resolves a concrete model's superclass
/// expression `AbstractTableModel<Row>::staticMetaObject` through inheritance to
/// `QAbstractTableModel::staticMetaObject`, so concrete subclasses compile with
/// `Q_OBJECT` intact and keep their `tr()` context and `qobject_cast` support.

#include <QAbstractTableModel>

#include <algorithm>
#include <utility>
#include <vector>

namespace eMule {

/// Storage + structural boilerplate shared by full-reset table models.
template<class Row>
class AbstractTableModel : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }

    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : columnCountValue();
    }

    /// Replace all rows with a new snapshot (full reset).
    void setRows(std::vector<Row> rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }

    /// Replace all rows. When @p sameKey pairs every old row with the new one at the
    /// same position, update in place (dataChanged) instead of a reset: a reset drops
    /// the view's pressed index, swallowing a double click, and kills persistent indexes.
    template<class SameKey>
    void updateRows(std::vector<Row> rows, SameKey sameKey)
    {
        if (rows.empty() || rows.size() != m_rows.size()
            || !std::ranges::equal(m_rows, rows, sameKey)) {
            setRows(std::move(rows));
            return;
        }
        m_rows = std::move(rows);
        emit this->dataChanged(this->index(0, 0),
                               this->index(rowCount() - 1, columnCount() - 1));
    }

    /// Remove all rows (full reset). No-op when already empty.
    void clear()
    {
        if (m_rows.empty())
            return;
        beginResetModel();
        m_rows.clear();
        endResetModel();
    }

    /// Number of rows currently held.
    [[nodiscard]] int count() const { return static_cast<int>(m_rows.size()); }

    /// Row snapshot at @p row, or nullptr if out of range.
    [[nodiscard]] const Row* rowAt(int row) const
    {
        if (row < 0 || row >= static_cast<int>(m_rows.size()))
            return nullptr;
        return &m_rows[static_cast<size_t>(row)];
    }

protected:
    /// Column count to report when the parent index is invalid (top level).
    /// Most models return a fixed constant; mode-driven models switch on state.
    [[nodiscard]] virtual int columnCountValue() const = 0;

    std::vector<Row> m_rows;
};

} // namespace eMule
