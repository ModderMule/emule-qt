#pragma once

/// @file SearchResultsProxy.h
/// @brief Sort proxy of a search result list that can hide rows by network.
///
/// A server mixes what it found on other networks into its eD2K answer. The
/// protocol has no per-search switch for that, so the rows are hidden here.

#include <QSortFilterProxyModel>

namespace eMule {

class SearchResultsProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    /// Which of the server's foreign rows are shown. Its own eD2K files always are.
    struct NetworkFilter {
        bool usenet = true;
        bool kad = true;      ///< found on Kad by the server; never our own Kad results
        bool torrent = true;

        bool operator==(const NetworkFilter&) const = default;
    };

    explicit SearchResultsProxy(QObject* parent = nullptr);

    void setNetworkFilter(const NetworkFilter& filter);
    [[nodiscard]] const NetworkFilter& networkFilter() const { return m_filter; }

    /// Source rows the filter keeps out of the list.
    [[nodiscard]] int hiddenCount() const;

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    NetworkFilter m_filter;
};

} // namespace eMule
