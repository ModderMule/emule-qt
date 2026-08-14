#pragma once

/// @file ViewNavigation.h
/// @brief MFC's CListCtrlItemWalk over a Qt item view.
///
/// The original client lets every detail dialog step to the previous/next row of
/// the list it was opened from (srchybrid/ListCtrlItemWalk.h,
/// ListViewWalkerPropertySheet.cpp:209-255). The walk has three properties worth
/// spelling out, because they are easy to get subtly wrong here:
///
///   * It runs over the **whole list in the order the user sees**, not over the
///     rows that happened to be selected when the dialog opened.
///   * It **moves the list's own selection** and scrolls the new row into view,
///     which is what makes dependent panes (the Shared Files bottom tabs) follow.
///   * It **does not wrap**; at either end MFC just beeps.
///
/// MFC's download list is flat with expanded sources inlined, so its walker
/// filters by row type instead of by nesting (DownloadListCtrl.cpp:2409-2464):
/// the file dialog steps over download rows only, the client dialog over source
/// rows only — crossing from one download's last source into the next
/// download's first. QTreeView::indexAbove()/indexBelow() walk in exactly that
/// order, so a row filter reproduces both cases; sibling-only stepping would not.

#include <QModelIndex>

#include <functional>

class QAbstractItemView;
class QTreeView;

namespace eMule::ViewNav {

/// Decides whether a row may become the walker's next stop.
/// The index is in the **view's** coordinates (topmost proxy), not the source model's.
using RowFilter = std::function<bool(const QModelIndex&)>;

/// Unwrap an arbitrarily deep chain of QAbstractProxyModels down to the root
/// source model. A source-model index is returned unchanged.
///
/// The Transfers download tree sits behind two chained proxies, so callers that
/// hand-roll `mapToSource(mapToSource(idx))` break the moment a third is added.
[[nodiscard]] QModelIndex toSource(QModelIndex index);

/// Inverse of toSource(): lift a source-model index up through every proxy in
/// @p view's chain. Invalid when the row is filtered out anywhere on the way up
/// (a category tab in Transfers, the folder filter in Shared Files).
[[nodiscard]] QModelIndex fromSource(const QAbstractItemView* view, QModelIndex sourceIndex);

/// The row the walker would land on, without touching the view.
///
/// Steps @p delta (-1 up, +1 down) from @p from in visual order — descending into
/// expanded children exactly as the Down-arrow key does — and keeps stepping while
/// @p accept rejects a row. Invalid at the ends. A default-constructed @p accept
/// takes every row (the plain CListCtrlItemWalk case).
[[nodiscard]] QModelIndex peekStep(const QTreeView* view, const QModelIndex& from,
                                   int delta, const RowFilter& accept = {});

/// peekStep() plus MFC's side effects: deselect the old row, select and focus the
/// new one, and scroll it into view (EnsureVisible).
/// Returns the new view index, invalid at the ends.
QModelIndex step(QTreeView* view, const QModelIndex& from, int delta,
                 const RowFilter& accept = {});

/// Row filter: top-level rows only — MFC's FILE_TYPE walk over download rows.
[[nodiscard]] bool isTopLevel(const QModelIndex& index);

/// Row filter: child rows only — MFC's AVAILABLE_SOURCE walk over source rows.
[[nodiscard]] bool isChild(const QModelIndex& index);

} // namespace eMule::ViewNav
