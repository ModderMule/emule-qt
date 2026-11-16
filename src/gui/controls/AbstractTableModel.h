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
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <type_traits>
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

    /// Replace all rows, matched by @p keyOf: rows that left are removed, new ones
    /// inserted, the rest updated in place (and reordered by a layout change).
    /// No reset on a refresh: a reset drops the view's pressed index, which swallows a
    /// double click landing across it, and kills selection + persistent indexes.
    /// Falls back to a reset when keys repeat (nothing to match on).
    /// @p keyOf must return a value: a joined string needs `-> QString`, or the
    /// deduced QStringBuilder dangles.
    template<class KeyOf>
    void syncRows(std::vector<Row> rows, KeyOf keyOf)
    {
        using Key = std::remove_cvref_t<std::invoke_result_t<KeyOf&, const Row&>>;
        QHash<Key, int> newPos;
        newPos.reserve(static_cast<qsizetype>(rows.size()));
        for (size_t i = 0; i < rows.size(); ++i)
            newPos.insert(keyOf(rows[i]), static_cast<int>(i));
        QSet<Key> oldKeys;
        oldKeys.reserve(static_cast<qsizetype>(m_rows.size()));
        for (const Row& r : m_rows)
            oldKeys.insert(keyOf(r));
        if (m_rows.empty() || rows.empty()
            || newPos.size() != static_cast<qsizetype>(rows.size())
            || oldKeys.size() != static_cast<qsizetype>(m_rows.size())) {
            setRows(std::move(rows));
            return;
        }

        // 1. rows that left, bottom-up a run at a time
        for (int last = count() - 1; last >= 0; --last) {
            if (newPos.contains(keyOf(m_rows[size_t(last)])))
                continue;
            int first = last;
            while (first > 0 && !newPos.contains(keyOf(m_rows[size_t(first - 1)])))
                --first;
            this->beginRemoveRows({}, first, last);
            m_rows.erase(m_rows.begin() + first, m_rows.begin() + last + 1);
            this->endRemoveRows();
            last = first;
        }

        // 2. survivors into the new relative order
        const auto posOf = [&](const Row& r) { return newPos.value(keyOf(r)); };
        if (!std::ranges::is_sorted(m_rows, {}, posOf)) {
            emit this->layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
            std::vector<int> order(m_rows.size());   // new row -> old row
            std::iota(order.begin(), order.end(), 0);
            std::ranges::sort(order, {}, [&](int r) { return posOf(m_rows[size_t(r)]); });
            std::vector<int> moved(order.size());    // old row -> new row
            std::vector<Row> sorted;
            sorted.reserve(m_rows.size());
            for (size_t i = 0; i < order.size(); ++i) {
                moved[size_t(order[i])] = static_cast<int>(i);
                sorted.push_back(std::move(m_rows[size_t(order[i])]));
            }
            m_rows = std::move(sorted);
            const QModelIndexList from = this->persistentIndexList();
            QModelIndexList to;
            to.reserve(from.size());
            for (const QModelIndex& idx : from)
                to.append(this->index(moved[size_t(idx.row())], idx.column()));
            this->changePersistentIndexList(from, to);
            emit this->layoutChanged({}, QAbstractItemModel::VerticalSortHint);
        }

        // 3. arrivals at their place, a run at a time; survivors keep their row
        std::vector<bool> arrived(rows.size(), false);
        for (size_t i = 0; i < rows.size();) {
            if (oldKeys.contains(keyOf(rows[i]))) {
                ++i;
                continue;
            }
            size_t end = i + 1;
            while (end < rows.size() && !oldKeys.contains(keyOf(rows[end])))
                ++end;
            this->beginInsertRows({}, static_cast<int>(i), static_cast<int>(end - 1));
            for (size_t k = i; k < end; ++k) {
                arrived[k] = true;
                m_rows.insert(m_rows.begin() + static_cast<std::ptrdiff_t>(k), std::move(rows[k]));
            }
            this->endInsertRows();
            i = end;
        }

        // 4. survivors' values
        for (size_t i = 0; i < rows.size(); ++i)
            if (!arrived[i])
                m_rows[i] = std::move(rows[i]);
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
