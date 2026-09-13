#pragma once

/// @file IndexerFeedList.h
/// @brief Saved feeds, polled on a schedule; the first thing here that spends
///        anything unattended.
///
/// Sibling of IndexerSearchList: DaemonApp owns one, publishes it as
/// theIndexerFeeds, and the IPC handlers reach it there. It holds its own
/// IndexerClient rather than sharing that one, so a feed poll and a user's
/// search cannot cancel each other, and it never probes capabilities -- it reads
/// whatever IndexerCapsStore already has and goes out ungated when there is
/// none, which buildIndexerSearchUrl() explicitly allows.
///
/// **It does not know the Usenet module exists.** indexer -> usenet is a
/// dependency this project does not have (docs/indexer-module.md), so the .nzb
/// goes out through an injected sink and DaemonApp -- the one component that
/// sees both -- decides what to do with it. The sink answers synchronously
/// because UsenetQueue::addNzb() does, which is what keeps the bookkeeping here
/// a straight line rather than a second state machine.
///
/// Three rules the code will not tell you on its own:
///
///   - **A feed's first poll adds nothing.** Everything an indexer still lists
///     is new to a feed that has never run, so acting on the first answer means
///     downloading the whole retention window for the query. Seeding is per
///     *account*, not per feed, because adding an indexer to an old feed has
///     exactly the same shape.
///   - **A filter that will not compile stops the feed.** Dropping the bad
///     pattern and carrying on fails permissively, and for an automatic
///     downloader that is the expensive direction.
///   - **One feed per tick.** Not a fairness nicety: it is what keeps five due
///     feeds from opening five simultaneous requests at daemon start.

#include "IndexerConfig.h"
#include "IndexerFeed.h"
#include "IndexerFeedMatch.h"
#include "IndexerFeedStore.h"
#include "IndexerResult.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

class QTimer;

namespace eMule::indexer {

class IndexerClient;

/// What the sink did with an .nzb.
///
/// Deliberately not UsenetAddOutcome: naming that type here would be the
/// dependency this class exists to avoid. DaemonApp maps one onto the other.
enum class FeedAddOutcome : quint8 {
    Added = 0,
    AlreadyHave,  ///< the queue has it -- terminal, and not a failure
    Rejected,     ///< unusable payload; never worth fetching again
    Retry,        ///< transient; worth another attempt on a later poll
};

/// One .nzb, on its way out of the module.
struct FeedAddRequest {
    QString feedName;
    QString title;
    QByteArray payload;

    /// The archive passphrase the feed advertised, empty when it advertised
    /// none. An *automatic* add, so the NZB's own metadata still outranks it —
    /// see UsenetQueue::addNzb().
    QString password;

    /// The download category the feed files its matches into, 0 for none.
    ///
    /// An opaque int on the way past: this module still names nothing in
    /// eMule::Usenet, and what the number indexes is the sink's business.
    int downloadCategory = 0;
};

/// What the GUI is shown about a feed. Everything here is a report, not a
/// setting -- the settings live in IndexerFeed.
struct IndexerFeedStatus {
    QString name;
    QDateTime lastPolled;
    QString lastError;
    int lastMatched = 0;
    int seenCount = 0;
    bool polling = false;
};

class IndexerFeedList : public QObject {
    Q_OBJECT

public:
    explicit IndexerFeedList(QObject* parent = nullptr);
    ~IndexerFeedList() override;

    using NzbSink = std::function<FeedAddOutcome(const FeedAddRequest&, QString& error)>;

    /// Where a matched .nzb goes. Without one the poller still runs and still
    /// records what it saw, but never grabs -- which is the right behaviour for
    /// a daemon whose Usenet session failed to start, and is what the tests use.
    void setNzbSink(NzbSink sink);

    /// Re-read the feed list and the accounts, load each feed's history, drop
    /// the sidecars of feeds that are gone. Called at startup and on every
    /// settings save, exactly like IndexerSearchList::applyPreferences().
    void applyPreferences();

    [[nodiscard]] QList<IndexerFeed> feeds() const { return m_feeds; }

    [[nodiscard]] QList<IndexerFeedStatus> statuses() const;
    [[nodiscard]] IndexerFeedStatus statusFor(const QString& name) const;

    /// Poll @p name now, ignoring its schedule but not its in-flight guard.
    /// An empty name marks every enabled feed due, which the tick then works
    /// through one at a time rather than all at once.
    bool pollNow(const QString& name, QString& error);

    /// How often the due check runs. A feed's own interval is a multiple of this
    /// in practice, never a divisor -- the floor is fifteen minutes.
    static constexpr int kTickMs = 60 * 1000;

    /// Attempts at fetching one row's .nzb before it is given up on. Bounded so
    /// a permanently dead URL cannot be retried every poll forever, and more
    /// than one so a single 503 does not lose a release.
    static constexpr int kMaxGrabAttempts = 3;

    /// Rows asked for per poll. One page: a feed wants the newest few, and
    /// everything behind them it has by definition already seen.
    static constexpr int kPageSize = 100;

signals:
    void feedStatusChanged(const eMule::indexer::IndexerFeedStatus& status);

private slots:
    void onTick();

private:
    /// One feed's live state: its config, its history, and whether a poll is out.
    struct Entry {
        IndexerFeed feed;
        FeedState state;
        bool polling = false;

        /// Set by pollNow(), cleared when the poll starts. Survives the tick's
        /// one-per-turn rule so a manual check of several feeds still happens.
        bool forced = false;

        /// Accounts still to hear from in the current poll.
        int outstanding = 0;

        /// Rows gathered from every account this poll, merged before any of them
        /// is acted on so the oldest-first ordering is release order and not
        /// whichever indexer answered first.
        QList<IndexerResult> harvested;

        /// Which account produced each row, so seeding can be per account.
        QHash<QString, QString> sourceOf;

        /// Rows that passed the filters, still to be fetched. Worked through one
        /// at a time: a feed matching thirty releases must not open thirty
        /// simultaneous downloads against the indexer that just listed them.
        QList<IndexerResult> pendingGrabs;

        /// Adds this poll, for the status line.
        int matched = 0;
    };

    void pollFeed(Entry& entry);
    void startSavedSearch(Entry& entry);
    void startUrlFeed(Entry& entry);
    void collect(const QString& feedKey, const QString& account,
                 const IndexerSearchPage& page, bool ok, const QString& error);

    /// Sort the harvest, record the seeds, and queue what matched.
    void beginActing(Entry& entry);

    /// Fetch the next queued row, or end the poll when there are none left.
    /// Chained through the fetch callback rather than looped, because the fetch
    /// is asynchronous and a nested event loop here would re-enter the tick.
    void grabNext(const QString& feedKey);

    /// Hand one fetched payload to the sink and record what came back.
    void deliver(Entry& entry, const IndexerResult& row, const QByteArray& payload);

    void endPoll(Entry& entry);
    [[nodiscard]] bool isDue(const Entry& entry) const;
    [[nodiscard]] QList<IndexerConfig> accountsFor(const IndexerFeed& feed) const;
    [[nodiscard]] Entry* entryFor(const QString& key);
    void publish(const Entry& entry);
    void persist(Entry& entry);

    IndexerClient* m_client = nullptr;
    NzbSink m_sink;
    QTimer* m_timer = nullptr;

    QList<IndexerFeed> m_feeds;
    QList<IndexerConfig> m_accounts;

    /// Keyed by IndexerFeed::key().
    QHash<QString, Entry> m_entries;
};

/// The daemon's feed poller, published for the IPC handlers. Null until
/// DaemonApp constructs it; every handler must check.
extern IndexerFeedList* theIndexerFeeds;

} // namespace eMule::indexer
