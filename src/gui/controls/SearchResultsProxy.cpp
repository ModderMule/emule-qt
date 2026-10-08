#include "pch.h"
/// @file SearchResultsProxy.cpp
/// @brief Sort proxy of a search result list that can hide rows by network.

#include "controls/SearchResultsProxy.h"
#include "controls/FilterEdit.h"
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

void SearchResultsProxy::setTextFilter(const QStringList& tokens, int column)
{
    if (tokens == m_tokens && column == m_tokenColumn)
        return;
    beginFilterChange();
    m_tokens = tokens;
    m_tokenColumn = column;
    endFilterChange(Direction::Rows);
}

int SearchResultsProxy::hiddenCount() const
{
    return sourceModel() ? sourceModel()->rowCount() - rowCount() : 0;
}

bool SearchResultsProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    // A name row goes wherever its file goes
    if (sourceParent.isValid())
        return true;

    if (!m_tokens.isEmpty()) {
        const QString target = sourceModel()->index(sourceRow, m_tokenColumn).data().toString();
        if (!FilterEdit::matches(m_tokens, target))
            return false;
    }

    // an indexer list has rows of one network only
    const auto* model = qobject_cast<const SearchResultsModel*>(sourceModel());
    const SearchResultRow* row = model ? model->resultAt(sourceRow) : nullptr;
    if (!row)
        return true;

    if (row->isUsenet())
        return m_filter.usenet;
    if (row->isTorrent())
        return m_filter.torrent;
    if (row->kadOrigin)
        return m_filter.kad;
    return true;
}

bool SearchResultsProxy::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    if (!qobject_cast<const SearchResultsModel*>(sourceModel()))
        return QSortFilterProxyModel::lessThan(left, right);

    // Name rows: by name or AICH when that is the sort column, otherwise the most
    // available first (MFC CompareChild, SearchListCtrl.cpp:604-618).
    if (left.parent().isValid()) {
        const int column = left.column();
        if (column == SearchResultsModel::ColFileName || column == SearchResultsModel::ColAichHash)
            return QSortFilterProxyModel::lessThan(left, right);
        const auto sources = [](const QModelIndex& idx) {
            return idx.siblingAtColumn(SearchResultsModel::ColAvailability)
                .data(Qt::UserRole).toLongLong();
        };
        return sources(left) > sources(right);
    }

    // Spam stays at the bottom in either direction (SearchListCtrl.cpp:622-629)
    const bool leftSpam = left.data(SearchResultsModel::SpamRole).toBool();
    const bool rightSpam = right.data(SearchResultsModel::SpamRole).toBool();
    if (leftSpam != rightSpam)
        return (sortOrder() == Qt::AscendingOrder) ? rightSpam : leftSpam;

    return QSortFilterProxyModel::lessThan(left, right);
}

} // namespace eMule
