#include "pch.h"
#include "controls/UsenetQueueModel.h"

#include "utils/MenuUtils.h"
#include "utils/StringUtils.h"

#include <QCborArray>
#include <QDateTime>
#include <QFont>
#include <QIcon>
#include <QLocale>

#include <algorithm>

namespace eMule {

namespace {

/// Most finished first, like the item rows; a file that cannot finish sorts last.
int fileStateRank(UsenetFileRowState s)
{
    switch (s) {
    case UsenetFileRowState::Complete: return 0;
    case UsenetFileRowState::Partial:  return 1;
    case UsenetFileRowState::Queued:   return 2;
    case UsenetFileRowState::Held:     return 3;
    case UsenetFileRowState::Skipped:  return 4;
    case UsenetFileRowState::Missing:  return 5;
    }
    return 2;
}

} // namespace

UsenetRowStatus usenetStatusFromInt(int v)
{
    switch (v) {
    case 1: return UsenetRowStatus::Downloading;
    case 2: return UsenetRowStatus::Paused;
    case 3: return UsenetRowStatus::Complete;
    case 4: return UsenetRowStatus::Failed;
    case 5: return UsenetRowStatus::Verifying;
    case 6: return UsenetRowStatus::Repairing;
    case 7: return UsenetRowStatus::Unpacking;
    case 8: return UsenetRowStatus::Checking;
    default: return UsenetRowStatus::Queued;
    }
}

UsenetItemRow usenetRowFromCbor(const QCborMap& m)
{
    UsenetItemRow r;
    r.id = m.value(QStringLiteral("id")).toString();
    r.name = m.value(QStringLiteral("name")).toString();
    r.status = usenetStatusFromInt(int(m.value(QStringLiteral("status")).toInteger()));
    r.statusText = m.value(QStringLiteral("statusText")).toString();
    r.postPercent = int(m.value(QStringLiteral("postPercent")).toInteger(0));
    r.postDetail = m.value(QStringLiteral("postDetail")).toString();
    r.stalledReason = m.value(QStringLiteral("stalledReason")).toString();
    // -1 is the daemon's "not assessed", and the default here has to agree with
    // it: an older daemon sends no key at all, and 0 would render as 0% health.
    r.healthPercent = int(m.value(QStringLiteral("healthPercent")).toInteger(-1));
    r.healthMissingBytes = m.value(QStringLiteral("healthMissingBytes")).toInteger();
    r.healthRecoveryBytes = m.value(QStringLiteral("healthRecoveryBytes")).toInteger();
    r.healthProbed = m.value(QStringLiteral("healthProbed")).toBool();
    r.priority = int(m.value(QStringLiteral("priority")).toInteger());
    r.category = int(m.value(QStringLiteral("category")).toInteger());
    r.percent = int(m.value(QStringLiteral("percent")).toInteger());
    r.totalBytes = m.value(QStringLiteral("totalBytes")).toInteger();
    r.decodedBytes = m.value(QStringLiteral("decodedBytes")).toInteger();
    r.segmentCount = int(m.value(QStringLiteral("segmentCount")).toInteger());
    r.doneSegments = int(m.value(QStringLiteral("doneSegments")).toInteger());
    r.missingSegments = int(m.value(QStringLiteral("missingSegments")).toInteger());
    r.error = m.value(QStringLiteral("error")).toString();
    // Both default false, which is what an older daemon's absent key should
    // mean: no password stored, and no reason to think one is wanted.
    r.hasPassword = m.value(QStringLiteral("hasPassword")).toBool();
    r.passwordRequired = m.value(QStringLiteral("passwordRequired")).toBool();

    const QCborArray files = m.value(QStringLiteral("files")).toArray();
    r.files.reserve(files.size());
    for (const auto& fv : files) {
        const QCborMap fm = fv.toMap();
        UsenetFileRow f;
        f.name = fm.value(QStringLiteral("name")).toString();
        f.size = fm.value(QStringLiteral("size")).toInteger();
        f.percent = int(fm.value(QStringLiteral("percent")).toInteger());
        f.finalPath = fm.value(QStringLiteral("finalPath")).toString();
        f.isPar2 = fm.value(QStringLiteral("isPar2")).toBool();
        f.missingSegments = int(fm.value(QStringLiteral("missingSegments")).toInteger());
        f.index = int(fm.value(QStringLiteral("index")).toInteger(-1));
        f.previewable = fm.value(QStringLiteral("previewable")).toBool();
        f.previewNote = fm.value(QStringLiteral("previewNote")).toString();
        f.skipped = fm.value(QStringLiteral("skipped")).toBool();
        f.segmentMap = fm.value(QStringLiteral("segmentMap")).toByteArray();
        if (const QCborValue state = fm.value(QStringLiteral("state")); state.isInteger()) {
            f.state = static_cast<UsenetFileRowState>(std::clamp<qint64>(state.toInteger(), 0, 5));
        } else {
            // A daemon that sends no state: the nearest reading of what it did send.
            const bool finished = f.percent >= 100 || fm.value(QStringLiteral("finalized")).toBool();
            f.state = finished        ? (f.missingSegments > 0 ? UsenetFileRowState::Missing
                                                               : UsenetFileRowState::Complete)
                      : f.percent > 0 ? UsenetFileRowState::Partial
                                      : UsenetFileRowState::Queued;
        }
        r.files.append(f);
    }

    const QCborArray published = m.value(QStringLiteral("publishedFiles")).toArray();
    r.publishedFiles.reserve(published.size());
    for (const auto& pv : published) {
        const QCborMap pm = pv.toMap();
        UsenetPublishedFile pf;
        pf.name = pm.value(QStringLiteral("name")).toString();
        pf.path = pm.value(QStringLiteral("path")).toString();
        pf.relPath = pm.value(QStringLiteral("relPath")).toString();
        pf.size = pm.value(QStringLiteral("size")).toInteger();
        r.publishedFiles.append(pf);
    }

    r.bar = usenetItemBar(r);
    return r;
}

QByteArray usenetFileBar(const UsenetFileRow& file)
{
    if (!file.segmentMap.isEmpty())
        return file.segmentMap;

    switch (file.state) {
    case UsenetFileRowState::Complete: return QByteArray(1, char(kUsenetBarDone));
    case UsenetFileRowState::Missing:  return QByteArray(1, char(kUsenetBarMissing));
    case UsenetFileRowState::Skipped:
    case UsenetFileRowState::Held:     return QByteArray(1, char(kUsenetBarSkipped));
    case UsenetFileRowState::Partial: {
        // No map from the daemon, which only an older one omits. The share done
        // is still worth drawing.
        QByteArray bar(100, char(kUsenetBarQueued));
        std::fill_n(bar.begin(), std::clamp(file.percent, 0, 100), char(kUsenetBarDone));
        return bar;
    }
    case UsenetFileRowState::Queued:
        break;
    }
    return QByteArray(1, char(kUsenetBarQueued));
}

QByteArray usenetItemBar(const UsenetItemRow& item)
{
    constexpr int kBuckets = 128;

    // Held recovery volumes are left out: a tenth of a release that will mostly
    // never download would read as a permanent blue tail.
    qint64 total = 0;
    for (const UsenetFileRow& f : item.files) {
        if (f.state != UsenetFileRowState::Held)
            total += std::max<qint64>(f.size, 1);
    }
    if (total <= 0)
        return {};

    // Worst first, the order one file's map uses: missing, in flight, queued.
    const auto rank = [](quint8 code) {
        switch (code) {
        case kUsenetBarMissing:  return 4;
        case kUsenetBarInFlight: return 3;
        case kUsenetBarQueued:   return 2;
        case kUsenetBarSkipped:  return 1;
        default:                 return 0;
        }
    };

    QByteArray out(kBuckets, char(kUsenetBarDone));
    QList<bool> written(kBuckets, false);
    qint64 offset = 0;
    for (const UsenetFileRow& f : item.files) {
        if (f.state == UsenetFileRowState::Held)
            continue;
        const qint64 size = std::max<qint64>(f.size, 1);
        const QByteArray bar = usenetFileBar(f);
        const int first = int(offset * kBuckets / total);
        const int end = std::max(first + 1, int((offset + size) * kBuckets / total));
        for (int b = first; b < end && b < kBuckets; ++b) {
            const qsizetype at = std::min<qsizetype>(qint64(b - first) * bar.size() / (end - first),
                                                     bar.size() - 1);
            const quint8 code = quint8(bar.at(at));
            if (!written[b] || rank(code) > rank(quint8(out.at(b)))) {
                out[b] = char(code);
                written[b] = true;
            }
        }
        offset += size;
    }
    return out;
}

QString usenetFileStateText(const UsenetFileRow& file, bool itemActive)
{
    switch (file.state) {
    case UsenetFileRowState::Skipped:  return QObject::tr("Skipped");
    case UsenetFileRowState::Held:     return QObject::tr("Held back — fetched if a repair needs it");
    case UsenetFileRowState::Missing:
        return QObject::tr("%n article(s) missing", nullptr, file.missingSegments);
    case UsenetFileRowState::Complete: return QObject::tr("Complete");
    case UsenetFileRowState::Partial:
        if (file.missingSegments > 0)
            return QObject::tr("%n article(s) missing", nullptr, file.missingSegments);
        return itemActive ? QObject::tr("Downloading") : QString();
    case UsenetFileRowState::Queued:
        return itemActive ? QObject::tr("Queued") : QString();
    }
    return {};
}

QIcon usenetFileStateIcon(const UsenetFileRow& file, bool itemActive)
{
    switch (file.state) {
    case UsenetFileRowState::Partial:  return itemActive ? menuIcon("Download.ico") : QIcon();
    case UsenetFileRowState::Queued:   return menuIcon("ClientsOnQueue.ico");
    case UsenetFileRowState::Missing:  return menuIcon("Cancel.ico");
    case UsenetFileRowState::Skipped:  return menuIcon("Pause.ico");
    case UsenetFileRowState::Complete:
    case UsenetFileRowState::Held:     break;
    }
    return {};
}

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
        const UsenetItemRow& owner = m_items[size_t(parentRow)];
        const bool ownerActive = owner.status == UsenetRowStatus::Downloading
                                 || owner.status == UsenetRowStatus::Queued;

        if (index.column() == ColProgress) {
            switch (role) {
            case kUsenetBarRole:         return usenetFileBar(f);
            case kUsenetBarPercentRole:  return f.percent;
            case kUsenetBarPausedRole:
                return owner.status == UsenetRowStatus::Paused
                       || owner.status == UsenetRowStatus::Failed;
            case kUsenetBarCompleteRole: return f.state == UsenetFileRowState::Complete;
            case Qt::ToolTipRole:
                return f.missingSegments > 0
                           ? tr("%1% — %n article(s) missing", nullptr, f.missingSegments)
                                 .arg(f.percent)
                           : QStringLiteral("%1%").arg(f.percent);
            default:
                break;
            }
        }

        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case ColName:     return f.name;
            case ColSize:     return formatByteSize(f.size);
            case ColProgress: return QStringLiteral("%1%").arg(f.percent);
            case ColStatus:   return usenetFileStateText(f, ownerActive);
            default:          return {};
            }
        }
        if (role == Qt::UserRole) {
            switch (index.column()) {
            case ColName:     return f.name;
            case ColSize:     return QVariant::fromValue(f.size);
            case ColProgress: return f.percent;
            case ColStatus:   return fileStateRank(f.state);
            default:
                // Every other column belongs to the item row and a file leaves it
                // blank. Sorting on the NZB position keeps the children in posting
                // order there, instead of making them all equal and letting the
                // sort shuffle a release's parts.
                return f.index;
            }
        }
        // Checked means "downloads". PAR2 files have no box: the queue fetches
        // them when a repair needs them, which is not the user's call per file.
        if (role == Qt::CheckStateRole && index.column() == ColName && !f.isPar2)
            return int(f.skipped ? Qt::Unchecked : Qt::Checked);
        if (role == Qt::DecorationRole && index.column() == ColStatus)
            return usenetFileStateIcon(f, ownerActive);
        if (role == Qt::ToolTipRole) {
            if (index.column() == ColName && f.skipped)
                return tr("Skipped — tick it to download this file");
            if (!f.finalPath.isEmpty())
                return f.finalPath;
            return {};
        }
        if (role == Qt::ForegroundRole
            && (f.isPar2 || f.state == UsenetFileRowState::Skipped)) {
            return QVariant::fromValue(QColor(0x88, 0x88, 0x88));
        }
        return {};
    }

    // -- Item row -----------------------------------------------------------
    if (index.row() < 0 || index.row() >= int(m_items.size()))
        return {};
    const UsenetItemRow& it = m_items[size_t(index.row())];

    if (index.column() == ColProgress) {
        switch (role) {
        case kUsenetBarRole:         return it.bar;
        // Whichever number the cell is about: a repair moves no articles.
        case kUsenetBarPercentRole:  return isPostProcessing(it.status) ? it.postPercent
                                                                        : it.percent;
        case kUsenetBarPausedRole:
            return it.status == UsenetRowStatus::Paused || it.status == UsenetRowStatus::Failed;
        case kUsenetBarCompleteRole: return it.status == UsenetRowStatus::Complete;
        case Qt::ToolTipRole: {
            const QLocale locale;
            QString tip = tr("%1% — %2 of %3 articles")
                              .arg(QString::number(it.percent), locale.toString(it.doneSegments),
                                   locale.toString(it.segmentCount));
            if (it.missingSegments > 0)
                tip += tr(", %n missing", nullptr, it.missingSegments);
            return tip;
        }
        default:
            break;
        }
    }

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColName:      return it.name;
        case ColSize:      return formatByteSize(it.totalBytes);
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
            if (!it.stalledReason.isEmpty())
                return QStringLiteral("%1 — %2").arg(it.statusText, it.stalledReason);
            return it.statusText;
        case ColSpeed:     return it.speed > 0 ? formatByteRate(it.speed) : QString{};
        case ColRemaining: return formatByteSize(it.totalBytes - it.decodedBytes);
        case ColPriority:  return usenetPriorityName(it.priority);
        case ColHealth:
            // A dash, not "100%": -1 means nothing was ever asked, and showing
            // that as full health is the one way this column can lie.
            if (it.healthPercent < 0)
                return QStringLiteral("—");
            if (it.healthPercent >= 100)
                return QStringLiteral("100%");
            return QStringLiteral("%1%").arg(it.healthPercent);
        case ColCategory:
            // Blank, not "All": index 0 is not a category a release is *in*, it
            // is the absence of one. Same call DownloadListModel makes.
            if (it.category <= 0)
                return QString{};
            // The name, not the index. A release can name a category the list no
            // longer holds — a sidecar written before it was deleted, or another
            // GUI editing the list — and the bare number says more than a blank.
            return it.category < m_categoryNames.size()
                       ? m_categoryNames.at(it.category)
                       : QString::number(it.category);
        default:           return {};
        }

    case Qt::UserRole:
        // Sort on the raw value, not on the formatted string, or "9.90 MB" ranks
        // above "10.00 GB", "7%" above "100%", and a B/s row interleaves with the
        // KB/s ones. Same split DownloadListModel makes one toolbar button away.
        switch (index.column()) {
        case ColSize:      return QVariant::fromValue(it.totalBytes);
        case ColProgress:
            // Whichever number the cell is actually showing.
            return isPostProcessing(it.status) ? it.postPercent : it.percent;
        case ColStatus:    return statusRank(it);
        case ColSpeed:     return QVariant::fromValue(it.speed);
        case ColRemaining: return QVariant::fromValue(it.totalBytes - it.decodedBytes);
        case ColPriority:
            // The number, now that the cell shows a word: sorting "Very high"
            // and "Very low" alphabetically puts them next to each other.
            return it.priority;
        case ColHealth:
            // -1 and not 0: "never assessed" is a third state, and it belongs at
            // one clean end of the order rather than mixed in with the ruins.
            return it.healthPercent;
        case ColCategory:
            // The index, not the name — the user's own tab order, and the call
            // DownloadListModel makes.
            return it.category;
        default:
            // Name already displays exactly what it sorts by.
            return data(index, Qt::DisplayRole);
        }

    case Qt::ToolTipRole: {
        if (index.column() == ColHealth) {
            if (it.healthPercent < 0)
                return tr("Not checked.");
            QString note = it.healthProbed
                               ? tr("%1% of this release looks obtainable.")
                                     .arg(it.healthPercent)
                               : tr("%1% by the NZB's own article counts. "
                                    "No server was asked.")
                                     .arg(it.healthPercent);
            if (it.healthMissingBytes > 0 && it.healthRecoveryBytes >= it.healthMissingBytes) {
                // Said out loud, because a percentage on its own reads as damage
                // when the release ships enough recovery data to repair it.
                note += QLatin1Char('\n')
                        + tr("The PAR2 recovery volumes should cover the shortfall.");
            }
            return note;
        }
        if (it.passwordRequired) {
            // The one failure a user can act on from here, so it says what to do
            // rather than what happened.
            return it.hasPassword
                       ? tr("%1\nThe password for this release did not work. "
                            "Right-click to set a different one.").arg(it.name)
                       : tr("%1\nThis release is password-protected. "
                            "Right-click to set its password.").arg(it.name);
        }
        if (it.missingSegments > 0) {
            return tr("%1\n%n article(s) could not be found on any server",
                      nullptr, it.missingSegments).arg(it.name);
        }
        if (it.hasPassword)
            return tr("%1\nA password is set for this release.").arg(it.name);
        return it.name;
    }

    case Qt::DecorationRole:
        // A padlock on the name rather than a column of its own: it is a
        // property of a handful of releases, and a column would be blank for
        // every other row. menuIcon() returns a null icon when the user has
        // turned the original artwork off, which renders as no icon at all.
        if (index.column() == ColName && (it.hasPassword || it.passwordRequired))
            return menuIcon("Security.ico");
        return {};

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
        // What CategoryFilterProxy reads. Answered on every column, because the
        // proxy asks column 0 and a caller inspecting a selected cell may be on
        // any of them.
        if (role == kCategoryRole)
            return it.category;
        return {};
    }
}

void UsenetQueueModel::setCategoryNames(QStringList names)
{
    if (m_categoryNames == names)
        return;

    m_categoryNames = std::move(names);

    // Only the one column changed, and only its text: a reset here would destroy
    // the view's selection and its expanded rows for a rename.
    if (!m_items.empty()) {
        emit dataChanged(index(0, ColCategory), index(rowCount() - 1, ColCategory),
                         {Qt::DisplayRole});
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
    case ColHealth:    return tr("Health");
    case ColCategory:  return tr("Category");
    default:           return {};
    }
}

Qt::ItemFlags UsenetQueueModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags result = QAbstractItemModel::flags(index);
    if (index.column() != ColName)
        return result;

    const UsenetFileRow* f = fileAt(index);
    if (!f || f->isPar2)
        return result;
    // fileAt() has validated the parent row.
    if (usenetItemAcceptsSkip(m_items[size_t(index.internalId() - 1)].status))
        result |= Qt::ItemIsUserCheckable;
    return result;
}

bool UsenetQueueModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::CheckStateRole || index.column() != ColName)
        return false;

    QString itemId;
    const UsenetFileRow* f = fileAt(index, &itemId);
    if (!f || f->isPar2)
        return false;

    const bool skip = value.toInt() == Qt::Unchecked;
    if (skip != f->skipped)
        emit fileSkipRequested(itemId, f->index, skip);
    return false;
}

// ---------------------------------------------------------------------------
// Updates
// ---------------------------------------------------------------------------

QString usenetPriorityName(int priority)
{
    switch (qBound(-2, priority, 2)) {
    case 2:  return QObject::tr("Very high");
    case 1:  return QObject::tr("High");
    case -1: return QObject::tr("Low");
    case -2: return QObject::tr("Very low");
    default: return QObject::tr("Normal");
    }
}

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
            copy.rateSampler.update(r.decodedBytes, QDateTime::currentMSecsSinceEpoch());
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
    copy.rateSampler.update(item.decodedBytes, QDateTime::currentMSecsSinceEpoch());
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
    ByteRateSampler sampler = target.rateSampler;
    const qint64 speed = sampler.update(incoming.decodedBytes, QDateTime::currentMSecsSinceEpoch());

    target = incoming;

    target.speed = incoming.status == UsenetRowStatus::Downloading ? speed : 0;
    target.rateSampler = sampler;
}

int UsenetQueueModel::statusRank(const UsenetItemRow& item)
{
    // Most finished first, so one click puts what needs attention at the bottom
    // and what is done at the top. Shared with the web UI's sort.
    return usenetStatusRank(int(item.status));
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
