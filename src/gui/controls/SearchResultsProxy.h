#pragma once

/// @file SearchResultsProxy.h
/// @brief Sort proxy of a search result list that can hide rows by network.
///
/// A server mixes what it found on other networks into its eD2K answer. The
/// protocol has no per-search switch for that, so the rows are hidden here.
/// It also applies the filter box, and keeps a file's name rows and spam where
/// MFC's sort puts them.

#include <QSortFilterProxyModel>
#include <QStringList>

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

    /// The filter box: FilterEdit tokens matched against one column's text. Files
    /// only — a file that stays keeps all its name rows (MFC IsFilteredOut).
    void setTextFilter(const QStringList& tokens, int column);

    /// Source rows the filter keeps out of the list.
    [[nodiscard]] int hiddenCount() const;

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    NetworkFilter m_filter;
    QStringList m_tokens;
    int m_tokenColumn = 0;
};

} // namespace eMule
