#pragma once

/// @file ListActivation.h
/// @brief MFC's list keyboard accelerators — Enter and Alt+Enter — in one place.

#include <QModelIndex>

#include <functional>

class QAbstractItemView;

namespace eMule {

/// What one of the two accelerators does, given the focused row.
using ListActivationHandler = std::function<void(const QModelIndex&)>;

/// Give @p view MFC's two list accelerators: Enter runs the list's primary
/// action, Alt+Enter opens its detail dialog.
///
/// The original needs a whole accelerator table for this, because a list control
/// never sees VK_RETURN in OnKeyDown: every CMuleListCtrl loads the one-entry
/// IDR_LISTVIEW table (srchybrid/MuleListCtrl.cpp:99,121) and
/// CMuleListCtrl::PreTranslateMessage (:1506) turns it into IDA_ENTER, posting
/// MPG_ALTENTER by hand for the Alt variant. Each control then answers those two
/// commands in its own OnCommand. Here that is one event filter plus a callback
/// per list.
///
/// The handlers are given the view's *current* row — the focused item, which is
/// what the original reads back in OnCommand. Resolve it to a hash inside the
/// handler rather than holding on to the index: these lists reset their model on
/// every poll tick.
///
/// @param view      the list; any view type, so the bare QListView lists are
///                  covered as well as the AbstractListView ones.
/// @param activate  what Enter does — the same action the list's double click
///                  performs. May be empty where the original does nothing.
/// @param details   what Alt+Enter does. Empty for a list with no detail dialog,
///                  as on MFC's server list.
void bindListActivation(QAbstractItemView* view,
                        ListActivationHandler activate,
                        ListActivationHandler details = {});

} // namespace eMule
