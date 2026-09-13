#include "pch.h"
/// @file IndexerResultsModel.cpp
/// @brief Table model for newznab/torznab search results — implementation.

#include "controls/IndexerResultsModel.h"
#include "controls/KnownTypeStyle.h"

#include "utils/StringUtils.h"

#include <QColor>
#include <QDateTime>
#include <QLocale>

namespace eMule {

namespace {

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

void IndexerResultsModel::updateKnownTypes(const QHash<QString, int>& typesByTitle)
{
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        auto& r = m_rows[static_cast<size_t>(i)];
        const auto it = typesByTitle.constFind(r.title);
        if (it != typesByTitle.constEnd() && r.knownType != it.value()) {
            r.knownType = it.value();
            emit dataChanged(index(i, 0), index(i, ColCount - 1));
        }
    }
}

void IndexerResultsModel::setKnownType(int row, int knownType)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    auto& r = m_rows[static_cast<size_t>(row)];
    if (r.knownType == knownType)
        return;
    r.knownType = knownType;
    emit dataChanged(index(row, 0), index(row, ColCount - 1));
}

QVariant IndexerResultsModel::data(const QModelIndex& index, int role) const
{
    const auto* row = rowAt(index.row());
    if (!row)
        return {};

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColTitle:    return row->title;
        case ColSize:     return row->size > 0 ? formatByteSize(row->size) : QString{};
        case ColAge:      return formatAge(row->ageDays);
        case ColCategory: return row->category;
        case ColGrabs:    return row->grabs >= 0 ? QString::number(row->grabs) : QString{};
        case ColIndexer:  return row->indexerName;
        case ColSeeders:  return row->seeders >= 0 ? QString::number(row->seeders) : QString{};
        case ColPeers:    return row->peers >= 0 ? QString::number(row->peers) : QString{};
        case ColKnown:    return knownTypeString(row->knownType);
        default:          return {};
        }
    }

    // Sort on the raw value, not the formatted string, or "9 MB" sorts above
    // "10 GB" and the size column is worse than useless.
    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColSize:    return QVariant::fromValue(row->size);
        case ColAge:     return row->ageDays;
        case ColGrabs:   return row->grabs;
        case ColSeeders: return row->seeders;
        case ColPeers:   return row->peers;
        case ColKnown:   return row->knownType;
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
        if (const QString known = knownTypeString(row->knownType); !known.isEmpty())
            parts.append(known);
        return parts.join(u'\n');
    }

    if (role == Qt::ForegroundRole) {
        // A password-protected release still needs the archive password to unpack,
        // which we have no way to supply — flagging it in the list is cheaper than
        // discovering it after the download. It outranks the known-type colour:
        // a release we cannot open is a stronger warning than one we already have.
        if (row->passwordProtected)
            return QColor(0xCC, 0x66, 0x00);
        if (const QColor c = knownTypeColor(row->knownType); c.isValid())
            return c;
    }

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
    case ColKnown:    return tr("Known");
    default:          return {};
    }
}

} // namespace eMule
