#include "pch.h"
/// @file FriendListModel.cpp
/// @brief FriendListModel implementation.

#include "controls/FriendListModel.h"
#include "prefs/Preferences.h"
#include "utils/CountryFlags.h"

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
        if (row.network == FriendNetwork::Ed3k) {
            // the ed3k marker; a friend without a name by its node
            const QString name = row.name.isEmpty() ? row.node.left(8) : row.name;
            return tr("%1 (ed3k)").arg(name);
        }
        return row.name.isEmpty() ? row.hash : row.name;
    case Qt::DecorationRole:
        return CountryFlags::withFlag(baseIcon(row), row.cc);
    case Qt::ToolTipRole:
        if (row.network == FriendNetwork::Ed3k)
            return row.asking ? tr("ed3k friend, request pending")
                   : row.mutual ? tr("ed3k friend")
                                : tr("ed3k friend, not confirmed by the other side");
        return CountryFlags::tooltip(row.cc);
    default:
        return {};
    }
}

void FriendListModel::refreshFromCborArray(const QCborArray& arr)
{
    // was: beginResetModel(); m_rows.clear(); — the ed3k rows stay (replaceRows)
    std::vector<FriendRow> rows;
    rows.reserve(static_cast<size_t>(arr.size()));
    for (int i = 0; i < arr.size(); ++i) {
        const QCborMap m = arr[i].toMap();
        FriendRow row;
        row.hash       = m.value(QStringLiteral("hash")).toString();
        row.name       = m.value(QStringLiteral("name")).toString();
        row.ip         = m.value(QStringLiteral("ip")).toInteger();
        row.addr       = m.value(QStringLiteral("addr")).toString();
        row.cc         = m.value(QStringLiteral("cc")).toString();
        row.port       = static_cast<int>(m.value(QStringLiteral("port")).toInteger());
        row.lastSeen   = m.value(QStringLiteral("lastSeen")).toInteger();
        row.lastChatted = m.value(QStringLiteral("lastChatted")).toInteger();
        row.friendSlot = m.value(QStringLiteral("friendSlot")).toBool();
        row.kadID      = m.value(QStringLiteral("kadID")).toString();
        row.id         = row.hash;
        rows.push_back(std::move(row));
    }
    replaceRows(FriendNetwork::Ed2k, std::move(rows));
}

void FriendListModel::refreshEd3k(const QCborArray& arr)
{
    std::vector<FriendRow> rows;
    rows.reserve(static_cast<size_t>(arr.size()));
    for (const QCborValue& v : arr) {
        const QCborMap m = v.toMap();
        FriendRow row;
        row.network  = FriendNetwork::Ed3k;
        row.id       = m.value(QStringLiteral("key")).toString();
        row.node     = m.value(QStringLiteral("node")).toString();
        row.hash     = ed3kKey(row.node);
        row.name     = m.value(QStringLiteral("name")).toString();
        row.addr     = m.value(QStringLiteral("addr")).toString();
        row.lastSeen = m.value(QStringLiteral("lastSeen")).toInteger();
        row.mutual   = m.value(QStringLiteral("mutual")).toBool();
        row.asking   = m.value(QStringLiteral("asking")).toBool();
        rows.push_back(std::move(row));
    }
    replaceRows(FriendNetwork::Ed3k, std::move(rows));
}

QString FriendListModel::ed3kKey(const QString& nodeHex)
{
    return QStringLiteral("ed3k:") + nodeHex.toLower();
}

QString FriendListModel::ed3kNodeOf(const QString& key)
{
    return key.startsWith(QLatin1String("ed3k:")) ? key.mid(5) : QString();
}

QIcon FriendListModel::baseIcon(const FriendRow& row)
{
    // Cached: withFlag() keys its composite cache on the base icon's cacheKey
    static const QIcon friends1(QStringLiteral(":/icons/Friends1.ico"));
    static const QIcon friends2(QStringLiteral(":/icons/Friends2.ico"));
    static const QIcon user(QStringLiteral(":/icons/User.ico"));
    if (thePrefs.useOriginalIcons()) {
        // Friends1 = no client, Friends2 = has client info (offline), Friends3 = connected.
        // Test the address string, not row.ip: the numeric form is 0 for an IPv6 friend,
        // which would show "no client info" for a friend we know perfectly well.
        if (!row.hasAddress() && row.kadID.isEmpty())
            return friends1;
        if (row.lastSeen > 0)
            return friends2;
        return friends1;
    }
    return user;
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

void FriendListModel::replaceRows(FriendNetwork network, std::vector<FriendRow> rows)
{
    beginResetModel();
    std::erase_if(m_rows, [network](const FriendRow& r) { return r.network == network; });
    if (network == FriendNetwork::Ed2k)
        m_rows.insert(m_rows.begin(), std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
    else
        m_rows.insert(m_rows.end(), std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
    endResetModel();
}

} // namespace eMule
