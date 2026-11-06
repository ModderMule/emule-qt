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
#include "net/MetaFileCache.h"
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
    void grab_secondFetchServedFromMetaFileCache();
    void grab_redirectToMagnetReturnsTheMagnet();
    void grab_magnetOnlyRowNeedsNoFetch();
    void torrentSearch_skipsUsenetRowsOfABothAccount();
    void hasEnabledAccount_followsKindAndEnabled();
    void tvSearch_fallsBackToKeywordsWithoutTheMode();
    void sizeBounds_dropRowsOutsideButKeepUnknownSize();
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

/// Torznab items: @p items is {title, enclosure path or "", magnet or ""}.
QByteArray torznabFeed(const QString& base, const QList<QStringList>& items)
{
    QByteArray out =
        R"(<?xml version="1.0"?><rss version="2.0"
             xmlns:torznab="http://torznab.com/schemas/2015/feed"><channel>)"
        R"(<torznab:response offset="0" total="1"/>)";
    for (const QStringList& it : items) {
        out += QStringLiteral("<item><title>%1</title><guid>%1</guid>").arg(it.at(0)).toUtf8();
        if (!it.at(1).isEmpty())
            out += QStringLiteral(R"(<enclosure url="%1%2" length="1"/>)").arg(base, it.at(1)).toUtf8();
        if (!it.at(2).isEmpty())
            out += QStringLiteral(R"(<torznab:attr name="magneturl" value="%1"/>)")
                       .arg(it.at(2).toHtmlEscaped()).toUtf8();
        out += R"(<torznab:attr name="size" value="1000"/></item>)";
    }
    out += "</channel></rss>";
    return out;
}

/// Run one search on @p list and grab its first row; returns {ok, payload}.
QPair<bool, QByteArray> searchAndGrab(IndexerSearchList& list, IndexerKind kind)
{
    QSignalSpy finished(&list, &IndexerSearchList::searchFinished);
    IndexerQuery query;
    query.text = QStringLiteral("x");
    QString error;
    const quint32 id = list.startSearch(query, kind, {}, error);
    if (id == 0 || !finished.wait(5000) || list.search(id)->results().isEmpty())
        return {false, {}};
    bool done = false;
    QPair<bool, QByteArray> out;
    list.grab(id, list.search(id)->results().first().id,
              [&](bool ok, const QByteArray& body, const QString&, const QString&, const QString&) {
        done = true;
        out = {ok, body};
    });
    if (!QTest::qWaitFor([&done] { return done; }, 5000))
        return {false, {}};
    return out;
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
    MetaFileCache::instance().clear();   // a payload from another test is no fetch
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

void tst_IndexerSearch::grab_secondFetchServedFromMetaFileCache()
{
    // Details fetches the .nzb, the Download after it must not spend a second grab
    MetaFileCache::instance().clear();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString savedConfigDir = thePrefs.configDir();
    thePrefs.setConfigDir(dir.path());

    QString base;
    int nzbFetches = 0;
    FakeIndexerServer server([&base, &nzbFetches](const QUrl& url) {
        if (url.path().endsWith(QStringLiteral(".nzb"))) {
            ++nzbFetches;
            return qMakePair(200, QByteArray("<nzb/>"));
        }
        return qMakePair(200, feedWithNzb(base));
    });
    base = server.baseUrl();
    thePrefs.setIndexers({configFor(server, QStringLiteral("One"))});
    thePrefs.setIndexerMaxPages(1);

    IndexerSearchList list;
    QSignalSpy finished(&list, &IndexerSearchList::searchFinished);
    IndexerQuery query;
    query.text = QStringLiteral("x");
    QString error;
    const quint32 id = list.startSearch(query, IndexerKind::Newznab, {}, error);
    QVERIFY2(id != 0, qPrintable(error));
    QVERIFY(finished.wait(5000));
    const QString resultId = list.search(id)->results().first().id;

    for (int i = 0; i < 2; ++i) {
        bool done = false;
        QByteArray payload;
        list.grab(id, resultId, [&](bool ok, const QByteArray& body, const QString&, const QString&,
                                    const QString&) {
            done = true;
            payload = ok ? body : QByteArray();
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QCOMPARE(payload, QByteArray("<nzb/>"));
    }
    QCOMPARE(nzbFetches, 1);

    MetaFileCache::instance().clear();
    thePrefs.setIndexers({});
    thePrefs.setConfigDir(savedConfigDir);
}

void tst_IndexerSearch::grab_redirectToMagnetReturnsTheMagnet()
{
    // Jackett's /dl answers 302 -> magnet: when the tracker has no .torrent
    MetaFileCache::instance().clear();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString savedConfigDir = thePrefs.configDir();
    thePrefs.setConfigDir(dir.path());

    const QByteArray magnet("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=Rel");
    QString base;
    FakeIndexerServer server([&base, &magnet](const QUrl& url) {
        if (url.path().contains(QStringLiteral("/dl/")))
            return qMakePair(302, magnet);
        return qMakePair(200, torznabFeed(base, {{QStringLiteral("Rel"), QStringLiteral("/dl/1"), {}}}));
    });
    base = server.baseUrl();
    IndexerConfig config = configFor(server, QStringLiteral("Jackett"));
    config.kind = IndexerKind::Torznab;
    thePrefs.setIndexers({config});
    thePrefs.setIndexerMaxPages(1);

    IndexerSearchList list;
    const auto [ok, payload] = searchAndGrab(list, IndexerKind::Torznab);
    QVERIFY(ok);
    QCOMPARE(payload, magnet);

    MetaFileCache::instance().clear();
    thePrefs.setIndexers({});
    thePrefs.setConfigDir(savedConfigDir);
}

void tst_IndexerSearch::grab_magnetOnlyRowNeedsNoFetch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString savedConfigDir = thePrefs.configDir();
    thePrefs.setConfigDir(dir.path());

    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567");
    QString base;
    FakeIndexerServer server([&base, &magnet](const QUrl&) {
        return qMakePair(200, torznabFeed(base, {{QStringLiteral("Rel"), {}, magnet}}));
    });
    base = server.baseUrl();
    IndexerConfig config = configFor(server, QStringLiteral("Tracker"));
    config.kind = IndexerKind::Torznab;
    thePrefs.setIndexers({config});
    thePrefs.setIndexerMaxPages(1);

    IndexerSearchList list;
    const auto [ok, payload] = searchAndGrab(list, IndexerKind::Torznab);
    QVERIFY(ok);
    QCOMPARE(QString::fromUtf8(payload), magnet);
    // caps and search only, no grab request
    for (const QString& path : server.seenPaths)
        QVERIFY2(path.endsWith(QStringLiteral("api")), qPrintable(path));

    thePrefs.setIndexers({});
    thePrefs.setConfigDir(savedConfigDir);
}

void tst_IndexerSearch::torrentSearch_skipsUsenetRowsOfABothAccount()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString savedConfigDir = thePrefs.configDir();
    thePrefs.setConfigDir(dir.path());

    QString base;
    FakeIndexerServer server([&base](const QUrl&) {
        return qMakePair(200, torznabFeed(base, {{QStringLiteral("Nzb.Row"), QStringLiteral("/a.nzb"), {}},
                                                 {QStringLiteral("Tor.Row"), {},
                                                  QStringLiteral("magnet:?xt=urn:btih:00")}}));
    });
    base = server.baseUrl();
    IndexerConfig config = configFor(server, QStringLiteral("Prowlarr"));
    config.kind = IndexerKind::Both;
    thePrefs.setIndexers({config});
    thePrefs.setIndexerMaxPages(1);

    IndexerSearchList list;
    QSignalSpy finished(&list, &IndexerSearchList::searchFinished);
    IndexerQuery query;
    query.text = QStringLiteral("x");
    QString error;
    const quint32 id = list.startSearch(query, IndexerKind::Torznab, {}, error);
    QVERIFY2(id != 0, qPrintable(error));
    QVERIFY(finished.wait(5000));
    QCOMPARE(list.search(id)->results().size(), 1);
    QCOMPARE(list.search(id)->results().first().title, QStringLiteral("Tor.Row"));

    thePrefs.setIndexers({});
    thePrefs.setConfigDir(savedConfigDir);
}

void tst_IndexerSearch::hasEnabledAccount_followsKindAndEnabled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString savedConfigDir = thePrefs.configDir();
    thePrefs.setConfigDir(dir.path());

    IndexerConfig usenet;
    usenet.name = QStringLiteral("U");
    usenet.url = QStringLiteral("http://127.0.0.1:1/");
    IndexerConfig torrent = usenet;
    torrent.name = QStringLiteral("T");
    torrent.kind = IndexerKind::Torznab;
    torrent.enabled = false;

    thePrefs.setIndexers({usenet, torrent});
    {
        IndexerSearchList list;
        QVERIFY(list.hasEnabledAccount(IndexerKind::Newznab));
        QVERIFY(!list.hasEnabledAccount(IndexerKind::Torznab));   // disabled
    }
    torrent.enabled = true;
    thePrefs.setIndexers({torrent});
    {
        IndexerSearchList list;
        QVERIFY(!list.hasEnabledAccount(IndexerKind::Newznab));
        QVERIFY(list.hasEnabledAccount(IndexerKind::Torznab));
    }

    thePrefs.setIndexers({});
    thePrefs.setConfigDir(savedConfigDir);
}

void tst_IndexerSearch::tvSearch_fallsBackToKeywordsWithoutTheMode()
{
    // two indexers: one with tv-search in its caps, one without
    QList<QUrl> withMode, withoutMode;
    FakeIndexerServer a([&withMode](const QUrl& url) {
        withMode.append(url);
        return qMakePair(200, feed({QStringLiteral("A")}, 0, 1));
    });
    FakeIndexerServer b([&withoutMode](const QUrl& url) {
        withoutMode.append(url);
        return qMakePair(200, feed({QStringLiteral("B")}, 0, 1));
    });
    const IndexerConfig ca = configFor(a, QStringLiteral("A"));
    const IndexerConfig cb = configFor(b, QStringLiteral("B"));

    IndexerCaps tv;
    tv.modes.insert(kModeSearch.toString(), IndexerSearchMode{true, {}});
    tv.modes.insert(kModeTvSearch.toString(), IndexerSearchMode{true, {QStringLiteral("q"),
                    QStringLiteral("season"), QStringLiteral("ep")}});
    IndexerCaps plain;
    plain.modes.insert(kModeSearch.toString(), IndexerSearchMode{true, {}});
    plain.modes.insert(kModeTvSearch.toString(), IndexerSearchMode{false, {}});

    IndexerClient client;
    IndexerQuery query;
    query.text = QStringLiteral("show");
    query.mode = kQueryTvSearch.toString();
    query.categories = {5000};
    query.season = QStringLiteral("2");
    query.episode = QStringLiteral("5");

    IndexerSearch search(1, &client, query, {ca, cb}, 1);
    QSignalSpy finished(&search, &IndexerSearch::finished);
    search.start({{ca.key(), tv}, {cb.key(), plain}});
    QVERIFY(finished.wait(5000));
    QCOMPARE(search.results().size(), 2);

    QCOMPARE(withMode.size(), 1);
    const QUrlQuery qa(withMode.first());
    QCOMPARE(qa.queryItemValue(QStringLiteral("t")), QStringLiteral("tvsearch"));
    QCOMPARE(qa.queryItemValue(QStringLiteral("q")), QStringLiteral("show"));
    QCOMPARE(qa.queryItemValue(QStringLiteral("season")), QStringLiteral("2"));
    QCOMPARE(qa.queryItemValue(QStringLiteral("ep")), QStringLiteral("5"));

    QCOMPARE(withoutMode.size(), 1);
    const QUrlQuery qb(withoutMode.first());
    QCOMPARE(qb.queryItemValue(QStringLiteral("t")), QStringLiteral("search"));
    QCOMPARE(qb.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded), QStringLiteral("show S02E05"));
    QCOMPARE(qb.queryItemValue(QStringLiteral("cat")), QStringLiteral("5000"));
    QVERIFY(!qb.hasQueryItem(QStringLiteral("season")));
}

void tst_IndexerSearch::sizeBounds_dropRowsOutsideButKeepUnknownSize()
{
    // sizes 500, 1500, 3000 and one with none; bounds 1000..2000
    FakeIndexerServer server([](const QUrl&) {
        QByteArray out = R"(<?xml version="1.0"?><rss version="2.0"
             xmlns:newznab="http://www.newznab.com/DTD/2010/feeds/attributes/"><channel>)";
        const QList<QPair<QString, int>> rows{{QStringLiteral("Small"), 500},
                                              {QStringLiteral("Fits"), 1500},
                                              {QStringLiteral("Big"), 3000},
                                              {QStringLiteral("Unknown"), 0}};
        for (const auto& [title, size] : rows) {
            out += QStringLiteral(R"(<item><title>%1</title><guid>%1</guid>)"
                                  R"(<link>http://x.example/%1.nzb</link>)").arg(title).toUtf8();
            if (size > 0)
                out += QStringLiteral(R"(<newznab:attr name="size" value="%1"/>)").arg(size).toUtf8();
            out += "</item>";
        }
        out += "</channel></rss>";
        return qMakePair(200, out);
    });

    IndexerClient client;
    IndexerQuery query;
    query.text = QStringLiteral("x");
    query.minSize = 1000;
    query.maxSize = 2000;

    IndexerSearch search(1, &client, query, {configFor(server, QStringLiteral("One"))}, 3);
    QSignalSpy finished(&search, &IndexerSearch::finished);
    search.start({});
    QVERIFY(finished.wait(5000));

    QStringList titles;
    for (const auto& row : search.results())
        titles.append(row.title);
    titles.sort();
    QCOMPARE(titles, (QStringList{QStringLiteral("Fits"), QStringLiteral("Unknown")}));
}

QTEST_MAIN(tst_IndexerSearch)
#include "tst_IndexerSearch.moc"
