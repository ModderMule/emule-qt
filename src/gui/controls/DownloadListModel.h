#pragma once

/// @file DownloadListModel.h
/// @brief Tree model for the downloads list in the Transfer window.
///
/// Top-level rows are downloads (PartFiles). Each download can have child rows
/// representing source clients, shown when the user expands the item.

#include "controls/CategoryFilterProxy.h"

#include <QAbstractItemModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

namespace eMule {

/// Row data for one source client shown under an expanded download.
struct SourceRow {
    QString userName;
    QString software;
    QString downloadState;   // state token, see downloadStateText()
    int64_t remoteQueueRank = 0;
    bool remoteQueueFull = false;
    int64_t transferredDown = 0;
    int64_t sessionDown = 0;
    int64_t datarate = 0;      // download speed from this source
    int availPartCount = 0;
    int partCount = 0;
    int sourceFrom = 0;        // SourceFrom enum
    QString userHash;
    int64_t ip = 0;      ///< eD2K byte order; 0 for an IPv6 peer — prefer addr.
    QString addr;        ///< Literal address, both families. Empty when unknown.
    QString cc;          ///< ISO country code (GeoLite2), empty when unknown.
    int64_t port = 0;
    int softwareId = -1;
    bool hasCredit = false;
    bool isFriend = false;
    QByteArray partMap;  // per-part: 0=no, 1=both, 2=client-only, 3=pending, 4=receiving
    int kadPort = 0;
    int kadVersion = 0;
    bool hasOtherRequests = false;  ///< also wanted for other files
    // A4AF row: a source of this file that is currently asking another one
    bool a4af = false;
    bool noNeededHere = false;      ///< nothing we need of this file
    bool swapSuspended = false;
    QString otherFileName;          ///< the file it is asking for
};

/// Byte-exact inputs of MFC's CPartFile::DrawStatusBar, as the bar delegate reads them.
/// Ranges are flat [start, end, start, end, ...] pairs with inclusive ends.
struct DownloadBarData {
    int64_t fileSize = 0;
    QList<qint64> gaps;
    QList<qint64> pending;
    QList<quint16> partFreq;
    QByteArray partMap;
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
    int availableSrcCount = 0;   ///< sources on queue or downloading
    int a4afSrcCount = 0;
    bool isPaused = false;
    bool isStopped = false;
    /// PartFileOp ordinal: 0 none, 1 hashing, 2 copying, 3 uncompressing, 4 importing.
    int fileOp = 0;
    bool completionError = false;
    bool isAutoDownPriority = false;
    int64_t category = 0;
    int64_t lastSeenComplete = 0;
    int completeSourcesLo = 0;
    int completeSourcesHi = 0;
    int64_t timeRemaining = -1;   ///< seconds, -1 unknown
    int64_t downTransferred = 0;  ///< bytes received for this file
    int privateMaxSources = 0;    ///< the file's own source limit, 0 = global
    bool previewPrio = false;     ///< first and last part early, for this file
    bool pauseOnPreview = false;  ///< pause once a preview is possible
    bool hashsetNeeded = false;   ///< no part hashes yet
    int64_t lastReception = 0;
    int64_t addedOn = 0;
    QString fileType;
    int64_t requests = 0;
    int64_t acceptedRequests = 0;
    int64_t upTransferred = 0;      // uploaded of this file, all time
    QByteArray partMap;  // per-part status: 0=done, 1=no-src, 2-254=src-freq, 255=downloading
    QList<qint64> gaps;       ///< flat [start, end] pairs, inclusive
    QList<qint64> pending;    ///< requested blocks not yet received, same shape
    QList<quint16> partFreq;  ///< source count per part
    bool hasBarRanges = false; ///< daemon sent gaps; else the bar falls back to partMap
    bool isPreviewPossible = false;

    // Comment/rating, as MFC's indicator reads them. userRating is the wire
    // value, so 6 means "a Kad note lookup is running".
    bool hasComment = false;
    int userRating = 0;

    // The file's own bytes contradict its name. Independent of the rating above:
    // that one is other people's opinion, this one is provable.
    bool containerSuspect = false;
    QString containerExpected;   // container the extension promises, e.g. "ASF"
    QString containerActual;     // what it really is; empty when unrecognised

    // Fake-file verdict (ids from the daemon)
    QString confidence;
    int fakeScore = 0;
    QStringList fakeReasons;

    std::vector<SourceRow> sources;  // child rows (populated when expanded)

    /// Model-assigned identity, never 0. Source indexes carry it as internalId.
    quintptr uid = 0;

    /// A completed download (green 100% bar). Completed files have no live
    /// sources, so they are never expandable in the tree.
    [[nodiscard]] bool isComplete() const { return status == QLatin1String("complete"); }
};

/// Tree model backing the downloads view in the Transfer panel.
/// Top-level items are downloads; children are source clients.
class DownloadListModel : public QAbstractItemModel {
    Q_OBJECT

public:
    /// Custom data roles for the progress column delegate.
    // eMule::kCategoryRole (CategoryFilterProxy.h) is also answered here. It
    // lives there rather than in either model so the two cannot drift apart on
    // its value, and so the proxy needs to know neither of them.
    static constexpr int PartMapRole = Qt::UserRole + 1;
    static constexpr int PausedRole  = Qt::UserRole + 2;
    /// DownloadBarData for a file row's progress cell; invalid on source rows.
    static constexpr int BarDataRole = Qt::UserRole + 3;
    /// True on a source row that asked for another file.
    static constexpr int A4afRole    = Qt::UserRole + 4;

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
        ColCountry,          ///< source rows only (MorphXT IP2Country)
        ColConfidence,       ///< file rows only: fake-file verdict
        ColTransferred,      ///< MFC column 2; appended so saved layouts keep their indexes
        ColCount
    };

    explicit DownloadListModel(QObject* parent = nullptr);

    /// Sort Remaining by the bytes left instead of the time (MFC m_bRemainSort).
    void setRemainingSortBySize(bool bySize) { m_remainingSortBySize = bySize; }

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

    /// Category titles by index, so the Category column can show a name instead
    /// of a number. Index 0 ("All") is present but never displayed — a download
    /// in it is uncategorised. Pushed in by TransferPanel, which owns the list.
    void setCategoryNames(QStringList names);

    /// Check if an index represents a source row (child of a download).
    [[nodiscard]] bool isSourceRow(const QModelIndex& index) const;

    /// Get the source row for a child index (nullptr if not a source row or out of range).
    [[nodiscard]] const SourceRow* sourceAt(const QModelIndex& index) const;

    /// Check if a file with the given hex hash is in the download list.
    [[nodiscard]] bool containsHash(const QString& hexHash) const;

    /// Find a download row by its hex hash (nullptr if not found).
    [[nodiscard]] const DownloadRow* findByHash(const QString& hexHash) const;

private:
    bool m_remainingSortBySize = false;
    /// The Status column's text, reproducing MFC CPartFile::getPartfileStatus
    /// (srchybrid/PartFile.cpp:3412-3453). The daemon sends the raw enum token —
    /// "ready", "empty" — which is the right wire format and the wrong thing to show
    /// a user; MFC shows "Downloading" or "Waiting" depending on whether any source is
    /// actually sending.
    [[nodiscard]] QString statusText(const DownloadRow& d) const;

    /// The Status cell of a source row.
    [[nodiscard]] QString sourceStatusText(const SourceRow& s) const;

    /// Sort order for the same column, so it groups the way MFC's does rather than
    /// alphabetically by token. srchybrid/PartFile.cpp:3456-3476.
    [[nodiscard]] static int statusRank(const DownloadRow& d);

    /// Row of the download with this uid, -1 if it has gone.
    [[nodiscard]] int rowOfUid(quintptr uid) const;
    void reindexRows();

    std::vector<DownloadRow> m_downloads;
    QHash<quintptr, int> m_rowByUid;
    quintptr m_nextUid = 1;
    QStringList m_categoryNames;
};

/// Sort proxy of the downloads list: plain column sort, except that A4AF sources
/// stay below a file's available ones.
class DownloadSortProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
};

} // namespace eMule

Q_DECLARE_METATYPE(eMule::DownloadBarData)
