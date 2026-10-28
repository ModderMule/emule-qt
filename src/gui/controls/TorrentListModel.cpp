#include "pch.h"
/// @file TorrentListModel.cpp
/// @brief Model for the Torrents tab — implementation.

#include "controls/TorrentListModel.h"

#include "utils/FileTypeIcons.h"
#include "utils/StringUtils.h"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QIcon>
#include <QLocale>

#include <QSet>

#include <algorithm>
#include <limits>

namespace eMule {

namespace {

/// Status sort order: busy first, then waiting, then done, then broken.
int stateRank(TorrentState s)
{
    switch (s) {
    case TorrentState::Downloading:         return 0;
    case TorrentState::DownloadingMetadata: return 1;
    case TorrentState::CheckingFiles:       return 2;
    case TorrentState::CheckingResume:      return 3;
    case TorrentState::Moving:              return 4;
    case TorrentState::Queued:              return 5;
    case TorrentState::Paused:              return 6;
    case TorrentState::Seeding:             return 7;
    case TorrentState::Finished:            return 8;
    case TorrentState::Error:               return 9;
    }
    return 10;
}

QString countWithTotal(int connected, int total)
{
    return total >= 0 ? QStringLiteral("%1 (%2)").arg(connected).arg(total)
                      : QString::number(connected);
}

} // namespace

QString torrentStateName(TorrentState state)
{
    switch (state) {
    case TorrentState::Queued:              return TorrentListModel::tr("Queued");
    case TorrentState::CheckingFiles:       return TorrentListModel::tr("Checking files");
    case TorrentState::DownloadingMetadata: return TorrentListModel::tr("Getting metadata");
    case TorrentState::Downloading:         return TorrentListModel::tr("Downloading");
    case TorrentState::Finished:            return TorrentListModel::tr("Completed");
    case TorrentState::Seeding:             return TorrentListModel::tr("Seeding");
    case TorrentState::CheckingResume:      return TorrentListModel::tr("Checking resume data");
    case TorrentState::Paused:              return TorrentListModel::tr("Paused");
    case TorrentState::Error:               return TorrentListModel::tr("Error");
    case TorrentState::Moving:              return TorrentListModel::tr("Moving");
    }
    return TorrentListModel::tr("Unknown");
}

bool torrentStateActive(TorrentState state)
{
    switch (state) {
    case TorrentState::CheckingFiles:
    case TorrentState::DownloadingMetadata:
    case TorrentState::Downloading:
    case TorrentState::CheckingResume:
    case TorrentState::Moving:
        return true;
    default:
        return false;
    }
}

QString torrentFilePriorityName(int priority)
{
    // the Files tab's words, under its context
    if (priority <= 0)
        return QCoreApplication::translate("eMule::TorrentListsPane", "Skip");
    if (priority <= 2)
        return QCoreApplication::translate("eMule::TorrentListsPane", "Low");
    if (priority <= 4)
        return QCoreApplication::translate("eMule::TorrentListsPane", "Normal");
    if (priority <= 6)
        return QCoreApplication::translate("eMule::TorrentListsPane", "High");
    return QCoreApplication::translate("eMule::TorrentListsPane", "Maximum");
}

double TorrentRow::ratio() const
{
    if (downloaded <= 0)
        return uploaded > 0 ? -2 : -1;   // -2 = seeded only (infinite)
    return double(uploaded) / double(downloaded);
}

bool TorrentRow::isComplete() const
{
    return state == TorrentState::Finished || state == TorrentState::Seeding;
}

TorrentRow torrentRowFromCbor(const QCborMap& m)
{
    auto str = [&m](const char* k) { return m.value(QLatin1StringView(k)).toString(); };
    auto i64 = [&m](const char* k, qint64 def = 0) {
        return m.value(QLatin1StringView(k)).toInteger(def);
    };
    auto i32 = [&](const char* k, int def = 0) { return static_cast<int>(i64(k, def)); };
    auto flag = [&m](const char* k) { return m.value(QLatin1StringView(k)).toBool(); };

    TorrentRow r;
    r.id = str("id");
    r.name = str("name");
    r.savePath = str("savePath");
    r.state = static_cast<TorrentState>(std::clamp(i32("state"), 0, int(TorrentState::Moving)));
    r.size = i64("size");
    r.done = i64("done");
    r.progress = m.value(QLatin1StringView("progress")).toDouble();
    r.rateDown = i64("rateDown");
    r.rateUp = i64("rateUp");
    r.peers = i32("peers");
    r.seeds = i32("seeds");
    r.peersTotal = i32("peersTotal", -1);
    r.seedsTotal = i32("seedsTotal", -1);
    r.downloaded = i64("downloaded");
    r.uploaded = i64("uploaded");
    r.failedBytes = i64("failedBytes");
    r.error = str("error");
    r.hashV1 = str("hashV1");
    r.hashV2 = str("hashV2");
    r.paused = flag("paused");
    r.hasMetadata = flag("hasMetadata");
    r.isPrivate = flag("private");
    r.addedAt = i64("addedAt");
    r.completedAt = i64("completedAt");
    r.seedingSeconds = i64("seedingSeconds");
    r.queuePosition = i32("queuePosition", -1);
    r.pieceMap = m.value(QLatin1StringView("pieceMap")).toByteArray();
    r.pieces = i32("pieces");
    r.pieceLength = i32("pieceLength");
    r.numFiles = i32("numFiles");
    r.eta = i64("eta", -1);
    r.comment = str("comment");
    r.creator = str("creator");
    r.createdAt = i64("createdAt");
    r.lastActivity = i64("lastActivity");
    r.activeSeconds = i64("activeSeconds");
    r.haveBytes = i64("haveBytes");
    r.totalSize = i64("totalSize");
    r.availability = m.value(QLatin1StringView("availability")).toDouble(-1);
    if (r.name.isEmpty())
        r.name = r.hashV1.isEmpty() ? r.id : r.hashV1;   // magnet without dn, no metadata yet
    return r;
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

QModelIndex TorrentListModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column < 0 || column >= ColCount || row < 0)
        return {};
    if (!parent.isValid())
        return row < count() ? createIndex(row, column, quintptr(0)) : QModelIndex{};
    if (parent.internalId() != 0 || parent.row() >= count())
        return {};   // files have no children
    const auto it = m_files.constFind(m_keys[size_t(parent.row())]);
    if (it == m_files.cend() || row >= int(it->files.size()))
        return {};
    return createIndex(row, column, it.key());
}

QModelIndex TorrentListModel::parent(const QModelIndex& child) const
{
    if (!child.isValid() || child.internalId() == 0)
        return {};
    const int row = rowOfKey(child.internalId());
    return row < 0 ? QModelIndex{} : createIndex(row, 0, quintptr(0));
}

int TorrentListModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return count();
    if (parent.internalId() != 0 || parent.column() != 0 || parent.row() >= count())
        return 0;
    const auto it = m_files.constFind(m_keys[size_t(parent.row())]);
    return it == m_files.cend() ? 0 : int(it->files.size());
}

int TorrentListModel::columnCount(const QModelIndex& /*parent*/) const
{
    return ColCount;
}

bool TorrentListModel::hasChildren(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return count() > 0;
    if (parent.internalId() != 0 || parent.column() != 0)
        return false;
    const TorrentRow* r = rowAt(parent.row());
    // the arrow shows before the files are loaded
    return r && r->numFiles > 1;
}

bool TorrentListModel::canFetchMore(const QModelIndex& parent) const
{
    if (!parent.isValid() || parent.internalId() != 0)
        return false;
    const TorrentRow* r = rowAt(parent.row());
    return r && r->numFiles > 1 && !m_files.contains(m_keys[size_t(parent.row())]);
}

void TorrentListModel::fetchMore(const QModelIndex& parent)
{
    if (canFetchMore(parent))
        emit filesWanted(idAt(parent.row()));
}

QVariant TorrentListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    if (index.internalId() != 0) {
        const TorrentRow* parentRow = parentRowOf(index);
        const TorrentFileRow* f = fileAt(index);
        return parentRow && f ? fileData(*parentRow, *f, index.column(), role) : QVariant{};
    }
    const TorrentRow* r = rowAt(index.row());
    if (!r)
        return {};

    if (index.column() == ColProgress && role >= kTorrentBarRole && role <= kTorrentBarSourcesRole)
        return barData(*r, role);

    switch (role) {
    case Qt::DisplayRole:
        return displayData(*r, index.column());
    case Qt::UserRole:
        return sortData(*r, index.column());
    case Qt::DecorationRole:
        // as the Transfer window: the file's type; a folder for a multi-file torrent
        if (index.column() == ColName) {
            static const QIcon folder(QStringLiteral(":/icons/FolderOpen.ico"));
            return r->numFiles > 1 ? folder : fileTypeIconForName(r->name);
        }
        return {};
    case Qt::TextAlignmentRole:
        switch (index.column()) {
        case ColSize: case ColDone: case ColDownSpeed: case ColUpSpeed:
        case ColSeeds: case ColPeers: case ColRatio: case ColEta:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return {};
        }
    case Qt::ForegroundRole:
        if (r->state == TorrentState::Error)
            return QVariant::fromValue(QColor(200, 0, 0));
        if (r->state == TorrentState::Paused)
            return QVariant::fromValue(QColor(0x88, 0x88, 0x88));
        return {};
    case Qt::ToolTipRole:
        if (index.column() == ColName) {
            QString tip = r->name;
            if (!r->savePath.isEmpty())
                tip += QLatin1Char('\n') + r->savePath;
            if (r->isPrivate)
                tip += QLatin1Char('\n') + tr("Private torrent (no DHT, PEX or LSD)");
            return tip;
        }
        if (index.column() == ColStatus && !r->error.isEmpty())
            return r->error;
        if (index.column() == ColProgress) {
            return tr("%1% — %2 of %3 pieces")
                .arg(QString::number(r->progress * 100.0, 'f', 1))
                .arg(qRound(r->progress * r->pieces))
                .arg(r->pieces);
        }
        return {};
    default:
        return {};
    }
}

QVariant TorrentListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColName:      return tr("Name");
    case ColSize:      return tr("Size");
    case ColDone:      return tr("Completed");
    case ColProgress:  return tr("Progress");
    case ColStatus:    return tr("Status");
    case ColDownSpeed: return tr("Down Speed");
    case ColUpSpeed:   return tr("Up Speed");
    case ColSeeds:     return tr("Seeds");
    case ColPeers:     return tr("Peers");
    case ColRatio:     return tr("Ratio");
    case ColEta:       return tr("Remaining Time");
    case ColAdded:     return tr("Added");
    default:           return {};
    }
}

void TorrentListModel::setRows(const QList<TorrentRow>& rows)
{
    QSet<QString> incoming;
    incoming.reserve(rows.size());
    for (const TorrentRow& r : rows)
        incoming.insert(r.id);

    // Bottom-up so the indices of rows still to check stay valid
    for (int row = count() - 1; row >= 0; --row) {
        if (incoming.contains(m_rows[size_t(row)].id))
            continue;
        beginRemoveRows({}, row, row);
        m_files.remove(m_keys[size_t(row)]);
        m_rows.erase(m_rows.begin() + row);
        m_keys.erase(m_keys.begin() + row);
        endRemoveRows();
    }
    for (const TorrentRow& r : rows)
        upsertRow(r);
}

void TorrentListModel::upsertRow(const TorrentRow& row)
{
    if (row.id.isEmpty())
        return;
    const int at = rowOf(row.id);
    if (at >= 0) {
        m_rows[size_t(at)] = row;
        emit dataChanged(index(at, 0), index(at, ColCount - 1));
        return;
    }
    const int first = count();
    beginInsertRows({}, first, first);
    m_rows.push_back(row);
    m_keys.push_back(m_nextKey++);
    endInsertRows();
}

void TorrentListModel::removeRow(const QString& id)
{
    const int at = rowOf(id);
    if (at < 0)
        return;
    beginRemoveRows({}, at, at);
    m_files.remove(m_keys[size_t(at)]);
    m_rows.erase(m_rows.begin() + at);
    m_keys.erase(m_keys.begin() + at);
    endRemoveRows();
}

void TorrentListModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
    m_keys.clear();
    m_files.clear();
    endResetModel();
}

const TorrentRow* TorrentListModel::findById(const QString& id) const
{
    return rowAt(rowOf(id));
}

const TorrentRow* TorrentListModel::rowAt(int row) const
{
    if (row < 0 || row >= count())
        return nullptr;
    return &m_rows[size_t(row)];
}

QString TorrentListModel::idAt(int row) const
{
    const TorrentRow* r = rowAt(row);
    return r ? r->id : QString();
}

int TorrentListModel::rowOf(const QString& id) const
{
    const auto it = std::ranges::find(m_rows, id, &TorrentRow::id);
    return it == m_rows.end() ? -1 : int(it - m_rows.begin());
}

QString TorrentListModel::idOf(const QModelIndex& index) const
{
    if (!index.isValid())
        return {};
    return index.internalId() == 0 ? idAt(index.row()) : idAt(rowOfKey(index.internalId()));
}

const TorrentFileRow* TorrentListModel::fileAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.internalId() == 0)
        return nullptr;
    const auto it = m_files.constFind(index.internalId());
    if (it == m_files.cend() || index.row() >= int(it->files.size()))
        return nullptr;
    return &it->files[size_t(index.row())];
}

bool TorrentListModel::hasFiles(const QString& id) const
{
    const int row = rowOf(id);
    return row >= 0 && m_files.contains(m_keys[size_t(row)]);
}

void TorrentListModel::setFiles(const QString& id, const QCborArray& files)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    FileSet set;
    int maxIndex = -1;
    for (const auto& v : files)
        maxIndex = std::max(maxIndex, int(v.toMap().value(QStringLiteral("index")).toInteger()));
    set.priorities = QList<int>(maxIndex + 1, 4);
    for (const auto& v : files) {
        const QCborMap m = v.toMap();
        TorrentFileRow f;
        f.index = int(m.value(QStringLiteral("index")).toInteger(-1));
        f.priority = int(m.value(QStringLiteral("priority")).toInteger(4));
        if (f.index >= 0 && f.index < set.priorities.size())
            set.priorities[f.index] = f.priority;
        if (m.value(QStringLiteral("pad")).toBool())
            continue;   // alignment padding, not a real file
        f.path = m.value(QStringLiteral("path")).toString();
        f.size = m.value(QStringLiteral("size")).toInteger();
        f.done = m.value(QStringLiteral("done")).toInteger();
        set.files.push_back(std::move(f));
    }

    const quintptr key = m_keys[size_t(row)];
    const QModelIndex parent = index(row, 0);
    auto it = m_files.find(key);
    const int oldCount = it == m_files.end() ? 0 : int(it->files.size());
    const int newCount = int(set.files.size());
    if (it != m_files.end() && oldCount == newCount) {
        // same file list: update in place, selection stays
        *it = std::move(set);
        if (newCount > 0)
            emit dataChanged(index(0, 0, parent), index(newCount - 1, ColCount - 1, parent));
        return;
    }
    if (oldCount > 0) {
        beginRemoveRows(parent, 0, oldCount - 1);
        it->files.clear();
        endRemoveRows();
    }
    if (newCount > 0) {
        beginInsertRows(parent, 0, newCount - 1);
        m_files.insert(key, std::move(set));
        endInsertRows();
    } else {
        m_files.insert(key, std::move(set));
    }
}

QList<int> TorrentListModel::priorities(const QString& id) const
{
    const int row = rowOf(id);
    if (row < 0)
        return {};
    return m_files.value(m_keys[size_t(row)]).priorities;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

QVariant TorrentListModel::displayData(const TorrentRow& r, int column) const
{
    switch (column) {
    case ColName:      return r.name;
    case ColSize:      return r.hasMetadata ? formatByteSize(r.size) : QStringLiteral("?");
    case ColDone:      return formatByteSize(r.done);
    case ColProgress:  return QStringLiteral("%1%").arg(r.progress * 100.0, 0, 'f', 1);
    case ColStatus:    return statusText(r);
    case ColDownSpeed: return r.rateDown > 0 ? formatByteRate(r.rateDown) : QString();
    case ColUpSpeed:   return r.rateUp > 0 ? formatByteRate(r.rateUp) : QString();
    case ColSeeds:     return countWithTotal(r.seeds, r.seedsTotal);
    case ColPeers:     return countWithTotal(r.peers, r.peersTotal);
    case ColRatio: {
        const double ratio = r.ratio();
        if (ratio == -2)
            return QStringLiteral("∞");
        return ratio < 0 ? QStringLiteral("—") : QLocale().toString(ratio, 'f', 2);
    }
    case ColEta:
        if (r.isComplete() || r.state == TorrentState::Paused || r.eta < 0)
            return QString();
        return formatSecondsHM(r.eta);
    case ColAdded:
        return r.addedAt > 0
                   ? QLocale().toString(QDateTime::fromSecsSinceEpoch(r.addedAt), QLocale::ShortFormat)
                   : QString();
    default:
        return {};
    }
}

QVariant TorrentListModel::sortData(const TorrentRow& r, int column) const
{
    // Raw values, not the formatted strings ("9.90 MB" > "10.00 GB" as text)
    switch (column) {
    case ColName:      return r.name;
    case ColSize:      return QVariant::fromValue(r.size);
    case ColDone:      return QVariant::fromValue(r.done);
    case ColProgress:  return r.progress;
    case ColStatus:    return stateRank(r.state);
    case ColDownSpeed: return QVariant::fromValue(r.rateDown);
    case ColUpSpeed:   return QVariant::fromValue(r.rateUp);
    case ColSeeds:     return r.seeds;
    case ColPeers:     return r.peers;
    case ColRatio: {
        const double ratio = r.ratio();
        return ratio == -2 ? 1e12 : ratio;
    }
    case ColEta:
        // unknown sorts after every known time
        return QVariant::fromValue(r.eta < 0 || r.isComplete() ? std::numeric_limits<qint64>::max()
                                                               : r.eta);
    case ColAdded:     return QVariant::fromValue(r.addedAt);
    default:           return {};
    }
}

QVariant TorrentListModel::barData(const TorrentRow& r, int role) const
{
    switch (role) {
    case kTorrentBarRole:         return r.pieceMap;
    case kTorrentBarPercentRole:  return r.progress * 100.0;
    case kTorrentBarPausedRole:   return r.state == TorrentState::Paused
                                         || r.state == TorrentState::Error;
    case kTorrentBarCompleteRole: return r.isComplete();
    case kTorrentBarSourcesRole:  return r.peers + r.seeds;
    default:                      return {};
    }
}

int TorrentListModel::rowOfKey(quintptr key) const
{
    const auto it = std::ranges::find(m_keys, key);
    return it == m_keys.end() ? -1 : int(it - m_keys.begin());
}

const TorrentRow* TorrentListModel::parentRowOf(const QModelIndex& child) const
{
    return rowAt(rowOfKey(child.internalId()));
}

QVariant TorrentListModel::fileData(const TorrentRow& r, const TorrentFileRow& f, int column,
                                    int role) const
{
    const qsizetype slash = f.path.lastIndexOf(QLatin1Char('/'));
    const QString name = slash >= 0 ? f.path.mid(slash + 1) : f.path;
    const double share = f.size > 0 ? double(f.done) / double(f.size) : 1.0;
    const bool complete = f.size > 0 && f.done >= f.size;

    if (column == ColProgress) {
        switch (role) {
        case kTorrentBarRole:         return QByteArray();
        case kTorrentBarPercentRole:  return share * 100.0;
        case kTorrentBarPausedRole:   return r.state == TorrentState::Paused
                                             || r.state == TorrentState::Error || f.priority == 0;
        case kTorrentBarCompleteRole: return complete;
        case kTorrentBarSourcesRole:  return r.peers + r.seeds;
        default: break;
        }
    }

    switch (role) {
    case Qt::DisplayRole:
        switch (column) {
        case ColName:     return name;
        case ColSize:     return formatByteSize(f.size);
        case ColDone:     return formatByteSize(f.done);
        case ColProgress: return QStringLiteral("%1%").arg(share * 100.0, 0, 'f', 1);
        case ColStatus:   return torrentFilePriorityName(f.priority);
        default:          return {};
        }
    case Qt::UserRole:
        switch (column) {
        case ColName:     return name;
        case ColSize:     return QVariant::fromValue(f.size);
        case ColDone:     return QVariant::fromValue(f.done);
        case ColProgress: return share;
        case ColStatus:   return f.priority;
        default:          return {};
        }
    case Qt::DecorationRole:
        return column == ColName ? QVariant(fileTypeIconForName(name)) : QVariant{};
    case Qt::TextAlignmentRole:
        return column == ColSize || column == ColDone
                   ? QVariant(int(Qt::AlignRight | Qt::AlignVCenter)) : QVariant{};
    case Qt::ForegroundRole:
        return f.priority == 0 ? QVariant::fromValue(QColor(0x88, 0x88, 0x88)) : QVariant{};
    case Qt::ToolTipRole:
        return column == ColName ? QVariant(f.path) : QVariant{};
    default:
        return {};
    }
}

QString TorrentListModel::statusText(const TorrentRow& r) const
{
    const QString name = torrentStateName(r.state);
    if (r.state == TorrentState::Error && !r.error.isEmpty())
        return QStringLiteral("%1 — %2").arg(name, r.error);
    if (r.state == TorrentState::Queued && r.queuePosition >= 0)
        return tr("%1 (#%2)").arg(name).arg(r.queuePosition + 1);
    return name;
}

} // namespace eMule
