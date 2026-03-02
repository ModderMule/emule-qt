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
    int64_t ip = 0;
    int     port = 0;
    int64_t lastSeen = 0;
    int64_t lastChatted = 0;
    bool    friendSlot = false;
    QString kadID;
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
