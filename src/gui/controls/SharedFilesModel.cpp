#include "pch.h"
/// @file SharedFilesModel.cpp
/// @brief Table model for the Shared Files list — implementation.

#include "controls/SharedFilesModel.h"

#include <algorithm>

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

/// Priority display string matching MFC eMule.
QString priorityDisplay(int prio, bool isAuto)
{
    QString name;
    switch (prio) {
    case 4:  name = QObject::tr("Very Low");  break;
    case 0:  name = QObject::tr("Low");       break;
    case 1:  name = QObject::tr("Normal");    break;
    case 2:  name = QObject::tr("High");      break;
    case 3:  name = QObject::tr("Very High"); break;
    default: name = QObject::tr("Normal");    break;
    }
    if (isAuto)
        return QObject::tr("Auto [%1]").arg(name);
    return name;
}

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
    : QAbstractTableModel(parent)
{
}

int SharedFilesModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_files.size());
}

int SharedFilesModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant SharedFilesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_files.size()))
        return {};

    const auto& f = m_files[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColFileName:
            return f.fileName;
        case ColSize:
            return formatSize(f.fileSize);
        case ColType:
            return fileTypeDisplay(f.fileType);
        case ColPriority:
            return priorityDisplay(f.upPriority, f.isAutoUpPriority);
        case ColRequests:
            return QStringLiteral("%1 (%2)").arg(f.requests).arg(f.allTimeRequests);
        case ColTransferred:
            return QStringLiteral("%1 (%2)")
                .arg(formatSize(f.transferred), formatSize(f.allTimeTransferred));
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
        return QStringLiteral(
            "File Name:\t%1\n"
            "ED2K Hash:\t%2\n"
            "Type:\t%3\n"
            "Priority:\t%4\n"
            "Requests:\t%5 (%6)\n"
            "Accepted:\t%7 (%8)\n"
            "Transferred:\t%9 (%10)\n"
            "Complete Sources:\t%11\n"
            "Folder:\t%12")
            .arg(f.fileName, f.hash, fileTypeDisplay(f.fileType),
                 priorityDisplay(f.upPriority, f.isAutoUpPriority))
            .arg(f.requests).arg(f.allTimeRequests)
            .arg(f.acceptedUploads).arg(f.allTimeAccepted)
            .arg(formatSize(f.transferred), formatSize(f.allTimeTransferred))
            .arg(f.completeSources)
            .arg(f.path);
    }

    // Raw data for sorting
    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColFileName:        return f.fileName.toLower();
        case ColSize:            return QVariant::fromValue(f.fileSize);
        case ColType:            return fileTypeDisplay(f.fileType);
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

    return {};
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

void SharedFilesModel::setFiles(std::vector<SharedFileRow> files)
{
    beginResetModel();
    m_files = std::move(files);
    endResetModel();
}

void SharedFilesModel::clear()
{
    beginResetModel();
    m_files.clear();
    endResetModel();
}

QString SharedFilesModel::hashAt(int row) const
{
    if (row >= 0 && row < static_cast<int>(m_files.size()))
        return m_files[static_cast<size_t>(row)].hash;
    return {};
}

const SharedFileRow* SharedFilesModel::fileAt(int row) const
{
    if (row >= 0 && row < static_cast<int>(m_files.size()))
        return &m_files[static_cast<size_t>(row)];
    return nullptr;
}

bool SharedFilesModel::containsHash(const QString& hexHash) const
{
    return std::any_of(m_files.begin(), m_files.end(),
        [&](const SharedFileRow& r) { return r.hash == hexHash; });
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

void SharedFilesSortProxy::setIncomingDir(const QString& dir)
{
    m_incomingDir = dir;
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
    case SharedFilterType::Incoming:
        return !f->isPartFile && f->path == m_incomingDir;
    case SharedFilterType::Incomplete:
        return f->isPartFile;
    case SharedFilterType::SharedDirs:
        return !f->isPartFile && f->path != m_incomingDir;
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
