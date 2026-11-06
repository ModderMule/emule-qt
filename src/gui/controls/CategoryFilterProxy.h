#pragma once

/// @file CategoryFilterProxy.h
/// @brief Filters a download list down to one category.
///
/// Shared by the Transfers tab and the Usenet tab. It was file-local to
/// TransferPanel until the Usenet queue grew categories too, and the move fixed
/// what its own comments recorded: it reached the category by casting
/// `sourceModel()` to `DownloadListModel*` through a second proxy, and when that
/// cast failed the fallback accepted every row -- so the tab bar looked like it
/// worked and filtered nothing.
///
/// Asking the *row* instead of the model removes both the cast and the hop:
/// QSortFilterProxyModel forwards data() down whatever stack it is sitting on,
/// so one role lookup works however the proxies are arranged.

#include "prefs/CategoryView.h"
#include "prefs/DownloadCategory.h"

#include <QList>
#include <QMetaType>
#include <QSortFilterProxyModel>
#include <QString>
#include <Qt>

#include <array>
#include <functional>

namespace eMule {

/// The category index of a top-level row, as an int.
///
/// One value shared by every model a CategoryFilterProxy can sit on, which is
/// the point: the proxy knows the role and nothing else about them. Kept clear
/// of DownloadListModel's PartMapRole/PausedRole (UserRole + 1 and + 2) and of
/// SharedFilesModel's SharePartMapRole.
inline constexpr int kCategoryRole = Qt::UserRole + 8;

/// A model answers CategoryRowFacts under this role; one that does not is filtered by
/// its category alone.
inline constexpr int kCategoryFactsRole = Qt::UserRole + 9;

class CategoryFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    /// The Transfers tab's status filter row, on top of the category. Persisted in UiState: append only.
    enum StatusFilter : int {
        StatusAll = 0,
        StatusDownloading,
        StatusWaiting,
        StatusPaused,
        StatusComplete,
        StatusError,
        StatusFilterCount
    };

    /// 0 is "All" and accepts everything.
    void setCategoryFilter(int category);

    [[nodiscard]] int categoryFilter() const { return m_category; }

    /// The category list, for each category's view filter. Without it a tab shows
    /// exactly the rows filed in its category.
    void setCategories(const QList<DownloadCategory>& categories);

    /// Rows the tab of @p category shows, whatever tab is selected.
    [[nodiscard]] int rowsShownIn(int category,
                                  const std::function<bool(const CategoryRowFacts&)>& also = {}) const;

    /// Status filter of the filter row; StatusAll shows every row of the category.
    void setStatusFilter(int filter);
    [[nodiscard]] int statusFilter() const { return m_status; }
    /// Rows of the selected category per status filter, for the filter row's counts.
    [[nodiscard]] std::array<int, StatusFilterCount> statusCounts() const;
    [[nodiscard]] static bool matchesStatus(const CategoryRowFacts& row, int filter);
    /// Localized button text, without the count.
    [[nodiscard]] static QString statusFilterName(int filter);

    /// Hands the sort to a sort proxy below, so that proxy's lessThan() decides.
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    [[nodiscard]] CategoryRowFacts factsOf(int sourceRow) const;

    int m_category = 0;
    int m_status = StatusAll;
    QList<DownloadCategory> m_categories;
};

} // namespace eMule

Q_DECLARE_METATYPE(eMule::CategoryRowFacts)
