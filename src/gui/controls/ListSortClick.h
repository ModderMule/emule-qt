#pragma once

/// @file ListSortClick.h
/// @brief What a click on a list header does to the sort — MFC's OnLvnColumnClick rule.
///
/// Qt sorts a newly clicked column ascending and toggles on every further click. MFC
/// starts some columns descending (the numeric ones, where the big values are the
/// interesting ones), and a few columns have a second value to sort by: clicking
/// through both directions switches between the two ("4-way sorting").

#include <Qt>

namespace eMule {

struct SortClickResult {
    Qt::SortOrder order = Qt::AscendingOrder;
    bool switchValue = false;   ///< a two-value column moves to its other value
};

/// @param sameColumn the clicked column was the sort column already
/// @param toggled    the order Qt chose for a click on the same column
[[nodiscard]] constexpr SortClickResult resolveSortClick(bool sameColumn, Qt::SortOrder toggled,
                                                         bool descendingFirst)
{
    const Qt::SortOrder initial = descendingFirst ? Qt::DescendingOrder : Qt::AscendingOrder;
    if (!sameColumn)
        return {initial, false};   // the column keeps the value it was last sorted by
    // Back at the first direction: both directions of this value were seen
    return {toggled, toggled == initial};
}

} // namespace eMule
