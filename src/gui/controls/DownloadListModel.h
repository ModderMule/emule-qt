#pragma once

/// @file DownloadListModel.h
/// @brief Table model for the downloads list in the Transfer window.

#include <QAbstractTableModel>
#include <QString>

#include <cstdint>
#include <vector>

namespace eMule {

/// Row data for one download (PartFile) shown in the downloads list.
struct DownloadRow {
    QString hash;
    QString fileName;
    QString status;
    QString priority;
    int64_t fileSize = 0;
    int64_t completedSize = 0;
    int64_t datarate = 0;
    double percentCompleted = 0.0;
    int sourceCount = 0;
    int transferringSrcCount = 0;
    bool isPaused = false;
    bool isStopped = false;
    bool isAutoDownPriority = false;
    int64_t category = 0;
    int64_t lastSeenComplete = 0;
    int64_t lastReception = 0;
    int64_t addedOn = 0;
    QString fileType;
    int64_t requests = 0;
    int64_t acceptedRequests = 0;
    int64_t transferredData = 0;
};

/// Table model backing the downloads tree view in the Transfer panel.
class DownloadListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColFileName = 0,
        ColSize,
        ColCompleted,
        ColSpeed,
        ColProgress,
        ColSources,
        ColPriority,
        ColStatus,
        ColRemaining,
        ColSeenComplete,
        ColLastReception,
        ColCategory,
        ColAddedOn,
        ColCount
    };

    explicit DownloadListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Replace all downloads with a new snapshot.
    void setDownloads(std::vector<DownloadRow> downloads);

    /// Clear all downloads.
    void clear();

    [[nodiscard]] int downloadCount() const { return static_cast<int>(m_downloads.size()); }

    /// Get the file hash for a row index.
    [[nodiscard]] QString hashAt(int row) const;

    /// Get the full download row for a row index (nullptr if out of range).
    [[nodiscard]] const DownloadRow* downloadAt(int row) const;

private:
    std::vector<DownloadRow> m_downloads;
};

} // namespace eMule
