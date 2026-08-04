#pragma once

/// @file AbstractListView.h
/// @brief Header-only template base for every column-based list in the GUI.
///
/// Factors out the boilerplate each list repeated by hand: seed the per-column
/// default widths, hand the header to UiState so the layout (widths, column
/// order, sort indicator) is restored at startup and saved on change, and guard
/// the view's selection against model resets.
///
/// Two bases are supported because the GUI uses both model-backed lists
/// (`QTreeView`) and item-backed lists (`QTreeWidget`); `QTreeWidget` derives
/// from `QTreeView`, so one template covers both.
///
/// @note The template intentionally carries no `Q_OBJECT` (templates can't, and
/// it adds no new signals or slots). `showEvent()` is a plain virtual, not a
/// slot, so nothing here needs moc. Same rationale as AbstractTableModel.

#include <QHeaderView>
#include <QShowEvent>
#include <QString>
#include <QTreeView>
#include <QTreeWidget>

#include <initializer_list>

#include "app/UiState.h"

namespace eMule {

/// Column-layout persistence shared by every list control.
template<class Base>
class AbstractListView : public Base {
public:
    using Base::Base;

    /// Seed @p defaultWidths (pixels, per logical column; 0 or negative leaves a
    /// column at the header default) and bind the header to UiState under
    /// @p stateKey. A saved layout overrides the defaults.
    ///
    /// Call this only once the columns exist — after `setModel()` on a
    /// `QTreeView`, or after `setHeaderLabels()` on a `QTreeWidget`. A header
    /// with no sections cannot take a restore, and caching that empty state
    /// would destroy the saved layout.
    void bindColumns(const QString& stateKey,
                     std::initializer_list<int> defaultWidths = {})
    {
        m_stateKey = stateKey;

        auto* hdr = this->header();
        int column = 0;
        for (const int width : defaultWidths) {
            if (width > 0 && column < hdr->count())
                hdr->resizeSection(column, width);
            ++column;
        }

        theUiState.bindHeaderView(hdr, stateKey);
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
    QString m_stateKey;
    bool    m_restoredOnShow = false;
};

/// Model-backed list with persistent column layout.
using ListTreeView = AbstractListView<QTreeView>;

/// Item-backed list with persistent column layout.
using ListTreeWidget = AbstractListView<QTreeWidget>;

} // namespace eMule
