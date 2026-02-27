#include "controls/KadContactsModel.h"

#include <QFont>
#include <QPainter>

namespace eMule {

KadContactsModel::KadContactsModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    // Pre-render the 5 contact type icons (colored circles)
    // Type 0: green (established, 2+ hours)
    // Type 1: green-yellow (older, 1-2 hours)
    // Type 2: orange (new, < 1 hour)
    // Type 3: orange-red (initial/unverified)
    // Type 4: red (expired)
    static constexpr QColor colors[] = {
        {0x00, 0xAA, 0x00},  // 0 - green
        {0x88, 0xCC, 0x00},  // 1 - yellow-green
        {0xFF, 0xAA, 0x00},  // 2 - orange
        {0xFF, 0x66, 0x00},  // 3 - orange-red
        {0xFF, 0x00, 0x00},  // 4 - red
    };
    for (int i = 0; i < 5; ++i)
        m_icons[i] = contactIcon(static_cast<uint8_t>(i));

    Q_UNUSED(colors); // used in contactIcon
}

int KadContactsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_contacts.size());
}

int KadContactsModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant KadContactsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_contacts.size()))
        return {};

    const auto& c = m_contacts[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColStatus:
            return QStringLiteral("%1(%2)").arg(c.type).arg(c.version);
        case ColClientId:
            return c.clientId;
        case ColDistance:
            return c.distance;
        default:
            break;
        }
    }

    if (role == Qt::DecorationRole && index.column() == ColStatus) {
        const int t = std::min(static_cast<int>(c.type), 4);
        return m_icons[t];
    }

    if (role == Qt::FontRole && index.column() != ColStatus) {
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
    case ColStatus:   return QStringLiteral("Status");
    case ColClientId: return QStringLiteral("Client ID");
    case ColDistance:  return QStringLiteral("Distance");
    default:          return {};
    }
}

void KadContactsModel::setContacts(std::vector<KadContactRow> contacts)
{
    beginResetModel();
    m_contacts = std::move(contacts);
    endResetModel();
}

void KadContactsModel::clear()
{
    beginResetModel();
    m_contacts.clear();
    endResetModel();
}

QPixmap KadContactsModel::contactIcon(uint8_t type)
{
    static constexpr QColor colors[] = {
        {0x00, 0xAA, 0x00},  // 0 - green
        {0x88, 0xCC, 0x00},  // 1 - yellow-green
        {0xFF, 0xAA, 0x00},  // 2 - orange
        {0xFF, 0x66, 0x00},  // 3 - orange-red
        {0xFF, 0x00, 0x00},  // 4 - red
    };
    const int t = std::min(static_cast<int>(type), 4);
    constexpr int sz = 12;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(colors[t]);
    p.drawEllipse(1, 1, sz - 2, sz - 2);
    return pm;
}

} // namespace eMule
