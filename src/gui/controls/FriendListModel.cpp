/// @file FriendListModel.cpp
/// @brief FriendListModel implementation.

#include "controls/FriendListModel.h"

#include <QCborMap>
#include <QIcon>

namespace eMule {

FriendListModel::FriendListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int FriendListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant FriendListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const auto& row = m_rows[static_cast<size_t>(index.row())];

    switch (role) {
    case Qt::DisplayRole:
        return row.name.isEmpty() ? row.hash : row.name;
    case Qt::DecorationRole:
        return QIcon(QStringLiteral(":/icons/User.ico"));
    default:
        return {};
    }
}

void FriendListModel::refreshFromCborArray(const QCborArray& arr)
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(static_cast<size_t>(arr.size()));
    for (int i = 0; i < arr.size(); ++i) {
        const QCborMap m = arr[i].toMap();
        FriendRow row;
        row.hash       = m.value(QStringLiteral("hash")).toString();
        row.name       = m.value(QStringLiteral("name")).toString();
        row.ip         = m.value(QStringLiteral("ip")).toInteger();
        row.port       = static_cast<int>(m.value(QStringLiteral("port")).toInteger());
        row.lastSeen   = m.value(QStringLiteral("lastSeen")).toInteger();
        row.lastChatted = m.value(QStringLiteral("lastChatted")).toInteger();
        row.friendSlot = m.value(QStringLiteral("friendSlot")).toBool();
        row.kadID      = m.value(QStringLiteral("kadID")).toString();
        m_rows.push_back(std::move(row));
    }
    endResetModel();
}

const FriendRow* FriendListModel::rowAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return nullptr;
    return &m_rows[static_cast<size_t>(row)];
}

int FriendListModel::findByHash(const QString& hash) const
{
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].hash == hash)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace eMule
