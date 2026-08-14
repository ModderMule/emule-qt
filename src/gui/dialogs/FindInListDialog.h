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

/// Ask for a term and a column, then select and scroll to the first row whose
/// text in that column contains it (case-insensitive). Modal; returns when the
/// user closes it. Searches the view's own rows, so the current sort and any
/// active filter apply.
void showFindInListDialog(QWidget* parent, QAbstractItemView* view);

} // namespace eMule
