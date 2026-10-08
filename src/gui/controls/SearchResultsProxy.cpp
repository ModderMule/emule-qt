#include "pch.h"
/// @file SearchResultsProxy.cpp
/// @brief Sort proxy of a search result list that can hide rows by network.

#include "controls/SearchResultsProxy.h"
#include "controls/SearchResultsModel.h"

namespace eMule {

SearchResultsProxy::SearchResultsProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

void SearchResultsProxy::setNetworkFilter(const NetworkFilter& filter)
{
    if (filter == m_filter)
        return;
    beginFilterChange();
    m_filter = filter;
    endFilterChange(Direction::Rows);
}

int SearchResultsProxy::hiddenCount() const
{
    return sourceModel() ? sourceModel()->rowCount() - rowCount() : 0;
}

bool SearchResultsProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    // an indexer list has rows of one network only
    const auto* model = qobject_cast<const SearchResultsModel*>(sourceModel());
    const SearchResultRow* row = model ? model->resultAt(sourceRow) : nullptr;
    if (!row)
        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);

    if (row->isUsenet())
        return m_filter.usenet;
    if (row->isTorrent())
        return m_filter.torrent;
    if (row->kadOrigin)
        return m_filter.kad;
    return true;
}

} // namespace eMule
