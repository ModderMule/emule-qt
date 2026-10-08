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

#include "prefs/DownloadCategory.h"

#include <QList>
#include <QMetaType>
#include <QSortFilterProxyModel>
#include <QString>
#include <Qt>

#include <functional>

namespace eMule {

/// The category index of a top-level row, as an int.
///
/// One value shared by every model a CategoryFilterProxy can sit on, which is
/// the point: the proxy knows the role and nothing else about them. Kept clear
/// of DownloadListModel's PartMapRole/PausedRole (UserRole + 1 and + 2) and of
/// SharedFilesModel's SharePartMapRole.
inline constexpr int kCategoryRole = Qt::UserRole + 8;

/// What a category's view filter needs to know about a top-level row
/// (kCategoryFactsRole). A model that does not answer it is filtered by its
/// category alone.
struct CategoryRowFacts {
    enum State { Other, Waiting, Transferring, Erroneous, Paused };

    int category = 0;
    QString fileName;
    bool unfinished = true;      ///< MFC IsPartFile: not completed yet
    State state = Other;
    bool seenComplete = false;   ///< every part had a source at some time
};

inline constexpr int kCategoryFactsRole = Qt::UserRole + 9;

/// MFC's view filter modes (srchybrid/TransferWnd.cpp:706-748).
namespace CategoryViewFilter {
enum : int {
    All = 0, Uncategorized = 1, Incomplete = 2, Completed = 3, Waiting = 4, Downloading = 5,
    Erroneous = 6, Paused = 7, SeenComplete = 8,
    Video = 10, Audio = 11, Archive = 12, CDImage = 13, Document = 14, Picture = 15,
    Program = 16, RegExp = 18, Collection = 20
};
}

/// Whether a row belongs on the tab of category @p inCategory — port of MFC
/// CPartFile::CheckShowItemInGivenCat (srchybrid/PartFile.cpp:5055-5122).
[[nodiscard]] bool categoryShowsRow(const QList<DownloadCategory>& categories, int inCategory,
                                    const CategoryRowFacts& row);

class CategoryFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    /// 0 is "All" and accepts everything.
    void setCategoryFilter(int category);

    [[nodiscard]] int categoryFilter() const { return m_category; }

    /// The category list, for each category's view filter. Without it a tab shows
    /// exactly the rows filed in its category.
    void setCategories(const QList<DownloadCategory>& categories);

    /// Rows the tab of @p category shows, whatever tab is selected.
    [[nodiscard]] int rowsShownIn(int category,
                                  const std::function<bool(const CategoryRowFacts&)>& also = {}) const;

    /// Hands the sort to a sort proxy below, so that proxy's lessThan() decides.
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    [[nodiscard]] CategoryRowFacts factsOf(int sourceRow) const;

    int m_category = 0;
    QList<DownloadCategory> m_categories;
};

} // namespace eMule

Q_DECLARE_METATYPE(eMule::CategoryRowFacts)
