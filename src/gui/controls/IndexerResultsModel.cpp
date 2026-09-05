#include "pch.h"
/// @file IndexerResultsModel.cpp
/// @brief Table model for newznab/torznab search results — implementation.

#include "controls/IndexerResultsModel.h"

#include <QColor>
#include <QDateTime>
#include <QLocale>

namespace eMule {

namespace {

QString formatSize(int64_t bytes)
{
    if (bytes <= 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MiB")
            .arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GiB")
        .arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

/// Age reads better than a date here: what a user judges a Usenet release on is
/// "how likely is it still complete", and that is a duration, not a calendar day.
QString formatAge(int days)
{
    if (days < 0)
        return {};
    if (days == 0)
        return IndexerResultsModel::tr("today");
    if (days == 1)
        return IndexerResultsModel::tr("1 day");
    if (days < 30)
        return IndexerResultsModel::tr("%1 days").arg(days);
    if (days < 365)
        return IndexerResultsModel::tr("%1 months").arg(days / 30);
    return IndexerResultsModel::tr("%1 years").arg(days / 365);
}

} // namespace

IndexerResultsModel::IndexerResultsModel(QObject* parent)
    : AbstractTableModel<IndexerResultRow>(parent)
{
}

void IndexerResultsModel::addResults(const std::vector<IndexerResultRow>& rows)
{
    if (rows.empty())
        return;

    const int first = static_cast<int>(m_rows.size());
    beginInsertRows({}, first, first + static_cast<int>(rows.size()) - 1);
    m_rows.insert(m_rows.end(), rows.begin(), rows.end());
    endInsertRows();
}

QString IndexerResultsModel::idAt(int row) const
{
    const auto* result = rowAt(row);
    return result ? result->id : QString{};
}

QVariant IndexerResultsModel::data(const QModelIndex& index, int role) const
{
    const auto* row = rowAt(index.row());
    if (!row)
        return {};

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColTitle:    return row->title;
        case ColSize:     return formatSize(row->size);
        case ColAge:      return formatAge(row->ageDays);
        case ColCategory: return row->category;
        case ColGrabs:    return row->grabs >= 0 ? QString::number(row->grabs) : QString{};
        case ColIndexer:  return row->indexerName;
        case ColSeeders:  return row->seeders >= 0 ? QString::number(row->seeders) : QString{};
        case ColPeers:    return row->peers >= 0 ? QString::number(row->peers) : QString{};
        default:          return {};
        }
    }

    // Sort on the raw value, not the formatted string, or "9 MiB" sorts above
    // "10 GiB" and the size column is worse than useless.
    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColSize:    return QVariant::fromValue(row->size);
        case ColAge:     return row->ageDays;
        case ColGrabs:   return row->grabs;
        case ColSeeders: return row->seeders;
        case ColPeers:   return row->peers;
        default:         return data(index, Qt::DisplayRole);
        }
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColSize:
        case ColAge:
        case ColGrabs:
        case ColSeeders:
        case ColPeers:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        QStringList parts;
        parts.append(row->title);
        if (row->published > 0) {
            parts.append(tr("Posted: %1")
                             .arg(QLocale().toString(
                                 QDateTime::fromSecsSinceEpoch(row->published),
                                 QLocale::ShortFormat)));
        }
        if (row->files > 0)
            parts.append(tr("%1 files").arg(row->files));
        if (row->passwordProtected)
            parts.append(tr("Password protected"));
        return parts.join(u'\n');
    }

    // A password-protected release still needs the archive password to unpack,
    // which we have no way to supply — flagging it in the list is cheaper than
    // discovering it after the download.
    if (role == Qt::ForegroundRole && row->passwordProtected)
        return QColor(0xCC, 0x66, 0x00);

    return {};
}

QVariant IndexerResultsModel::headerData(int section, Qt::Orientation orientation,
                                         int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColTitle:    return tr("Name");
    case ColSize:     return tr("Size");
    case ColAge:      return tr("Age");
    case ColCategory: return tr("Category");
    case ColGrabs:    return tr("Grabs");
    case ColIndexer:  return tr("Indexer");
    case ColSeeders:  return tr("Seeders");
    case ColPeers:    return tr("Peers");
    default:          return {};
    }
}

} // namespace eMule
