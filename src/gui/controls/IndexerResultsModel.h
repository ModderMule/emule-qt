#pragma once

/// @file IndexerResultsModel.h
/// @brief Table model for newznab/torznab search results.
///
/// A sibling of SearchResultsModel, not a variant of it. An ED2K row is an MD4
/// hash with source counts and media tags; an indexer row is a title, an age, a
/// category and a poster count, with no hash and no notion of sources. One model
/// covering both would show half its columns empty whichever kind it held.
///
/// The Search panel keeps one model per tab, so a Usenet tab binds this and an
/// ED2K tab binds SearchResultsModel — the view already swaps models on tab
/// change, which is what makes two row shapes possible in one panel.

#include "AbstractTableModel.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace eMule {

/// One indexer hit. The download URL is deliberately absent: it carries the API
/// key, so the daemon keeps it and the GUI refers to a row by `id`.
struct IndexerResultRow {
    QString id;
    QString indexerName;
    QString title;
    int64_t size = 0;
    int64_t published = 0;   ///< Unix seconds; 0 when the indexer gave no date.
    int ageDays = -1;
    QString category;
    int grabs = -1;
    int files = -1;
    bool passwordProtected = false;

    // Parsed and carried for a future BitTorrent module; the columns stay hidden
    // until something can act on them.
    int seeders = -1;
    int peers = -1;
    bool isUsenet = true;
};

class IndexerResultsModel : public AbstractTableModel<IndexerResultRow> {
    Q_OBJECT

public:
    enum Column {
        ColTitle = 0,
        ColSize,
        ColAge,
        ColCategory,
        ColGrabs,
        ColIndexer,
        ColSeeders,   ///< Hidden until a BitTorrent module exists.
        ColPeers,     ///< Likewise.
        ColCount
    };

    explicit IndexerResultsModel(QObject* parent = nullptr);

    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Append a batch. Results arrive per indexer as each one answers, so this is
    /// an append rather than a reset — and appending keeps the user's selection,
    /// which a reset would destroy every time a second indexer replied.
    void addResults(const std::vector<IndexerResultRow>& rows);

    [[nodiscard]] int resultCount() const { return count(); }

    /// Result id at @p row, the key a grab and a selection restore both use.
    [[nodiscard]] QString idAt(int row) const;

    [[nodiscard]] const IndexerResultRow* resultAt(int row) const { return rowAt(row); }

protected:
    [[nodiscard]] int columnCountValue() const override { return ColCount; }
};

} // namespace eMule
