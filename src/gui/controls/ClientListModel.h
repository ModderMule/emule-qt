#pragma once

/// @file ClientListModel.h
/// @brief Table model for the client lists in the Transfer window bottom tabs.
///
/// A single model class serves all 4 tabs (Uploading, Downloading, On Queue,
/// Known Clients) by switching the column set based on the mode.

#include <QByteArray>
#include <QMetaType>
#include <QString>

#include "AbstractTableModel.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace eMule {

/// The Obtained Parts bar of one uploading or queued client (MFC DrawUpStatusBar).
struct UpStatusBar {
    int64_t fileSize = 0;                                 ///< 0 = nothing to draw
    QByteArray parts;                                     ///< one byte per part, non-zero = peer has it
    std::vector<int> nextParts;                           ///< whole parts about to be sent
    std::vector<std::pair<int64_t, int64_t>> sentRanges;  ///< inclusive byte ranges already sent
    bool greyed = false;                                  ///< slot past the active upload count
};

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
    int64_t queueSessionPayloadUp = 0;  ///< file payload only; sessionUp is wire bytes
    int64_t sessionDown = 0;
    int64_t askedCount = 0;
    int64_t waitStartTime = 0;  // elapsed wait time in ms (computed daemon-side)
    int partCount = 0;
    int availPartCount = 0;
    int remoteQueueRank = 0;
    int sourceFrom = 0;
    int softwareId = -1;
    uint32_t ip = 0;     ///< eD2K byte order; 0 for an IPv6 peer — prefer addr.
    QString  addr;       ///< Literal address, both families. Empty when unknown.
    uint16_t port = 0;
    bool isBanned = false;
    bool hasCredit = false;
    bool isFriend = false;
    int64_t upDatarate = 0;       // bytes/sec upload rate
    int64_t downDatarate = 0;     // bytes/sec download rate
    int64_t downloadedTotal = 0;  // credit totals across sessions (0 = no credits)
    int64_t uploadedTotal = 0;
    int64_t uploadStartDelay = 0;  // ms since upload started (0 = not uploading)
    int uploadFilePriority = -1;   // up priority of the upload file (-1 = unknown)
    bool uploadFileAutoPriority = false;
    int upPartCount = 0;           // parts client has (upload context, PARTSIZE chunks)
    bool isConnected = false;      // has active socket connection
    int64_t queueRating = 0;       // MFC GetScore(false, false, true)
    int64_t queueScore = 0;        // MFC GetScore(false)
    int64_t lastUpRequestDelay = 0;  // ms since the last upload request
    bool hasLowID = false;
    bool addNextConnect = false;   // LowID peer owed the next free slot
    UpStatusBar upStatus;
};

/// Table model backing the client list tree views in the Transfer panel.
class ClientListModel : public AbstractTableModel<ClientRow> {
    Q_OBJECT

public:
    /// The row's UpStatusBar, for UploadStatusDelegate.
    static constexpr int UpStatusRole = Qt::UserRole + 1;

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

Q_DECLARE_METATYPE(eMule::UpStatusBar)
