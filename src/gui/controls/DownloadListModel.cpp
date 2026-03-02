/// @file DownloadListModel.cpp
/// @brief Table model for the downloads list — implementation.

#include "controls/DownloadListModel.h"

#include <QDateTime>
#include <QLocale>

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

} // anonymous namespace

DownloadListModel::DownloadListModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int DownloadListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_downloads.size());
}

int DownloadListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant DownloadListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_downloads.size()))
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
    beginResetModel();
    m_downloads = std::move(downloads);
    endResetModel();
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

} // namespace eMule
