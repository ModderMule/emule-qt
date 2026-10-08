#include "pch.h"
#include "controls/KadContactsModel.h"
#include "utils/CountryFlags.h"

#include <QFont>

#include <algorithm>

namespace eMule {

KadContactsModel::KadContactsModel(QObject* parent)
    : AbstractTableModel<KadContactRow>(parent)
{
    // MFC SetAllIcons (KadContactListCtrl.cpp:79-92); SrcUnknown is res\Client4.ico (emule.rc:1413)
    for (int i = 0; i < 5; ++i)
        m_icons[static_cast<size_t>(i)] = QIcon(QStringLiteral(":/icons/Contact%1.ico").arg(i));
    m_icons[5] = QIcon(QStringLiteral(":/icons/Client4.ico"));
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

    if (role == Qt::FontRole && index.column() != ColStatus && index.column() != ColCountry) {
        QFont font;
        font.setFamily(QStringLiteral("Courier New"));
        font.setPointSize(8);
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

} // namespace eMule
