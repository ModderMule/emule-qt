#include "pch.h"
/// @file CategoryFilterProxy.cpp
/// @brief Filters a download list down to one category — implementation.

#include "controls/CategoryFilterProxy.h"

#include "utils/OtherFunctions.h"

#include <QRegularExpression>

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

bool categoryShowsRow(const QList<DownloadCategory>& categories, int inCategory,
                      const CategoryRowFacts& row)
{
    using namespace CategoryViewFilter;

    // No list yet (or a tab past its end): membership alone
    if (inCategory < 0 || inCategory >= categories.size())
        return inCategory == 0 || row.category == inCategory;

    const DownloadCategory& cat = categories.at(inCategory);
    const int filter = cat.filter;
    if (row.category == inCategory && filter == All)
        return true;
    if (inCategory > 0 && row.category != inCategory && !cat.care4all)
        return false;

    bool shown = filter <= All;
    // The status modes say nothing about a finished file
    if (!shown && (filter < Waiting || filter > SeenComplete || row.unfinished)) {
        const auto typeIs = [&row](ED2KFileType type) {
            return getED2KFileTypeID(row.fileName) == type;
        };
        switch (filter) {
        case Uncategorized: shown = row.category == 0; break;
        case Incomplete:    shown = row.unfinished; break;
        case Completed:     shown = !row.unfinished; break;
        case Waiting:       shown = row.state == CategoryRowFacts::Waiting; break;
        case Downloading:   shown = row.state == CategoryRowFacts::Transferring; break;
        case Erroneous:     shown = row.state == CategoryRowFacts::Erroneous; break;
        case Paused:        shown = row.state == CategoryRowFacts::Paused; break;
        case SeenComplete:  shown = row.seenComplete; break;
        case Video:         shown = typeIs(ED2KFileType::Video); break;
        case Audio:         shown = typeIs(ED2KFileType::Audio); break;
        case Archive:       shown = typeIs(ED2KFileType::Archive); break;
        case CDImage:       shown = typeIs(ED2KFileType::CDImage); break;
        case Document:      shown = typeIs(ED2KFileType::Document); break;
        case Picture:       shown = typeIs(ED2KFileType::Image); break;
        case Program:       shown = typeIs(ED2KFileType::Program); break;
        case Collection:    shown = typeIs(ED2KFileType::EmuleCollection); break;
        case RegExp: {
            // The whole name, case as written (MFC RegularExpressionMatch: regex_match)
            const QRegularExpression re(QRegularExpression::anchoredPattern(cat.regexp));
            shown = re.isValid() && re.match(row.fileName).hasMatch();
            break;
        }
        default: break;
        }
    }
    return cat.filterNeg ? !shown : shown;
}

} // namespace eMule
