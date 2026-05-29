#pragma once

/// @file SearchResultsModel.h
/// @brief Table model for search results in the Search window.

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
    bool isSpam = false;
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
    void setResults(std::vector<SearchResultRow> results) { setRows(std::move(results)); }

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
};

} // namespace eMule
