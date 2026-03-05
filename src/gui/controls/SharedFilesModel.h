#pragma once

/// @file SharedFilesModel.h
/// @brief Table model for the Shared Files list.

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QString>

#include <cstdint>
#include <vector>

namespace eMule {

/// Row data for one shared file.
struct SharedFileRow {
    QString hash;
    QString fileName;
    int64_t fileSize = 0;
    QString fileType;
    int     upPriority = 1;     // kPrNormal
    bool    isAutoUpPriority = true;
    int64_t requests = 0;
    int64_t acceptedUploads = 0;
    int64_t transferred = 0;
    int64_t allTimeRequests = 0;
    int64_t allTimeAccepted = 0;
    int64_t allTimeTransferred = 0;
    int     completeSources = 0;
    bool    publishedED2K = false;
    bool    kadPublished = false;
    QString path;               // directory
    QString filePath;           // full path
    QString ed2kLink;
    bool    isPartFile = false;
    int     uploadingClients = 0;
    int     partCount = 0;
    int64_t completedSize = 0;
    QByteArray sharePartMap;    ///< Per-part availability encoding for status bar
};

/// Table model backing the shared files tree view.
class SharedFilesModel : public QAbstractTableModel {
    Q_OBJECT

public:
    /// Custom data role for the per-part availability map.
    static constexpr int SharePartMapRole = Qt::UserRole + 1;

    enum Column {
        ColFileName = 0,
        ColSize,
        ColType,
        ColPriority,
        ColRequests,
        ColTransferred,
        ColSharedParts,
        ColCompleteSources,
        ColSharedNetworks,
        ColFolder,
        ColCount
    };

    explicit SharedFilesModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Replace all files with a new snapshot.
    void setFiles(std::vector<SharedFileRow> files);

    /// Clear all files.
    void clear();

    [[nodiscard]] int fileCount() const { return static_cast<int>(m_files.size()); }

    /// Get the file hash for a row index.
    [[nodiscard]] QString hashAt(int row) const;

    /// Get the full row for a row index (nullptr if out of range).
    [[nodiscard]] const SharedFileRow* fileAt(int row) const;

    /// Check if a file with the given hex hash is in the shared files list.
    [[nodiscard]] bool containsHash(const QString& hexHash) const;

private:
    std::vector<SharedFileRow> m_files;
};

// ---------------------------------------------------------------------------
// SharedFilesSortProxy — folder/filter proxy for shared files
// ---------------------------------------------------------------------------

/// Filter modes for the folder tree.
enum class SharedFilterType {
    AllShared,      ///< Show everything
    Incoming,       ///< Only incoming directory
    Incomplete,     ///< Only PartFiles
    SharedDirs,     ///< Non-incoming completed files
    SpecificDir     ///< Exact directory match
};

class SharedFilesSortProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setFolderFilter(SharedFilterType type, const QString& path = {});
    void setIncomingDir(const QString& dir);

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    SharedFilterType m_filterType = SharedFilterType::AllShared;
    QString m_filterPath;
    QString m_incomingDir;
};

} // namespace eMule
