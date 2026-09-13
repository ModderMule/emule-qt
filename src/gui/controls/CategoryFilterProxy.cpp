#include "pch.h"
/// @file CategoryFilterProxy.cpp
/// @brief Filters a download list down to one category — implementation.

#include "controls/CategoryFilterProxy.h"

namespace eMule {

void CategoryFilterProxy::setCategoryFilter(int category)
{
    if (m_category == category)
        return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_category = category;
    endFilterChange();
#else
    m_category = category;
    invalidateFilter();
#endif
}

bool CategoryFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    // Child rows -- ED2K sources, the files inside an NZB -- are never filtered:
    // they belong to whichever parent survived.
    if (sourceParent.isValid())
        return true;
    if (m_category == 0)
        return true;

    const QAbstractItemModel* src = sourceModel();
    if (!src)
        return true;

    // Whatever proxies sit in between forward this for us, so the arrangement of
    // the stack cannot change the answer. A model that does not answer the role
    // yields an invalid variant, and toInt() then gives 0 -- uncategorised, which
    // is refused by every tab but "All". That is the safe direction: a row is
    // hidden, not silently shown under a category it is not in.
    return src->data(src->index(sourceRow, 0, sourceParent), kCategoryRole).toInt()
           == m_category;
}

} // namespace eMule
