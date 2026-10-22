#include "pch.h"
/// @file TorrentFilterProxy.cpp
/// @brief Sort proxy of the Torrents list with a state filter — implementation.

#include "controls/TorrentFilterProxy.h"

#include "controls/TorrentListModel.h"

namespace eMule {

TorrentFilterProxy::TorrentFilterProxy(QObject* parent)
    : SortValueProxy(parent)
{
}

void TorrentFilterProxy::setSourceModel(QAbstractItemModel* model)
{
    m_torrents = qobject_cast<TorrentListModel*>(model);
    SortValueProxy::setSourceModel(model);
}

void TorrentFilterProxy::setFilter(Filter filter)
{
    if (filter == m_filter)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_filter = filter;
    endFilterChange();
#else
    m_filter = filter;
    invalidateFilter();
#endif
}

bool TorrentFilterProxy::matches(const TorrentRow& row, Filter filter)
{
    switch (filter) {
    case All:
        return true;
    case Downloading:
        switch (row.state) {
        case TorrentState::Downloading:
        case TorrentState::DownloadingMetadata:
        case TorrentState::CheckingFiles:
        case TorrentState::CheckingResume:
            return true;
        case TorrentState::Queued:
            return !row.isComplete() && (row.size == 0 || row.done < row.size);
        default:
            return false;
        }
    case Seeding:
        return row.isComplete() && !row.paused;
    case Paused:
        return row.paused;
    case Active:
        return row.rateDown > 0 || row.rateUp > 0;
    case Error:
        return row.state == TorrentState::Error || !row.error.isEmpty();
    case FilterCount:
        break;
    }
    return false;
}

QString TorrentFilterProxy::filterName(Filter filter)
{
    switch (filter) {
    case All:         return tr("All");
    case Downloading: return tr("Downloading");
    case Seeding:     return tr("Seeding");
    case Paused:      return tr("Paused");
    case Active:      return tr("Active");
    case Error:       return tr("Error");
    case FilterCount: break;
    }
    return {};
}

bool TorrentFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (m_filter == All || !m_torrents || sourceParent.isValid())
        return true;
    const TorrentRow* row = m_torrents->rowAt(sourceRow);
    return row && matches(*row, m_filter);
}

} // namespace eMule
