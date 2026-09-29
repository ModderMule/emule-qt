/// @file tst_IndexerSearch.cpp
/// @brief The fan-out: several indexers, paging, dedup and failure isolation.
///
/// Runs against a fake HTTP server in-process, so no network and no API key.
/// What it is really testing is that one indexer misbehaving costs its share of
/// the results and nothing more — with three configured, one being down should
/// lose a third of the answers, not all of them.

#include "FakeIndexerServer.h"
#include "TestFixtures.h"

#include "IndexerClient.h"
#include "IndexerSearch.h"
#include "IndexerSearchList.h"
#include "IndexerQuery.h"

#include "prefs/Preferences.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace eMule;
using namespace eMule::indexer;
using namespace eMule::testing;

// NOTE: feed() lives below the Q_OBJECT class because it holds raw strings, and
// moc emits a zero-byte .moc for any file with one above the class — surfacing
// only as a "missing vtable" link error.

class tst_IndexerSearch : public QObject {
    Q_OBJECT

private slots:
    void singleIndexer_returnsItsRows();
    void twoIndexers_bothContributeAndDuplicatesCollapse();
    void oneIndexerFailing_doesNotLoseTheOther();
    void anErrorDocumentIsReportedNotSilentlyEmpty();
    void pagingStopsAtTheCap();
    void pagingStopsWhenTheIndexerRunsOut();
    void stopEndsTheSearchAndKeepsWhatArrived();
    void grabCache_grabsARowAfterARestart();
};

namespace {

QByteArray feed(const QStringList& titles, int offset, int total)
{
    QByteArray out =
        R"(<?xml version="1.0"?><rss version="2.0"
             xmlns:newznab="http://www.newznab.com/DTD/2010/feeds/attributes/"><channel>)";
    out += QStringLiteral(R"(<newznab:response offset="%1" total="%2"/>)")
               .arg(offset).arg(total).toUtf8();
    for (const QString& title : titles) {
        out += QStringLiteral(
                   R"(<item><title>%1</title><guid>%1</guid>)"
                   R"(<enclosure url="http://x.example/%1.nzb" length="1"/>)"
                   R"(<newznab:attr name="size" value="1000"/></item>)")
                   .arg(title).toUtf8();
    }
    out += "</channel></rss>";
    return out;
}

/// One item whose enclosure points back at @p base, so a grab hits the fake.
QByteArray feedWithNzb(const QString& base)
{
    return QStringLiteral(
               R"(<?xml version="1.0"?><rss version="2.0"
                  xmlns:newznab="http://www.newznab.com/DTD/2010/feeds/attributes/"><channel>)"
               R"(<newznab:response offset="0" total="1"/>)"
               R"(<item><title>Release</title><guid>g1</guid>)"
               R"(<enclosure url="%1/get/Release.nzb" length="1"/>)"
               R"(<newznab:attr name="size" value="1000"/></item></channel></rss>)")
        .arg(base).toUtf8();
}

IndexerConfig configFor(const FakeIndexerServer& server, const QString& name)
{
    IndexerConfig config;
    config.name = name;
    config.url = server.baseUrl();
    config.apiKey = QStringLiteral("k");
    config.timeoutMs = 5000;
    return config;
}

} // namespace

void tst_IndexerSearch::singleIndexer_returnsItsRows()
{
    FakeIndexerServer server([](const QUrl&) {
        return qMakePair(200, feed({QStringLiteral("A"), QStringLiteral("B")}, 0, 2));
    });

    IndexerClient client;
    IndexerQuery query;
    query.text = QStringLiteral("x");
    query.limit = 100;

    IndexerSearch search(1, &client, query, {configFor(server, QStringLiteral("One"))}, 3);
    QSignalSpy finished(&search, &IndexerSearch::finished);
    QSignalSpy results(&search, &IndexerSearch::resultsReady);
    search.start({});

    QVERIFY(finished.wait(5000));
    QCOMPARE(search.results().size(), 2);
    QCOMPARE(results.count(), 1);
    QCOMPARE(search.results().at(0).indexerName, QStringLiteral("One"));
}

void tst_IndexerSearch::twoIndexers_bothContributeAndDuplicatesCollapse()
{
    // "Shared" is offered by both. Dedup is heuristic — title plus size, because
    // the same release carries a different guid at every indexer — so the row
    // appears once and the unique ones survive.
    FakeIndexerServer first([](const QUrl&) {
        return qMakePair(200, feed({QStringLiteral("Shared"), QStringLiteral("OnlyFirst")}, 0, 2));
    });
    FakeIndexerServer second([](const QUrl&) {
        return qMakePair(200, feed({QStringLiteral("Shared"), QStringLiteral("OnlySecond")}, 0, 2));
    });

    IndexerClient client;
    IndexerQuery query;
    query.text = QStringLiteral("x");
    query.limit = 100;

    IndexerSearch search(1, &client, query,
                         {configFor(first, QStringLiteral("First")),
                          configFor(second, QStringLiteral("Second"))}, 3);
    QSignalSpy finished(&search, &IndexerSearch::finished);
    search.start({});

    QVERIFY(finished.wait(5000));
    QCOMPARE(search.results().size(), 3);

    QStringList titles;
    for (const auto& row : search.results())
        titles.append(row.title);
    titles.sort();
    QCOMPARE(titles, QStringList({QStringLiteral("OnlyFirst"), QStringLiteral("OnlySecond"),
                                  QStringLiteral("Shared")}));
}

void tst_IndexerSearch::oneIndexerFailing_doesNotLoseTheOther()
{
    FakeIndexerServer good([](const QUrl&) {
        return qMakePair(200, feed({QStringLiteral("Kept")}, 0, 1));
    });
    FakeIndexerServer bad([](const QUrl&) {
        return qMakePair(500, QByteArray("boom"));
    });
    ScopedStatistics stats;

    IndexerClient client;
    IndexerQuery query;
    query.text = QStringLiteral("x");
    query.limit = 100;

    IndexerSearch search(1, &client, query,
                         {configFor(good, QStringLiteral("Good")),
                          configFor(bad, QStringLiteral("Bad"))}, 3);
    QSignalSpy finished(&search, &IndexerSearch::finished);
    search.start({});

    QVERIFY(finished.wait(5000));

    // The good indexer's row survives, and the failure is reported rather than
    // swallowed — but it is reported *alongside* results, not instead of them.
    QCOMPARE(search.results().size(), 1);
    QCOMPARE(search.results().at(0).title, QStringLiteral("Kept"));

    const QString error = finished.first().at(1).toString();
    QVERIFY2(!error.isEmpty(), "a failing indexer must be named");
    QVERIFY(error.contains(QStringLiteral("Bad")));

    // Load on the indexers: two requests, one of them failed — the 500's body
    // is not a second error on top of the status.
    QCOMPARE(stats->indexerSession().apiRequests, uint64(2));
    QCOMPARE(stats->indexerSession().apiErrors, uint64(1));
}

void tst_IndexerSearch::anErrorDocumentIsReportedNotSilentlyEmpty()
{
    // HTTP 200 with an <error> body — how a wrong API key actually arrives.
    FakeIndexerServer server([](const QUrl&) {
        return qMakePair(200,
            QByteArray(R"(<error code="100" description="Incorrect user credentials"/>)"));
    });
    ScopedStatistics stats;

    IndexerClient client;
    IndexerQuery query;
    query.text = QStringLiteral("x");

    IndexerSearch search(1, &client, query, {configFor(server, QStringLiteral("Wrong"))}, 3);
    QSignalSpy finished(&search, &IndexerSearch::finished);
    search.start({});

    QVERIFY(finished.wait(5000));
    QVERIFY(search.results().isEmpty());
    QVERIFY2(finished.first().at(1).toString().contains(
                 QStringLiteral("Incorrect user credentials")),
             "the indexer's own words must reach the user");

    // A 200 carrying an error document is an error, counted once.
    QCOMPARE(stats->indexerSession().apiRequests, uint64(1));
    QCOMPARE(stats->indexerSession().apiErrors, uint64(1));
}

void tst_IndexerSearch::pagingStopsAtTheCap()
{
    // The indexer always answers with a full page and claims plenty more, so
    // only the page cap can end this. That cap is a spending limit: every page
    // is an API call against the user's allowance.
    FakeIndexerServer server([](const QUrl& url) {
        const int offset =
            QUrlQuery(url.query()).queryItemValue(QStringLiteral("offset")).toInt();
        QStringList titles;
        for (int i = 0; i < 2; ++i)
            titles.append(QStringLiteral("R%1").arg(offset + i));
        return qMakePair(200, feed(titles, offset, 1000));
    });

    IndexerClient client;
    IndexerQuery query;
    query.text = QStringLiteral("x");
    query.limit = 2;

    IndexerSearch search(1, &client, query, {configFor(server, QStringLiteral("Deep"))},
                         /*maxPages*/ 3);
    QSignalSpy finished(&search, &IndexerSearch::finished);
    search.start({});

    QVERIFY(finished.wait(5000));
    QCOMPARE(server.requestCount, 3);
    QCOMPARE(search.results().size(), 6);
}

void tst_IndexerSearch::pagingStopsWhenTheIndexerRunsOut()
{
    FakeIndexerServer server([](const QUrl& url) {
        const int offset =
            QUrlQuery(url.query()).queryItemValue(QStringLiteral("offset")).toInt();
        if (offset >= 2)
            return qMakePair(200, feed({}, offset, 3));
        return qMakePair(200, feed({QStringLiteral("A"), QStringLiteral("B")}, offset, 3));
    });

    IndexerClient client;
    IndexerQuery query;
    query.text = QStringLiteral("x");
    query.limit = 2;

    IndexerSearch search(1, &client, query, {configFor(server, QStringLiteral("Short"))}, 10);
    QSignalSpy finished(&search, &IndexerSearch::finished);
    search.start({});

    QVERIFY(finished.wait(5000));
    // Two full pages then an empty one; the cap of 10 was never reached.
    QVERIFY(server.requestCount <= 3);
    QCOMPARE(search.results().size(), 2);
}

void tst_IndexerSearch::stopEndsTheSearchAndKeepsWhatArrived()
{
    FakeIndexerServer server([](const QUrl& url) {
        const int offset =
            QUrlQuery(url.query()).queryItemValue(QStringLiteral("offset")).toInt();
        return qMakePair(200, feed({QStringLiteral("P%1").arg(offset)}, offset, 100));
    });

    IndexerClient client;
    IndexerQuery query;
    query.text = QStringLiteral("x");
    query.limit = 1;

    ScopedStatistics stats;
    IndexerSearch search(1, &client, query, {configFor(server, QStringLiteral("Slow"))}, 10);
    QSignalSpy results(&search, &IndexerSearch::resultsReady);
    QSignalSpy finished(&search, &IndexerSearch::finished);
    search.start({});

    QVERIFY(results.wait(5000));
    search.stop();
    QVERIFY(finished.wait(5000));

    // A stopped search is a shorter search, not a lost one.
    QVERIFY(!search.results().isEmpty());
    QVERIFY(!search.isRunning());

    // The page we cancelled is not the indexer failing.
    QCOMPARE(stats->indexerSession().apiErrors, uint64(0));
}

void tst_IndexerSearch::grabCache_grabsARowAfterARestart()
{
    // A restored GUI tab grabs with searchId 0; the daemon must still know the URL.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString savedConfigDir = thePrefs.configDir();
    thePrefs.setConfigDir(dir.path());

    QString base;
    FakeIndexerServer server([&base](const QUrl& url) {
        if (url.path().endsWith(QStringLiteral(".nzb")))
            return qMakePair(200, QByteArray("<nzb/>"));
        return qMakePair(200, feedWithNzb(base));
    });
    base = server.baseUrl();
    thePrefs.setIndexers({configFor(server, QStringLiteral("One"))});
    thePrefs.setIndexerMaxPages(1);

    QString resultId;
    {
        IndexerSearchList list;
        QSignalSpy finished(&list, &IndexerSearchList::searchFinished);
        IndexerQuery query;
        query.text = QStringLiteral("x");
        QString error;
        const quint32 id = list.startSearch(query, IndexerKind::Newznab, {}, error);
        QVERIFY2(id != 0, qPrintable(error));
        QVERIFY(finished.wait(5000));
        QCOMPARE(list.search(id)->results().size(), 1);
        resultId = list.search(id)->results().first().id;
    }   // destructor flushes the cache

    IndexerSearchList restarted;
    bool done = false;
    bool ok = false;
    QByteArray payload;
    QString name;
    restarted.grab(0, resultId, [&](bool okIn, const QByteArray& body, const QString& nameIn,
                                    const QString&, const QString&) {
        done = true;
        ok = okIn;
        payload = body;
        name = nameIn;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(ok);
    QCOMPARE(payload, QByteArray("<nzb/>"));
    QCOMPARE(name, QStringLiteral("Release"));

    // an id nobody cached still fails cleanly
    done = ok = false;
    restarted.grab(0, QStringLiteral("nope"), [&](bool okIn, const QByteArray&, const QString&,
                                                  const QString&, const QString&) {
        done = true;
        ok = okIn;
    });
    QVERIFY(done);
    QVERIFY(!ok);

    thePrefs.setIndexers({});
    thePrefs.setConfigDir(savedConfigDir);
}

QTEST_MAIN(tst_IndexerSearch)
#include "tst_IndexerSearch.moc"
