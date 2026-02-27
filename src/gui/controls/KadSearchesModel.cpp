#include "controls/KadSearchesModel.h"

namespace eMule {

KadSearchesModel::KadSearchesModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int KadSearchesModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_searches.size());
}

int KadSearchesModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant KadSearchesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_searches.size()))
        return {};

    const auto& s = m_searches[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColNumber:      return s.searchId;
        case ColKey:         return s.key;
        case ColType:        return s.type;
        case ColName:        return s.name;
        case ColStatus:      return s.status;
        case ColLoad:        return QStringLiteral("%1 (0.00)").arg(s.load, 0, 'f', 0);
        case ColPacketsSent: return s.packetsSent;
        case ColResponses:   return s.responses;
        default:             break;
        }
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColLoad:
        case ColPacketsSent:
        case ColResponses:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return {};
}

QVariant KadSearchesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColNumber:      return tr("No.");
    case ColKey:         return tr("Key");
    case ColType:        return tr("Type");
    case ColName:        return tr("Name");
    case ColStatus:      return tr("Status");
    case ColLoad:        return tr("Load");
    case ColPacketsSent: return tr("Packets Sent");
    case ColResponses:   return tr("Responses");
    default:             return {};
    }
}

void KadSearchesModel::setSearches(std::vector<KadSearchRow> searches)
{
    beginResetModel();
    m_searches = std::move(searches);
    endResetModel();
}

void KadSearchesModel::clear()
{
    beginResetModel();
    m_searches.clear();
    endResetModel();
}

} // namespace eMule
