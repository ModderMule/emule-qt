#pragma once

/// @file SearchResultsModel.h
/// @brief Table model for search results in the Search window.

#include <QCborArray>
#include <QCborMap>
#include <QHash>
#include <QString>

#include "AbstractTableModel.h"

#include <cstdint>
#include <vector>

namespace eMule {

/// Row data for one search result (SearchFile) in the results list.
struct SearchResultRow {
    QString hash;
    QString fileName;
    QString fileType;
    QString artist;
    QString album;
    QString title;
    QString codec;
    int64_t fileSize = 0;
    int64_t sourceCount = 0;
    int64_t completeSourceCount = 0;
    int64_t length = 0;
    int64_t bitrate = 0;
    int knownType = 0;
    bool isKad = false;         ///< Kad results carry no complete-source count
    bool kadOrigin = false;     ///< server result the server found on Kad: Kad badge
    bool inDirectory = false;   ///< from browsing a client's shared files
    bool isSpam = false;
    bool hasComment = false;
    bool previewPossible = false;   ///< a browsed peer can send preview frames
    int userRating = 0;   // wire value: 6 means a Kad note lookup is running

    // eNode meta row — network from the meta hash (enodemeta::Kind), 0 = eD2K
    int metaKind = 0;
    QString magnet;       ///< torrents only, may be empty
    int64_t metaAgeDays = 0;
    QString metaIndexer;
    QString metaCatalogId;   ///< FT_META_ID, echoed on the metafile fetch
    QCborArray metaServers;  ///< answering servers, [[ip, port], …]

    [[nodiscard]] bool isMeta() const { return metaKind != 0; }
    [[nodiscard]] bool isTorrent() const { return metaKind == 1 || metaKind == 2; }
    [[nodiscard]] bool isUsenet() const { return metaKind == 3; }

    /// ed2k:// link; empty for meta rows (their hash is not an MD4).
    [[nodiscard]] QString ed2kLink() const;
    /// urn:ed2k magnet, or the server's magnet for a meta row (empty for Usenet).
    [[nodiscard]] QString magnetLink() const;
    /// What the daemon needs to fetch a meta row whose search is gone (a restored tab).
    [[nodiscard]] QCborMap metaRef() const;
};

/// Table model backing the search results tree view in the Search panel.
class SearchResultsModel : public AbstractTableModel<SearchResultRow> {
    Q_OBJECT

public:
    enum Column {
        ColFileName = 0,
        ColSize,
        ColAvailability,
        ColComplete,
        ColType,
        ColArtist,
        ColAlbum,
        ColTitle,
        ColLength,
        ColBitrate,
        ColCodec,
        ColKnown,
        ColCount
    };

    explicit SearchResultsModel(QObject* parent = nullptr);

    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Replace all results with a new snapshot.
    void setResults(std::vector<SearchResultRow> results);

    /// Remove a single row by source-model row index.
    void removeRow(int row);

    [[nodiscard]] int resultCount() const { return count(); }

    /// Get the file hash for a row index.
    [[nodiscard]] QString hashAt(int row) const;

    /// Get the full result row for a row index (nullptr if out of range).
    [[nodiscard]] const SearchResultRow* resultAt(int row) const { return rowAt(row); }

    /// Update the knownType for a specific row (triggers dataChanged).
    void setKnownType(int row, int knownType);

    /// Batch-update knownType by hash. Map: hash → knownType.
    void updateKnownTypes(const QHash<QString, int>& typesByHash);

protected:
    [[nodiscard]] int columnCountValue() const override { return ColCount; }

private:
    void updateHasMeta();

    /// Any torrent/Usenet row: eD2K rows then reserve the network badge slot.
    bool m_hasMeta = false;
};

} // namespace eMule
