#include "pch.h"
/// @file DownloadListModel.cpp
/// @brief Tree model for the downloads list — implementation.

#include "controls/DownloadListModel.h"

#include "client/ClientStateDefs.h"

#include "utils/OtherFunctions.h"

#include <QColor>
#include <QDateTime>

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
        return QStringLiteral("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

/// Format a speed value (bytes/sec) for display.
QString formatSpeed(int64_t bytesPerSec)
{
    if (bytesPerSec <= 0)
        return {};
    if (bytesPerSec < 1024)
        return QStringLiteral("%1 B/s").arg(bytesPerSec);
    return QStringLiteral("%1 KiB/s").arg(static_cast<double>(bytesPerSec) / 1024.0, 0, 'f', 1);
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
///
/// The daemon sends the raw core value — CborSerializers.h pushes
/// static_cast<int>(client->sourceFrom()) — so these cases must track SourceFrom
/// (ClientStateDefs.h) exactly. This table used to start at "Local Server" and was shifted
/// one position off the enum, mislabelling every source in the panel.
QString sourceFromDisplay(int sourceFrom)
{
    switch (sourceFrom) {
    case 0:  return QObject::tr("eD2K Server");     // SourceFrom::Server
    case 1:  return QObject::tr("Kademlia");        // SourceFrom::Kademlia
    case 2:  return QObject::tr("Source Exchange"); // SourceFrom::SourceExchange
    case 3:  return QObject::tr("Passive");         // SourceFrom::Passive
    case 4:  return QObject::tr("Link");            // SourceFrom::Link
    case 7:  return QObject::tr("SLS");             // SourceFrom::SLS (saved source list)
    case 8:  return QObject::tr("HTTP Cache");      // SourceFrom::HttpCache
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
        if (row >= 0 && row < static_cast<int>(m_downloads.size())) {
            const auto& dl = m_downloads[static_cast<size_t>(row)];
            // Completed downloads have no live sources — not expandable
            if (dl.isComplete())
                return 0;
            return static_cast<int>(dl.sources.size());
        }
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
            // Completed downloads have no live sources — never expandable
            if (dl.isComplete())
                return false;
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

        // A source fetching over HTTP Cache is not costing the uploader anything,
        // which is worth seeing at a glance — MFC gave PeerCache its own bar for
        // the same reason. Teal, distinct from the blue used for the connected
        // server and from the plain text of ordinary ed2k sources.
        if (role == Qt::ForegroundRole
            && s.sourceFrom == static_cast<int>(SourceFrom::HttpCache))
            return QColor(0x00, 0x99, 0x99);

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
            "Size:\t%3\n"
            "Completed:\t%4 (%5%)\n"
            "Type:\t%6\n"
            "Status:\t%7\n"
            "Priority:\t%8\n"
            "Sources:\t%9\n"
            "Requests:\t%10\n"
            "Accepted Requests:\t%11\n"
            "Transferred Data:\t%12")
            .arg(d.fileName, d.hash,
                 formatSize(d.fileSize),
                 formatSize(d.completedSize),
                 QString::number(d.percentCompleted, 'f', 1))
            .arg(fileTypeDisplay(d.fileType),
                 d.status, d.priority,
                 QStringLiteral("%1 / %2").arg(d.transferringSrcCount).arg(d.sourceCount))
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

void DownloadListModel::setDownloads(std::vector<DownloadRow> incoming)
{
    // Incremental update: avoids beginResetModel()/endResetModel() so that
    // the view's selection, expansion, and scroll state are preserved.

    // 1. Build lookup of incoming hashes
    QHash<QString, size_t> incomingByHash;
    incomingByHash.reserve(static_cast<qsizetype>(incoming.size()));
    for (size_t i = 0; i < incoming.size(); ++i)
        incomingByHash.insert(incoming[i].hash, i);

    // 2. Remove departed downloads (reverse order keeps indices stable for
    //    rows below the removal point, preserving internalId encoding)
    for (int i = static_cast<int>(m_downloads.size()) - 1; i >= 0; --i) {
        if (!incomingByHash.contains(m_downloads[static_cast<size_t>(i)].hash)) {
            beginRemoveRows({}, i, i);
            m_downloads.erase(m_downloads.begin() + i);
            endRemoveRows();
        }
    }

    // 3. Update surviving rows in-place (preserve existing sources)
    QSet<QString> existingHashes;
    existingHashes.reserve(static_cast<qsizetype>(m_downloads.size()));
    for (size_t i = 0; i < m_downloads.size(); ++i) {
        auto& existing = m_downloads[i];
        existingHashes.insert(existing.hash);
        if (auto it = incomingByHash.constFind(existing.hash); it != incomingByHash.cend()) {
            auto& fresh = incoming[it.value()];
            if (fresh.isComplete()) {
                // Download just completed (or is complete): drop its source children
                // so the view collapses cleanly and the expand arrow disappears.
                if (!existing.sources.empty()) {
                    beginRemoveRows(index(static_cast<int>(i), 0), 0,
                                    static_cast<int>(existing.sources.size()) - 1);
                    existing.sources.clear();
                    endRemoveRows();
                }
                // fresh.sources is already empty from the daemon snapshot
            } else {
                fresh.sources = std::move(existing.sources); // preserve child rows
            }
            existing = std::move(fresh);
        }
    }
    if (!m_downloads.empty())
        emit dataChanged(index(0, 0), index(static_cast<int>(m_downloads.size()) - 1, ColCount - 1));

    // 4. Append new downloads in one batch
    std::vector<DownloadRow> toInsert;
    for (auto& row : incoming) {
        if (!row.hash.isEmpty() && !existingHashes.contains(row.hash))
            toInsert.push_back(std::move(row));
    }
    if (!toInsert.empty()) {
        const int first = static_cast<int>(m_downloads.size());
        const int last  = first + static_cast<int>(toInsert.size()) - 1;
        beginInsertRows({}, first, last);
        for (auto& r : toInsert)
            m_downloads.push_back(std::move(r));
        endInsertRows();
    }
}

void DownloadListModel::setSources(const QString& hash, std::vector<SourceRow> incoming)
{
    for (int i = 0; i < static_cast<int>(m_downloads.size()); ++i) {
        if (m_downloads[static_cast<size_t>(i)].hash != hash)
            continue;

        auto& dl = m_downloads[static_cast<size_t>(i)];

        // Completed downloads have no live sources and report 0 child rows;
        // never insert children under them (would desync the view's row count).
        if (dl.isComplete())
            return;

        const QModelIndex parentIdx = index(i, 0);

        // Incremental update keyed by userHash — preserves selection & scroll.

        // 1. Build lookup of incoming sources by userHash
        QHash<QString, size_t> incomingByHash;
        incomingByHash.reserve(static_cast<qsizetype>(incoming.size()));
        for (size_t j = 0; j < incoming.size(); ++j)
            incomingByHash.insert(incoming[j].userHash, j);

        // 2. Remove departed sources (reverse order keeps indices stable)
        for (int j = static_cast<int>(dl.sources.size()) - 1; j >= 0; --j) {
            if (!incomingByHash.contains(dl.sources[static_cast<size_t>(j)].userHash)) {
                beginRemoveRows(parentIdx, j, j);
                dl.sources.erase(dl.sources.begin() + j);
                endRemoveRows();
            }
        }

        // 3. Update surviving sources in-place
        QSet<QString> existingHashes;
        existingHashes.reserve(static_cast<qsizetype>(dl.sources.size()));
        for (size_t j = 0; j < dl.sources.size(); ++j) {
            existingHashes.insert(dl.sources[j].userHash);
            if (auto it = incomingByHash.constFind(dl.sources[j].userHash); it != incomingByHash.cend())
                dl.sources[j] = std::move(incoming[it.value()]);
        }
        if (!dl.sources.empty())
            emit dataChanged(index(0, 0, parentIdx),
                             index(static_cast<int>(dl.sources.size()) - 1, ColCount - 1, parentIdx));

        // 4. Append new sources in one batch
        std::vector<SourceRow> toInsert;
        for (auto& src : incoming) {
            if (!src.userHash.isEmpty() && !existingHashes.contains(src.userHash))
                toInsert.push_back(std::move(src));
        }
        if (!toInsert.empty()) {
            const int first = static_cast<int>(dl.sources.size());
            const int last  = first + static_cast<int>(toInsert.size()) - 1;
            beginInsertRows(parentIdx, first, last);
            for (auto& s : toInsert)
                dl.sources.push_back(std::move(s));
            endInsertRows();
        }
        return;
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
        [&](const DownloadRow& r) { return sameHash(r.hash, hexHash); });
}

const DownloadRow* DownloadListModel::findByHash(const QString& hexHash) const
{
    auto it = std::find_if(m_downloads.begin(), m_downloads.end(),
        [&](const DownloadRow& r) { return sameHash(r.hash, hexHash); });
    return (it != m_downloads.end()) ? &(*it) : nullptr;
}

} // namespace eMule
