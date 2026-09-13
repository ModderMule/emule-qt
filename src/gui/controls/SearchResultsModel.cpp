#include "pch.h"
/// @file SearchResultsModel.cpp
/// @brief Table model for search results — implementation.

#include "controls/SearchResultsModel.h"
#include "controls/KnownTypeStyle.h"

#include "prefs/Preferences.h"
#include "utils/ColorUtils.h"
#include "utils/FileTypeIcons.h"
#include "utils/RatingIcons.h"
#include "utils/StringUtils.h"

#include <QColor>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QStyleHints>

#include <algorithm>

namespace eMule {

namespace {

/// MFC SearchListCtrl.cpp:1413-1416, 1493-1504: 13 steps from the text colour toward blue,
/// one per source after the first. Dark mode heads for the palette's link blue instead —
/// pure blue is unreadable on a dark list.
QColor availabilityShade(int64_t sources)
{
    constexpr int64_t kShades = 13;   // MFC AVBLYSHADECOUNT
    const int64_t step = std::clamp<int64_t>(sources - 1, 0, kShades - 1);
    if (step == 0)
        return {};
    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    const QPalette pal = QGuiApplication::palette();
    const QColor base = dark ? pal.color(QPalette::Link) : QColor(0, 0, 255);
    return blend(pal.color(QPalette::Text), base,
                 static_cast<qreal>(step) / static_cast<qreal>(kShades));
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

} // anonymous namespace

SearchResultsModel::SearchResultsModel(QObject* parent)
    : AbstractTableModel<SearchResultRow>(parent)
{
}

QVariant SearchResultsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const auto& r = m_rows[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColFileName:     return r.fileName;
        case ColSize:         return formatByteSize(r.fileSize);
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
        // Spam takes the rating mark's place rather than sitting beside it --
        // MFC SearchListCtrl.cpp:1276-1278. A result flagged as spam has nothing
        // useful to say about quality, so the two never compete for the cell.
        const FileMark mark = r.isSpam ? FileMark::Spam
                                       : ratingMark(r.hasComment, r.userRating);
        // No container mark and no own-comment overlay: these files are on other
        // people's disks, so we have neither their bytes nor a comment to publish.
        // MFC's search list registers the overlay image and then never draws it.
        return fileMarksIcon(r.fileType, /*containerSuspect*/ false,
                             /*ownComment*/ false, mark);
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

    // MFC SearchListCtrl.cpp:1381-1417: what we have or had wins (shared with
    // IndexerResultsModel), then spam in grey text, then the availability shade.
    if (role == Qt::ForegroundRole) {
        if (const QColor c = knownTypeColor(r.knownType); c.isValid())
            return c;
        if (r.isSpam && thePrefs.enableSearchResultFilter())
            return dimmedText(0.5);   // COLOR_GRAYTEXT
        if (const QColor c = availabilityShade(r.sourceCount); c.isValid())
            return c;
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

QString SearchResultsModel::hashAt(int row) const
{
    if (const SearchResultRow* r = rowAt(row))
        return r->hash;
    return {};
}

void SearchResultsModel::setKnownType(int row, int knownType)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    auto& r = m_rows[static_cast<size_t>(row)];
    if (r.knownType == knownType)
        return;
    r.knownType = knownType;
    emit dataChanged(index(row, 0), index(row, ColCount - 1));
}

void SearchResultsModel::updateKnownTypes(const QHash<QString, int>& typesByHash)
{
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        auto it = typesByHash.find(m_rows[static_cast<size_t>(i)].hash);
        if (it != typesByHash.end() && m_rows[static_cast<size_t>(i)].knownType != it.value()) {
            m_rows[static_cast<size_t>(i)].knownType = it.value();
            emit dataChanged(index(i, 0), index(i, ColCount - 1));
        }
    }
}

void SearchResultsModel::removeRow(int row)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    beginRemoveRows({}, row, row);
    m_rows.erase(m_rows.begin() + row);
    endRemoveRows();
}

} // namespace eMule
