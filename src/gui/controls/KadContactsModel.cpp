#include "pch.h"
#include "controls/KadContactsModel.h"
#include "utils/AddressSortKey.h"
#include "utils/Appearance.h"
#include "utils/CountryFlags.h"
#include "app/SkinIcons.h"

#include <QFont>

#include <algorithm>

namespace eMule {

KadContactsModel::KadContactsModel(QObject* parent)
    : AbstractTableModel<KadContactRow>(parent)
{
    // MFC SetAllIcons (KadContactListCtrl.cpp:79-92); SrcUnknown is res\Client4.ico (emule.rc:1413)
    for (int i = 0; i < 5; ++i)
        m_icons[static_cast<size_t>(i)] = skinIcon(QStringLiteral(":/icons/Contact%1.ico").arg(i));
    m_icons[5] = skinIcon(QStringLiteral(":/icons/Client4.ico"));
    connect(&m_resolver, &HostnameResolver::resolved, this, &KadContactsModel::hostnameChanged);
}

void KadContactsModel::setResolveHostnames(bool enabled)
{
    if (m_resolver.enabled() == enabled)
        return;
    m_resolver.setEnabled(enabled);
    refreshHostnames();
    emit headerDataChanged(Qt::Horizontal, ColIp, ColIp);
    if (count() > 0)
        emit dataChanged(index(0, ColIp), index(count() - 1, ColIp),
                         {Qt::DisplayRole, Qt::ToolTipRole, Qt::UserRole});
}

QVariant KadContactsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const auto& c = m_rows[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColStatus:
            return QStringLiteral("%1(%2)").arg(c.type).arg(c.version);
        case ColClientId:
            return c.clientId;
        case ColDistance:
            return c.distance;
        case ColCountry:
            return CountryFlags::columnText(c.cc);
        case ColIp:
            return c.addr.isEmpty() ? QString() : m_resolver.endpointText(c.addr, c.udpPort);
        default:
            break;
        }
    }

    if (role == Qt::UserRole) {
        switch (index.column()) {
        // type, then version (MFC KadContactListCtrl.cpp:213-221)
        case ColStatus:   return c.type * 256 + c.version;
        case ColClientId: return c.clientId;
        case ColDistance:  return c.distance;
        case ColCountry:   return CountryFlags::sortKey(c.cc);
        case ColIp:        return m_resolver.displayText(c.addr, addressSortKey(c.addr, c.udpPort));
        default:          break;
        }
    }

    // Deliberate: icon on Type, not ID as MFC (KadContactListCtrl.cpp:117-125). The flag
    // delegate widens logical column 0 only; elsewhere icon + flag get squeezed
    if (role == Qt::DecorationRole && index.column() == ColStatus)
        return CountryFlags::withFlag(m_icons[static_cast<size_t>(contactImage(c))], c.cc);

    if (role == Qt::ToolTipRole && (index.column() == ColStatus || index.column() == ColCountry)
        && !c.cc.isEmpty())
        return CountryFlags::tooltip(c.cc);

    if (role == Qt::ToolTipRole && index.column() == ColIp && !c.addr.isEmpty())
        return tr("%1\nUDP port: %2\nTCP port: %3").arg(m_resolver.toolTip(c.addr)).arg(c.udpPort).arg(c.tcpPort);

    if (role == Qt::FontRole && index.column() != ColStatus && index.column() != ColCountry) {
        QFont font;
        font.setFamily(QStringLiteral("Courier New"));
        font.setPointSize(scaledPointSize(8));
        return font;
    }

    return {};
}

QVariant KadContactsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColStatus:   return tr("Type");
    case ColClientId: return tr("ID");
    case ColDistance:  return tr("Distance");
    case ColCountry:   return tr("Country");
    case ColIp:        return m_resolver.enabled() ? tr("Hostname / IP") : tr("IP");
    default:          return {};
    }
}

int KadContactsModel::contactImage(const KadContactRow& c)
{
    if (c.bootstrap)
        return 5;
    const int image = std::min(static_cast<int>(c.type), 4);
    // An active contact that isn't IP-verified is not handed out, so it gets the unknown icon
    return image < 3 && !c.ipVerified ? 5 : image;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void KadContactsModel::refreshHostnames()
{
    QStringList addresses;
    addresses.reserve(count());
    for (const auto& c : m_rows)
        addresses.append(c.addr);
    m_resolver.sync(addresses);
}

void KadContactsModel::hostnameChanged(const QString& address)
{
    for (int row = 0; row < count(); ++row) {
        if (m_rows[static_cast<size_t>(row)].addr == address)
            emit dataChanged(index(row, ColIp), index(row, ColIp),
                             {Qt::DisplayRole, Qt::ToolTipRole, Qt::UserRole});
    }
}

} // namespace eMule
