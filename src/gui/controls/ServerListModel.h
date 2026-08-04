#pragma once

/// @file ServerListModel.h
/// @brief Table model for the ED2K server list in the Server tab.

#include <QCborArray>
#include <QString>

#include "AbstractTableModel.h"

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

    // Server identity for IPC connect-to-specific-server. numericIp is 0 for an IPv6
    // server, so addr (the literal, both families) is what actually keys the request;
    // numericIp is still sent so an older daemon keeps working.
    uint32_t numericIp = 0;
    QString  addr;

    // Unique server identity for connected-server highlighting
    uint32_t serverId = 0;

    // Internal reference for double-click / context menu
    const Server* serverPtr = nullptr;
};

/// Table model backing the server list tree view.
class ServerListModel : public AbstractTableModel<ServerRow> {
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

    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Rebuild model from a ServerList.
    void refreshFromServerList(const ServerList* serverList);

    /// Rebuild model from CBOR array received via IPC.
    void refreshFromCborArray(const QCborArray& servers);

    /// Get the server pointer for a given row index.
    [[nodiscard]] const Server* serverAtRow(int row) const;

    /// Set the currently connected server (0 to clear).
    void setConnectedServer(uint32_t serverId);

protected:
    [[nodiscard]] int columnCountValue() const override { return ColCount; }

private:
    uint32_t m_connectedServerId = 0;
};

} // namespace eMule
