#pragma once

/// @file IndexerCaps.h
/// @brief The `t=caps` document: what one indexer can actually be asked.
///
/// This is what "keyword search if supported by given provider" means in
/// practice. An indexer advertises which search modes exist, which parameters
/// each accepts, how many rows it will return per call, and its category tree.
/// The GUI greys out what is not advertised rather than sending a query the
/// indexer will reject.
///
/// Capabilities are **cached, not configured** — they belong to the indexer, not
/// to the user's settings — so they live in a sidecar (IndexerCapsStore) and
/// never in preferences.yml, whose category tree would run to a hundred nodes in
/// a file people edit by hand.

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace eMule::indexer {

/// The XML namespaces the two flavours declare. Kept here because both the caps
/// reader and the search reader need them.
inline constexpr QStringView kNewznabNs{
    u"http://www.newznab.com/DTD/2010/feeds/attributes/"};
inline constexpr QStringView kTorznabNs{u"http://torznab.com/schemas/2015/feed"};

/// Search-mode names as they appear in `t=`.
inline constexpr QStringView kModeSearch{u"search"};
inline constexpr QStringView kModeTvSearch{u"tv-search"};
inline constexpr QStringView kModeMovieSearch{u"movie-search"};
inline constexpr QStringView kModeAudioSearch{u"audio-search"};
inline constexpr QStringView kModeBookSearch{u"book-search"};

struct IndexerCategory {
    int id = 0;
    QString name;
    QList<IndexerCategory> subcategories;
};

struct IndexerSearchMode {
    bool available = false;
    QStringList supportedParams;   ///< "q", "season", "ep", "imdbid", …
};

struct IndexerCaps {
    QString serverTitle;

    /// From `<limits max default/>`. A request must clamp to `max` — asking for
    /// more is not an error, it is silently truncated, and a caller that assumed
    /// otherwise pages wrong.
    int limitMax = 100;
    int limitDefault = 100;

    /// Keyed by mode name; absent means the indexer did not mention it.
    QHash<QString, IndexerSearchMode> modes;

    QList<IndexerCategory> categories;

    /// When this document was fetched. Drives the refresh interval; an empty
    /// value means "never probed", which is not an error and never blocks a
    /// search.
    QDateTime probedAt;

    [[nodiscard]] bool isEmpty() const { return modes.isEmpty() && categories.isEmpty(); }

    /// Whether @p mode exists and is advertised as available.
    [[nodiscard]] bool supportsMode(QStringView mode) const;

    /// Whether @p mode accepts @p param. An indexer that advertises the mode but
    /// lists no supportedParams is treated as accepting the basics rather than
    /// nothing — several real indexers omit the attribute entirely.
    [[nodiscard]] bool supportsParam(QStringView mode, QStringView param) const;

    /// Human-readable name for a category id, walking into subcategories.
    /// Returns an empty string when the tree does not know it.
    [[nodiscard]] QString categoryName(int id) const;
};

/// Parse a `t=caps` document. Returns an empty result with @p error set when the
/// payload is an `<error>` document or is not XML at all.
[[nodiscard]] IndexerCaps parseIndexerCaps(const QByteArray& xml, QString& error);

/// Detect the newznab/torznab `<error code= description=/>` document.
///
/// **This must run before any response is treated as data.** Indexers routinely
/// return their errors with HTTP 200 — "Incorrect user credentials" arrives as a
/// perfectly successful HTTP response — so a client that only checks
/// QNetworkReply::error() reports success and then parses zero rows.
///
/// Returns the description (falling back to the code) or an empty string when
/// the document is not an error.
[[nodiscard]] QString parseIndexerError(const QByteArray& xml);

} // namespace eMule::indexer
