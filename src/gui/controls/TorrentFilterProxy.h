#pragma once

/// @file TorrentFilterProxy.h
/// @brief Sort proxy of the Torrents list with Transmission's state filter.

#include "controls/SortableItems.h"

namespace eMule {

class TorrentListModel;
struct TorrentRow;

class TorrentFilterProxy : public SortValueProxy {
    Q_OBJECT

public:
    /// Persisted in UiState: append only.
    enum Filter : int {
        All = 0,
        Downloading,
        Seeding,
        Paused,
        Active,
        Error,
        FilterCount
    };

    explicit TorrentFilterProxy(QObject* parent = nullptr);

    void setSourceModel(QAbstractItemModel* model) override;

    [[nodiscard]] Filter filter() const { return m_filter; }
    void setFilter(Filter filter);

    /// Whether @p row belongs to @p filter. Shared with the filter bar's counts.
    [[nodiscard]] static bool matches(const TorrentRow& row, Filter filter);

    /// Localized button text, without the count.
    [[nodiscard]] static QString filterName(Filter filter);

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    TorrentListModel* m_torrents = nullptr;
    Filter m_filter = All;
};

} // namespace eMule
