#pragma once

/// @file SharedFilesModel.h
/// @brief Table model for the Shared Files list.

#include <QHash>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>

#include "AbstractTableModel.h"

#include <cstdint>
#include <vector>

namespace eMule {

/// Row data for one shared file.
struct SharedFileRow {
    QString hash;
    QString fileName;
    int64_t fileSize = 0;
    QString fileType;
    bool    hasComment = false;
    int     userRating = 0;     // wire value: 6 means a Kad note lookup is running
    bool    ownComment = false; // *we* commented or rated it — drawn as an overlay

    // The file's own bytes contradict its name. Independent of the rating above:
    // that one is an opinion, this one is a fact about the first twelve bytes.
    bool    containerSuspect = false;
    QString containerExpected;  // container the extension promises, e.g. "ASF"
    QString containerActual;    // what it really is; empty when unrecognised
    int     upPriority = 1;     // kPrNormal
    bool    isAutoUpPriority = true;
    int64_t requests = 0;
    int64_t acceptedUploads = 0;
    int64_t transferred = 0;
    int64_t allTimeRequests = 0;
    int64_t allTimeAccepted = 0;
    int64_t allTimeTransferred = 0;
    int     completeSources = 0;
    int     completeSourcesLo = 0;
    int     completeSourcesHi = 0;
    // Media tags; empty / 0 when the file has none
    QString artist;
    QString album;
    QString title;
    int64_t length = 0;         // seconds
    int64_t bitrate = 0;        // kbit/s
    QString codec;              // display name
    bool    publishedED2K = false;
    bool    kadPublished = false;
    QString path;               // directory
    QString filePath;           // full path
    QString ed2kLink;
    bool    isPartFile = false;
    int     category = 0;       ///< part files only
    int     uploadingClients = 0;
    int     queuedClients = 0;
    int     partCount = 0;
    int64_t completedSize = 0;
    QByteArray sharePartMap;    ///< Per-part availability encoding for status bar
    bool isCollection = false;
    bool hasCollectionAuthorKey = false;
    bool hasPartHashes = false;         ///< a link with the part hashes can be asked for
    int64_t uploadDataRate = 0;         ///< bytes/sec upload rate for this file

    // -- Share membership, only meaningful in browse mode (see setBrowseMode) -----
    /// Is this file currently shared? Always true for rows that came from the shared
    /// list; a browsed directory also yields unshared rows, where it is false.
    bool shareChecked = true;
    /// May the user change that? False where the state is forced — the incoming
    /// directory is always shared, eMule's own directories never are. Renders as
    /// MFC's CBS_CHECKEDDISABLED / CBS_UNCHECKEDDISABLED.
    bool shareToggleable = false;

    bool operator==(const SharedFileRow&) const = default;
};

/// Table model backing the shared files tree view.
class SharedFilesModel : public AbstractTableModel<SharedFileRow> {
    Q_OBJECT

public:
    /// Custom data role for the per-part availability map.
    static constexpr int SharePartMapRole = Qt::UserRole + 1;
    /// A sort key that means "no value": such rows go last in either direction.
    static constexpr int UndefinedRole = Qt::UserRole + 2;
    /// A column's other sort value (MFC "4-way sorting"): this session's figure for
    /// Requests / Accepted / Transferred, Kad before eD2K for Shared eD2K|Kad.
    static constexpr int AltSortRole = Qt::UserRole + 3;

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
        // MFC's default-hidden columns, appended so saved layouts keep their indices
        ColFileId,
        ColAccepted,
        ColArtist,
        ColAlbum,
        ColTitle,
        ColLength,
        ColBitrate,
        ColCodec,
        ColCount
    };

    explicit SharedFilesModel(QObject* parent = nullptr);

    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Show a share checkbox on the file-name column. On while a directory is being
    /// browsed, where the list holds unshared files too and ticking one is how the
    /// user shares it — MFC's CSharedFilesCtrl checkbox mode.
    void setBrowseMode(bool on);
    [[nodiscard]] bool browseMode() const { return m_browseMode; }

    /// Bring the list in line with a new snapshot: rows that left are removed, rows
    /// that changed are updated in place, new ones are appended. The view keeps its
    /// selection and scroll position, which a model reset would drop.
    /// @return true when it had to fall back to a reset (rows without a hash, or
    ///         nearly everything changed).
    bool setFiles(std::vector<SharedFileRow> files);

    /// Replace everything with a reset — a browsed directory, whose unshared rows
    /// have no hash to match by.
    void resetFiles(std::vector<SharedFileRow> files);

    /// Add or update single rows (a push from the daemon).
    void upsertFiles(std::vector<SharedFileRow> files);
    /// Drop the row of @p hexHash. False if there is none.
    bool removeFile(const QString& hexHash);

    void clear();

    [[nodiscard]] int fileCount() const { return count(); }

    /// Get the file hash for a row index.
    [[nodiscard]] QString hashAt(int row) const;

    /// Get the full row for a row index (nullptr if out of range).
    [[nodiscard]] const SharedFileRow* fileAt(int row) const { return rowAt(row); }

    /// Check if a file with the given hex hash is in the shared files list.
    [[nodiscard]] bool containsHash(const QString& hexHash) const;

    /// Row for @p hexHash, or nullptr. Lets callers hold a hash instead of a row pointer —
    /// rows move when others are removed, so any kept pointer dangles after a change.
    [[nodiscard]] const SharedFileRow* findByHash(const QString& hexHash) const;

signals:
    /// The user ticked or unticked a file's share checkbox. The panel turns this into
    /// a SetFileShared request; the model does not change the row itself, so the list
    /// only moves once the daemon has agreed.
    void shareToggleRequested(const QString& filePath, bool shared);

protected:
    [[nodiscard]] int columnCountValue() const override { return ColCount; }

private:
    /// The daemon sends hashes in upper case, the GUI's own are lower case.
    [[nodiscard]] static QString keyOf(const QString& hexHash) { return hexHash.toLower(); }
    void reindex();

    bool m_browseMode = false;
    /// keyOf(hash) -> row. Rows without a hash (browse mode) are not in it.
    QHash<QString, int> m_rowOf;
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
    SpecificDir,    ///< Exact directory match
    /// Part files of one category; the filter "path" is the category index
    /// (MFC SDI_TEMP with m_nCatFilter, SharedDirsTreeCtrl.cpp:356-367).
    IncompleteCategory
};

/// The sub-nodes MFC hangs under "Incoming Files" and "Incomplete Files" once there
/// is more than one category (SharedDirsTreeCtrl.cpp:324-368).
struct SharedCategoryNodes {
    QStringList incomingDirs;                    ///< distinct, other than the main one
    QList<std::pair<int, QString>> incomplete;   ///< category index and title
};

/// @param categories  (title, incoming dir) per category, index 0 first
[[nodiscard]] SharedCategoryNodes sharedCategoryNodes(const QList<std::pair<QString, QString>>& categories,
                                                      const QString& mainIncomingDir);

class SharedFilesSortProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setFolderFilter(SharedFilterType type, const QString& path = {});

    /// MFC CSharedFilesCtrl::IsFilteredOut: every token must be in the text of
    /// @p column, a "-token" must not. Applied on top of the folder filter.
    void setTextFilter(const QStringList& tokens, int column);

    /// Sort @p column by its other value (SharedFilesModel::AltSortRole).
    void setAltSort(int column, bool alt);

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    SharedFilterType m_filterType = SharedFilterType::AllShared;
    QString m_filterPath;
    QStringList m_tokens;
    int m_tokenColumn = 0;
    QSet<int> m_altSortColumns;
};

} // namespace eMule
