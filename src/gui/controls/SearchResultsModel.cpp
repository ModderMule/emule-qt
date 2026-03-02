/// @file SearchResultsModel.cpp
/// @brief Table model for search results — implementation.

#include "controls/SearchResultsModel.h"

#include <QColor>
#include <QHash>
#include <QIcon>

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

/// Format media length in seconds to mm:ss or hh:mm:ss.
QString formatLength(int64_t seconds)
{
    if (seconds <= 0)
        return {};
    if (seconds < 3600)
        return QStringLiteral("%1:%2")
            .arg(seconds / 60)
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600)
        .arg((seconds % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

/// Format bitrate in kbps.
QString formatBitrate(int64_t bitrate)
{
    if (bitrate <= 0)
        return {};
    return QStringLiteral("%1 kbps").arg(bitrate);
}

/// Known type display string matching MFC.
QString knownTypeString(int knownType)
{
    switch (knownType) {
    case 1:  return QObject::tr("Shared");
    case 2:  return QObject::tr("Downloading");
    case 3:  return QObject::tr("Downloaded");
    case 4:  return QObject::tr("Cancelled");
    default: return {};
    }
}

/// Map a file type string to its icon, with caching.
QIcon fileTypeIcon(const QString& type)
{
    static QHash<QString, QIcon> cache;
    auto it = cache.find(type);
    if (it != cache.end())
        return *it;

    QString path;
    if (type == u"Audio")
        path = QStringLiteral(":/icons/FileTypeAudio.ico");
    else if (type == u"Video")
        path = QStringLiteral(":/icons/FileTypeVideo.ico");
    else if (type == u"Image")
        path = QStringLiteral(":/icons/FileTypePicture.ico");
    else if (type == u"Doc")
        path = QStringLiteral(":/icons/FileTypeDocument.ico");
    else if (type == u"Pro")
        path = QStringLiteral(":/icons/FileTypeProgram.ico");
    else if (type == u"Arc")
        path = QStringLiteral(":/icons/FileTypeArchive.ico");
    else if (type == u"Iso")
        path = QStringLiteral(":/icons/FileTypeCDImage.ico");
    else if (type == u"EmuleCollection")
        path = QStringLiteral(":/icons/emuleCollectionFileType.ico");
    else
        path = QStringLiteral(":/icons/FileTypeAny.ico");

    QIcon icon(path);
    cache.insert(type, icon);
    return icon;
}

} // anonymous namespace

SearchResultsModel::SearchResultsModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int SearchResultsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_results.size());
}

int SearchResultsModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant SearchResultsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_results.size()))
        return {};

    const auto& r = m_results[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColFileName:     return r.fileName;
        case ColSize:         return formatSize(r.fileSize);
        case ColAvailability: return r.sourceCount > 0 ? QString::number(r.sourceCount) : QString{};
        case ColComplete:
            return r.completeSourceCount > 0 ? QString::number(r.completeSourceCount) : QString{};
        case ColType:         return r.fileType;
        case ColArtist:       return r.artist;
        case ColAlbum:        return r.album;
        case ColTitle:        return r.title;
        case ColLength:       return formatLength(r.length);
        case ColBitrate:      return formatBitrate(r.bitrate);
        case ColCodec:        return r.codec;
        case ColKnown:        return knownTypeString(r.knownType);
        default: break;
        }
    }

    if (role == Qt::DecorationRole && index.column() == ColFileName) {
        return fileTypeIcon(r.fileType);
    }

    // Raw values for sorting
    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColFileName:     return r.fileName;
        case ColSize:         return QVariant::fromValue(r.fileSize);
        case ColAvailability: return QVariant::fromValue(r.sourceCount);
        case ColComplete:     return QVariant::fromValue(r.completeSourceCount);
        case ColType:         return r.fileType;
        case ColArtist:       return r.artist;
        case ColAlbum:        return r.album;
        case ColTitle:        return r.title;
        case ColLength:       return QVariant::fromValue(r.length);
        case ColBitrate:      return QVariant::fromValue(r.bitrate);
        case ColCodec:        return r.codec;
        case ColKnown:        return r.knownType;
        default: break;
        }
    }

    // Color coding: red for spam, blue for downloading, gray for shared/downloaded
    if (role == Qt::ForegroundRole) {
        if (r.isSpam)
            return QColor(0xCC, 0x00, 0x00); // red
        switch (r.knownType) {
        case 1: return QColor(0x80, 0x80, 0x80); // Shared — gray
        case 2: return QColor(0x00, 0x66, 0xCC); // Downloading — blue
        case 3: return QColor(0x00, 0x88, 0x00); // Downloaded — green
        case 4: return QColor(0x80, 0x80, 0x80); // Cancelled — gray
        default: break;
        }
    }

    return {};
}

QVariant SearchResultsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColFileName:     return tr("File Name");
    case ColSize:         return tr("Size");
    case ColAvailability: return tr("Availability");
    case ColComplete:     return tr("Complete Sources");
    case ColType:         return tr("Type");
    case ColArtist:       return tr("Artist");
    case ColAlbum:        return tr("Album");
    case ColTitle:        return tr("Title");
    case ColLength:       return tr("Length");
    case ColBitrate:      return tr("Bitrate");
    case ColCodec:        return tr("Codec");
    case ColKnown:        return tr("Known");
    default:              return {};
    }
}

void SearchResultsModel::setResults(std::vector<SearchResultRow> results)
{
    beginResetModel();
    m_results = std::move(results);
    endResetModel();
}

void SearchResultsModel::clear()
{
    beginResetModel();
    m_results.clear();
    endResetModel();
}

QString SearchResultsModel::hashAt(int row) const
{
    if (row >= 0 && row < static_cast<int>(m_results.size()))
        return m_results[static_cast<size_t>(row)].hash;
    return {};
}

const SearchResultRow* SearchResultsModel::resultAt(int row) const
{
    if (row >= 0 && row < static_cast<int>(m_results.size()))
        return &m_results[static_cast<size_t>(row)];
    return nullptr;
}

void SearchResultsModel::removeRow(int row)
{
    if (row < 0 || row >= static_cast<int>(m_results.size()))
        return;
    beginRemoveRows({}, row, row);
    m_results.erase(m_results.begin() + row);
    endRemoveRows();
}

} // namespace eMule
