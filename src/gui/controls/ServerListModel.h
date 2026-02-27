#pragma once

/// @file ServerListModel.h
/// @brief Table model for the ED2K server list in the Server tab.

#include <QAbstractTableModel>
#include <QString>

#include <cstdint>
#include <vector>

namespace eMule {

class Server;
class ServerList;

/// Row data snapshot for one server.
struct ServerRow {
    QString name;
    QString ip;
    uint16_t port = 0;
    QString description;
    uint32_t ping = 0;
    uint32_t users = 0;
    uint32_t maxUsers = 0;
    QString preference;
    uint32_t failed = 0;
    bool isStatic = false;
    uint32_t softFiles = 0;
    uint32_t lowIdUsers = 0;
    bool obfuscation = false;
    uint32_t files = 0;

    // Internal reference for double-click / context menu
    const Server* serverPtr = nullptr;
};

/// Table model backing the server list tree view.
class ServerListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColName = 0,
        ColIP,
        ColDescription,
        ColPing,
        ColUsers,
        ColMaxUsers,
        ColPreference,
        ColFailed,
        ColStatic,
        ColSoftFiles,
        ColLowID,
        ColObfuscation,
        ColCount
    };

    explicit ServerListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Rebuild model from a ServerList.
    void refreshFromServerList(const ServerList* serverList);

    /// Clear all rows.
    void clear();

    /// Get the server pointer for a given row index.
    [[nodiscard]] const Server* serverAtRow(int row) const;

private:
    std::vector<ServerRow> m_rows;
};

} // namespace eMule
