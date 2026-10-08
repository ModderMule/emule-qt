#include "pch.h"
/// @file SharedFilesModel.cpp
/// @brief Table model for the Shared Files list — implementation.

#include "controls/SharedFilesModel.h"

#include "utils/FileTypeText.h"
#include "utils/OtherFunctions.h"
#include "utils/PriorityText.h"
#include "utils/RatingIcons.h"
#include "utils/StringUtils.h"

namespace eMule {

namespace {

/// Priority ordinal for sorting (higher priority = higher ordinal).
int priorityOrdinal(int prio)
{
    switch (prio) {
    case 4:  return 0; // Very Low
    case 0:  return 1; // Low
    case 1:  return 2; // Normal
    case 2:  return 3; // High
    case 3:  return 4; // Very High
    default: return 2;
    }
}

/// Shared network display string.
QString networkDisplay(bool ed2k, bool kad)
{
    if (ed2k && kad) return QStringLiteral("eD2K|Kad");
    if (ed2k)        return QStringLiteral("eD2K");
    if (kad)         return QStringLiteral("Kad");
    return {};
}

int networkOrdinal(bool ed2k, bool kad)
{
    if (ed2k && kad) return 3;
    if (kad)         return 2;
    if (ed2k)        return 1;
    return 0;
}

} // anonymous namespace

SharedFilesModel::SharedFilesModel(QObject* parent)
    : AbstractTableModel<SharedFileRow>(parent)
{
}

QVariant SharedFilesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const auto& f = m_rows[static_cast<size_t>(index.row())];

    // Type icon plus eMule's comment/rating mark, as MFC's shared list draws them
    // (srchybrid/SharedFilesCtrl.cpp:561-568), plus the port's own fake-file mark,
    // the same one the download list draws. Nothing here touches the disk: the
    // daemon's background sweep settled the verdict and sent a bool.
    if (role == Qt::DecorationRole && index.column() == ColFileName) {
        return fileMarksIcon(f.fileType, f.containerSuspect, f.ownComment,
                             ratingMark(f.hasComment, f.userRating));
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColFileName:
            return f.fileName;
        case ColSize:
            return formatByteSize(f.fileSize);
        case ColType:
            return fileTypeText(f.fileType, f.fileName);
        case ColPriority:
            return uploadPriorityText(f.upPriority, f.isAutoUpPriority);
        case ColRequests:
            return QStringLiteral("%1 (%2)").arg(f.requests).arg(f.allTimeRequests);
        case ColTransferred:
            return QStringLiteral("%1 (%2)")
                .arg(formatByteSize(f.transferred), formatByteSize(f.allTimeTransferred));
        case ColSharedParts: {
            if (f.fileSize <= 0) return QStringLiteral("0%");
            const double pct = 100.0 * static_cast<double>(f.completedSize) / static_cast<double>(f.fileSize);
            return QStringLiteral("%1%").arg(pct, 0, 'f', 0);
        }
        case ColCompleteSources:
            return f.completeSources;
        case ColSharedNetworks:
            return networkDisplay(f.publishedED2K, f.kadPublished);
        case ColFolder:
            return f.path;
        default: break;
        }
    }

    if (role == Qt::ToolTipRole) {
        const QString tip = QStringLiteral(
            "File Name:\t%1\n"
            "ED2K Hash:\t%2\n"
            "Type:\t%3\n"
            "Priority:\t%4\n"
            "Requests:\t%5 (%6)\n"
            "Accepted:\t%7 (%8)\n"
            "Transferred:\t%9 (%10)\n"
            "Complete Sources:\t%11\n"
            "Folder:\t%12")
            .arg(f.fileName, f.hash, fileTypeText(f.fileType, f.fileName),
                 uploadPriorityText(f.upPriority, f.isAutoUpPriority))
            .arg(f.requests).arg(f.allTimeRequests)
            .arg(f.acceptedUploads).arg(f.allTimeAccepted)
            .arg(formatByteSize(f.transferred), formatByteSize(f.allTimeTransferred))
            .arg(f.completeSources)
            .arg(f.path);
        // The marks in column 0 explain themselves here, worded exactly as they are
        // in the download list — same helper, same cell, same sentence.
        return QString(tip + fileMarksTooltip(f.fileName, f.containerSuspect,
                                             f.containerActual, f.hasComment, f.userRating));
    }

    // Raw data for sorting
    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColFileName:        return f.fileName.toLower();
        case ColSize:            return QVariant::fromValue(f.fileSize);
        case ColType:            return fileTypeText(f.fileType, f.fileName);
        case ColPriority:        return priorityOrdinal(f.upPriority);
        case ColRequests:        return QVariant::fromValue(f.allTimeRequests);
        case ColTransferred:     return QVariant::fromValue(f.allTimeTransferred);
        case ColSharedParts: {
            if (f.fileSize <= 0) return 0.0;
            return 100.0 * static_cast<double>(f.completedSize) / static_cast<double>(f.fileSize);
        }
        case ColCompleteSources: return f.completeSources;
        case ColSharedNetworks:  return networkOrdinal(f.publishedED2K, f.kadPublished);
        case ColFolder:          return f.path;
        default: break;
        }
    }

    if (role == SharePartMapRole && index.column() == ColSharedParts)
        return QVariant::fromValue(f.sharePartMap);

    if (role == Qt::CheckStateRole && index.column() == ColFileName && m_browseMode)
        return f.shareChecked ? Qt::Checked : Qt::Unchecked;

    return {};
}

bool SharedFilesModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::CheckStateRole || index.column() != ColFileName || !m_browseMode)
        return AbstractTableModel<SharedFileRow>::setData(index, value, role);

    const SharedFileRow* f = rowAt(index.row());
    if (!f || !f->shareToggleable || f->filePath.isEmpty())
        return false;

    // Only ask. The row keeps its old state until the daemon has actually shared or
    // excluded the file and the list is refetched — a checkbox that ticks itself and
    // then silently reverts is worse than one that waits.
    emit shareToggleRequested(f->filePath, value.toInt() == Qt::Checked);
    return true;
}

Qt::ItemFlags SharedFilesModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags f = AbstractTableModel<SharedFileRow>::flags(index);
    if (!m_browseMode || index.column() != ColFileName)
        return f;

    // The checkbox is drawn for every row, but only a toggleable one accepts a click.
    // Qt has no "checked but disabled" item state, so withholding ItemIsUserCheckable
    // is how MFC's CBS_CHECKEDDISABLED / CBS_UNCHECKEDDISABLED are expressed here.
    const SharedFileRow* row = rowAt(index.row());
    if (row && row->shareToggleable)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

void SharedFilesModel::setBrowseMode(bool on)
{
    if (m_browseMode == on)
        return;
    m_browseMode = on;
    // The checkbox lives in a column that is already populated, so the view needs to
    // repaint it — but the rows themselves are about to be replaced anyway.
    if (count() > 0)
        emit dataChanged(index(0, ColFileName), index(count() - 1, ColFileName),
                         {Qt::CheckStateRole});
}

QVariant SharedFilesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColFileName:        return tr("File Name");
    case ColSize:            return tr("Size");
    case ColType:            return tr("Type");
    case ColPriority:        return tr("Priority");
    case ColRequests:        return tr("Requests");
    case ColTransferred:     return tr("Transferred Data");
    case ColSharedParts:     return tr("Shared parts");
    case ColCompleteSources: return tr("Complete Sources");
    case ColSharedNetworks:  return tr("Shared eD2K/Kad");
    case ColFolder:          return tr("Folder");
    default:                 return {};
    }
}

QString SharedFilesModel::hashAt(int row) const
{
    if (const SharedFileRow* r = rowAt(row))
        return r->hash;
    return {};
}

bool SharedFilesModel::containsHash(const QString& hexHash) const
{
    return m_rowOf.contains(keyOf(hexHash));
}

const SharedFileRow* SharedFilesModel::findByHash(const QString& hexHash) const
{
    const auto it = m_rowOf.constFind(keyOf(hexHash));
    return it != m_rowOf.constEnd() ? rowAt(*it) : nullptr;
}

// ---------------------------------------------------------------------------
// Updates
// ---------------------------------------------------------------------------

void SharedFilesModel::reindex()
{
    m_rowOf.clear();
    m_rowOf.reserve(static_cast<qsizetype>(m_rows.size()));
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (!m_rows[i].hash.isEmpty())
            m_rowOf.insert(keyOf(m_rows[i].hash), static_cast<int>(i));
}

void SharedFilesModel::clear()
{
    AbstractTableModel::clear();
    m_rowOf.clear();
}

void SharedFilesModel::resetFiles(std::vector<SharedFileRow> files)
{
    setRows(std::move(files));
    reindex();
}

bool SharedFilesModel::setFiles(std::vector<SharedFileRow> files)
{
    // Matching is by hash; without one on every row there is nothing to match by.
    const auto hasHash = [](const SharedFileRow& r) { return !r.hash.isEmpty(); };
    if (m_rows.empty() || m_rowOf.size() != static_cast<qsizetype>(m_rows.size())
        || !std::ranges::all_of(files, hasHash)) {
        resetFiles(std::move(files));
        return true;
    }

    QHash<QString, size_t> incoming;
    incoming.reserve(static_cast<qsizetype>(files.size()));
    for (size_t i = 0; i < files.size(); ++i)
        incoming.insert(keyOf(files[i].hash), i);

    // Rows that left, bottom-up and a run at a time. Removing one by one from a
    // large list is quadratic; when most of it goes, a reset is the cheaper answer.
    size_t leaving = 0;
    for (const SharedFileRow& row : m_rows)
        if (!incoming.contains(keyOf(row.hash)))
            ++leaving;
    if (leaving > 2000 && leaving > m_rows.size() / 2) {
        resetFiles(std::move(files));
        return true;
    }
    for (int last = static_cast<int>(m_rows.size()) - 1; last >= 0 && leaving > 0; --last) {
        if (incoming.contains(keyOf(m_rows[static_cast<size_t>(last)].hash)))
            continue;
        int first = last;
        while (first > 0 && !incoming.contains(keyOf(m_rows[static_cast<size_t>(first - 1)].hash)))
            --first;
        beginRemoveRows({}, first, last);
        m_rows.erase(m_rows.begin() + first, m_rows.begin() + last + 1);
        endRemoveRows();
        leaving -= static_cast<size_t>(last - first + 1);
        last = first;
    }

    // Survivors, in place; only the rows that differ are announced.
    std::vector<bool> used(files.size(), false);
    for (size_t r = 0; r < m_rows.size(); ++r) {
        const size_t src = incoming.value(keyOf(m_rows[r].hash));
        used[src] = true;
        if (m_rows[r] == files[src])
            continue;
        m_rows[r] = std::move(files[src]);
        emit dataChanged(index(static_cast<int>(r), 0), index(static_cast<int>(r), ColCount - 1));
    }

    // Arrivals, in one batch at the end.
    const auto arrivals = static_cast<int>(std::ranges::count(used, false));
    if (arrivals > 0) {
        const int first = static_cast<int>(m_rows.size());
        beginInsertRows({}, first, first + arrivals - 1);
        for (size_t i = 0; i < files.size(); ++i)
            if (!used[i])
                m_rows.push_back(std::move(files[i]));
        endInsertRows();
    }

    reindex();
    return false;
}

void SharedFilesModel::upsertFiles(std::vector<SharedFileRow> files)
{
    std::vector<SharedFileRow> arrivals;
    for (SharedFileRow& file : files) {
        if (file.hash.isEmpty())
            continue;
        const auto it = m_rowOf.constFind(keyOf(file.hash));
        if (it == m_rowOf.constEnd()) {
            // the same file twice in one batch: the later row wins
            const auto dup = std::ranges::find(arrivals, file.hash, &SharedFileRow::hash);
            if (dup != arrivals.end())
                *dup = std::move(file);
            else
                arrivals.push_back(std::move(file));
            continue;
        }
        const int row = *it;
        if (m_rows[static_cast<size_t>(row)] == file)
            continue;
        m_rows[static_cast<size_t>(row)] = std::move(file);
        emit dataChanged(index(row, 0), index(row, ColCount - 1));
    }
    if (arrivals.empty())
        return;

    const int first = static_cast<int>(m_rows.size());
    beginInsertRows({}, first, first + static_cast<int>(arrivals.size()) - 1);
    for (SharedFileRow& file : arrivals) {
        m_rowOf.insert(keyOf(file.hash), static_cast<int>(m_rows.size()));
        m_rows.push_back(std::move(file));
    }
    endInsertRows();
}

bool SharedFilesModel::removeFile(const QString& hexHash)
{
    const auto it = m_rowOf.constFind(keyOf(hexHash));
    if (it == m_rowOf.constEnd())
        return false;
    const int row = *it;
    beginRemoveRows({}, row, row);
    m_rows.erase(m_rows.begin() + row);
    endRemoveRows();
    reindex();   // every row behind it moved up
    return true;
}

// ---------------------------------------------------------------------------
// SharedFilesSortProxy
// ---------------------------------------------------------------------------

void SharedFilesSortProxy::setFolderFilter(SharedFilterType type, const QString& path)
{
    if (m_filterType == type && m_filterPath == path)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_filterType = type;
    m_filterPath = path;
    endFilterChange();
#else
    m_filterType = type;
    m_filterPath = path;
    invalidateFilter();
#endif
}

bool SharedFilesSortProxy::filterAcceptsRow(int sourceRow, const QModelIndex& /*sourceParent*/) const
{
    auto* model = qobject_cast<SharedFilesModel*>(sourceModel());
    if (!model)
        return true;
    const auto* f = model->fileAt(sourceRow);
    if (!f)
        return false;

    switch (m_filterType) {
    case SharedFilterType::AllShared:
        return true;
    // "Incoming" is every folder finished downloads land in — the global one and
    // each category that overrides it. The daemon already answers that question
    // per file: a file it refuses to let us unshare is one of these, because
    // that is the only rule that forces sharing on
    // (IpcClientHandler's `canUnshare`). Comparing against one remembered path
    // could only ever recognise a single incoming directory.
    case SharedFilterType::Incoming:
        return !f->isPartFile && !f->shareToggleable;
    case SharedFilterType::Incomplete:
        return f->isPartFile;
    case SharedFilterType::SharedDirs:
        return !f->isPartFile && f->shareToggleable;
    case SharedFilterType::SpecificDir: {
        // Normalize: strip trailing '/' (but keep bare "/") and compare case-insensitively
        auto normalize = [](QStringView p) -> QStringView {
            if (p.size() > 1 && p.endsWith(u'/'))
                return p.chopped(1);
            return p;
        };
        return normalize(f->path).compare(normalize(m_filterPath), Qt::CaseInsensitive) == 0;
    }
    }
    return true;
}

bool SharedFilesSortProxy::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const QVariant lv = sourceModel()->data(left, Qt::UserRole);
    const QVariant rv = sourceModel()->data(right, Qt::UserRole);

    // Compare by type: int64, double, then string
    if (lv.typeId() == QMetaType::LongLong || lv.typeId() == QMetaType::Int)
        return lv.toLongLong() < rv.toLongLong();
    if (lv.typeId() == QMetaType::Double)
        return lv.toDouble() < rv.toDouble();
    return lv.toString().compare(rv.toString(), Qt::CaseInsensitive) < 0;
}

} // namespace eMule
