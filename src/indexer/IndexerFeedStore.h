#pragma once

/// @file IndexerFeedStore.h
/// @brief What a feed has already seen: one YAML sidecar per feed.
///
/// Not preferences.yml, for the reason IndexerCapsStore gives: this is generated
/// bulk that grows to thousands of lines, and preferences.yml is a file people
/// open and edit by hand. It is also a cache to be rebuilt rather than a setting
/// to be preserved -- losing it costs one re-seed, not a configuration.
///
/// The seen set is the *first* of two layers. It stops a feed spending an
/// indexer API hit and an .nzb fetch on a release it has already handled; the
/// queue's own duplicate guard stops the download itself. That the two overlap
/// is deliberate: this file is capped and evicts, and the duplicate guard is
/// what catches the release an eviction re-admits.

#include "IndexerFeed.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

namespace eMule::indexer {

/// What became of one row the feed decided to look at.
enum class FeedSeenState : quint8 {
    /// Present on the feed's first poll and deliberately not acted on. The bulk
    /// of a new feed's file, and the reason a first poll is safe.
    Seeded = 0,
    Added,      ///< handed to the queue and accepted
    Have,       ///< the queue already had it -- terminal, and not a failure
    Failed,     ///< given up on after kMaxGrabAttempts
    Pending,    ///< a fetch failed; worth another attempt
};

[[nodiscard]] QString feedSeenStateToString(FeedSeenState state);
[[nodiscard]] FeedSeenState feedSeenStateFromString(const QString& text);

struct FeedSeenEntry {
    QString guid;
    QDateTime at;
    FeedSeenState state = FeedSeenState::Seeded;
    int attempts = 0;

    /// Kept only for the states a person might want to read back. Seeded entries
    /// are the bulk and carry none, which is what keeps the file to a sensible
    /// size on a busy feed.
    QString title;

    /// Nothing more will be tried for this row.
    [[nodiscard]] bool isTerminal() const
    {
        return state != FeedSeenState::Pending;
    }
};

/// One feed's history and its last-poll report.
struct FeedState {
    QString name;
    QDateTime lastPolled;
    QString lastError;
    int lastMatched = 0;

    /// Accounts whose backlog has already been recorded.
    ///
    /// Per account rather than per feed, because adding a second indexer to a
    /// feed that has run for a month makes that account's whole retention window
    /// new to it. A per-feed flag would let that straight through.
    QStringList seededAccounts;

    QList<FeedSeenEntry> seen;

    /// Oldest first, so eviction is a prefix removal.
    void recordSeen(const FeedSeenEntry& entry);
    [[nodiscard]] const FeedSeenEntry* find(const QString& guid) const;
    [[nodiscard]] bool hasSeeded(const QString& account) const;

    /// Drop the oldest entries until the set is within cap.
    void evict(int cap);

    /// A URL feed has no account behind it, so its seed marker needs a name that
    /// no real account can collide with.
    static QString urlSeedKey();
};

class IndexerFeedStore {
public:
    /// `<configDir>/Feeds`, created on demand.
    [[nodiscard]] static QString directory();

    [[nodiscard]] static QString pathFor(const IndexerFeed& feed);

    /// False when there is no sidecar, or it is unreadable. Neither is an error
    /// worth surfacing -- but it *is* worth knowing, because a feed with no
    /// history seeds instead of acting, and that is the safe direction.
    [[nodiscard]] static bool load(const IndexerFeed& feed, FeedState& out);

    static bool save(const IndexerFeed& feed, const FeedState& state);

    /// Drop the sidecar for a feed the user deleted, so a later feed reusing the
    /// name does not inherit a stranger's history -- and, more to the point, does
    /// not skip its own seed pass because of it.
    static void remove(const IndexerFeed& feed);

    /// Sidecars for feeds that no longer exist. Called after a settings save.
    static void removeOrphans(const QList<IndexerFeed>& feeds);

    /// Entries kept per feed. Generous: the set only holds seeds and rows the
    /// feed acted on, since a filtered-out row is deliberately never recorded.
    static constexpr int kMaxSeenEntries = 2000;
};

} // namespace eMule::indexer
