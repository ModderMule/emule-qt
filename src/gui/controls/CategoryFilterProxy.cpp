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

void CategoryFilterProxy::setCategories(const QList<DownloadCategory>& categories)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_categories = categories;
    endFilterChange();
#else
    m_categories = categories;
    invalidateFilter();
#endif
}

int CategoryFilterProxy::rowsShownIn(int category,
                                     const std::function<bool(const CategoryRowFacts&)>& also) const
{
    const QAbstractItemModel* src = sourceModel();
    if (!src)
        return 0;
    int shown = 0;
    for (int row = 0; row < src->rowCount(); ++row) {
        const CategoryRowFacts facts = factsOf(row);
        if (categoryShowsRow(m_categories, category, facts) && (!also || also(facts)))
            ++shown;
    }
    return shown;
}

void CategoryFilterProxy::sort(int column, Qt::SortOrder order)
{
    // a sort proxy below owns the order; its lessThan would never run otherwise
    if (auto* below = qobject_cast<QSortFilterProxyModel*>(sourceModel())) {
        below->sort(column, order);
        return;
    }
    QSortFilterProxyModel::sort(column, order);
}

bool CategoryFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    // Child rows -- ED2K sources, the files inside an NZB -- are never filtered:
    // they belong to whichever parent survived.
    if (sourceParent.isValid())
        return true;
    if (!sourceModel())
        return true;
    return categoryShowsRow(m_categories, m_category, factsOf(sourceRow));
}

CategoryRowFacts CategoryFilterProxy::factsOf(int sourceRow) const
{
    // Whatever proxies sit in between forward the roles for us, so the arrangement
    // of the stack cannot change the answer.
    const QAbstractItemModel* src = sourceModel();
    const QModelIndex idx = src->index(sourceRow, 0);
    if (const QVariant facts = src->data(idx, kCategoryFactsRole); facts.canConvert<CategoryRowFacts>())
        return facts.value<CategoryRowFacts>();

    // A model that only names the category: an invalid variant reads 0 --
    // uncategorised, refused by every tab but "All". The safe direction.
    CategoryRowFacts facts;
    facts.category = src->data(idx, kCategoryRole).toInt();
    return facts;
}

} // namespace eMule
