#pragma once

/// @file IndexerResult.h
/// @brief One search hit, and the RSS reader that produces them.
///
/// Deliberately **not** SearchFile. An ED2K result is an MD4 hash with source
/// counts and ED2K media tags; an indexer result is a title, an age, a category
/// and a download URL, with no hash and no notion of sources. Forcing them into
/// one model would lose columns from both.
///
/// One row type covers newznab and torznab, because one endpoint can serve both
/// and an aggregator's answer may mix them. The torznab fields are parsed and
/// carried today with nothing consuming them — that is the point of building the
/// module shared rather than building it twice.

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

namespace eMule::indexer {

struct IndexerResult {
    /// Stable within a search: the indexer's slug plus the item guid. This is
    /// what GrabIndexerResult names and what the GUI restores a selection by, so
    /// it must survive a model reset and must not be a row index.
    QString id;

    /// Which account produced the row. Shown as a column, because with several
    /// indexers configured "who says so" is part of the answer.
    QString indexerName;

    QString title;
    QString guid;

    /// The .nzb or .torrent to fetch. Frequently carries the API key in its
    /// query, which is why grabbing happens daemon-side and this never reaches
    /// the GUI.
    QUrl downloadUrl;

    qint64 size = 0;
    QDateTime published;

    /// Resolved against the indexer's caps when possible, else the raw ids.
    QString category;
    QList<int> categoryIds;

    // -- newznab ------------------------------------------------------------
    int grabs = -1;              ///< -1 when the indexer does not report it.
    int files = -1;
    QString poster;
    QString group;
    bool passwordProtected = false;

    // -- torznab ------------------------------------------------------------
    // Parsed now, consumed when a BitTorrent module exists. See §7.3.
    int seeders = -1;
    int peers = -1;
    QUrl magnetUrl;
    QString infoHash;

    /// A row is Usenet-shaped when it has no torrent payload. An aggregator can
    /// return both kinds from one query, so the *row* decides where a grab goes,
    /// not the account's `kind`.
    [[nodiscard]] bool isUsenet() const { return magnetUrl.isEmpty() && infoHash.isEmpty(); }

    /// Days since posting, or -1 when the indexer gave no date.
    [[nodiscard]] int ageDays() const;

    /// Cross-indexer dedup key. **Heuristic**: the same release carries a
    /// different guid at every indexer, so there is nothing exact to match on
    /// and this falls back to title plus size. It will occasionally merge two
    /// distinct releases that share both, and miss a duplicate whose title
    /// differs by a tag. Do not let a caller assume it is precise.
    [[nodiscard]] QString dedupKey() const;
};

/// What one `t=search` response yielded.
struct IndexerSearchPage {
    QList<IndexerResult> results;
    int offset = 0;
    int total = -1;   ///< From newznab:response/@total; -1 when absent.
    QString error;    ///< Non-empty when the indexer returned an <error> document.
};

/// Parse an RSS 2.0 search response.
///
/// @p indexerName and @p slug tag every row with its origin and build its id.
/// A document that is an `<error>` rather than an `<rss>` comes back with
/// `error` set and no rows — see parseIndexerError().
[[nodiscard]] IndexerSearchPage parseIndexerSearch(const QByteArray& xml,
                                                   const QString& indexerName,
                                                   const QString& slug);

} // namespace eMule::indexer
