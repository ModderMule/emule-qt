#pragma once

/// @file IndexerFeedMatch.h
/// @brief Whether a feed acts on a row. No I/O, on purpose.
///
/// Split out from IndexerFeedList the way IndexerQuery is split from
/// IndexerClient, and for the same reason: this is the part that decides what
/// gets downloaded unattended, so it should be testable without a network, a
/// timer or a queue behind it.
///
/// Two rules here are not obvious from the code and are the reason this file has
/// its own tests:
///
///   - **A rule that cannot be evaluated is not a pass.** An invalid regex makes
///     the whole filter invalid and the feed does not act. The tempting
///     alternative — drop the bad pattern and carry on — fails in the permissive
///     direction: a broken *reject* pattern that matches nothing lets everything
///     through, which for an automatic downloader is the expensive mistake.
///   - **A value the indexer did not report is neither a match nor a mismatch.**
///     No size and no date are both common; treating either as zero would filter
///     out every row from an indexer that omits it.

#include "IndexerFeed.h"
#include "IndexerResult.h"

#include <QRegularExpression>
#include <QString>

namespace eMule::indexer {

/// A feed's patterns, compiled once per poll rather than once per row.
struct CompiledFeedFilter {
    QRegularExpression accept;
    QRegularExpression reject;

    /// Both patterns compiled. False means the feed must not act at all.
    bool valid = true;

    /// Why not, for the feed's status line. Empty when valid.
    QString error;

    [[nodiscard]] bool hasAccept() const { return !accept.pattern().isEmpty(); }
    [[nodiscard]] bool hasReject() const { return !reject.pattern().isEmpty(); }
};

[[nodiscard]] CompiledFeedFilter compileFeedFilter(const IndexerFeed& feed);

/// Whether @p row passes @p feed's filters. Undefined unless @p filter is valid;
/// callers check that first and skip the whole poll when it is not.
[[nodiscard]] bool feedAccepts(const IndexerFeed& feed, const CompiledFeedFilter& filter,
                               const IndexerResult& row);

} // namespace eMule::indexer
