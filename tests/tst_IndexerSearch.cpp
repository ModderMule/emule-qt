/// @file tst_IndexerSearch.cpp
/// @brief The fan-out: several indexers, paging, dedup and failure isolation.
///
/// Runs against a fake HTTP server in-process, so no network and no API key.
/// What it is really testing is that one indexer misbehaving costs its share of
/// the results and nothing more — with three configured, one being down should
/// lose a third of the answers, not all of them.

#include "IndexerClient.h"
#include "IndexerSearch.h"
#include "IndexerQuery.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

using namespace eMule::indexer;

namespace {

/// Minimal HTTP/1.0 responder. One request per connection, closed on write —
/// enough for a client that issues plain GETs and reads to the end.
class FakeIndexerServer : public QTcpServer {
public:
    /// Called with the request's query string; returns [status, body].
    using Handler = std::function<QPair<int, QByteArray>(const QUrlQuery&)>;

    explicit FakeIndexerServer(Handler handler, QObject* parent = nullptr)
        : QTcpServer(parent)
        , m_handler(std::move(handler))
    {
        listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1/api").arg(serverPort());
    }

    int requestCount = 0;
    QStringList seenQueries;

protected:
    void incomingConnection(qintptr handle) override
    {
        auto* socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);

        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            const QByteArray request = socket->readAll();
            const qsizetype start = request.indexOf(' ');
            const qsizetype end = request.indexOf(' ', start + 1);
            if (start < 0 || end < 0)
                return;

            const QUrl url(QString::fromUtf8(request.mid(start + 1, end - start - 1)));
            const QUrlQuery query(url.query());

            ++requestCount;
            seenQueries.append(url.query());

            const auto [status, body] = m_handler(query);
            const QByteArray header =
                QByteArrayLiteral("HTTP/1.0 ") + QByteArray::number(status)
                + QByteArrayLiteral(" X\r\nContent-Type: application/xml\r\nContent-Length: ")
                + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n");
            socket->write(header);
            socket->write(body);
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    Handler m_handler;
};

} // namespace

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
    FakeIndexerServer server([](const QUrlQuery&) {
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
    FakeIndexerServer first([](const QUrlQuery&) {
        return qMakePair(200, feed({QStringLiteral("Shared"), QStringLiteral("OnlyFirst")}, 0, 2));
    });
    FakeIndexerServer second([](const QUrlQuery&) {
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
    FakeIndexerServer good([](const QUrlQuery&) {
        return qMakePair(200, feed({QStringLiteral("Kept")}, 0, 1));
    });
    FakeIndexerServer bad([](const QUrlQuery&) {
        return qMakePair(500, QByteArray("boom"));
    });

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
}

void tst_IndexerSearch::anErrorDocumentIsReportedNotSilentlyEmpty()
{
    // HTTP 200 with an <error> body — how a wrong API key actually arrives.
    FakeIndexerServer server([](const QUrlQuery&) {
        return qMakePair(200,
            QByteArray(R"(<error code="100" description="Incorrect user credentials"/>)"));
    });

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
}

void tst_IndexerSearch::pagingStopsAtTheCap()
{
    // The indexer always answers with a full page and claims plenty more, so
    // only the page cap can end this. That cap is a spending limit: every page
    // is an API call against the user's allowance.
    FakeIndexerServer server([](const QUrlQuery& query) {
        const int offset = query.queryItemValue(QStringLiteral("offset")).toInt();
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
    FakeIndexerServer server([](const QUrlQuery& query) {
        const int offset = query.queryItemValue(QStringLiteral("offset")).toInt();
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
    FakeIndexerServer server([](const QUrlQuery& query) {
        const int offset = query.queryItemValue(QStringLiteral("offset")).toInt();
        return qMakePair(200, feed({QStringLiteral("P%1").arg(offset)}, offset, 100));
    });

    IndexerClient client;
    IndexerQuery query;
    query.text = QStringLiteral("x");
    query.limit = 1;

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
}

QTEST_MAIN(tst_IndexerSearch)
#include "tst_IndexerSearch.moc"
