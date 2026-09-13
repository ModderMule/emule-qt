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

#include <QSortFilterProxyModel>
#include <Qt>

namespace eMule {

/// The category index of a top-level row, as an int.
///
/// One value shared by every model a CategoryFilterProxy can sit on, which is
/// the point: the proxy knows the role and nothing else about them. Kept clear
/// of DownloadListModel's PartMapRole/PausedRole (UserRole + 1 and + 2) and of
/// SharedFilesModel's SharePartMapRole.
inline constexpr int kCategoryRole = Qt::UserRole + 8;

class CategoryFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    /// 0 is "All" and accepts everything.
    void setCategoryFilter(int category);

    [[nodiscard]] int categoryFilter() const { return m_category; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    int m_category = 0;
};

} // namespace eMule
