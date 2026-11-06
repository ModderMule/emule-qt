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

void CategoryFilterProxy::setStatusFilter(int filter)
{
    if (m_status == filter)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_status = filter;
    endFilterChange();
#else
    m_status = filter;
    invalidateFilter();
#endif
}

std::array<int, CategoryFilterProxy::StatusFilterCount> CategoryFilterProxy::statusCounts() const
{
    std::array<int, StatusFilterCount> counts{};
    const QAbstractItemModel* src = sourceModel();
    if (!src)
        return counts;
    for (int row = 0; row < src->rowCount(); ++row) {
        const CategoryRowFacts facts = factsOf(row);
        if (!categoryShowsRow(m_categories, m_category, facts))
            continue;
        for (int f = 0; f < StatusFilterCount; ++f)
            if (matchesStatus(facts, f))
                ++counts[std::size_t(f)];
    }
    return counts;
}

bool CategoryFilterProxy::matchesStatus(const CategoryRowFacts& row, int filter)
{
    switch (filter) {
    case StatusAll:         return true;
    case StatusDownloading: return row.unfinished && row.state == CategoryRowFacts::Transferring;
    case StatusWaiting:     return row.unfinished && row.state == CategoryRowFacts::Waiting;
    case StatusPaused:      return row.unfinished && row.state == CategoryRowFacts::Paused;
    case StatusComplete:    return !row.unfinished;
    case StatusError:       return row.state == CategoryRowFacts::Erroneous;
    default:                return false;
    }
}

QString CategoryFilterProxy::statusFilterName(int filter)
{
    switch (filter) {
    case StatusAll:         return tr("All");
    case StatusDownloading: return tr("Downloading");
    case StatusWaiting:     return tr("Waiting");
    case StatusPaused:      return tr("Paused");
    case StatusComplete:    return tr("Complete");
    case StatusError:       return tr("Error");
    default:                return {};
    }
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
    const CategoryRowFacts facts = factsOf(sourceRow);
    return categoryShowsRow(m_categories, m_category, facts) && matchesStatus(facts, m_status);
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
