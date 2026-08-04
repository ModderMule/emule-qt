#pragma once

/// @file FriendListModel.h
/// @brief QAbstractListModel for the friend list in the Messages panel.

#include <QAbstractListModel>
#include <QCborArray>
#include <QString>

#include <cstdint>
#include <vector>

namespace eMule {

struct FriendRow {
    QString hash;
    QString name;
    int64_t ip = 0;      ///< eD2K byte order; 0 for an IPv6 friend — prefer addr.
    QString addr;        ///< Literal address, both families. Empty when unknown.
    int     port = 0;
    int64_t lastSeen = 0;
    int64_t lastChatted = 0;
    bool    friendSlot = false;
    QString kadID;

    /// True when we know any address for this friend, IPv4 or IPv6.
    [[nodiscard]] bool hasAddress() const
    {
        return ip != 0 || (!addr.isEmpty() && addr != QLatin1String("0.0.0.0"));
    }
};

class FriendListModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit FriendListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

    /// Replace all rows from a CBOR array received via IPC.
    void refreshFromCborArray(const QCborArray& arr);

    /// Access a row by index. Returns nullptr if out of range.
    [[nodiscard]] const FriendRow* rowAt(int row) const;

    /// Find the row index for a given hash. Returns -1 if not found.
    [[nodiscard]] int findByHash(const QString& hash) const;

private:
    std::vector<FriendRow> m_rows;
};

} // namespace eMule
