#pragma once

/// @file KadContactsModel.h
/// @brief Table model for the Kad contacts list in the Kad tab.

#include <QIcon>
#include <QString>

#include "AbstractTableModel.h"

#include <array>
#include <cstdint>
#include <vector>

namespace eMule {

/// Row data for one Kad contact displayed in the contacts list.
struct KadContactRow {
    QString clientId;
    QString distance;
    uint32_t ip = 0;
    uint16_t udpPort = 0;
    uint16_t tcpPort = 0;
    uint8_t version = 0;
    uint8_t type = 0;
    bool ipVerified = false;
    bool bootstrap = false;
};

/// Table model backing the Kad contacts tree view.
class KadContactsModel : public AbstractTableModel<KadContactRow> {
    Q_OBJECT

public:
    enum Column {
        ColStatus = 0,
        ColClientId,
        ColDistance,
        ColCount
    };

    explicit KadContactsModel(QObject* parent = nullptr);

    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Replace all contacts with a new snapshot.
    void setContacts(std::vector<KadContactRow> contacts) { setRows(std::move(contacts)); }

    [[nodiscard]] int contactCount() const { return count(); }

    /// MFC's image index (KadContactListCtrl.cpp:117-124): Contact0-4 by type, and 5
    /// (SrcUnknown) for a bootstrap contact or an active one that isn't IP-verified.
    [[nodiscard]] static int contactImage(const KadContactRow& c);

protected:
    [[nodiscard]] int columnCountValue() const override { return ColCount; }

private:
    std::array<QIcon, 6> m_icons;
};

} // namespace eMule
