#pragma once

/// @file FindInListDialog.h
/// @brief The "Find..." context-menu action shared by every list panel.
///
/// MFC has one Find behind MP_FIND on the Transfers, Shared Files and Search
/// lists; this is the Qt equivalent, driven entirely off the view so each list
/// gets its own column names without repeating the dialog three times.

#include <QString>

class QAbstractItemView;
class QWidget;

namespace eMule {

/// Ask for a term and a column (pre-filled with the view's last ones, term
/// selected), then select and scroll to the next row after the selected one
/// (or the first, with none) whose text in that column contains it
/// (case-insensitive, wrapping). Says so when nothing matches. Modal. Searches
/// the view's own rows, so the current sort and any active filter apply.
void showFindInListDialog(QWidget* parent, QAbstractItemView* view);

/// F3 / Shift+F3: repeat the view's last find from the row after (or before)
/// the selected one, wrapping around; beeps on no match. With no earlier find
/// on @p view it opens the dialog instead — MFC CMuleListCtrl::OnFindNext does the same.
void findNextInList(QWidget* parent, QAbstractItemView* view, bool backwards = false);

} // namespace eMule
