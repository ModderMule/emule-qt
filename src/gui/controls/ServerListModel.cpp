#include "controls/ServerListModel.h"

#include "server/Server.h"
#include "server/ServerList.h"

namespace eMule {

namespace {

QString priorityString(ServerPriority pref)
{
    switch (pref) {
    case ServerPriority::High:   return QObject::tr("High");
    case ServerPriority::Low:    return QObject::tr("Low");
    case ServerPriority::Normal:
    default:                     return QObject::tr("Normal");
    }
}

} // anonymous namespace

ServerListModel::ServerListModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int ServerListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int ServerListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant ServerListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const auto& r = m_rows[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName:        return r.name;
        case ColIP:          return QStringLiteral("%1:%2").arg(r.ip).arg(r.port);
        case ColDescription: return r.description;
        case ColPing:        return r.ping;
        case ColUsers:       return r.users;
        case ColMaxUsers:    return r.maxUsers;
        case ColPreference:  return r.preference;
        case ColFailed:      return r.failed;
        case ColStatic:      return r.isStatic ? tr("Yes") : tr("No");
        case ColSoftFiles:   return r.softFiles;
        case ColLowID:       return r.lowIdUsers;
        case ColObfuscation: return r.obfuscation ? tr("Yes") : tr("No");
        default:             break;
        }
    }

    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColName:        return r.name;
        case ColIP:          return QStringLiteral("%1:%2").arg(r.ip).arg(r.port, 5, 10, QLatin1Char('0'));
        case ColDescription: return r.description;
        case ColPing:        return r.ping;
        case ColUsers:       return r.users;
        case ColMaxUsers:    return r.maxUsers;
        case ColPreference:  return r.preference;
        case ColFailed:      return r.failed;
        case ColStatic:      return r.isStatic ? 1 : 0;
        case ColSoftFiles:   return r.softFiles;
        case ColLowID:       return r.lowIdUsers;
        case ColObfuscation: return r.obfuscation ? 1 : 0;
        default:             break;
        }
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColPing:
        case ColUsers:
        case ColMaxUsers:
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
    case ColPreference:  return tr("Preference");
    case ColFailed:      return tr("Failed");
    case ColStatic:      return tr("Static");
    case ColSoftFiles:   return tr("Soft Files");
    case ColLowID:       return tr("Low ID");
    case ColObfuscation: return tr("Obfuscation");
    default:             return {};
    }
}

void ServerListModel::refreshFromServerList(const ServerList* serverList)
{
    beginResetModel();
    m_rows.clear();

    if (serverList) {
        const auto& servers = serverList->servers();
        m_rows.reserve(servers.size());

        for (const auto& srv : servers) {
            ServerRow row;
            row.name = srv->name();
            row.ip = srv->address();
            row.port = srv->port();
            row.description = srv->description();
            row.ping = srv->ping();
            row.users = srv->users();
            row.maxUsers = srv->maxUsers();
            row.preference = priorityString(srv->preference());
            row.failed = srv->failedCount();
            row.isStatic = srv->isStaticMember();
            row.softFiles = srv->softFiles();
            row.lowIdUsers = srv->lowIDUsers();
            row.obfuscation = srv->supportsObfuscationTCP();
            row.files = srv->files();
            row.serverPtr = srv.get();
            m_rows.push_back(std::move(row));
        }
    }

    endResetModel();
}

void ServerListModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

const Server* ServerListModel::serverAtRow(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return nullptr;
    return m_rows[static_cast<size_t>(row)].serverPtr;
}

} // namespace eMule
