#include "pch.h"
#include "controls/ServerListModel.h"

#include "prefs/Preferences.h"
#include "utils/ColorUtils.h"
#include "utils/StringUtils.h"

#include <QCborMap>
#include <QColor>

namespace eMule {

namespace {

/// A fixed-width sort key for an address, because the displayed form sorts by
/// leading digit: "192.168.1.10" lands before "192.168.1.9".
///
/// An IPv6 literal has no octets to pad and is left as it is. The key stays a
/// QString either way, so the column keeps a single type and every IPv4 row
/// sorts ahead of every IPv6 one — arbitrary, but stable and grouped.
QString addressSortKey(const QString& ip, uint16_t port)
{
    QString host = ip;
    const QStringList octets = ip.split(QLatin1Char('.'));
    if (octets.size() == 4) {
        QStringList padded;
        padded.reserve(4);
        for (const QString& octet : octets)
            padded << QStringLiteral("%1").arg(octet.toUInt(), 3, 10, QLatin1Char('0'));
        host = padded.join(QLatin1Char('.'));
    }
    return QStringLiteral("%1:%2").arg(host).arg(port, 5, 10, QLatin1Char('0'));
}

/// Strength order, which the wire value is not — it is 0 Normal, 1 High, 2 Low.
/// MFC sorts this column by strength (ServerListCtrl.cpp:632-645); by name it
/// comes out High, Low, Normal.
int preferenceRank(int wireValue)
{
    switch (wireValue) {
    case 2:  return 0;   // Low
    case 1:  return 2;   // High
    default: return 1;   // Normal
    }
}

} // namespace

ServerListModel::ServerListModel(QObject* parent)
    : AbstractTableModel<ServerRow>(parent)
{
}

QVariant ServerListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const auto& r = m_rows[static_cast<size_t>(index.row())];

    // MFC ServerListCtrl.cpp:117-190: an unknown ping, user or file count is blank,
    // not 0, and Max Users is blank until the server has reported users at all.
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName:        return r.name;
        case ColIP:          return QStringLiteral("%1 : %2").arg(r.ip).arg(r.port);
        case ColDescription: return r.description;
        case ColPing:        return r.ping ? QString::number(r.ping) : QString{};
        case ColUsers:       return r.users ? formatShortNumber(r.users) : QString{};
        case ColMaxUsers:    return r.users ? formatShortNumber(r.maxUsers) : QString{};
        case ColFiles:       return r.files ? formatShortNumber(r.files) : QString{};
        case ColPreference:  return r.preference;
        case ColFailed:      return r.failed;
        case ColStatic:      return r.isStatic ? tr("Yes") : tr("No");
        case ColSoftFiles:   return formatShortNumber(r.softFiles);
        case ColLowID:       return formatShortNumber(r.lowIdUsers);
        case ColObfuscation: return r.obfuscation ? tr("Yes") : tr("No");
        default:             break;
        }
    }

    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColName:        return r.name;
        case ColIP:          return addressSortKey(r.ip, r.port);
        case ColDescription: return r.description;
        case ColPing:        return r.ping;
        case ColUsers:       return r.users;
        case ColMaxUsers:    return r.maxUsers;
        case ColFiles:       return r.files;
        case ColPreference:  return preferenceRank(r.preferenceValue);
        case ColFailed:      return r.failed;
        case ColStatic:      return r.isStatic ? 1 : 0;
        case ColSoftFiles:   return r.softFiles;
        case ColLowID:       return r.lowIdUsers;
        case ColObfuscation: return r.obfuscation ? 1 : 0;
        default:             break;
        }
    }

    // MFC ServerListCtrl.cpp:209-214: light grey once dead, grey from the second failure.
    // deadServerRetries 0 means "never remove" here, so nothing counts as dead.
    if (role == Qt::ForegroundRole) {
        if (m_connectedServerId != 0 && r.serverId == m_connectedServerId)
            return QColor(0x33, 0x99, 0xFF);
        const uint32_t deadRetries = thePrefs.deadServerRetries();
        if (deadRetries > 0 && r.failed >= deadRetries)
            return dimmedText(0.75);
        if (r.failed >= 2)
            return dimmedText(0.5);
        return {};
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColPing:
        case ColUsers:
        case ColMaxUsers:
        case ColFiles:
        case ColFailed:
        case ColSoftFiles:
        case ColLowID:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return {};
}

QVariant ServerListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColName:        return tr("Server Name");
    case ColIP:          return tr("IP");
    case ColDescription: return tr("Description");
    case ColPing:        return tr("Ping");
    case ColUsers:       return tr("Users");
    case ColMaxUsers:    return tr("Max Users");
    case ColFiles:       return tr("Files");
    case ColPreference:  return tr("Preference");
    case ColFailed:      return tr("Failed");
    case ColStatic:      return tr("Static");
    case ColSoftFiles:   return tr("Soft File Limit");
    case ColLowID:       return tr("Low ID");
    case ColObfuscation: return tr("Obfuscation");
    default:             return {};
    }
}

void ServerListModel::refreshFromCborArray(const QCborArray& servers)
{
    std::vector<ServerRow> rows;
    rows.reserve(static_cast<size_t>(servers.size()));

    for (const auto& val : servers) {
        const QCborMap m = val.toMap();
        ServerRow row;
        row.name        = m.value(QStringLiteral("name")).toString();
        row.ip          = m.value(QStringLiteral("address")).toString();
        row.port        = static_cast<uint16_t>(m.value(QStringLiteral("port")).toInteger());
        row.description = m.value(QStringLiteral("description")).toString();
        row.ping        = static_cast<uint32_t>(m.value(QStringLiteral("ping")).toInteger());
        row.users       = static_cast<uint32_t>(m.value(QStringLiteral("users")).toInteger());
        row.maxUsers    = static_cast<uint32_t>(m.value(QStringLiteral("maxUsers")).toInteger());
        row.files       = static_cast<uint32_t>(m.value(QStringLiteral("files")).toInteger());
        row.failed      = static_cast<uint32_t>(m.value(QStringLiteral("failedCount")).toInteger());
        row.isStatic    = m.value(QStringLiteral("isStatic")).toBool();
        row.softFiles   = static_cast<uint32_t>(m.value(QStringLiteral("softFiles")).toInteger());
        row.lowIdUsers  = static_cast<uint32_t>(m.value(QStringLiteral("lowIDUsers")).toInteger());
        row.obfuscation = m.value(QStringLiteral("obfuscation")).toBool();

        const int pref  = static_cast<int>(m.value(QStringLiteral("preference")).toInteger());
        row.preferenceValue = pref;
        switch (pref) {
        case 1:  row.preference = tr("High");   break;
        case 2:  row.preference = tr("Low");    break;
        default: row.preference = tr("Normal"); break;
        }

        row.numericIp = static_cast<uint32_t>(m.value(QStringLiteral("ip")).toInteger());
        row.addr      = m.value(QStringLiteral("addr")).toString();
        row.serverId  = static_cast<uint32_t>(m.value(QStringLiteral("serverId")).toInteger());
        rows.push_back(std::move(row));
    }

    setRows(std::move(rows));

    // Force repaint of foreground color after model reset so the connected
    // server row reliably shows blue through the sort proxy model.
    if (m_connectedServerId != 0 && !m_rows.empty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::ForegroundRole});
}

void ServerListModel::setConnectedServer(uint32_t serverId)
{
    if (m_connectedServerId == serverId)
        return;
    m_connectedServerId = serverId;
    // Connected state only changes the row's foreground color, not the row
    // order/count — so emit dataChanged(ForegroundRole), NOT layoutChanged().
    // A bare layoutChanged() (without a preceding layoutAboutToBeChanged())
    // violates the model contract: QSortFilterProxyModel tears down and
    // rebuilds its source mappings, leaving the view's selectionModel holding
    // a dangling proxy currentIndex. The next requestServerList() ->
    // saveSelection() then dereferences it via QModelIndex::data() and crashes
    // inside QSortFilterProxyModel::data(). Mirror refreshFromCborArray().
    if (!m_rows.empty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::ForegroundRole});
}

} // namespace eMule
