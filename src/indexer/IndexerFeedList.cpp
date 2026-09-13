#include "pch.h"
/// @file IndexerFeedList.cpp
/// @brief Saved feeds, polled on a schedule — implementation.

#include "IndexerFeedList.h"

#include "IndexerCapsStore.h"
#include "IndexerClient.h"
#include "IndexerQuery.h"

#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "utils/Log.h"

#include <QPointer>
#include <QTimer>

#include <QSet>

#include <algorithm>

namespace eMule::indexer {

IndexerFeedList* theIndexerFeeds = nullptr;

IndexerFeedList::IndexerFeedList(QObject* parent)
    : QObject(parent)
    , m_client(new IndexerClient(this))
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &IndexerFeedList::onTick);
    m_timer->start();
}

IndexerFeedList::~IndexerFeedList() = default;

void IndexerFeedList::setNzbSink(NzbSink sink)
{
    m_sink = std::move(sink);
}

void IndexerFeedList::applyPreferences()
{
    m_feeds = thePrefs.indexerFeeds();
    m_accounts = thePrefs.indexers();

    // Rebuild the entry map, carrying over the live state of every feed that
    // survived. A poll in flight keeps its `polling` flag and its harvest — the
    // callbacks re-find their entry by key rather than holding a pointer, so a
    // settings save mid-poll costs nothing.
    QHash<QString, Entry> rebuilt;
    for (const auto& feed : m_feeds) {
        const QString key = feed.key();
        Entry entry;
        if (auto it = m_entries.find(key); it != m_entries.end()) {
            entry = std::move(*it);
        } else if (!IndexerFeedStore::load(feed, entry.state)) {
            // No history: the feed seeds on its next poll rather than acting.
            entry.state = FeedState{};
            entry.state.name = feed.name;
        }
        entry.feed = feed;
        rebuilt.insert(key, std::move(entry));
    }
    m_entries = std::move(rebuilt);

    IndexerFeedStore::removeOrphans(m_feeds);

    for (const auto& entry : m_entries)
        publish(entry);
}

QList<IndexerFeedStatus> IndexerFeedList::statuses() const
{
    QList<IndexerFeedStatus> out;
    out.reserve(m_feeds.size());
    // Feed order, not hash order: the list the user arranged is the list they
    // expect back.
    for (const auto& feed : m_feeds)
        out.append(statusFor(feed.name));
    return out;
}

IndexerFeedStatus IndexerFeedList::statusFor(const QString& name) const
{
    IndexerFeedStatus status;
    status.name = name;

    const auto it = m_entries.constFind(IndexerFeed{.name = name}.key());
    if (it == m_entries.constEnd())
        return status;

    status.name = it->feed.name;
    status.lastPolled = it->state.lastPolled;
    status.lastError = it->state.lastError;
    status.lastMatched = it->state.lastMatched;
    status.seenCount = int(it->state.seen.size());
    status.polling = it->polling;
    return status;
}

bool IndexerFeedList::pollNow(const QString& name, QString& error)
{
    if (name.isEmpty()) {
        bool any = false;
        for (auto& entry : m_entries) {
            if (!entry.feed.enabled)
                continue;
            entry.forced = true;
            any = true;
        }
        if (!any) {
            error = tr("There are no enabled feeds to check.");
            return false;
        }
        // Deliberately not polling them all here: the tick's one-per-turn rule
        // is what keeps a "check everything" click from opening one request per
        // feed at the same instant.
        onTick();
        return true;
    }

    Entry* entry = entryFor(IndexerFeed{.name = name}.key());
    if (entry == nullptr) {
        error = tr("There is no feed called \"%1\".").arg(name);
        return false;
    }
    if (entry->polling) {
        // Not an error: the thing the user asked for is already happening.
        return true;
    }

    // Deliberately not checking `enabled` here, where onTick() does. The switch
    // governs *automatic* activity; an explicit request always works — the same
    // convention networkED2K and kadEnabled follow for manual connects.
    pollFeed(*entry);
    return true;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void IndexerFeedList::onTick()
{
    // One feed per turn. Five due feeds spread over five minutes, and a daemon
    // start does not open one request per configured feed at once.
    Entry* chosen = nullptr;
    for (auto& entry : m_entries) {
        if (entry.polling || !entry.feed.enabled)
            continue;
        if (!entry.forced && !isDue(entry))
            continue;
        // Forced beats due, so a manual check is not queued behind a schedule.
        if (chosen == nullptr || (entry.forced && !chosen->forced))
            chosen = &entry;
    }

    if (chosen != nullptr)
        pollFeed(*chosen);
}

bool IndexerFeedList::isDue(const Entry& entry) const
{
    if (!entry.state.lastPolled.isValid())
        return true;

    const qint64 elapsed = entry.state.lastPolled.secsTo(QDateTime::currentDateTimeUtc());
    // A clock that went backwards makes elapsed negative. Counting that as due
    // is what VersionChecker does, and it is right here for the same reason:
    // one extra poll is cheap, a feed frozen until the clock catches up is not.
    if (elapsed < 0)
        return true;

    const int minutes = std::max(entry.feed.intervalMinutes, IndexerFeed::kMinIntervalMinutes);
    return elapsed >= qint64(minutes) * 60;
}

void IndexerFeedList::pollFeed(Entry& entry)
{
    entry.forced = false;

    const CompiledFeedFilter filter = compileFeedFilter(entry.feed);
    if (!filter.valid) {
        // The feed does not act at all. Dropping the bad pattern and carrying on
        // would fail permissively: a reject pattern that matches nothing lets
        // everything through.
        entry.state.lastError = filter.error;
        entry.state.lastPolled = QDateTime::currentDateTimeUtc();
        logWarning(QStringLiteral("Feeds: \"%1\" is not checking — %2")
                       .arg(entry.feed.name, filter.error));
        persist(entry);
        publish(entry);
        return;
    }

    entry.harvested.clear();
    entry.sourceOf.clear();
    entry.state.lastError.clear();

    if (entry.feed.kind == IndexerFeedKind::Url)
        startUrlFeed(entry);
    else
        startSavedSearch(entry);
}

void IndexerFeedList::startSavedSearch(Entry& entry)
{
    const QList<IndexerConfig> accounts = accountsFor(entry.feed);
    if (accounts.isEmpty()) {
        // No verdict and no spend. Not recorded as a poll either — marking it
        // polled would push the next attempt a whole interval away for a
        // condition the user may fix in the next minute.
        entry.state.lastError = tr("No enabled indexer serves this feed.");
        publish(entry);
        return;
    }

    entry.polling = true;
    entry.outstanding = int(accounts.size());
    publish(entry);

    const QString key = entry.feed.key();
    for (const auto& account : accounts) {
        IndexerQuery query;
        query.text = entry.feed.query;
        query.categories = entry.feed.categories;
        query.limit = kPageSize;
        query.offset = 0;

        // Whatever the cache has. A feed must never trigger a caps probe: that
        // is IndexerSearchList's job, and buildIndexerSearchUrl() is documented
        // to accept a null one.
        IndexerCaps caps;
        const bool haveCaps = IndexerCapsStore::load(account, caps);

        const QString accountName = account.displayName();
        QPointer<IndexerFeedList> guard(this);
        m_client->search(account, query, haveCaps ? &caps : nullptr,
                         [guard, key, accountName](bool ok, const IndexerSearchPage& page,
                                                   const QString& error) {
            if (guard)
                guard->collect(key, accountName, page, ok, error);
        });
    }
}

void IndexerFeedList::startUrlFeed(Entry& entry)
{
    const QUrl url = entry.feed.feedUrl();
    if (!url.isValid()) {
        entry.state.lastError = tr("The feed URL is not a usable http or https address.");
        publish(entry);
        return;
    }

    entry.polling = true;
    entry.outstanding = 1;
    publish(entry);

    const QString key = entry.feed.key();
    const QString source = entry.feed.name;
    QPointer<IndexerFeedList> guard(this);
    m_client->searchUrl(url, thePrefs.indexerTimeoutSeconds() * 1000, source,
                        [guard, key, source](bool ok, const IndexerSearchPage& page,
                                             const QString& error) {
        if (guard)
            guard->collect(key, FeedState::urlSeedKey(), page, ok, error);
    });
}

void IndexerFeedList::collect(const QString& feedKey, const QString& account,
                              const IndexerSearchPage& page, bool ok, const QString& error)
{
    Entry* entry = entryFor(feedKey);
    if (entry == nullptr)
        return;   // the feed was deleted while its poll was out

    if (!ok) {
        // One failing account costs its share of the rows and nothing more —
        // IndexerSearch's rule, and a feed needs it more, not less.
        const QString line = redactApiKey(error);
        entry->state.lastError = entry->state.lastError.isEmpty()
                                     ? line
                                     : entry->state.lastError + QStringLiteral("; ") + line;
    } else {
        for (const auto& row : page.results) {
            if (row.guid.isEmpty())
                continue;
            entry->harvested.append(row);
            entry->sourceOf.insert(row.guid, account);
        }
    }

    if (--entry->outstanding <= 0)
        beginActing(*entry);
}

void IndexerFeedList::beginActing(Entry& entry)
{
    entry.state.lastPolled = QDateTime::currentDateTimeUtc();
    entry.matched = 0;
    if (theApp.statistics)
        ++theApp.statistics->indexerSession().feedPolls;

    const CompiledFeedFilter filter = compileFeedFilter(entry.feed);

    // Oldest first, so the queue ends up in posting order rather than in
    // whichever order the indexer chose to list them.
    std::stable_sort(entry.harvested.begin(), entry.harvested.end(),
                     [](const IndexerResult& a, const IndexerResult& b) {
                         if (a.published.isValid() != b.published.isValid())
                             return !a.published.isValid();
                         return a.published < b.published;
                     });

    QSet<QString> accountsThisPoll;

    for (const auto& row : entry.harvested) {
        // A torznab row has no .nzb behind it. Dropped before anything else,
        // because handing a magnet's bytes to the sink produces a rejection that
        // then has to be told apart from a real one.
        if (!row.isUsenet())
            continue;

        const QString account = entry.sourceOf.value(row.guid);
        accountsThisPoll.insert(account);

        const FeedSeenEntry* known = entry.state.find(row.guid);
        if (known != nullptr && known->isTerminal())
            continue;

        // Seeding is per account: an indexer added to a feed that has run for a
        // month brings its whole retention window with it, which is the same
        // flood a brand-new feed would cause.
        if (!entry.feed.grabExisting && !entry.state.hasSeeded(account)) {
            // Recorded, not acted on. This is the whole of the first-poll rule:
            // the row is remembered so it is never new again, and nothing is
            // spent on it.
            FeedSeenEntry seed;
            seed.guid = row.guid;
            seed.at = QDateTime::currentDateTimeUtc();
            seed.state = FeedSeenState::Seeded;
            entry.state.recordSeen(seed);
            continue;
        }

        // A row that fails the filter is deliberately *not* recorded. Filtering
        // costs no network, so re-evaluating it every poll is free — and it
        // means relaxing a filter picks up whatever the indexer still lists,
        // instead of the feed being permanently blind to its own history.
        if (!feedAccepts(entry.feed, filter, row))
            continue;

        // A match the first time only: a failed grab keeps its seen entry and
        // comes back through here on every poll until it is terminal.
        if (known == nullptr && theApp.statistics)
            ++theApp.statistics->indexerSession().feedMatches;
        entry.pendingGrabs.append(row);
    }

    // Marked seeded only after the pass, so a row arriving mid-poll from the
    // same account is still covered by this poll's seed.
    for (const QString& account : accountsThisPoll) {
        if (!entry.state.hasSeeded(account))
            entry.state.seededAccounts.append(account);
    }

    grabNext(entry.feed.key());
}

void IndexerFeedList::grabNext(const QString& feedKey)
{
    Entry* entry = entryFor(feedKey);
    if (entry == nullptr)
        return;   // the feed was deleted while its poll was out

    if (entry->pendingGrabs.isEmpty()) {
        endPoll(*entry);
        return;
    }

    const IndexerResult row = entry->pendingGrabs.takeFirst();

    const IndexerConfig* account = nullptr;
    const QString wanted = entry->sourceOf.value(row.guid);
    for (const auto& candidate : m_accounts) {
        if (candidate.displayName() == wanted) {
            account = &candidate;
            break;
        }
    }

    QPointer<IndexerFeedList> guard(this);
    const auto onFetched = [guard, feedKey, row](bool ok, const QByteArray& body,
                                                 const QString& error) {
        if (!guard)
            return;
        Entry* live = guard->entryFor(feedKey);
        if (live == nullptr)
            return;

        if (!ok || body.isEmpty()) {
            FeedSeenEntry record;
            record.guid = row.guid;
            record.at = QDateTime::currentDateTimeUtc();
            record.title = row.title;
            const FeedSeenEntry* known = live->state.find(row.guid);
            record.attempts = (known != nullptr ? known->attempts : 0) + 1;
            // Bounded, and more than one: a single 503 must not lose a release,
            // and a permanently dead URL must not be retried every poll forever.
            record.state = record.attempts >= kMaxGrabAttempts ? FeedSeenState::Failed
                                                               : FeedSeenState::Pending;
            live->state.recordSeen(record);
            logWarning(QStringLiteral("Feeds: \"%1\" could not fetch \"%2\": %3")
                           .arg(live->feed.name, row.title, redactApiKey(error)));
        } else {
            guard->deliver(*live, row, body);
        }

        guard->grabNext(feedKey);
    };

    if (account != nullptr)
        m_client->fetch(*account, row.downloadUrl, onFetched);
    else
        m_client->fetchUrl(row.downloadUrl, thePrefs.indexerTimeoutSeconds() * 1000, onFetched);
}

void IndexerFeedList::deliver(Entry& entry, const IndexerResult& row, const QByteArray& payload)
{
    FeedSeenEntry record;
    record.guid = row.guid;
    record.at = QDateTime::currentDateTimeUtc();
    record.title = row.title;
    const FeedSeenEntry* known = entry.state.find(row.guid);
    record.attempts = (known != nullptr ? known->attempts : 0) + 1;

    if (!m_sink) {
        // Nothing to hand it to. Left pending without spending an attempt: a
        // daemon whose Usenet session has not started must not lose the release.
        record.attempts = known != nullptr ? known->attempts : 0;
        record.state = FeedSeenState::Pending;
        entry.state.recordSeen(record);
        return;
    }

    QString addError;
    const FeedAddOutcome outcome =
        m_sink({entry.feed.name, row.title, payload, row.password,
                entry.feed.downloadCategory},
               addError);

    switch (outcome) {
    case FeedAddOutcome::Added:
        record.state = FeedSeenState::Added;
        ++entry.matched;
        logInfo(QStringLiteral("Feeds: \"%1\" queued \"%2\"")
                    .arg(entry.feed.name, row.title));
        break;
    case FeedAddOutcome::AlreadyHave:
        // Terminal, and not a failure. Without this the queue's own duplicate
        // refusal would read as a transient error and be retried every poll for
        // as long as the indexer keeps listing the release.
        record.state = FeedSeenState::Have;
        break;
    case FeedAddOutcome::Rejected:
        record.state = FeedSeenState::Failed;
        logWarning(QStringLiteral("Feeds: \"%1\" could not queue \"%2\": %3")
                       .arg(entry.feed.name, row.title, addError));
        break;
    case FeedAddOutcome::Retry:
        record.state = record.attempts >= kMaxGrabAttempts ? FeedSeenState::Failed
                                                           : FeedSeenState::Pending;
        break;
    }

    entry.state.recordSeen(record);
}

void IndexerFeedList::endPoll(Entry& entry)
{
    entry.polling = false;
    entry.state.lastMatched = entry.matched;
    entry.state.evict(IndexerFeedStore::kMaxSeenEntries);
    entry.harvested.clear();
    entry.sourceOf.clear();
    entry.pendingGrabs.clear();

    persist(entry);
    publish(entry);
}

QList<IndexerConfig> IndexerFeedList::accountsFor(const IndexerFeed& feed) const
{
    QList<IndexerConfig> out;
    for (const auto& account : m_accounts) {
        if (!account.enabled || !account.servesUsenet())
            continue;
        if (!feed.indexers.isEmpty()
            && !feed.indexers.contains(account.name, Qt::CaseInsensitive)) {
            continue;
        }
        out.append(account);
    }
    return out;
}

IndexerFeedList::Entry* IndexerFeedList::entryFor(const QString& key)
{
    const auto it = m_entries.find(key);
    return it == m_entries.end() ? nullptr : &*it;
}

void IndexerFeedList::publish(const Entry& entry)
{
    IndexerFeedStatus status;
    status.name = entry.feed.name;
    status.lastPolled = entry.state.lastPolled;
    status.lastError = entry.state.lastError;
    status.lastMatched = entry.state.lastMatched;
    status.seenCount = int(entry.state.seen.size());
    status.polling = entry.polling;
    emit feedStatusChanged(status);
}

void IndexerFeedList::persist(Entry& entry)
{
    IndexerFeedStore::save(entry.feed, entry.state);
}

} // namespace eMule::indexer
