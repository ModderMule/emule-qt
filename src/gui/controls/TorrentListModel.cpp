#include "pch.h"
/// @file TorrentListModel.cpp
/// @brief Flat model for the Torrents tab — implementation.

#include "controls/TorrentListModel.h"

#include "utils/StringUtils.h"

#include <QColor>
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

int TorrentListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

int TorrentListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant TorrentListModel::data(const QModelIndex& index, int role) const
{
    const TorrentRow* r = index.isValid() ? rowAt(index.row()) : nullptr;
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
        if (index.column() == ColName) {
            static const QIcon icon(QStringLiteral(":/icons/Torrent.ico"));
            return icon;
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
        m_rows.erase(m_rows.begin() + row);
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
    endInsertRows();
}

void TorrentListModel::removeRow(const QString& id)
{
    const int at = rowOf(id);
    if (at < 0)
        return;
    beginRemoveRows({}, at, at);
    m_rows.erase(m_rows.begin() + at);
    endRemoveRows();
}

void TorrentListModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
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
