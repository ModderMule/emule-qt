#pragma once

/// @file IndexerQuery.h
/// @brief What to ask an indexer, and how that becomes a URL.
///
/// Split out from IndexerClient so the URL rules are testable without a network:
/// capability gating, the row limit the indexer actually allows, paging offsets,
/// and — the one that matters most — API-key redaction.

#include "IndexerCaps.h"
#include "IndexerConfig.h"

#include <QList>
#include <QString>
#include <QUrl>

namespace eMule::indexer {

struct IndexerQuery {
    QString text;

    /// The `t=` value: `search`, or a typed one (kQueryTvSearch, kQueryMovie)
    /// from the search form. Typed modes' parameters are gated on caps.
    QString mode = kModeSearch.toString();

    /// Newznab category ids. Empty means every category.
    QList<int> categories;

    int limit = 100;
    int offset = 0;

    // Optional extended parameters. Each is sent only when the indexer's caps
    // advertise it for this mode, because an indexer that does not know a
    // parameter may reject the whole request rather than ignore it.
    QString season;
    QString episode;
    QString imdbId;

    /// Size bounds in bytes, 0 = none. Never sent (newznab has no size
    /// parameter); IndexerSearch drops rows outside them.
    qint64 minSize = 0;
    qint64 maxSize = 0;
};

/// @p query as asked of an indexer: unchanged, unless @p caps were probed and lack
/// its typed mode. Then a plain `t=search`, categories kept, season/episode
/// added to the keywords as "S02E05" (the IMDb id has no keyword form, dropped).
[[nodiscard]] IndexerQuery queryForIndexer(const IndexerQuery& query, const IndexerCaps& caps);

/// Build a `t=caps` request for @p config.
[[nodiscard]] QUrl buildIndexerCapsUrl(const IndexerConfig& config);

/// Build a search request.
///
/// @p caps may be null (nothing probed yet): the query then goes out with the
/// basic parameters only, which every newznab implementation accepts. When caps
/// are present, `limit` is clamped to `limits/@max` and any parameter the mode
/// does not advertise is dropped.
[[nodiscard]] QUrl buildIndexerSearchUrl(const IndexerConfig& config,
                                         const IndexerQuery& query,
                                         const IndexerCaps* caps);

/// The URL with every credential-shaped query parameter replaced by
/// `<redacted>` — `apikey`, but also the `r` that newznab's own RSS endpoint
/// uses and which a pasted feed URL therefore carries.
///
/// **Every log line, error string and IPC field that carries a URL must go
/// through this.** The key is a query parameter, so an unredacted URL in
/// log.log, in a message shown to the user, or in a bug report hands the key to
/// whoever reads it. There is no second layer catching this.
[[nodiscard]] QString redactApiKey(const QUrl& url);

/// Same, for text that merely contains a URL — Qt's own network error strings
/// embed the request URL, so they are exactly as dangerous as the URL itself.
[[nodiscard]] QString redactApiKey(const QString& text);

} // namespace eMule::indexer
