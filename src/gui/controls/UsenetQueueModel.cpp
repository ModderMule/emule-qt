#include "pch.h"
#include "controls/UsenetQueueModel.h"

#include <QDateTime>
#include <QFont>

#include <algorithm>

namespace eMule {

namespace {

/// Same shape as DownloadListModel's local helpers, deliberately: the two lists
/// sit one toolbar button apart and a user reads them the same way.
QString formatSize(qint64 bytes)
{
    if (bytes < 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KiB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MiB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GiB").arg(double(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString formatSpeed(qint64 bytesPerSec)
{
    if (bytesPerSec <= 0)
        return {};
    if (bytesPerSec < 1024)
        return QStringLiteral("%1 B/s").arg(bytesPerSec);
    return QStringLiteral("%1 KiB/s").arg(double(bytesPerSec) / 1024.0, 0, 'f', 1);
}

} // namespace

UsenetQueueModel::UsenetQueueModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

// ---------------------------------------------------------------------------
// QAbstractItemModel
// ---------------------------------------------------------------------------

QModelIndex UsenetQueueModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    // Top-level rows carry internalId 0; a child carries its parent's row + 1.
    if (!parent.isValid())
        return createIndex(row, column, quintptr(0));

    if (parent.internalId() == 0)
        return createIndex(row, column, quintptr(parent.row() + 1));

    return {};   // no deeper nesting
}

QModelIndex UsenetQueueModel::parent(const QModelIndex& index) const
{
    if (!index.isValid())
        return {};

    const quintptr id = index.internalId();
    if (id == 0)
        return {};

    return createIndex(int(id - 1), 0, quintptr(0));
}

int UsenetQueueModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return int(m_items.size());

    if (parent.internalId() == 0) {
        const int row = parent.row();
        if (row >= 0 && row < int(m_items.size()))
            return int(m_items[size_t(row)].files.size());
    }
    return 0;
}

int UsenetQueueModel::columnCount(const QModelIndex&) const
{
    return ColCount;
}

bool UsenetQueueModel::hasChildren(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return !m_items.empty();
    if (parent.internalId() == 0) {
        const int row = parent.row();
        if (row >= 0 && row < int(m_items.size()))
            return !m_items[size_t(row)].files.isEmpty();
    }
    return false;
}

QVariant UsenetQueueModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};

    // -- File row -----------------------------------------------------------
    if (index.internalId() != 0) {
        const int parentRow = int(index.internalId() - 1);
        if (parentRow < 0 || parentRow >= int(m_items.size()))
            return {};
        const auto& files = m_items[size_t(parentRow)].files;
        if (index.row() < 0 || index.row() >= files.size())
            return {};
        const UsenetFileRow& f = files.at(index.row());

        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case ColName:     return f.name;
            case ColSize:     return formatSize(f.size);
            case ColProgress: return QStringLiteral("%1%").arg(f.percent);
            case ColStatus:
                if (f.missingSegments > 0)
                    return tr("%n article(s) missing", nullptr, f.missingSegments);
                return f.percent >= 100 ? tr("Complete") : QString();
            default:          return {};
            }
        }
        if (role == Qt::ToolTipRole && !f.finalPath.isEmpty())
            return f.finalPath;
        if (role == Qt::ForegroundRole && f.isPar2)
            return QVariant::fromValue(QColor(0x88, 0x88, 0x88));
        return {};
    }

    // -- Item row -----------------------------------------------------------
    if (index.row() < 0 || index.row() >= int(m_items.size()))
        return {};
    const UsenetItemRow& it = m_items[size_t(index.row())];

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColName:      return it.name;
        case ColSize:      return formatSize(it.totalBytes);
        case ColProgress:
            // During post-processing the segment percentage is pinned at 100 and
            // tells the user nothing; show the stage's own progress instead.
            return isPostProcessing(it.status)
                ? QStringLiteral("%1%").arg(it.postPercent)
                : QStringLiteral("%1%").arg(it.percent);
        case ColStatus:
            if (!it.error.isEmpty())
                return QStringLiteral("%1 — %2").arg(it.statusText, it.error);
            if (isPostProcessing(it.status) && !it.postDetail.isEmpty())
                return QStringLiteral("%1 — %2").arg(it.statusText, it.postDetail);
            return it.statusText;
        case ColSpeed:     return formatSpeed(it.speed);
        case ColRemaining: return formatSize(it.totalBytes - it.decodedBytes);
        case ColPriority:  return it.priority;
        default:           return {};
        }

    case Qt::ToolTipRole:
        if (it.missingSegments > 0) {
            return tr("%1\n%n article(s) could not be found on any server",
                      nullptr, it.missingSegments).arg(it.name);
        }
        return it.name;

    case Qt::FontRole:
        if (it.status == UsenetRowStatus::Downloading || isPostProcessing(it.status)) {
            QFont f;
            f.setBold(true);
            return f;
        }
        return {};

    case Qt::ForegroundRole:
        if (it.status == UsenetRowStatus::Failed)
            return QVariant::fromValue(QColor(0xCC, 0x00, 0x00));
        if (it.status == UsenetRowStatus::Paused)
            return QVariant::fromValue(QColor(0x88, 0x88, 0x88));
        if (isPostProcessing(it.status))
            return QVariant::fromValue(QColor(0x33, 0x99, 0xFF));
        return {};

    default:
        return {};
    }
}

QVariant UsenetQueueModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColName:      return tr("Name");
    case ColSize:      return tr("Size");
    case ColProgress:  return tr("Progress");
    case ColStatus:    return tr("Status");
    case ColSpeed:     return tr("Speed");
    case ColRemaining: return tr("Remaining");
    case ColPriority:  return tr("Priority");
    default:           return {};
    }
}

// ---------------------------------------------------------------------------
// Updates
// ---------------------------------------------------------------------------

void UsenetQueueModel::setItems(const QList<UsenetItemRow>& items)
{
    // Phase 1: drop departed rows, bottom-up. internalId encodes the parent's row
    // index, so removing top-down invalidates every surviving child's id.
    for (int row = int(m_items.size()) - 1; row >= 0; --row) {
        const QString& id = m_items[size_t(row)].id;
        const bool stillThere = std::any_of(items.cbegin(), items.cend(),
                                            [&id](const UsenetItemRow& r) { return r.id == id; });
        if (stillThere)
            continue;

        beginRemoveRows({}, row, row);
        m_items.erase(m_items.begin() + row);
        endRemoveRows();
    }

    // Phase 2: update survivors in place.
    for (const UsenetItemRow& incoming : items) {
        const int row = rowOf(incoming.id);
        if (row < 0)
            continue;

        const int oldFileCount = int(m_items[size_t(row)].files.size());
        applyInto(m_items[size_t(row)], incoming);
        const int newFileCount = int(m_items[size_t(row)].files.size());

        if (oldFileCount != newFileCount) {
            // The file list only ever grows once, when the NZB is first parsed,
            // but a layout change is the honest signal for it.
            emit dataChanged(index(row, 0), index(row, ColCount - 1));
            beginResetModel();
            endResetModel();
            return;
        }
        emit dataChanged(index(row, 0), index(row, ColCount - 1));

        if (newFileCount > 0) {
            const QModelIndex p = index(row, 0);
            emit dataChanged(index(0, 0, p), index(newFileCount - 1, ColCount - 1, p));
        }
    }

    // Phase 3: append arrivals in one batch.
    QList<UsenetItemRow> arrivals;
    for (const UsenetItemRow& incoming : items) {
        if (rowOf(incoming.id) < 0)
            arrivals.append(incoming);
    }
    if (!arrivals.isEmpty()) {
        const int first = int(m_items.size());
        beginInsertRows({}, first, first + int(arrivals.size()) - 1);
        for (const UsenetItemRow& r : arrivals) {
            UsenetItemRow copy = r;
            copy.lastDecodedBytes = r.decodedBytes;
            copy.lastSampleMs = QDateTime::currentMSecsSinceEpoch();
            m_items.push_back(std::move(copy));
        }
        endInsertRows();
    }
}

void UsenetQueueModel::upsertItem(const UsenetItemRow& item)
{
    const int row = rowOf(item.id);
    if (row >= 0) {
        const int oldFileCount = int(m_items[size_t(row)].files.size());
        applyInto(m_items[size_t(row)], item);
        if (int(m_items[size_t(row)].files.size()) != oldFileCount) {
            beginResetModel();
            endResetModel();
            return;
        }
        emit dataChanged(index(row, 0), index(row, ColCount - 1));
        if (!m_items[size_t(row)].files.isEmpty()) {
            const QModelIndex p = index(row, 0);
            emit dataChanged(index(0, 0, p),
                             index(int(m_items[size_t(row)].files.size()) - 1, ColCount - 1, p));
        }
        return;
    }

    const int first = int(m_items.size());
    beginInsertRows({}, first, first);
    UsenetItemRow copy = item;
    copy.lastDecodedBytes = item.decodedBytes;
    copy.lastSampleMs = QDateTime::currentMSecsSinceEpoch();
    m_items.push_back(std::move(copy));
    endInsertRows();
}

void UsenetQueueModel::removeItem(const QString& id)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_items.erase(m_items.begin() + row);
    endRemoveRows();
}

void UsenetQueueModel::clear()
{
    if (m_items.empty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
}

bool UsenetQueueModel::isFileRow(const QModelIndex& index) const
{
    return index.isValid() && index.internalId() != 0;
}

const UsenetFileRow* UsenetQueueModel::fileAt(const QModelIndex& index, QString* itemId) const
{
    if (!isFileRow(index))
        return nullptr;

    const int parentRow = int(index.internalId() - 1);
    if (parentRow < 0 || parentRow >= int(m_items.size()))
        return nullptr;

    const UsenetItemRow& item = m_items.at(size_t(parentRow));
    if (index.row() < 0 || index.row() >= item.files.size())
        return nullptr;

    if (itemId)
        *itemId = item.id;
    return &item.files.at(index.row());
}

QString UsenetQueueModel::idAt(int row) const
{
    if (row < 0 || row >= int(m_items.size()))
        return {};
    return m_items[size_t(row)].id;
}

const UsenetItemRow* UsenetQueueModel::findById(const QString& id) const
{
    const int row = rowOf(id);
    return row < 0 ? nullptr : &m_items[size_t(row)];
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void UsenetQueueModel::applyInto(UsenetItemRow& target, const UsenetItemRow& incoming)
{
    // Derive the rate before overwriting the sample it is measured against. The
    // daemon reports the engine's total rate, not a per-item one, so an item's
    // own speed can only come from its own byte counter moving.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = nowMs - target.lastSampleMs;
    if (target.lastSampleMs > 0 && elapsed >= 500) {
        const qint64 delta = incoming.decodedBytes - target.lastDecodedBytes;
        target.speed = delta > 0 ? delta * 1000 / elapsed : 0;
        target.lastDecodedBytes = incoming.decodedBytes;
        target.lastSampleMs = nowMs;
    } else if (target.lastSampleMs == 0) {
        target.lastDecodedBytes = incoming.decodedBytes;
        target.lastSampleMs = nowMs;
    }

    const qint64 speed = target.speed;
    const qint64 lastBytes = target.lastDecodedBytes;
    const qint64 lastMs = target.lastSampleMs;

    target = incoming;

    target.speed = incoming.status == UsenetRowStatus::Downloading ? speed : 0;
    target.lastDecodedBytes = lastBytes;
    target.lastSampleMs = lastMs;
}

int UsenetQueueModel::rowOf(const QString& id) const
{
    for (int i = 0; i < int(m_items.size()); ++i) {
        if (m_items[size_t(i)].id == id)
            return i;
    }
    return -1;
}

} // namespace eMule
