#pragma once

/// @file ClientListModel.h
/// @brief Table model for the client lists in the Transfer window bottom tabs.
///
/// A single model class serves all 4 tabs (Uploading, Downloading, On Queue,
/// Known Clients) by switching the column set based on the mode.

#include <QString>

#include "AbstractTableModel.h"

#include <cstdint>
#include <vector>

namespace eMule {

/// Which bottom tab this model is configured for.
enum class ClientListMode {
    Uploading,
    Downloading,
    OnQueue,
    KnownClients
};

/// Row data for one client displayed in any of the 4 bottom tabs.
struct ClientRow {
    QString userName;
    QString software;
    QString fileName;
    QString uploadState;
    QString downloadState;
    QString userHash;
    int64_t transferredUp = 0;
    int64_t transferredDown = 0;
    int64_t sessionUp = 0;
    int64_t sessionDown = 0;
    int64_t askedCount = 0;
    int64_t waitStartTime = 0;  // elapsed wait time in ms (computed daemon-side)
    int partCount = 0;
    int availPartCount = 0;
    int remoteQueueRank = 0;
    int sourceFrom = 0;
    int softwareId = -1;
    uint32_t ip = 0;
    uint16_t port = 0;
    bool isBanned = false;
    bool hasCredit = false;
    bool isFriend = false;
    int64_t upDatarate = 0;       // bytes/sec upload rate
    int64_t uploadStartDelay = 0;  // ms since upload started (0 = not uploading)
    int filePriority = -1;         // download priority of queued file (-1 = unknown)
    bool isAutoPriority = false;   // whether file priority is auto
    int upPartCount = 0;           // parts client has (upload context, PARTSIZE chunks)
    uint8_t fileRating = 0;        // client's rating for file (0-5)
    bool isConnected = false;      // has active socket connection
};

/// Table model backing the client list tree views in the Transfer panel.
class ClientListModel : public AbstractTableModel<ClientRow> {
    Q_OBJECT

public:
    explicit ClientListModel(ClientListMode mode, QObject* parent = nullptr);

    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Replace all clients with a new snapshot.
    void setClients(std::vector<ClientRow> clients) { setRows(std::move(clients)); }

    [[nodiscard]] int clientCount() const { return count(); }
    [[nodiscard]] ClientListMode mode() const { return m_mode; }
    [[nodiscard]] const ClientRow* clientAt(int row) const { return rowAt(row); }

protected:
    [[nodiscard]] int columnCountValue() const override;

private:
    [[nodiscard]] QVariant displayData(const ClientRow& c, int column) const;
    [[nodiscard]] QVariant sortData(const ClientRow& c, int column) const;
    [[nodiscard]] QVariant headerLabel(int column) const;

    ClientListMode m_mode;
};

} // namespace eMule
