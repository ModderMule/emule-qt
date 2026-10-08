#pragma once

/// @file AbstractListView.h
/// @brief Header-only template base for every column-based list in the GUI.
///
/// Factors out the boilerplate each list repeated by hand: seed the per-column
/// default widths, hand the header to UiState so the layout (widths, column
/// order, sort indicator, hidden columns) is restored at startup and saved on
/// change, and guard the view's selection against model resets.
///
/// A right-click on the header pops the MFC CMuleListCtrl column menu: one
/// checkable entry per column except the first, which can't be hidden.
///
/// Two bases are supported because the GUI uses both model-backed lists
/// (`QTreeView`) and item-backed lists (`QTreeWidget`); `QTreeWidget` derives
/// from `QTreeView`, so one template covers both.
///
/// @note The template intentionally carries no `Q_OBJECT` (templates can't, and
/// it adds no new signals or slots). `showEvent()` is a plain virtual, not a
/// slot, so nothing here needs moc. Same rationale as AbstractTableModel.

#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QShowEvent>
#include <QString>
#include <QTreeView>
#include <QTreeWidget>

#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "app/UiState.h"
#include "controls/ListSortClick.h"
#include "controls/SortArrowStyle.h"

namespace eMule {

/// Column-layout persistence shared by every list control.
template<class Base>
class AbstractListView : public Base {
public:
    using Base::Base;

    /// Seed @p defaultWidths (pixels, per logical column; 0 or negative leaves a
    /// column at the header default), hide @p defaultHidden, and bind the header
    /// to UiState under @p stateKey. A saved layout overrides both defaults.
    ///
    /// Call this only once the columns exist — after `setModel()` on a
    /// `QTreeView`, or after `setHeaderLabels()` on a `QTreeWidget`. A header
    /// with no sections cannot take a restore, and caching that empty state
    /// would destroy the saved layout.
    void bindColumns(const QString& stateKey,
                     std::initializer_list<int> defaultWidths = {},
                     std::initializer_list<int> defaultHidden = {})
    {
        m_stateKey = stateKey;
        m_defaultWidths.assign(defaultWidths);
        m_lockedColumns.clear();

        auto* hdr = this->header();
        int column = 0;
        for (const int width : defaultWidths) {
            if (width > 0 && column < hdr->count())
                hdr->resizeSection(column, width);
            ++column;
        }
        // Before the restore, so a saved layout wins (MFC: no saved value → default)
        for (const int hidden : defaultHidden) {
            if (hidden > 0 && hidden < hdr->count())
                hdr->setSectionHidden(hidden, true);
        }

        // Fresh install only: a saved layout carries its own sort order
        if (m_defaultSort && !theUiState.hasHeaderState(stateKey))
            this->sortByColumn(m_defaultSort->first, m_defaultSort->second);
        m_defaultSort.reset();

        theUiState.bindHeaderView(hdr, stateKey);
        installHeaderMenu();
        m_sortColumn = hdr->sortIndicatorSection();
        restoreSortValues();
    }

    /// Columns whose first click sorts descending, as MFC's numeric columns do.
    /// Survives bindColumns(); a view that swaps models sets it again per model.
    void setDescendingFirst(std::initializer_list<int> columns)
    {
        m_descendingFirst = columns;
        installSortHook();
    }

    /// A column with a second value to sort by (MFC "4-way sorting"): clicking through
    /// both directions switches between the two, shown by a double sort arrow.
    /// @p apply tells the model or proxy which value counts; it is called at once
    /// with the stored choice (@p secondByDefault on a fresh install).
    void setSortValueColumn(int column, std::function<void(bool second)> apply,
                            bool secondByDefault = false)
    {
        m_sortValues[column] = {std::move(apply), secondByDefault};
        installSortHook();
        restoreSortValues();
    }

    /// Sort order used until the user picks one. Consumed by the next
    /// bindColumns(), so set it before that call.
    void setDefaultSort(int column, Qt::SortOrder order)
    {
        m_defaultSort = std::pair{column, order};
    }

    /// Columns left out of the header menu (no data to show yet). Cleared by
    /// bindColumns(), so set them after it.
    void setLockedColumns(std::initializer_list<int> columns)
    {
        m_lockedColumns = columns;
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        Base::showEvent(event);

        // Lists that spend startup hidden — a stacked page, a non-current tab, a
        // dialog — only get real geometry here, and Qt re-lays the header out on
        // that first show (stretchLastSection above all), which can undo part of
        // the restore. Push the saved layout back exactly once; any later user
        // resize must never be overwritten.
        if (!m_restoredOnShow && !m_stateKey.isEmpty()) {
            m_restoredOnShow = true;
            theUiState.applyHeaderState(this->header(), m_stateKey);
        }
    }

private:
    struct SortValue {
        std::function<void(bool)> apply;
        bool second = false;
    };

    /// Once per view. Qt has picked the order by the time this runs; a click on a new
    /// column is detected by the press that came before it, so a restored or
    /// programmatic sort is never rewritten.
    void installSortHook()
    {
        if (m_sortHookInstalled)
            return;
        m_sortHookInstalled = true;

        auto* hdr = this->header();
        hdr->setStyle(new SortArrowStyle(hdr));
        QObject::connect(hdr, &QHeaderView::sectionPressed, this,
                         [this](int section) { m_pressedColumn = section; });
        QObject::connect(hdr, &QHeaderView::sortIndicatorChanged, this,
                         [this](int column, Qt::SortOrder order) { onSortIndicatorChanged(column, order); });
    }

    void onSortIndicatorChanged(int column, Qt::SortOrder order)
    {
        if (m_fixingSort)
            return;
        const bool clicked = m_pressedColumn == column;
        m_pressedColumn = -1;
        const bool sameColumn = column == m_sortColumn;
        m_sortColumn = column;
        if (clicked) {
            const SortClickResult click =
                resolveSortClick(sameColumn, order, m_descendingFirst.contains(column));
            const auto value = m_sortValues.find(column);
            const bool switchValue = click.switchValue && value != m_sortValues.end();
            if (switchValue) {
                value->second.second = !value->second.second;
                if (!m_stateKey.isEmpty())
                    theUiState.setSortValue(m_stateKey, column, value->second.second);
                value->second.apply(value->second.second);
            }
            if (click.order != order || switchValue) {
                m_fixingSort = true;
                this->sortByColumn(column, click.order);
                m_fixingSort = false;
            }
        }
        updateSortArrow();
    }

    void restoreSortValues()
    {
        for (auto& [column, value] : m_sortValues) {
            if (!m_stateKey.isEmpty())
                value.second = theUiState.sortValue(m_stateKey, column, value.second);
            value.apply(value.second);
        }
        updateSortArrow();
    }

    /// Double arrow while the sort column is sorted by its second value.
    void updateSortArrow()
    {
        const auto value = m_sortValues.find(m_sortColumn);
        auto* hdr = this->header();
        hdr->setProperty(SortArrowStyle::kDoubleArrowProperty,
                         value != m_sortValues.end() && value->second.second);
        hdr->viewport()->update();
    }

    /// Once per view: SearchPanel re-binds on every model switch.
    void installHeaderMenu()
    {
        if (m_headerMenuInstalled)
            return;
        m_headerMenuInstalled = true;

        auto* hdr = this->header();
        hdr->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(hdr, &QWidget::customContextMenuRequested, this,
                         [this](const QPoint& pos) { execHeaderMenu(pos); });
    }

    /// Built on open: the model and its columns may have changed since binding.
    void execHeaderMenu(const QPoint& pos)
    {
        auto* hdr = this->header();
        const auto* model = this->model();
        if (!model || hdr->count() < 2)
            return;

        // Non-modal: no nested event loop for an IPC callback or quit to land in
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        for (int column = 1; column < hdr->count(); ++column) {
            if (m_lockedColumns.contains(column))
                continue;
            auto* action = menu->addAction(
                model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
            action->setCheckable(true);
            action->setChecked(!hdr->isSectionHidden(column));
            QObject::connect(action, &QAction::triggered, this,
                             [this, column] { toggleColumn(column); });
        }
        menu->popup(hdr->viewport()->mapToGlobal(pos));
    }

    void toggleColumn(int column)
    {
        auto* hdr = this->header();
        if (column < 1 || column >= hdr->count())
            return;

        if (!hdr->isSectionHidden(column)) {
            hdr->setSectionHidden(column, true);
        } else {
            hdr->setSectionHidden(column, false);
            // A column hidden since first bind has no width of its own to come back to
            if (hdr->sectionSize(column) < hdr->minimumSectionSize()) {
                const int fallback = column < static_cast<int>(m_defaultWidths.size())
                                             && m_defaultWidths[column] > 0
                                         ? m_defaultWidths[column]
                                         : hdr->defaultSectionSize();
                hdr->resizeSection(column, fallback);
            }
        }
        if (!m_stateKey.isEmpty())
            theUiState.captureHeaderState(hdr, m_stateKey);
    }

    QString          m_stateKey;
    std::vector<int> m_defaultWidths;
    std::set<int>    m_lockedColumns;
    std::optional<std::pair<int, Qt::SortOrder>> m_defaultSort;
    bool             m_restoredOnShow = false;
    bool             m_headerMenuInstalled = false;

    std::set<int>            m_descendingFirst;
    std::map<int, SortValue> m_sortValues;
    int                      m_sortColumn = -1;
    int                      m_pressedColumn = -1;
    bool                     m_sortHookInstalled = false;
    bool                     m_fixingSort = false;
};

/// Model-backed list with persistent column layout.
using ListTreeView = AbstractListView<QTreeView>;

/// Item-backed list with persistent column layout.
using ListTreeWidget = AbstractListView<QTreeWidget>;

} // namespace eMule
