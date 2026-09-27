#pragma once

/// @file ListActivation.h
/// @brief MFC's list keyboard accelerators — Enter, Alt+Enter, Del, F2, Ctrl+C/V/X,
///        Ctrl+F/F3 and friends — in one place.

#include <QModelIndex>

#include <functional>

class QAbstractItemView;

namespace eMule {

/// What one of the two accelerators does, given the focused row.
using ListActivationHandler = std::function<void(const QModelIndex&)>;

/// A selection-wide command (Del, F2, Copy, ...). Reads the view's selection itself.
using ListCommandHandler = std::function<void()>;

/// Every key CMuleListCtrl turns into a command (srchybrid/MuleListCtrl.cpp:1028
/// OnKeyDown). Each list answers only some of them in its OnCommand; an empty
/// handler means "not answered here" and the key travels on to Qt untouched.
struct ListKeyHandlers {
    ListActivationHandler activate;   ///< Enter        — IDA_ENTER
    ListActivationHandler details;    ///< Alt+Enter    — MPG_ALTENTER
    ListCommandHandler    remove;     ///< Del / ⌫      — MPG_DELETE
    ListCommandHandler    rename;     ///< F2           — MPG_F2
    ListCommandHandler    copy;       ///< Copy key     — MP_COPYSELECTED
    ListCommandHandler    paste;      ///< Paste key    — MP_PASTE
    ListCommandHandler    cut;        ///< Cut key      — MP_CUT
    ListCommandHandler    insert;     ///< Insert       — FriendListCtrl's MP_ADDFRIEND
    ListCommandHandler    refresh;    ///< F5           — SharedFilesCtrl's reload
    /// Ctrl+F / F3 / Shift+F3 — MFC's "general purpose find" (FindInListDialog).
    bool find = false;
    /// Run after a find moved the current row — for lists whose details follow a click.
    ListActivationHandler found;
    /// Alt+Right / Alt+Left expand / collapse the current tree row
    /// (TransferWnd.cpp:1153, SearchListCtrl.cpp:1517). Numpad +/- is native.
    bool expandKeys = false;
};

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

/// Give @p view every MFC list key it answers. Enter and Alt+Enter behave as in
/// bindListActivation(). Del and Backspace are the same key here (⌫ is the Mac's
/// delete key). Copy/Paste/Cut/Find match the platform sequence, so it's Cmd on
/// macOS. Like the Enter pair, nothing fires while a cell editor is open.
///
/// Call once per view; a second call installs a second filter.
void bindListKeys(QAbstractItemView* view, ListKeyHandlers handlers);

} // namespace eMule
