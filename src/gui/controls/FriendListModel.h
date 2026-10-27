#pragma once

/// @file FriendListModel.h
/// @brief QAbstractListModel for the friend list in the Messages panel.

#include <QAbstractListModel>
#include <QCborArray>
#include <QIcon>
#include <QString>

#include <cstdint>
#include <vector>

namespace eMule {

/// Which network a friend is on: both share the Messages panel.
enum class FriendNetwork : quint8 { Ed2k, Ed3k };

struct FriendRow {
    /// The chat key: the MD4 hex for eD2K, "ed3k:" + node hex for ed3k (FriendListModel::ed3kKey).
    QString hash;
    FriendNetwork network = FriendNetwork::Ed2k;
    /// MD4 hex (eD2K) or the 32-byte node key hex (ed3k)
    QString id;
    QString node;          ///< ed3k: node ID hex (chats go by node)
    bool    mutual = false;   ///< ed3k: it added us too
    bool    asking = false;   ///< ed3k: our request is pending
    QString name;
    int64_t ip = 0;      ///< eD2K byte order; 0 for an IPv6 friend — prefer addr.
    QString addr;        ///< Literal address, both families. Empty when unknown.
    QString cc;          ///< ISO country code (GeoLite2), empty when unknown.
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

    /// Replace the eD2K rows from a CBOR array received via IPC (GetFriends).
    void refreshFromCborArray(const QCborArray& arr);
    /// Replace the ed3k rows (GetEd3kFriends "friends"); they follow the eD2K ones.
    void refreshEd3k(const QCborArray& arr);

    /// The chat key of an ed3k node (hex), "ed3k:<node>".
    [[nodiscard]] static QString ed3kKey(const QString& nodeHex);
    /// The node hex of an ed3k chat key; empty for an eD2K key.
    [[nodiscard]] static QString ed3kNodeOf(const QString& key);

    /// Access a row by index. Returns nullptr if out of range.
    [[nodiscard]] const FriendRow* rowAt(int row) const;

    /// Find the row index for a given hash. Returns -1 if not found.
    [[nodiscard]] int findByHash(const QString& hash) const;

private:
    [[nodiscard]] static QIcon baseIcon(const FriendRow& row);
    /// Puts @p rows of @p network in place of the old ones, eD2K first.
    void replaceRows(FriendNetwork network, std::vector<FriendRow> rows);

    std::vector<FriendRow> m_rows;
};

} // namespace eMule
