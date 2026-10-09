#pragma once

/// @file ServerListModel.h
/// @brief Table model for the ED2K server list in the Server tab.

#include <QCborArray>
#include <QString>

#include "AbstractTableModel.h"

#include <cstdint>
#include <vector>

namespace eMule {

/// Row data snapshot for one server.
///
/// Rows are built from the daemon's CBOR payload — the GUI process holds no core
/// Server objects to point at.
struct ServerRow {
    QString name;
    QString ip;
    uint16_t port = 0;
    QString description;
    uint32_t ping = 0;
    uint32_t users = 0;
    uint32_t maxUsers = 0;
    QString preference;

    /// The wire value behind `preference`, kept because the name cannot be
    /// sorted: the daemon sends 0 Normal, 1 High, 2 Low, which is neither
    /// alphabetical nor a strength order.
    int preferenceValue = 0;
    uint32_t failed = 0;
    bool isStatic = false;
    bool disabled = false;   // failed too often; kept, but not dialed automatically
    uint32_t softFiles = 0;
    uint32_t hardFiles = 0;
    QString version;
    uint32_t lowIdUsers = 0;
    bool obfuscation = false;
    bool hasMetaApi = false;   ///< eNode server announcing a Meta API (torrent/Usenet downloads)
    QString addrPort;          ///< daemon's "addr:port" key for Meta API requests
    uint32_t files = 0;

    // Server identity for IPC connect-to-specific-server. numericIp is 0 for an IPv6
    // server, so addr (the literal, both families) is what actually keys the request;
    // numericIp is still sent so an older daemon keeps working.
    uint32_t numericIp = 0;
    QString  addr;
    QString  addr6;            ///< dual-stack server's IPv6 next to its IPv4 addr, else empty
    QString  cc;               ///< ISO country code (GeoLite2), empty when unknown

    // Unique server identity for connected-server highlighting
    uint32_t serverId = 0;
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
        ColFiles,
        ColPreference,
        ColFailed,
        ColStatic,
        ColSoftFiles,
        ColLowID,
        ColObfuscation,
        ColCountry,          ///< MorphXT IP2Country
        ColIPv6,             ///< dual-stack server's IPv6 (hidden by default)
        // MFC columns 11 and 12, hidden by default there too; appended so saved
        // layouts keep their indexes (ServerListCtrl.cpp:83-84)
        ColHardFiles,
        ColVersion,
        ColCount
    };

    /// Row identity for selection save/restore: the daemon's serverId, which a
    /// dual-stack merge does not change (the IP column text can).
    static constexpr int ServerIdRole = Qt::UserRole + 1;

    explicit ServerListModel(QObject* parent = nullptr);

    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Rebuild model from CBOR array received via IPC.
    void refreshFromCborArray(const QCborArray& servers);

    /// Row listing @p address (IPv4/IPv6 literal, bracketed or not, or a dynIP
    /// hostname) on @p port, -1 if none. Either address of a dual-stack row matches.
    [[nodiscard]] int rowForAddress(const QString& address, uint16_t port) const;

    /// Set the currently connected server (0 to clear).
    void setConnectedServer(uint32_t serverId);

protected:
    [[nodiscard]] int columnCountValue() const override { return ColCount; }

private:
    uint32_t m_connectedServerId = 0;
};

} // namespace eMule
