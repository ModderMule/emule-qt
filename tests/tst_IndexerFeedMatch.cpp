/// @file tst_IndexerFeedMatch.cpp
/// @brief Whether a feed acts on a row, with nothing else in the way.
///
/// This is the part of the feature that decides what gets downloaded while
/// nobody is watching, and it has no I/O, so it is the part that can be tested
/// exhaustively. Two of the cases below are about which way a rule *fails*
/// rather than what it does when it works — a distinction the code reads past
/// but a user pays for.

#include "IndexerFeedMatch.h"

#include <QTest>

using namespace eMule::indexer;

namespace {

IndexerFeed plainFeed()
{
    IndexerFeed feed;
    feed.name = QStringLiteral("Test");
    return feed;
}

IndexerResult row(const QString& title, qint64 size = 1000,
                  const QDateTime& published = QDateTime::currentDateTimeUtc())
{
    IndexerResult out;
    out.title = title;
    out.guid = title;
    out.size = size;
    out.published = published;
    out.downloadUrl = QUrl(QStringLiteral("http://x.example/a.nzb"));
    return out;
}

} // namespace

class tst_IndexerFeedMatch : public QObject {
    Q_OBJECT

private slots:
    void anEmptyFilterAcceptsEverything();
    void anAcceptPatternExcludesWhatItDoesNotMatch();
    void aRejectPatternBeatsAnAcceptPattern();
    void anUnevaluableFilterMatchesNothingRatherThanEverything();
    void anIndexerThatReportsNoSizeIsNotFilteredOutBySize();
    void anIndexerThatReportsNoDateIsNotFilteredOutByAge();
    void sizeBoundsAreInclusive();
    void theAgeCeilingExcludesOlderRows();
};

void tst_IndexerFeedMatch::anEmptyFilterAcceptsEverything()
{
    const IndexerFeed feed = plainFeed();
    const CompiledFeedFilter filter = compileFeedFilter(feed);

    QVERIFY(filter.valid);
    QVERIFY(feedAccepts(feed, filter, row(QStringLiteral("Anything At All"))));
}

void tst_IndexerFeedMatch::anAcceptPatternExcludesWhatItDoesNotMatch()
{
    IndexerFeed feed = plainFeed();
    feed.accept = QStringLiteral("\\bdesktop\\b");
    const CompiledFeedFilter filter = compileFeedFilter(feed);

    QVERIFY(filter.valid);
    QVERIFY(feedAccepts(feed, filter, row(QStringLiteral("ubuntu desktop 24.04"))));
    QVERIFY(!feedAccepts(feed, filter, row(QStringLiteral("ubuntu server 24.04"))));

    // Case-insensitive: a user typing a pattern is not writing a regex exam.
    QVERIFY(feedAccepts(feed, filter, row(QStringLiteral("Ubuntu DESKTOP 24.04"))));
}

void tst_IndexerFeedMatch::aRejectPatternBeatsAnAcceptPattern()
{
    IndexerFeed feed = plainFeed();
    feed.accept = QStringLiteral("ubuntu");
    feed.reject = QStringLiteral("arm64");
    const CompiledFeedFilter filter = compileFeedFilter(feed);

    QVERIFY(feedAccepts(feed, filter, row(QStringLiteral("ubuntu amd64"))));

    // Matches both. An exclusion the user wrote is more specific than an
    // inclusion, and this is the direction that errs towards not downloading.
    QVERIFY(!feedAccepts(feed, filter, row(QStringLiteral("ubuntu arm64"))));
}

void tst_IndexerFeedMatch::anUnevaluableFilterMatchesNothingRatherThanEverything()
{
    // An unclosed group. The tempting handling is to drop the bad pattern and
    // carry on -- which for a *reject* pattern means it now matches nothing, and
    // a feed that was excluding things silently starts downloading them.
    IndexerFeed feed = plainFeed();
    feed.reject = QStringLiteral("(unclosed");

    const CompiledFeedFilter filter = compileFeedFilter(feed);
    QVERIFY(!filter.valid);
    QVERIFY(!filter.error.isEmpty());
    QVERIFY(filter.error.contains(QStringLiteral("reject")));

    // And the same for accept, so neither direction can be the one that leaks.
    IndexerFeed other = plainFeed();
    other.accept = QStringLiteral("[unclosed");
    const CompiledFeedFilter otherFilter = compileFeedFilter(other);
    QVERIFY(!otherFilter.valid);
    QVERIFY(otherFilter.error.contains(QStringLiteral("accept")));
}

void tst_IndexerFeedMatch::anIndexerThatReportsNoSizeIsNotFilteredOutBySize()
{
    IndexerFeed feed = plainFeed();
    feed.minSize = 100 * 1024 * 1024;
    const CompiledFeedFilter filter = compileFeedFilter(feed);

    // size == 0 means the indexer did not say, not that the release is empty.
    // Applying the bound to it would empty every feed on an indexer that omits
    // the attribute -- and plenty do.
    QVERIFY(feedAccepts(feed, filter, row(QStringLiteral("No Size Reported"), 0)));

    // A reported size below the floor is still excluded.
    QVERIFY(!feedAccepts(feed, filter, row(QStringLiteral("Tiny"), 1024)));
}

void tst_IndexerFeedMatch::anIndexerThatReportsNoDateIsNotFilteredOutByAge()
{
    IndexerFeed feed = plainFeed();
    feed.maxAgeDays = 7;
    const CompiledFeedFilter filter = compileFeedFilter(feed);

    QVERIFY(feedAccepts(feed, filter, row(QStringLiteral("No Date"), 1000, QDateTime())));
}

void tst_IndexerFeedMatch::sizeBoundsAreInclusive()
{
    IndexerFeed feed = plainFeed();
    feed.minSize = 1000;
    feed.maxSize = 2000;
    const CompiledFeedFilter filter = compileFeedFilter(feed);

    QVERIFY(feedAccepts(feed, filter, row(QStringLiteral("At the floor"), 1000)));
    QVERIFY(feedAccepts(feed, filter, row(QStringLiteral("At the ceiling"), 2000)));
    QVERIFY(!feedAccepts(feed, filter, row(QStringLiteral("Under"), 999)));
    QVERIFY(!feedAccepts(feed, filter, row(QStringLiteral("Over"), 2001)));

    // 0 as a ceiling means no ceiling, not "nothing passes".
    IndexerFeed open = plainFeed();
    open.maxSize = 0;
    const CompiledFeedFilter openFilter = compileFeedFilter(open);
    QVERIFY(feedAccepts(open, openFilter, row(QStringLiteral("Huge"), 900000000000LL)));
}

void tst_IndexerFeedMatch::theAgeCeilingExcludesOlderRows()
{
    IndexerFeed feed = plainFeed();
    feed.maxAgeDays = 7;
    const CompiledFeedFilter filter = compileFeedFilter(feed);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(feedAccepts(feed, filter, row(QStringLiteral("Fresh"), 1000, now.addDays(-1))));
    QVERIFY(!feedAccepts(feed, filter, row(QStringLiteral("Stale"), 1000, now.addDays(-30))));
}

QTEST_MAIN(tst_IndexerFeedMatch)
#include "tst_IndexerFeedMatch.moc"
