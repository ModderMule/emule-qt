#include "pch.h"
/// @file DownloadListModel.cpp
/// @brief Tree model for the downloads list — implementation.

#include "controls/DownloadListModel.h"

#include <QDateTime>
#include <QLocale>
#include <algorithm>
#include <climits>

namespace eMule {

namespace {

/// Format a byte count for display (B / KiB / MiB / GiB).
QString formatSize(int64_t bytes)
{
    if (bytes < 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GiB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

/// Format a speed value (bytes/sec) for display.
QString formatSpeed(int64_t bytesPerSec)
{
    if (bytesPerSec <= 0)
        return {};
    if (bytesPerSec < 1024)
        return QStringLiteral("%1 B/s").arg(bytesPerSec);
    return QStringLiteral("%1 KiB/s").arg(bytesPerSec / 1024.0, 0, 'f', 1);
}

/// Estimate remaining time from size and speed.
QString formatRemaining(int64_t remaining, int64_t speed)
{
    if (speed <= 0 || remaining <= 0)
        return {};
    const int64_t secs = remaining / speed;
    if (secs < 60)
        return QStringLiteral("%1s").arg(secs);
    if (secs < 3600)
        return QStringLiteral("%1m %2s").arg(secs / 60).arg(secs % 60);
    if (secs < 86400)
        return QStringLiteral("%1h %2m").arg(secs / 3600).arg((secs % 3600) / 60);
    return QStringLiteral("%1d %2h").arg(secs / 86400).arg((secs % 86400) / 3600);
}

/// Format a timestamp as date-time string, or "Never" if 0.
QString formatTimestamp(int64_t epoch)
{
    if (epoch <= 0)
        return QObject::tr("Never");
    return QDateTime::fromSecsSinceEpoch(epoch).toString(QStringLiteral("dd/MM/yyyy HH:mm:ss"));
}

/// Map ED2K file type codes to display names matching MFC.
QString fileTypeDisplay(const QString& type)
{
    if (type == QLatin1String("Arc"))      return QObject::tr("Archive");
    if (type == QLatin1String("Audio"))    return QObject::tr("Audio");
    if (type == QLatin1String("Video"))    return QObject::tr("Video");
    if (type == QLatin1String("Image"))    return QObject::tr("Image");
    if (type == QLatin1String("Pro"))      return QObject::tr("Program");
    if (type == QLatin1String("Doc"))      return QObject::tr("Document");
    if (type == QLatin1String("Iso"))      return QObject::tr("CD-Image");
    if (type == QLatin1String("EmuleCollection")) return QObject::tr("eMule Collection");
    if (!type.isEmpty())                   return type;
    return {};
}

/// Map SourceFrom enum to display string.
QString sourceFromDisplay(int sourceFrom)
{
    switch (sourceFrom) {
    case 0:  return QObject::tr("Local Server");
    case 1:  return QObject::tr("eD2K Server");
    case 2:  return QObject::tr("Kademlia");
    case 3:  return QObject::tr("Source Exchange");
    case 4:  return QObject::tr("Passive");
    case 5:  return QObject::tr("URL");
    case 6:  return QObject::tr("SLS");
    default: return QObject::tr("Unknown");
    }
}

/// Map download state string to sort priority (lower = more important).
int downloadStateSortOrder(const QString& state)
{
    if (state == QLatin1String("Downloading"))       return 0;
    if (state == QLatin1String("OnQueue"))            return 1;
    if (state == QLatin1String("Connected"))          return 2;
    if (state == QLatin1String("Connecting"))         return 3;
    if (state == QLatin1String("ReqHashSet"))         return 4;
    if (state == QLatin1String("WaitCallback"))       return 5;
    if (state == QLatin1String("WaitCallbackKad"))    return 6;
    if (state == QLatin1String("NoNeededParts"))      return 7;
    if (state == QLatin1String("RemoteQueueFull"))    return 8;
    if (state == QLatin1String("TooManyConns"))       return 9;
    if (state == QLatin1String("TooManyConnsKad"))    return 10;
    if (state == QLatin1String("LowToLowIp"))         return 11;
    if (state == QLatin1String("Banned"))             return 12;
    if (state == QLatin1String("Error"))              return 13;
    if (state == QLatin1String("None"))               return 14;
    return 15;
}

} // anonymous namespace

DownloadListModel::DownloadListModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

QModelIndex DownloadListModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    if (!parent.isValid()) {
        // Top-level (download) row — internalId = 0 (no parent row encoded)
        // We encode the parent row + 1 in internalId so that source rows
        // can find their parent. Top-level rows use internalId = 0.
        return createIndex(row, column, quintptr(0));
    }

    // Child (source) row — encode parent row + 1 in internalId
    if (parent.internalId() == 0) {
        // parent is a top-level row
        return createIndex(row, column, quintptr(parent.row() + 1));
    }

    // No deeper nesting
    return {};
}

QModelIndex DownloadListModel::parent(const QModelIndex& index) const
{
    if (!index.isValid())
        return {};

    const quintptr id = index.internalId();
    if (id == 0)
        return {}; // top-level row has no parent

    // Source row — parent is top-level row at (id - 1)
    const int parentRow = static_cast<int>(id - 1);
    return createIndex(parentRow, 0, quintptr(0));
}

int DownloadListModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return static_cast<int>(m_downloads.size());

    // Only top-level rows (downloads) can have children
    if (parent.internalId() == 0) {
        const int row = parent.row();
        if (row >= 0 && row < static_cast<int>(m_downloads.size()))
            return static_cast<int>(m_downloads[static_cast<size_t>(row)].sources.size());
    }

    return 0;
}

int DownloadListModel::columnCount(const QModelIndex& /*parent*/) const
{
    return ColCount;
}

bool DownloadListModel::hasChildren(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return !m_downloads.empty();

    // Top-level rows may have source children
    if (parent.internalId() == 0) {
        const int row = parent.row();
        if (row >= 0 && row < static_cast<int>(m_downloads.size())) {
            const auto& dl = m_downloads[static_cast<size_t>(row)];
            return !dl.sources.empty() || dl.sourceCount > 0;
        }
    }

    return false;
}

QVariant DownloadListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};

    const quintptr id = index.internalId();

    // --------------- Source (child) row ---------------
    if (id != 0) {
        const int parentRow = static_cast<int>(id - 1);
        if (parentRow < 0 || parentRow >= static_cast<int>(m_downloads.size()))
            return {};
        const auto& dl = m_downloads[static_cast<size_t>(parentRow)];
        if (index.row() < 0 || index.row() >= static_cast<int>(dl.sources.size()))
            return {};
        const auto& s = dl.sources[static_cast<size_t>(index.row())];

        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case ColFileName:      return s.userName;
            case ColSize:          return sourceFromDisplay(s.sourceFrom);
            case ColCompleted:     return s.sessionDown > 0 ? formatSize(s.sessionDown) : QString{};
            case ColSpeed:         return formatSpeed(s.datarate);
            case ColProgress:      return {};
            case ColSources:
                if (s.downloadState == QLatin1String("Downloading"))
                    return tr("Downloading");
                return s.remoteQueueRank > 0
                    ? QStringLiteral("QR: %1").arg(s.remoteQueueRank)
                    : QString{};
            case ColPriority:      return {};
            case ColStatus:        return s.downloadState;
            case ColRemaining:     return {};
            case ColSeenComplete:
                return (s.partCount > 0)
                    ? QStringLiteral("%1 / %2").arg(s.availPartCount).arg(s.partCount)
                    : QString{};
            case ColLastReception: return s.software;
            case ColCategory:      return {};
            case ColAddedOn:       return {};
            default: break;
            }
        }

        if (role == Qt::UserRole) {
            switch (index.column()) {
            case ColFileName:      return s.userName;
            case ColSize:          return s.sourceFrom;
            case ColCompleted:     return QVariant::fromValue(s.sessionDown);
            case ColSpeed:         return QVariant::fromValue(s.datarate);
            case ColProgress:      return 0.0;
            case ColSources: {
                if (s.downloadState == QLatin1String("Downloading"))
                    return -1;
                return s.remoteQueueRank > 0 ? static_cast<qlonglong>(s.remoteQueueRank) : qlonglong(INT_MAX);
            }
            case ColStatus:        return downloadStateSortOrder(s.downloadState);
            case ColSeenComplete:  return s.availPartCount;
            case ColLastReception: return s.software;
            default:               return {};
            }
        }

        if (role == PartMapRole && index.column() == ColProgress)
            return s.partMap;

        if (role == PausedRole && index.column() == ColProgress)
            return false;

        return {};
    }

    // --------------- Download (top-level) row ---------------
    if (index.row() >= static_cast<int>(m_downloads.size()))
        return {};

    const auto& d = m_downloads[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColFileName:   return d.fileName;
        case ColSize:       return formatSize(d.fileSize);
        case ColCompleted:  return formatSize(d.completedSize);
        case ColSpeed:      return formatSpeed(d.datarate);
        case ColProgress:   return QStringLiteral("%1%").arg(d.percentCompleted, 0, 'f', 1);
        case ColSources:
            return QStringLiteral("%1 / %2").arg(d.transferringSrcCount).arg(d.sourceCount);
        case ColPriority: {
            if (d.isAutoDownPriority)
                return tr("Auto [%1]").arg(d.priority);
            return d.priority;
        }
        case ColStatus:     return d.status;
        case ColRemaining:
            return formatRemaining(d.fileSize - d.completedSize, d.datarate);
        case ColSeenComplete:
            return formatTimestamp(d.lastSeenComplete);
        case ColLastReception:
            return formatTimestamp(d.lastReception);
        case ColCategory:
            return d.category > 0 ? QString::number(d.category) : QString{};
        case ColAddedOn:
            return formatTimestamp(d.addedOn);
        default: break;
        }
    }

    if (role == Qt::ToolTipRole) {
        return tr(
            "File Name:\t%1\n"
            "ED2K Hash:\t%2\n"
            "Type:\t%3\n"
            "Status:\t%4\n"
            "Priority:\t%5\n"
            "Requests:\t%6\n"
            "Accepted Requests:\t%7\n"
            "Transferred Data:\t%8")
            .arg(d.fileName, d.hash, fileTypeDisplay(d.fileType),
                 d.status, d.priority)
            .arg(d.requests).arg(d.acceptedRequests)
            .arg(formatSize(d.transferredData));
    }

    // Raw data for sorting
    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColFileName:   return d.fileName;
        case ColSize:       return QVariant::fromValue(d.fileSize);
        case ColCompleted:  return QVariant::fromValue(d.completedSize);
        case ColSpeed:      return QVariant::fromValue(d.datarate);
        case ColProgress:   return d.percentCompleted;
        case ColSources:    return d.sourceCount;
        case ColPriority:   return d.priority;
        case ColStatus:     return d.status;
        case ColRemaining: {
            if (d.datarate > 0)
                return QVariant::fromValue((d.fileSize - d.completedSize) / d.datarate);
            return QVariant::fromValue(int64_t{-1});
        }
        case ColSeenComplete: return QVariant::fromValue(d.lastSeenComplete);
        case ColLastReception: return QVariant::fromValue(d.lastReception);
        case ColCategory:   return QVariant::fromValue(d.category);
        case ColAddedOn:    return QVariant::fromValue(d.addedOn);
        default: break;
        }
    }

    if (role == PartMapRole && index.column() == ColProgress)
        return d.partMap;

    if (role == PausedRole && index.column() == ColProgress)
        return d.isPaused || d.isStopped;

    return {};
}

QVariant DownloadListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColFileName:   return tr("File Name");
    case ColSize:       return tr("Size");
    case ColCompleted:  return tr("Completed");
    case ColSpeed:      return tr("Speed");
    case ColProgress:   return tr("Progress");
    case ColSources:    return tr("Sources");
    case ColPriority:   return tr("Priority");
    case ColStatus:     return tr("Status");
    case ColRemaining:      return tr("Remaining");
    case ColSeenComplete:   return tr("Seen Complete");
    case ColLastReception:  return tr("Last reception");
    case ColCategory:       return tr("Category");
    case ColAddedOn:        return tr("Added On");
    default:                return {};
    }
}

void DownloadListModel::setDownloads(std::vector<DownloadRow> downloads)
{
    // Preserve existing sources for downloads that still exist
    // Build a map from hash → sources
    std::unordered_map<std::string, std::vector<SourceRow>> savedSources;
    for (auto& dl : m_downloads) {
        if (!dl.sources.empty())
            savedSources[dl.hash.toStdString()] = std::move(dl.sources);
    }

    beginResetModel();
    m_downloads = std::move(downloads);

    // Restore preserved sources
    for (auto& dl : m_downloads) {
        auto it = savedSources.find(dl.hash.toStdString());
        if (it != savedSources.end())
            dl.sources = std::move(it->second);
    }
    endResetModel();
}

void DownloadListModel::setSources(const QString& hash, std::vector<SourceRow> sources)
{
    for (int i = 0; i < static_cast<int>(m_downloads.size()); ++i) {
        if (m_downloads[static_cast<size_t>(i)].hash == hash) {
            auto& dl = m_downloads[static_cast<size_t>(i)];
            const QModelIndex parentIdx = index(i, 0);
            const int oldCount = static_cast<int>(dl.sources.size());
            const int newCount = static_cast<int>(sources.size());

            if (oldCount > 0) {
                beginRemoveRows(parentIdx, 0, oldCount - 1);
                dl.sources.clear();
                endRemoveRows();
            }
            if (newCount > 0) {
                beginInsertRows(parentIdx, 0, newCount - 1);
                dl.sources = std::move(sources);
                endInsertRows();
            }
            return;
        }
    }
}

void DownloadListModel::clear()
{
    beginResetModel();
    m_downloads.clear();
    endResetModel();
}

QString DownloadListModel::hashAt(int row) const
{
    if (row >= 0 && row < static_cast<int>(m_downloads.size()))
        return m_downloads[static_cast<size_t>(row)].hash;
    return {};
}

const DownloadRow* DownloadListModel::downloadAt(int row) const
{
    if (row >= 0 && row < static_cast<int>(m_downloads.size()))
        return &m_downloads[static_cast<size_t>(row)];
    return nullptr;
}

bool DownloadListModel::isSourceRow(const QModelIndex& index) const
{
    return index.isValid() && index.internalId() != 0;
}

const SourceRow* DownloadListModel::sourceAt(const QModelIndex& index) const
{
    if (!isSourceRow(index))
        return nullptr;
    const auto parentRow = static_cast<int>(index.internalId() - 1);
    if (parentRow < 0 || parentRow >= static_cast<int>(m_downloads.size()))
        return nullptr;
    const auto& srcs = m_downloads[static_cast<size_t>(parentRow)].sources;
    if (index.row() < 0 || index.row() >= static_cast<int>(srcs.size()))
        return nullptr;
    return &srcs[static_cast<size_t>(index.row())];
}

bool DownloadListModel::containsHash(const QString& hexHash) const
{
    return std::any_of(m_downloads.begin(), m_downloads.end(),
        [&](const DownloadRow& r) { return r.hash == hexHash; });
}

} // namespace eMule
