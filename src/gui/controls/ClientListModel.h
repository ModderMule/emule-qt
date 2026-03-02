#pragma once

/// @file ClientListModel.h
/// @brief Table model for the client lists in the Transfer window bottom tabs.
///
/// A single model class serves all 4 tabs (Uploading, Downloading, On Queue,
/// Known Clients) by switching the column set based on the mode.

#include <QAbstractTableModel>
#include <QString>

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
    int64_t waitStartTime = 0;
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
    int64_t uploadStartDelay = 0;  // ms since upload started (0 = not uploading)
    int filePriority = -1;         // download priority of queued file (-1 = unknown)
    bool isAutoPriority = false;   // whether file priority is auto
    uint8_t fileRating = 0;        // client's rating for file (0-5)
    bool isConnected = false;      // has active socket connection
};

/// Table model backing the client list tree views in the Transfer panel.
class ClientListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit ClientListModel(ClientListMode mode, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Replace all clients with a new snapshot.
    void setClients(std::vector<ClientRow> clients);

    /// Clear all clients.
    void clear();

    [[nodiscard]] int clientCount() const { return static_cast<int>(m_clients.size()); }
    [[nodiscard]] ClientListMode mode() const { return m_mode; }
    [[nodiscard]] const ClientRow* clientAt(int row) const;

private:
    [[nodiscard]] QVariant displayData(const ClientRow& c, int column) const;
    [[nodiscard]] QVariant sortData(const ClientRow& c, int column) const;
    [[nodiscard]] QVariant headerLabel(int column) const;

    ClientListMode m_mode;
    std::vector<ClientRow> m_clients;
};

} // namespace eMule
