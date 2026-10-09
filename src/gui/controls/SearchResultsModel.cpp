#include "pch.h"
/// @file SearchResultsModel.cpp
/// @brief Table model for search results — implementation.

#include "controls/SearchResultsModel.h"
#include "controls/ConfidenceStyle.h"
#include "controls/KnownTypeStyle.h"

#include "media/MediaInfo.h"
#include "prefs/Preferences.h"
#include "protocol/ED2KLink.h"
#include "utils/ColorUtils.h"
#include "utils/FileTypeIcons.h"
#include "utils/FileTypeText.h"
#include "utils/Opcodes.h"
#include "utils/RatingIcons.h"
#include "utils/StringUtils.h"

#include <QColor>
#include <QDateTime>
#include <QLocale>
#include <QGuiApplication>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QSet>
#include <QStyleHints>
#include <QUrl>

#include <algorithm>
#include <limits>

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

/// MFC CSearchFile::IsComplete (SearchFile.cpp:382-396): -1 unknown, 1 complete, 0 not.
int completeness(const SearchResultRow& r)
{
    if (r.isKad || r.isMeta())   // eNode torrent/Usenet rows carry no eD2K completeness
        return -1;
    // Found on Kad by the server: Kad seldom says how many sources are complete,
    // so a zero is "not reported", not "nobody has it all"
    if (r.kadOrigin && r.completeSourceCount == 0)
        return -1;
    if (r.inDirectory && r.sourceCount == 1 && r.completeSourceCount == 0)
        return -1;   // a browsed file: nobody said how complete it is
    return r.sourceCount > 0 && r.completeSourceCount > 0 ? 1 : 0;
}

/// MFC GetCompleteSourcesDisplayString (SearchListCtrl.cpp:444-482).
QString completeSourcesText(const SearchResultRow& r)
{
    int complete = completeness(r);
    int64_t completeSources = r.completeSourceCount;
    if (complete < 0 && !r.isMeta() && static_cast<uint64_t>(r.fileSize) <= PARTSIZE) {
        complete = 1;   // a single part is complete wherever it is found
        if (r.inDirectory)
            completeSources = 1;
    }
    if (complete < 0)
        return QStringLiteral("?");

    const bool ext = thePrefs.showExtControls();
    if (complete == 0)
        return ext ? QStringLiteral("0% (0)") : QStringLiteral("0%");
    if (r.sourceCount == 0 || completeSources == 0)
        return SearchResultsModel::tr("Yes");

    QString text = QStringLiteral("%1%").arg(completeSources * 100 / r.sourceCount);
    if (ext)
        text += QStringLiteral(" (%1)").arg(completeSources);
    return text;
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

/// MFC "%u Kbit/s" (IDS_KBITSSEC).
QString formatBitrate(int64_t bitrate)
{
    if (bitrate <= 0)
        return {};
    return QStringLiteral("%1 Kbit/s").arg(bitrate);
}

/// MFC's Availability cell: the count, and in advanced mode the publishers (Kad) or
/// named clients (server) behind it (SearchListCtrl.cpp:1555-1567).
QString availabilityText(const SearchResultRow& r)
{
    QString text = QString::number(r.sourceCount);
    if (thePrefs.showExtControls() && !r.isMeta()) {
        const int detail = r.isKad ? r.kadPublishers : r.clientCount;
        if (detail > 0)
            text += QStringLiteral(" (%1)").arg(detail);
    }
    return text;
}

/// Complete sources as a share of all, the way MFC sorts the column.
qint64 completePercent(const SearchResultRow& r)
{
    return r.sourceCount > 0 ? r.completeSourceCount * 100 / r.sourceCount : 0;
}

} // anonymous namespace

QString SearchResultRow::ed2kLink(const QString& name) const
{
    if (isMeta())   // never mint ed2k:// for a meta pseudo-hash
        return {};
    return ed2kFileLink(name.isEmpty() ? fileName : name, static_cast<uint64>(fileSize), hash);
}

QString SearchResultRow::magnetLink(const QString& name) const
{
    if (isMeta())
        return magnet;   // server-supplied for torrents; Usenet has none

    // the form ED2KLink::parseMagnetLink reads back
    return QStringLiteral("magnet:?xt=urn:ed2k:%1&xl=%2&dn=%3")
        .arg(hash.toUpper())
        .arg(fileSize)
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(name.isEmpty() ? fileName : name)));
}

QCborMap SearchResultRow::metaRef() const
{
    return {{QStringLiteral("name"), fileName},
            {QStringLiteral("metaKind"), metaKind},
            {QStringLiteral("metaCatalogId"), metaCatalogId},
            {QStringLiteral("metaServers"), metaServers}};
}

QCborMap SearchResultRow::fileRef() const
{
    QCborArray names;
    for (const SearchChildRow& child : children)
        names.append(child.fileName);

    // A row saved before the seed was kept: a server result's root is as good, as
    // long as no name it was found under says otherwise.
    QString seed = aichSeed;
    bool vouched = aichVouched;
    if (seed.isEmpty() && !isKad && !aichHash.isEmpty()
        && std::ranges::none_of(children, [this](const SearchChildRow& child) {
               return !child.aichHash.isEmpty() && child.aichHash != aichHash;
           })) {
        seed = aichHash;
        vouched = true;
    }
    return {{QStringLiteral("name"), fileName},
            {QStringLiteral("names"), names},
            {QStringLiteral("directory"), directory},
            {QStringLiteral("previewPossible"), previewPossible},
            {QStringLiteral("clients"), clients},
            {QStringLiteral("aichSeed"), seed},
            {QStringLiteral("aichVouched"), vouched},
            {QStringLiteral("aichVoters"), aichVoters},
            {QStringLiteral("size"), static_cast<qint64>(fileSize)},
            {QStringLiteral("type"), fileType},
            {QStringLiteral("sources"), static_cast<qint64>(sourceCount)},
            {QStringLiteral("completeSources"), static_cast<qint64>(completeSourceCount)},
            {QStringLiteral("isKad"), isKad},
            {QStringLiteral("isSpam"), isSpam},
            {QStringLiteral("artist"), artist},
            {QStringLiteral("album"), album},
            {QStringLiteral("title"), title},
            {QStringLiteral("codec"), codec},
            {QStringLiteral("length"), static_cast<qint64>(length)},
            {QStringLiteral("bitrate"), static_cast<qint64>(bitrate)}};
}

SearchResultsModel::SearchResultsModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

QModelIndex SearchResultsModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
        return {};
    // Files carry internalId 0, a name row its file's uid (rows shift, uids do not)
    if (!parent.isValid())
        return createIndex(row, column, quintptr(0));
    if (parent.internalId() == 0)
        return createIndex(row, column, m_rows[static_cast<size_t>(parent.row())].uid);
    return {};
}

QModelIndex SearchResultsModel::parent(const QModelIndex& index) const
{
    if (!index.isValid() || index.internalId() == 0)
        return {};
    const int row = rowOfUid(index.internalId());
    return row < 0 ? QModelIndex{} : createIndex(row, 0, quintptr(0));
}

int SearchResultsModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return static_cast<int>(m_rows.size());
    if (parent.internalId() != 0 || parent.column() != 0
        || parent.row() >= static_cast<int>(m_rows.size()))
        return 0;
    return static_cast<int>(m_rows[static_cast<size_t>(parent.row())].children.size());
}

int SearchResultsModel::columnCount(const QModelIndex& /*parent*/) const
{
    return ColCount;
}

const SearchResultRow* SearchResultsModel::resultAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return nullptr;
    return &m_rows[static_cast<size_t>(row)];
}

SearchResultRef SearchResultsModel::resultAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.model() != this)
        return {};
    if (index.internalId() == 0)
        return {resultAt(index.row()), nullptr};

    const SearchResultRow* file = resultAt(rowOfUid(index.internalId()));
    if (!file || index.row() < 0 || index.row() >= static_cast<int>(file->children.size()))
        return {};
    return {file, &file->children[static_cast<size_t>(index.row())]};
}

QVariant SearchResultsModel::foreground(const SearchResultRow& r, int64_t sources, int column,
                                        bool child) const
{
    // MFC SearchListCtrl.cpp:1381-1417: what we have or had wins (shared with
    // IndexerResultsModel), then spam in grey text, then the availability shade —
    // a name row shades by its own count.
    // SearchListCtrl.cpp:1452-1459: this one cell goes red for a file nobody has
    // complete, over whatever colour the row has. Unknown (-1) stays uncoloured.
    if (!child && column == ColComplete && completeness(r) == 0)
        return QColor(255, 0, 0);
    // The verdict colours its own cell only; the row keeps its meaning.
    if (!child && column == ColConfidence) {
        if (const QColor c = confidenceColor(r.confidence); c.isValid())
            return c;
    }
    if (const QColor c = knownTypeColor(r.knownType); c.isValid())
        return c;
    if (r.isSpam && thePrefs.enableSearchResultFilter())
        return dimmedText(0.5);   // COLOR_GRAYTEXT
    if (const QColor c = availabilityShade(sources); c.isValid())
        return c;
    return {};
}

QVariant SearchResultsModel::childData(const SearchResultRow& r, const SearchChildRow& c,
                                       int column, int role) const
{
    // MFC GetItemDisplayText: a name row leaves Size, Complete Sources, Type and
    // File ID blank (SearchListCtrl.cpp:1541-1654).
    if (role == Qt::DisplayRole) {
        switch (column) {
        case ColFileName:     return c.fileName;
        case ColAvailability: return QString::number(c.sourceCount);
        case ColArtist:       return c.artist;
        case ColAlbum:        return c.album;
        case ColTitle:        return c.title;
        case ColLength:       return formatLength(c.length);
        case ColBitrate:      return formatBitrate(c.bitrate);
        case ColCodec:        return codecDisplayName(c.codec);
        case ColKnown:        return knownTypeString(r.knownType);
        case ColFolder:       return c.directory;
        case ColAichHash:     return c.aichHash;
        default:              return {};
        }
    }

    if (role == Qt::UserRole) {
        switch (column) {
        case ColFileName:     return c.fileName;
        case ColAvailability: return QVariant::fromValue(c.sourceCount);
        case ColAichHash:     return c.aichHash;
        default:              return {};
        }
    }

    if (role == Qt::ForegroundRole)
        return foreground(r, c.sourceCount, column, /*child*/ true);

    return {};
}

QVariant SearchResultsModel::data(const QModelIndex& index, int role) const
{
    const SearchResultRef ref = resultAt(index);
    if (!ref)
        return {};
    const SearchResultRow& r = *ref.row;

    // MFC's LVCFMT_RIGHT columns (SearchListCtrl.cpp:263-272)
    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColSize: case ColAvailability: case ColComplete: case ColLength: case ColBitrate:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    if (role == ChildRole)
        return ref.child != nullptr;
    if (role == SpamRole)
        return r.isSpam && thePrefs.enableSearchResultFilter();

    if (ref.child)
        return childData(r, *ref.child, index.column(), role);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColFileName:     return r.fileName;
        case ColSize:         return formatByteSize(r.fileSize);
        case ColAvailability: return availabilityText(r);
        case ColComplete:     return completeSourcesText(r);
        case ColType:         return fileTypeText(r.fileType, r.fileName);
        case ColArtist:       return r.artist;
        case ColAlbum:        return r.album;
        case ColTitle:        return r.title;
        case ColLength:       return formatLength(r.length);
        case ColBitrate:      return formatBitrate(r.bitrate);
        case ColCodec:        return codecDisplayName(r.codec);
        case ColKnown:        return knownTypeString(r.knownType);
        case ColSeen:         return seenText(r);
        case ColConfidence:   return confidenceText(r.confidence, r.fakeScore);
        case ColFileID:       return r.isMeta() ? QString{} : r.hash;
        case ColFolder:       return r.directory;
        case ColAichHash:     return r.aichHash;
        default: break;
        }
    }

    if (role == Qt::ToolTipRole && index.column() == ColConfidence)
        return confidenceTooltip(r.confidence, r.fakeScore, r.fakeReasons);

    if (role == Qt::DecorationRole && index.column() == ColFileName) {
        // Spam takes the rating mark's place rather than sitting beside it --
        // MFC SearchListCtrl.cpp:1276-1278. A result flagged as spam has nothing
        // useful to say about quality, so the two never compete for the cell.
        const FileMark mark = r.isSpam ? FileMark::Spam
                                       : ratingMark(r.hasComment, r.userRating);
        // No container mark and no own-comment overlay: these files are on other
        // people's disks, so we have neither their bytes nor a comment to publish.
        // MFC's search list registers the overlay image and then never draws it.
        // eNode torrent/Usenet rows keep their type icon and add their network
        // (the toolbar icons) right after it, and so does a file the server found on
        // Kad; eD2K rows beside them keep the slot blank.
        QString network;
        if (r.isUsenet())
            network = QStringLiteral(":/icons/Usenet.ico");
        else if (r.isTorrent())
            network = QStringLiteral(":/icons/Torrent.ico");
        else if (r.kadOrigin)
            network = QStringLiteral(":/icons/Kad.ico");
        else if (m_hasMeta)
            network = kEmptyBadge;
        return fileMarksIcon(r.fileType, /*containerSuspect*/ false,
                             /*ownComment*/ false, mark, network);
    }

    // Raw values for sorting
    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColFileName:     return r.fileName;
        case ColSize:         return QVariant::fromValue(r.fileSize);
        case ColAvailability: return QVariant::fromValue(r.sourceCount);
        // By share, as MFC (SearchListCtrl.cpp:637-640)
        case ColComplete:     return QVariant::fromValue(completePercent(r));
        // By the name shown, then by extension (SearchListCtrl.cpp:642-648)
        case ColType:         return QString(fileTypeText(r.fileType, r.fileName) + u'\n'
                                             + QFileInfo(r.fileName).suffix().toLower());
        case ColArtist:       return r.artist;
        case ColAlbum:        return r.album;
        case ColTitle:        return r.title;
        case ColLength:       return QVariant::fromValue(r.length);
        case ColBitrate:      return QVariant::fromValue(r.bitrate);
        case ColCodec:        return r.codec;
        case ColKnown:        return r.knownType;
        // Oldest acquaintance first; what was never seen sorts last.
        case ColSeen:         return r.seenBefore ? QVariant::fromValue<qint64>(r.firstSeen)
                                                  : QVariant::fromValue<qint64>(std::numeric_limits<qint64>::max());
        case ColConfidence:   return confidenceSortKey(r.confidence, r.fakeScore);
        case ColFileID:       return r.hash;
        case ColFolder:       return r.directory;
        case ColAichHash:     return r.aichHash;
        default: break;
        }
    }

    if (role == Qt::ForegroundRole)
        return foreground(r, r.sourceCount, index.column(), /*child*/ false);

    return {};
}

QString SearchResultsModel::seenText(const SearchResultRow& r)
{
    if (!r.seenBefore)
        return {};
    const QString since = QLocale().toString(
        QDateTime::fromSecsSinceEpoch(r.firstSeen).date(), QLocale::ShortFormat);
    // More than one name for the same content is worth a look.
    return r.seenNames > 1 ? tr("%1 · %n name(s)", nullptr, r.seenNames).arg(since) : since;
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
    case ColSeen:         return tr("Seen");
    case ColConfidence:   return tr("Confidence");
    case ColFileID:       return tr("File ID");
    case ColFolder:       return tr("Folder");
    case ColAichHash:     return tr("AICH Hash");
    default:              return {};
    }
}

QString SearchResultsModel::hashAt(int row) const
{
    if (const SearchResultRow* r = resultAt(row))
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

void SearchResultsModel::setSpam(const QString& hash, bool spam)
{
    for (int row = 0; row < static_cast<int>(m_rows.size()); ++row) {
        auto& r = m_rows[static_cast<size_t>(row)];
        if (r.isSpam == spam || r.hash.compare(hash, Qt::CaseInsensitive) != 0)
            continue;
        r.isSpam = spam;
        emit dataChanged(index(row, 0), index(row, ColCount - 1));
    }
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

void SearchResultsModel::setResults(std::vector<SearchResultRow> incoming)
{
    // Incremental, not a reset: the list is refetched while a search runs, and a
    // reset would collapse every expanded file and drop the selection.
    QHash<QString, size_t> incomingByHash;
    incomingByHash.reserve(static_cast<qsizetype>(incoming.size()));
    for (size_t i = 0; i < incoming.size(); ++i)
        incomingByHash.insert(incoming[i].hash, i);

    // Rows that cannot be told apart by hash: nothing to match on, start over
    if (incomingByHash.size() != static_cast<qsizetype>(incoming.size())) {
        beginResetModel();
        m_rows = std::move(incoming);
        for (auto& row : m_rows)
            row.uid = m_nextUid++;
        reindexRows();
        endResetModel();
        updateHasMeta();
        return;
    }

    // 1. Files that left. Reindexed before endRemoveRows(), where views start asking
    //    parent() of the name rows under the shifted files.
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        if (!incomingByHash.contains(m_rows[static_cast<size_t>(i)].hash)) {
            beginRemoveRows({}, i, i);
            m_rows.erase(m_rows.begin() + i);
            reindexRows();
            endRemoveRows();
        }
    }

    // 2. Files that stayed: names first, then the row itself
    QSet<QString> existing;
    existing.reserve(static_cast<qsizetype>(m_rows.size()));
    for (size_t i = 0; i < m_rows.size(); ++i) {
        existing.insert(m_rows[i].hash);
        SearchResultRow& fresh = incoming[incomingByHash.value(m_rows[i].hash)];
        setChildren(static_cast<int>(i), std::move(fresh.children));
        fresh.children = std::move(m_rows[i].children);
        fresh.uid = m_rows[i].uid;
        m_rows[i] = std::move(fresh);
    }
    if (!m_rows.empty())
        emit dataChanged(index(0, 0), index(static_cast<int>(m_rows.size()) - 1, ColCount - 1));

    // 3. New files, appended in one batch
    std::vector<SearchResultRow> toInsert;
    QSet<QString> queued;
    for (auto& row : incoming) {
        if (existing.contains(row.hash) || queued.contains(row.hash))
            continue;
        queued.insert(row.hash);
        toInsert.push_back(std::move(row));
    }
    if (!toInsert.empty()) {
        const int first = static_cast<int>(m_rows.size());
        beginInsertRows({}, first, first + static_cast<int>(toInsert.size()) - 1);
        for (auto& row : toInsert) {
            row.uid = m_nextUid++;
            m_rowByUid.insert(row.uid, static_cast<int>(m_rows.size()));
            m_rows.push_back(std::move(row));
        }
        endInsertRows();
    }
    updateHasMeta();
}

void SearchResultsModel::setChildren(int row, std::vector<SearchChildRow> incoming)
{
    auto& children = m_rows[static_cast<size_t>(row)].children;
    const QModelIndex parentIdx = index(row, 0);

    QHash<QString, size_t> incomingByName;
    for (size_t i = 0; i < incoming.size(); ++i)
        incomingByName.insert(incoming[i].fileName, i);

    for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i) {
        if (!incomingByName.contains(children[static_cast<size_t>(i)].fileName)) {
            beginRemoveRows(parentIdx, i, i);
            children.erase(children.begin() + i);
            endRemoveRows();
        }
    }

    QSet<QString> existing;
    for (auto& child : children) {
        existing.insert(child.fileName);
        child = incoming[incomingByName.value(child.fileName)];
    }

    std::vector<SearchChildRow> toInsert;
    for (auto& child : incoming) {
        if (!existing.contains(child.fileName)) {
            existing.insert(child.fileName);
            toInsert.push_back(std::move(child));
        }
    }
    if (!toInsert.empty()) {
        const int first = static_cast<int>(children.size());
        beginInsertRows(parentIdx, first, first + static_cast<int>(toInsert.size()) - 1);
        for (auto& child : toInsert)
            children.push_back(std::move(child));
        endInsertRows();
    }
    if (!children.empty())
        emit dataChanged(index(0, 0, parentIdx),
                         index(static_cast<int>(children.size()) - 1, ColCount - 1, parentIdx));
}

void SearchResultsModel::removeRow(int row)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    beginRemoveRows({}, row, row);
    m_rows.erase(m_rows.begin() + row);
    reindexRows();
    endRemoveRows();
    updateHasMeta();
}

void SearchResultsModel::removeChild(int row, int childRow)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    auto& children = m_rows[static_cast<size_t>(row)].children;
    if (childRow < 0 || childRow >= static_cast<int>(children.size()))
        return;
    beginRemoveRows(index(row, 0), childRow, childRow);
    children.erase(children.begin() + childRow);
    endRemoveRows();
}

void SearchResultsModel::reindexRows()
{
    m_rowByUid.clear();
    for (size_t i = 0; i < m_rows.size(); ++i)
        m_rowByUid.insert(m_rows[i].uid, static_cast<int>(i));
}

void SearchResultsModel::updateHasMeta()
{
    const bool hasMeta = std::ranges::any_of(
        m_rows, [](const SearchResultRow& r) { return r.isMeta() || r.kadOrigin; });
    if (hasMeta == m_hasMeta)
        return;
    m_hasMeta = hasMeta;
    // The eD2K rows gain or lose their blank badge slot
    if (!m_rows.empty())
        emit dataChanged(index(0, ColFileName), index(resultCount() - 1, ColFileName),
                         {Qt::DecorationRole});
}

} // namespace eMule
