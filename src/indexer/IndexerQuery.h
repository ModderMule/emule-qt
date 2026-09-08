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

    /// The `t=` mode. Defaults to plain keyword search; the typed modes exist
    /// for a future "TV" / "Movies" search form and are gated on caps.
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
};

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
