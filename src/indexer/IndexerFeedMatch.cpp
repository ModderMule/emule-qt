#include "pch.h"
/// @file IndexerFeedMatch.cpp
/// @brief Whether a feed acts on a row — implementation.

#include "IndexerFeedMatch.h"

namespace eMule::indexer {

namespace {

/// Compile one pattern, or explain why it would not.
bool compileOne(const QString& pattern, const QString& label, QRegularExpression& out,
                QString& error)
{
    if (pattern.trimmed().isEmpty())
        return true;

    out = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
    if (out.isValid())
        return true;

    error = QStringLiteral("%1 pattern: %2").arg(label, out.errorString());
    return false;
}

} // namespace

CompiledFeedFilter compileFeedFilter(const IndexerFeed& feed)
{
    CompiledFeedFilter out;
    if (!compileOne(feed.accept, QStringLiteral("accept"), out.accept, out.error)
        || !compileOne(feed.reject, QStringLiteral("reject"), out.reject, out.error)) {
        out.valid = false;
    }
    return out;
}

bool feedAccepts(const IndexerFeed& feed, const CompiledFeedFilter& filter,
                 const IndexerResult& row)
{
    // Reject first and unconditionally. A row matching both patterns is excluded:
    // an exclusion the user wrote is more specific than an inclusion, and this is
    // the direction that errs towards not downloading.
    if (filter.hasReject() && filter.reject.match(row.title).hasMatch())
        return false;

    if (filter.hasAccept() && !filter.accept.match(row.title).hasMatch())
        return false;

    // size == 0 means the indexer did not report one, not that the release is
    // empty -- a row is dropped at parse time only for a missing title or URL.
    // Applying a bound to it would silently empty every feed on an indexer that
    // omits the attribute.
    if (row.size > 0) {
        if (feed.minSize > 0 && row.size < feed.minSize)
            return false;
        if (feed.maxSize > 0 && row.size > feed.maxSize)
            return false;
    }

    // Same for the date: ageDays() returns -1 when the indexer gave none.
    if (feed.maxAgeDays > 0) {
        const int age = row.ageDays();
        if (age >= 0 && age > feed.maxAgeDays)
            return false;
    }

    return true;
}

} // namespace eMule::indexer
