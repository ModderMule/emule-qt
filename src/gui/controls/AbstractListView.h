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

#include <initializer_list>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "app/UiState.h"

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
};

/// Model-backed list with persistent column layout.
using ListTreeView = AbstractListView<QTreeView>;

/// Item-backed list with persistent column layout.
using ListTreeWidget = AbstractListView<QTreeWidget>;

} // namespace eMule
