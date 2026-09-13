/// @file tst_IndexerFeedPoller.cpp
/// @brief The feed poller: what it spends, and what it refuses to spend.
///
/// Runs against an in-process newznab endpoint, so no network and no API key.
/// The sink is a stub that records what it was handed, which is also how the
/// layering is enforced -- this binary does not link eMule::Usenet, and if the
/// poller ever reached into the queue directly it would stop building.
///
/// The cases that matter most assert *absences*: a first poll that adds nothing,
/// a duplicate that is not retried, a torznab row that is never fetched. Those
/// are the ones that silently pass when the fix is wrong.

#include "FakeIndexerServer.h"
#include "TestFixtures.h"

#include "IndexerFeedList.h"
#include "IndexerFeedStore.h"
#include "IndexerQuery.h"
#include "prefs/Preferences.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace eMule;
using namespace eMule::indexer;
using namespace eMule::testing;

class tst_IndexerFeedPoller : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aNewFeedsFirstPollAddsNothing();
    void grabExistingMakesTheFirstPollAct();
    void aFeedQueuesIntoItsDownloadCategory();
    void theSecondPollActsOnlyOnWhatIsNew();
    void addingAnIndexerToAnExistingFeedSeedsItRatherThanGrabbingItsBacklog();
    void aDuplicateRefusalIsTerminalNotARetry();
    void aFailedGrabIsRetriedAndThenGivenUpOn();
    void aTorznabRowIsNeverGrabbedAsAnNzb();
    void aRejectedRowIsNeverRetried();
    void theSeenSetSurvivesARestart();
    void theSeenSetIsBoundedAndEvictsTheOldest();
    void removingAFeedRemovesItsSidecar();
    void aFeedWithNoEnabledIndexerDoesNotPoll();
    void aUrlFeedPollsItsOwnAddress();
    void aUrlFeedNeverPrintsItsKey();
    void checkingEveryFeedStillPollsOneAtATime();
    void removingAFeedMidPollIsSurvivable();

private:
    QTemporaryDir m_dir;
};

namespace {

/// Titles the fake serves, and the .nzb it points each of them at.
QByteArray searchFeed(const QStringList& titles, const QString& host, bool torznab = false)
{
    QByteArray out =
        R"(<?xml version="1.0"?><rss version="2.0"
             xmlns:newznab="http://www.newznab.com/DTD/2010/feeds/attributes/"><channel>)";
    for (const QString& title : titles) {
        out += QStringLiteral(R"(<item><title>%1</title><guid>%1</guid>)").arg(title).toUtf8();
        out += QStringLiteral(R"(<enclosure url="%1/nzb/%2" length="1"/>)")
                   .arg(host, title).toUtf8();
        out += R"(<newznab:attr name="size" value="1000"/>)";
        if (torznab) {
            out += R"(<newznab:attr name="magneturl" value="magnet:?xt=urn:btih:abc"/>)";
            out += R"(<newznab:attr name="seeders" value="4"/>)";
        }
        out += "</item>";
    }
    out += "</channel></rss>";
    return out;
}

QByteArray anNzb()
{
    // Not QByteArrayLiteral: it is a macro, and the preprocessor splits its
    // argument before the raw strings are a single token.
    return QByteArray(
        R"(<?xml version="1.0"?><nzb xmlns="http://www.newzbin.com/DTD/2003/nzb">)"
        // Custom delimiter: the subject's `(1/1)"` would otherwise close a plain
        // R"( ... )" raw string right in the middle of the line.
        R"NZB(<file subject="a (1/1)" poster="p" date="1"><groups><group>alt.bin</group></groups>)NZB"
        R"(<segments><segment bytes="10" number="1">m1@x</segment></segments></file></nzb>)");
}

IndexerConfig accountFor(const FakeIndexerServer& server, const QString& name)
{
    IndexerConfig config;
    config.name = name;
    config.url = server.baseUrl();
    config.apiKey = QStringLiteral("secret-key");
    config.timeoutMs = 5000;
    return config;
}

IndexerFeed feedNamed(const QString& name)
{
    IndexerFeed feed;
    feed.name = name;
    feed.query = QStringLiteral("ubuntu");
    feed.intervalMinutes = IndexerFeed::kMinIntervalMinutes;
    return feed;
}

/// Records everything handed to the sink, and answers however the case needs.
struct SinkLog {
    QStringList titles;
    QList<int> categories;
    FeedAddOutcome answer = FeedAddOutcome::Added;

    IndexerFeedList::NzbSink install()
    {
        return [this](const FeedAddRequest& request, QString& error) {
            titles.append(request.title);
            categories.append(request.downloadCategory);
            if (answer == FeedAddOutcome::Rejected)
                error = QStringLiteral("not an NZB");
            return answer;
        };
    }
};

/// Poll @p name and wait for the poller to report it finished.
void pollAndWait(IndexerFeedList& feeds, const QString& name)
{
    QSignalSpy status(&feeds, &IndexerFeedList::feedStatusChanged);
    QString error;
    QVERIFY(feeds.pollNow(name, error));

    // The last report of a poll carries polling == false. Waiting for the signal
    // alone would catch the one that announced the poll starting.
    QDeadlineTimer deadline(5000);
    while (!deadline.hasExpired()) {
        if (!feeds.statusFor(name).polling && feeds.statusFor(name).lastPolled.isValid())
            return;
        QTest::qWait(20);
    }
    QFAIL("the feed never finished polling");
}

} // namespace

void tst_IndexerFeedPoller::init()
{
    QVERIFY(m_dir.isValid());
    // Every sidecar this test writes lands under the temp config dir, and
    // IndexerFeedStore::directory() reads it from thePrefs.
    thePrefs.setConfigDir(m_dir.path());
    thePrefs.setIndexerFeeds({});
    thePrefs.setIndexers({});
}

void tst_IndexerFeedPoller::cleanup()
{
    thePrefs.setIndexerFeeds({});
    thePrefs.setIndexers({});
}

void tst_IndexerFeedPoller::aNewFeedsFirstPollAddsNothing()
{
    // Fifty releases the indexer still lists. To a feed that has never run every
    // one of them is new -- which is exactly why acting on the first answer is
    // the wrong thing and this is the headline case of the whole feature.
    QStringList titles;
    for (int i = 0; i < 50; ++i)
        titles.append(QStringLiteral("ubuntu-%1").arg(i));
    ScopedStatistics stats;

    QString host;
    FakeIndexerServer server([&titles, &host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        return qMakePair(200, searchFeed(titles, host));
    });
    host = server.urlFor(QString());

    thePrefs.setIndexers({accountFor(server, QStringLiteral("Main"))});
    thePrefs.setIndexerFeeds({feedNamed(QStringLiteral("Ubuntu"))});

    SinkLog sink;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    pollAndWait(feeds, QStringLiteral("Ubuntu"));

    QCOMPARE(sink.titles.size(), 0);
    QCOMPARE(feeds.statusFor(QStringLiteral("Ubuntu")).seenCount, 50);
    QCOMPARE(feeds.statusFor(QStringLiteral("Ubuntu")).lastMatched, 0);

    // And it fetched only the search: not one .nzb was pulled.
    for (const QString& path : server.seenPaths)
        QVERIFY(!path.startsWith(QStringLiteral("/nzb/")));

    QCOMPARE(stats->indexerSession().feedPolls, uint64(1));
    QCOMPARE(stats->indexerSession().feedMatches, uint64(0));
    QCOMPARE(stats->indexerSession().nzbFetches, uint64(0));
}

void tst_IndexerFeedPoller::grabExistingMakesTheFirstPollAct()
{
    QString host;
    FakeIndexerServer server([&host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        return qMakePair(200, searchFeed({QStringLiteral("A"), QStringLiteral("B")}, host));
    });
    host = server.urlFor(QString());

    thePrefs.setIndexers({accountFor(server, QStringLiteral("Main"))});
    IndexerFeed feed = feedNamed(QStringLiteral("Eager"));
    feed.grabExisting = true;
    thePrefs.setIndexerFeeds({feed});

    SinkLog sink;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    pollAndWait(feeds, QStringLiteral("Eager"));

    QCOMPARE(sink.titles.size(), 2);
    QCOMPARE(feeds.statusFor(QStringLiteral("Eager")).lastMatched, 2);
}

void tst_IndexerFeedPoller::aFeedQueuesIntoItsDownloadCategory()
{
    QString host;
    FakeIndexerServer server([&host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        return qMakePair(200, searchFeed({QStringLiteral("A"), QStringLiteral("B")}, host));
    });
    host = server.urlFor(QString());

    thePrefs.setIndexers({accountFor(server, QStringLiteral("Main"))});
    IndexerFeed feed = feedNamed(QStringLiteral("Shows"));
    feed.grabExisting = true;
    feed.downloadCategory = 3;
    thePrefs.setIndexerFeeds({feed});

    SinkLog sink;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    pollAndWait(feeds, QStringLiteral("Shows"));

    // Every match carries the feed's category out through the sink. It is an
    // opaque int on the way past -- this module names nothing in eMule::Usenet,
    // and what the number indexes is the sink's business.
    QCOMPARE(sink.titles.size(), 2);
    QCOMPARE(sink.categories, QList<int>({3, 3}));
}

void tst_IndexerFeedPoller::theSecondPollActsOnlyOnWhatIsNew()
{
    QStringList titles{QStringLiteral("old-1"), QStringLiteral("old-2")};
    ScopedStatistics stats;

    QString host;
    FakeIndexerServer server([&titles, &host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        return qMakePair(200, searchFeed(titles, host));
    });
    host = server.urlFor(QString());

    thePrefs.setIndexers({accountFor(server, QStringLiteral("Main"))});
    thePrefs.setIndexerFeeds({feedNamed(QStringLiteral("Rolling"))});

    SinkLog sink;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    pollAndWait(feeds, QStringLiteral("Rolling"));
    QCOMPARE(sink.titles.size(), 0);

    // Two more appear, the old two are still listed.
    titles.prepend(QStringLiteral("new-2"));
    titles.prepend(QStringLiteral("new-1"));

    pollAndWait(feeds, QStringLiteral("Rolling"));

    QCOMPARE(sink.titles.size(), 2);
    QVERIFY(sink.titles.contains(QStringLiteral("new-1")));
    QVERIFY(sink.titles.contains(QStringLiteral("new-2")));

    QCOMPARE(stats->indexerSession().feedPolls, uint64(2));
    QCOMPARE(stats->indexerSession().feedMatches, uint64(2));
    QCOMPARE(stats->indexerSession().nzbFetches, uint64(2));
    QCOMPARE(stats->indexerSession().nzbFetchErrors, uint64(0));
}

void tst_IndexerFeedPoller::addingAnIndexerToAnExistingFeedSeedsItRatherThanGrabbingItsBacklog()
{
    QString firstHost;
    FakeIndexerServer first([&firstHost](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        return qMakePair(200, searchFeed({QStringLiteral("first-1")}, firstHost));
    });
    firstHost = first.urlFor(QString());

    QString secondHost;
    FakeIndexerServer second([&secondHost](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        QStringList backlog;
        for (int i = 0; i < 20; ++i)
            backlog.append(QStringLiteral("second-%1").arg(i));
        return qMakePair(200, searchFeed(backlog, secondHost));
    });
    secondHost = second.urlFor(QString());

    thePrefs.setIndexers({accountFor(first, QStringLiteral("First"))});
    thePrefs.setIndexerFeeds({feedNamed(QStringLiteral("Grower"))});

    SinkLog sink;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();
    pollAndWait(feeds, QStringLiteral("Grower"));
    QCOMPARE(sink.titles.size(), 0);

    // A second account joins a feed that has already seeded the first. Its whole
    // retention window is new to this feed -- the same flood a brand-new feed
    // would cause, which is why seeding is per account and not per feed.
    thePrefs.setIndexers({accountFor(first, QStringLiteral("First")),
                          accountFor(second, QStringLiteral("Second"))});
    feeds.applyPreferences();
    pollAndWait(feeds, QStringLiteral("Grower"));

    QCOMPARE(sink.titles.size(), 0);
    QCOMPARE(feeds.statusFor(QStringLiteral("Grower")).seenCount, 21);
}

void tst_IndexerFeedPoller::aDuplicateRefusalIsTerminalNotARetry()
{
    QString host;
    FakeIndexerServer server([&host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        return qMakePair(200, searchFeed({QStringLiteral("dupe")}, host));
    });
    host = server.urlFor(QString());

    thePrefs.setIndexers({accountFor(server, QStringLiteral("Main"))});
    IndexerFeed feed = feedNamed(QStringLiteral("Dupes"));
    feed.grabExisting = true;
    thePrefs.setIndexerFeeds({feed});

    SinkLog sink;
    sink.answer = FeedAddOutcome::AlreadyHave;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    pollAndWait(feeds, QStringLiteral("Dupes"));
    QCOMPARE(sink.titles.size(), 1);

    // The queue already had it. That is the answer, not a failure -- so the feed
    // must never ask again. Treating it as transient is an infinite loop that
    // spends an API hit and an .nzb fetch on every single poll, forever.
    pollAndWait(feeds, QStringLiteral("Dupes"));
    pollAndWait(feeds, QStringLiteral("Dupes"));
    QCOMPARE(sink.titles.size(), 1);
}

void tst_IndexerFeedPoller::aFailedGrabIsRetriedAndThenGivenUpOn()
{
    int nzbRequests = 0;
    ScopedStatistics stats;
    QString host;
    FakeIndexerServer server([&nzbRequests, &host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/"))) {
            ++nzbRequests;
            return qMakePair(500, QByteArray());
        }
        return qMakePair(200, searchFeed({QStringLiteral("flaky")}, host));
    });
    host = server.urlFor(QString());

    thePrefs.setIndexers({accountFor(server, QStringLiteral("Main"))});
    IndexerFeed feed = feedNamed(QStringLiteral("Flaky"));
    feed.grabExisting = true;
    thePrefs.setIndexerFeeds({feed});

    SinkLog sink;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    // Bounded, and more than one: a single 503 must not lose a release, and a
    // permanently dead URL must not be retried on every poll forever.
    for (int i = 0; i < 6; ++i)
        pollAndWait(feeds, QStringLiteral("Flaky"));

    QCOMPARE(nzbRequests, IndexerFeedList::kMaxGrabAttempts);
    QCOMPARE(sink.titles.size(), 0);

    // One match, however many times it was retried; every attempt a failed fetch.
    QCOMPARE(stats->indexerSession().feedPolls, uint64(6));
    QCOMPARE(stats->indexerSession().feedMatches, uint64(1));
    QCOMPARE(stats->indexerSession().nzbFetches, uint64(IndexerFeedList::kMaxGrabAttempts));
    QCOMPARE(stats->indexerSession().nzbFetchErrors,
             uint64(IndexerFeedList::kMaxGrabAttempts));
}

void tst_IndexerFeedPoller::aTorznabRowIsNeverGrabbedAsAnNzb()
{
    QString host;
    FakeIndexerServer server([&host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        return qMakePair(200, searchFeed({QStringLiteral("torrent-only")}, host,
                                         /*torznab*/ true));
    });
    host = server.urlFor(QString());

    IndexerConfig account = accountFor(server, QStringLiteral("Aggregator"));
    account.kind = IndexerKind::Both;
    thePrefs.setIndexers({account});

    IndexerFeed feed = feedNamed(QStringLiteral("Mixed"));
    feed.grabExisting = true;
    thePrefs.setIndexerFeeds({feed});

    SinkLog sink;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    pollAndWait(feeds, QStringLiteral("Mixed"));

    // A magnet has no .nzb behind it. Fetching one and handing the bytes to the
    // sink produces a rejection that then has to be told apart from a real one.
    QCOMPARE(sink.titles.size(), 0);
    for (const QString& path : server.seenPaths)
        QVERIFY(!path.startsWith(QStringLiteral("/nzb/")));
}

void tst_IndexerFeedPoller::aRejectedRowIsNeverRetried()
{
    int nzbRequests = 0;
    QString host;
    FakeIndexerServer server([&nzbRequests, &host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/"))) {
            ++nzbRequests;
            return qMakePair(200, anNzb());
        }
        return qMakePair(200, searchFeed({QStringLiteral("junk")}, host));
    });
    host = server.urlFor(QString());

    thePrefs.setIndexers({accountFor(server, QStringLiteral("Main"))});
    IndexerFeed feed = feedNamed(QStringLiteral("Junk"));
    feed.grabExisting = true;
    thePrefs.setIndexerFeeds({feed});

    SinkLog sink;
    sink.answer = FeedAddOutcome::Rejected;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    pollAndWait(feeds, QStringLiteral("Junk"));
    pollAndWait(feeds, QStringLiteral("Junk"));

    // An unparseable payload is the same payload next time.
    QCOMPARE(nzbRequests, 1);
}

void tst_IndexerFeedPoller::theSeenSetSurvivesARestart()
{
    QString host;
    FakeIndexerServer server([&host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        return qMakePair(200, searchFeed({QStringLiteral("kept-1"), QStringLiteral("kept-2")}, host));
    });
    host = server.urlFor(QString());

    thePrefs.setIndexers({accountFor(server, QStringLiteral("Main"))});
    thePrefs.setIndexerFeeds({feedNamed(QStringLiteral("Persistent"))});

    {
        SinkLog sink;
        IndexerFeedList feeds;
        feeds.setNzbSink(sink.install());
        feeds.applyPreferences();
        pollAndWait(feeds, QStringLiteral("Persistent"));
        QCOMPARE(feeds.statusFor(QStringLiteral("Persistent")).seenCount, 2);
    }

    // A fresh poller: without the sidecar it would seed again, and with a
    // *wrongly* restored one it would grab the backlog it already dismissed.
    SinkLog sink;
    IndexerFeedList restarted;
    restarted.setNzbSink(sink.install());
    restarted.applyPreferences();

    QCOMPARE(restarted.statusFor(QStringLiteral("Persistent")).seenCount, 2);
    pollAndWait(restarted, QStringLiteral("Persistent"));
    QCOMPARE(sink.titles.size(), 0);
}

void tst_IndexerFeedPoller::theSeenSetIsBoundedAndEvictsTheOldest()
{
    FeedState state;
    for (int i = 0; i < IndexerFeedStore::kMaxSeenEntries + 25; ++i) {
        FeedSeenEntry entry;
        entry.guid = QStringLiteral("g%1").arg(i);
        entry.at = QDateTime::currentDateTimeUtc().addSecs(i);
        state.recordSeen(entry);
    }

    state.evict(IndexerFeedStore::kMaxSeenEntries);
    QCOMPARE(int(state.seen.size()), IndexerFeedStore::kMaxSeenEntries);

    // The oldest twenty-five went; the newest survived. An eviction that dropped
    // the newest would re-admit whatever the feed just handled.
    QVERIFY(state.find(QStringLiteral("g0")) == nullptr);
    QVERIFY(state.find(QStringLiteral("g24")) == nullptr);
    QVERIFY(state.find(QStringLiteral("g25")) != nullptr);
}

void tst_IndexerFeedPoller::removingAFeedRemovesItsSidecar()
{
    const IndexerFeed feed = feedNamed(QStringLiteral("Doomed"));
    thePrefs.setIndexerFeeds({feed});

    FeedState state;
    state.name = feed.name;
    state.seededAccounts.append(QStringLiteral("Main"));
    QVERIFY(IndexerFeedStore::save(feed, state));
    QVERIFY(QFile::exists(IndexerFeedStore::pathFor(feed)));

    thePrefs.setIndexerFeeds({});
    IndexerFeedList feeds;
    feeds.applyPreferences();

    // A later feed reusing the name must seed its own history rather than skip
    // the seed pass because a stranger's file said it already had.
    QVERIFY(!QFile::exists(IndexerFeedStore::pathFor(feed)));
}

void tst_IndexerFeedPoller::aFeedWithNoEnabledIndexerDoesNotPoll()
{
    IndexerConfig account;
    account.name = QStringLiteral("Off");
    account.url = QStringLiteral("http://127.0.0.1:1/api");
    account.enabled = false;
    thePrefs.setIndexers({account});
    thePrefs.setIndexerFeeds({feedNamed(QStringLiteral("Orphan"))});

    SinkLog sink;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    QString error;
    QVERIFY(feeds.pollNow(QStringLiteral("Orphan"), error));
    QTest::qWait(200);

    // No verdict and no spend -- and deliberately not recorded as a poll, so the
    // next attempt is not pushed a whole interval away for a condition the user
    // may fix in the next minute.
    const IndexerFeedStatus status = feeds.statusFor(QStringLiteral("Orphan"));
    QVERIFY(!status.lastPolled.isValid());
    QVERIFY(!status.lastError.isEmpty());
    QCOMPARE(sink.titles.size(), 0);
}

void tst_IndexerFeedPoller::aUrlFeedPollsItsOwnAddress()
{
    QString host;
    FakeIndexerServer server([&host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        return qMakePair(200, searchFeed({QStringLiteral("from-url")}, host));
    });
    host = server.urlFor(QString());

    // No account at all: a URL feed carries its own credentials.
    thePrefs.setIndexers({});

    IndexerFeed feed;
    feed.name = QStringLiteral("Pasted");
    feed.kind = IndexerFeedKind::Url;
    feed.url = server.urlFor(QStringLiteral("/rss?t=search&r=secret-key"));
    feed.grabExisting = true;
    feed.intervalMinutes = IndexerFeed::kMinIntervalMinutes;
    thePrefs.setIndexerFeeds({feed});

    SinkLog sink;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    pollAndWait(feeds, QStringLiteral("Pasted"));

    QCOMPARE(sink.titles.size(), 1);
    QCOMPARE(sink.titles.first(), QStringLiteral("from-url"));
}

void tst_IndexerFeedPoller::aUrlFeedNeverPrintsItsKey()
{
    // newznab's RSS endpoint names the key `r`, not `apikey` -- a pasted feed URL
    // is the one place that spelling reaches this code, and redaction is the
    // only layer between it and log.log.
    const QString url =
        QStringLiteral("https://indexer.example/rss?t=search&q=x&r=SUPERSECRET&extended=1");

    const QString redacted = redactApiKey(QUrl(url));
    QVERIFY(!redacted.contains(QStringLiteral("SUPERSECRET")));
    QVERIFY2(redacted.contains(QStringLiteral("r=%3Credacted%3E"))
                 || redacted.contains(QStringLiteral("r=<redacted>")),
             qPrintable(redacted));

    // And in prose, which is how Qt's own network errors carry it.
    const QString prose = QStringLiteral("Error transferring %1 - server replied: Forbidden")
                              .arg(url);
    QVERIFY(!redactApiKey(prose).contains(QStringLiteral("SUPERSECRET")));
}

void tst_IndexerFeedPoller::checkingEveryFeedStillPollsOneAtATime()
{
    QString host;
    FakeIndexerServer server([&host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        return qMakePair(200, searchFeed({QStringLiteral("only")}, host));
    });
    host = server.urlFor(QString());

    thePrefs.setIndexers({accountFor(server, QStringLiteral("Main"))});
    thePrefs.setIndexerFeeds({feedNamed(QStringLiteral("One")),
                              feedNamed(QStringLiteral("Two")),
                              feedNamed(QStringLiteral("Three"))});

    SinkLog sink;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    // All three are due — none has ever been polled. "Check all" must not turn
    // that into three simultaneous requests against one indexer, which is also
    // what stops a daemon start from doing it.
    QString error;
    QVERIFY(feeds.pollNow(QString(), error));
    QTest::qWait(1000);

    QCOMPARE(server.requestCount, 1);

    int polled = 0;
    for (const QString& name : {QStringLiteral("One"), QStringLiteral("Two"),
                                QStringLiteral("Three")}) {
        if (feeds.statusFor(name).lastPolled.isValid())
            ++polled;
    }
    QCOMPARE(polled, 1);
}

void tst_IndexerFeedPoller::removingAFeedMidPollIsSurvivable()
{
    QString host;
    FakeIndexerServer server([&host](const QUrl& url) {
        if (url.path().startsWith(QStringLiteral("/nzb/")))
            return qMakePair(200, anNzb());
        return qMakePair(200, searchFeed({QStringLiteral("doomed")}, host));
    });
    host = server.urlFor(QString());

    thePrefs.setIndexers({accountFor(server, QStringLiteral("Main"))});
    IndexerFeed feed = feedNamed(QStringLiteral("Vanishing"));
    feed.grabExisting = true;
    thePrefs.setIndexerFeeds({feed});

    SinkLog sink;
    IndexerFeedList feeds;
    feeds.setNzbSink(sink.install());
    feeds.applyPreferences();

    QString error;
    QVERIFY(feeds.pollNow(QStringLiteral("Vanishing"), error));

    // The settings page saves while the search is still out. Every callback is
    // QPointer-guarded and re-finds its feed by name rather than holding a
    // pointer into a map that has just been rebuilt — the trap IndexerSearch
    // documents and this inherits.
    thePrefs.setIndexerFeeds({});
    feeds.applyPreferences();

    QTest::qWait(1500);
    QCOMPARE(sink.titles.size(), 0);
    QVERIFY(feeds.statuses().isEmpty());
}

QTEST_MAIN(tst_IndexerFeedPoller)
#include "tst_IndexerFeedPoller.moc"
