#include "pch.h"
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
        case ColPacketsSent: return QStringLiteral("%1|%2").arg(s.packetsSent).arg(s.requestAnswers);
        case ColResponses:   return s.responses;
        default:             break;
        }
    }

    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColNumber:      return s.searchId;
        case ColKey:         return s.key;
        case ColType:        return s.type;
        case ColName:        return s.name;
        case ColStatus:      return s.status;
        case ColLoad:        return static_cast<double>(s.load);
        case ColPacketsSent: return s.packetsSent;  // sort by node-finding count (matching MFC)
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

void KadSearchesModel::setSearches(std::vector<KadSearchRow> incoming)
{
    // Incremental update: avoids beginResetModel()/endResetModel() so that
    // the view's selection, scroll, and sort state are preserved.

    // 1. Build lookup of incoming searches by searchId
    QHash<uint32_t, size_t> incomingById;
    incomingById.reserve(static_cast<qsizetype>(incoming.size()));
    for (size_t i = 0; i < incoming.size(); ++i)
        incomingById.insert(incoming[i].searchId, i);

    // 2. Remove departed searches (reverse order keeps indices stable)
    for (int i = static_cast<int>(m_searches.size()) - 1; i >= 0; --i) {
        if (!incomingById.contains(m_searches[static_cast<size_t>(i)].searchId)) {
            beginRemoveRows({}, i, i);
            m_searches.erase(m_searches.begin() + i);
            endRemoveRows();
        }
    }

    // 3. Update surviving rows in-place
    QSet<uint32_t> existingIds;
    existingIds.reserve(static_cast<qsizetype>(m_searches.size()));
    for (size_t i = 0; i < m_searches.size(); ++i) {
        existingIds.insert(m_searches[i].searchId);
        if (auto it = incomingById.constFind(m_searches[i].searchId); it != incomingById.cend())
            m_searches[i] = std::move(incoming[it.value()]);
    }
    if (!m_searches.empty())
        emit dataChanged(index(0, 0), index(static_cast<int>(m_searches.size()) - 1, ColCount - 1));

    // 4. Append new searches in one batch
    std::vector<KadSearchRow> toInsert;
    for (auto& row : incoming) {
        if (row.searchId != 0 && !existingIds.contains(row.searchId))
            toInsert.push_back(std::move(row));
    }
    if (!toInsert.empty()) {
        const int first = static_cast<int>(m_searches.size());
        const int last  = first + static_cast<int>(toInsert.size()) - 1;
        beginInsertRows({}, first, last);
        for (auto& r : toInsert)
            m_searches.push_back(std::move(r));
        endInsertRows();
    }
}

void KadSearchesModel::clear()
{
    beginResetModel();
    m_searches.clear();
    endResetModel();
}

} // namespace eMule
