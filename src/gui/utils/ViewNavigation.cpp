#include "pch.h"
/// @file ViewNavigation.cpp
/// @brief Prev/Next walking over an item view — see ViewNavigation.h.

#include "utils/ViewNavigation.h"

#include <QAbstractItemView>
#include <QAbstractProxyModel>
#include <QItemSelectionModel>
#include <QTreeView>

#include <vector>

namespace eMule::ViewNav {

QModelIndex toSource(QModelIndex index)
{
    while (const auto* proxy = qobject_cast<const QAbstractProxyModel*>(index.model()))
        index = proxy->mapToSource(index);
    return index;
}

QModelIndex fromSource(const QAbstractItemView* view, QModelIndex sourceIndex)
{
    if (!view || !sourceIndex.isValid())
        return {};

    // Collect the chain top-down (view model first), then apply mapFromSource in
    // reverse so the deepest proxy — the one sitting directly on the source
    // model — is the first to lift the index.
    std::vector<const QAbstractProxyModel*> chain;
    for (const auto* model = view->model(); model;) {
        const auto* proxy = qobject_cast<const QAbstractProxyModel*>(model);
        if (!proxy)
            break;
        chain.push_back(proxy);
        model = proxy->sourceModel();
    }

    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        sourceIndex = (*it)->mapFromSource(sourceIndex);
        if (!sourceIndex.isValid())
            return {};   // filtered out somewhere on the way up
    }
    return sourceIndex;
}

QModelIndex peekStep(const QTreeView* view, const QModelIndex& from,
                     int delta, const RowFilter& accept)
{
    if (!view || !from.isValid() || delta == 0)
        return {};

    // indexAbove()/indexBelow() are column-0 based; a hit on any other column
    // would silently walk nothing.
    QModelIndex cursor = from.sibling(from.row(), 0);

    // const_cast: both are non-const only because they flush a posted layout.
    auto* mutableView = const_cast<QTreeView*>(view);
    while (true) {
        cursor = (delta < 0) ? mutableView->indexAbove(cursor)
                             : mutableView->indexBelow(cursor);
        if (!cursor.isValid())
            return {};                  // end of the list — MFC beeps here
        if (!accept || accept(cursor))
            return cursor;
    }
}

QModelIndex step(QTreeView* view, const QModelIndex& from, int delta,
                 const RowFilter& accept)
{
    const QModelIndex to = peekStep(view, from, delta, accept);
    if (!to.isValid())
        return {};

    auto* selection = view->selectionModel();
    if (!selection)
        return {};

    // ClearAndSelect|Rows == MFC's deselect-old + select-new-whole-row;
    // setCurrentIndex additionally reproduces LVIS_FOCUSED / SetSelectionMark.
    selection->setCurrentIndex(to, QItemSelectionModel::ClearAndSelect
                                       | QItemSelectionModel::Rows);
    view->scrollTo(to, QAbstractItemView::EnsureVisible);
    return to;
}

bool isTopLevel(const QModelIndex& index)
{
    return index.isValid() && !index.parent().isValid();
}

bool isChild(const QModelIndex& index)
{
    return index.isValid() && index.parent().isValid();
}

} // namespace eMule::ViewNav
