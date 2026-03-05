#pragma once

/// @file DownloadListModel.h
/// @brief Tree model for the downloads list in the Transfer window.
///
/// Top-level rows are downloads (PartFiles). Each download can have child rows
/// representing source clients, shown when the user expands the item.

#include <QAbstractItemModel>
#include <QByteArray>
#include <QString>

#include <cstdint>
#include <vector>

namespace eMule {

/// Row data for one source client shown under an expanded download.
struct SourceRow {
    QString userName;
    QString software;
    QString downloadState;   // "On Queue", "Downloading", etc.
    int64_t remoteQueueRank = 0;
    int64_t transferredDown = 0;
    int64_t sessionDown = 0;
    int64_t datarate = 0;      // download speed from this source
    int availPartCount = 0;
    int partCount = 0;
    int sourceFrom = 0;        // SourceFrom enum
    QString userHash;
    QByteArray partMap;  // per-part: 0=no, 1=both, 2=client-only, 3=pending, 4=receiving
};

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
    QByteArray partMap;  // per-part status: 0=done, 1=no-src, 2-254=src-freq, 255=downloading

    std::vector<SourceRow> sources;  // child rows (populated when expanded)
};

/// Tree model backing the downloads view in the Transfer panel.
/// Top-level items are downloads; children are source clients.
class DownloadListModel : public QAbstractItemModel {
    Q_OBJECT

public:
    /// Custom data roles for the progress column delegate.
    static constexpr int PartMapRole = Qt::UserRole + 1;
    static constexpr int PausedRole  = Qt::UserRole + 2;

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

    // QAbstractItemModel interface
    [[nodiscard]] QModelIndex index(int row, int column,
                                     const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& index) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] bool hasChildren(const QModelIndex& parent = {}) const override;

    /// Replace all downloads with a new snapshot (preserves existing sources).
    void setDownloads(std::vector<DownloadRow> downloads);

    /// Set source rows for a specific download hash.
    void setSources(const QString& hash, std::vector<SourceRow> sources);

    /// Clear all downloads.
    void clear();

    [[nodiscard]] int downloadCount() const { return static_cast<int>(m_downloads.size()); }

    /// Get the file hash for a top-level row index.
    [[nodiscard]] QString hashAt(int row) const;

    /// Get the full download row for a top-level row index (nullptr if out of range).
    [[nodiscard]] const DownloadRow* downloadAt(int row) const;

    /// Check if an index represents a source row (child of a download).
    [[nodiscard]] bool isSourceRow(const QModelIndex& index) const;

private:
    std::vector<DownloadRow> m_downloads;
};

} // namespace eMule
