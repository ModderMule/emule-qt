/// @file tst_IndexerLiveSearch.cpp
/// @brief A real query against a real indexer. Labelled `live`.
///
/// Every value comes from the project-root .env (or the process environment,
/// which wins over it) — **there are no defaults**. Naming one indexer in the
/// source would make it this project's suggested provider, and the URL is as
/// much the user's private account detail as the key is.
///
///   EMULE_INDEXER_APIKEY   the account's API key
///   EMULE_INDEXER_URL      the API base, e.g. a newznab host or a Jackett path
///   EMULE_INDEXER_QUERY    a keyword expected to match something
///
/// **The key is never checked in.** It is a live credential against a paid
/// account, and a key in a fixture is a key in the git history for good. With
/// any of the three unset every case skips, the same way tst_UsenetLiveConnect
/// skips without EMULE_NNTP_HOST. .env is gitignored.
///
/// Excluded from `ctest -LE live`. Run it deliberately:
///   ctest -L live -R tst_IndexerLiveSearch

#include "IndexerCaps.h"
#include "IndexerClient.h"
#include "IndexerQuery.h"
#include "IndexerSearch.h"

#include "TestHelpers.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule::indexer;
using eMule::testing::loadProjectEnv;

namespace {

QString env(const char* name)
{
    return qEnvironmentVariable(name).trimmed();
}

/// The three settings, or the reason the run has to skip.
QString missingSetting()
{
    for (const char* name : {"EMULE_INDEXER_APIKEY", "EMULE_INDEXER_URL",
                             "EMULE_INDEXER_QUERY"}) {
        if (env(name).isEmpty())
            return QString::fromLatin1(name);
    }
    return {};
}

IndexerConfig liveConfig()
{
    IndexerConfig config;
    config.name = QStringLiteral("live");
    config.url = env("EMULE_INDEXER_URL");
    config.apiKey = env("EMULE_INDEXER_APIKEY");
    config.timeoutMs = 30000;
    return config;
}

} // namespace

class tst_IndexerLiveSearch : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void probesCapabilities();
    void runsAKeywordSearch();
    void rejectsABadApiKeyWithTheIndexersOwnWords();
};

void tst_IndexerLiveSearch::initTestCase()
{
    // .env fills in whatever the process environment did not already set.
    loadProjectEnv();
}

void tst_IndexerLiveSearch::probesCapabilities()
{
    if (const QString missing = missingSetting(); !missing.isEmpty())
        QSKIP(qPrintable(QStringLiteral("%1 is not set in .env").arg(missing)));

    IndexerClient client;
    bool ok = false;
    IndexerCaps caps;
    QString error;

    bool done = false;
    client.probeCaps(liveConfig(), [&](bool o, const IndexerCaps& c, const QString& e) {
        ok = o;
        caps = c;
        error = e;
        done = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(caps.limitMax > 0);
    QVERIFY2(!caps.categories.isEmpty(), "a real indexer advertises categories");
    QVERIFY(caps.supportsMode(kModeSearch));
}

void tst_IndexerLiveSearch::runsAKeywordSearch()
{
    if (const QString missing = missingSetting(); !missing.isEmpty())
        QSKIP(qPrintable(QStringLiteral("%1 is not set in .env").arg(missing)));

    IndexerClient client;
    IndexerQuery query;
    query.text = env("EMULE_INDEXER_QUERY");
    query.limit = 20;

    // One page only: this is someone's paid API allowance.
    IndexerSearch search(1, &client, query, {liveConfig()}, /*maxPages*/ 1);
    QSignalSpy finished(&search, &IndexerSearch::finished);
    search.start({});

    QVERIFY(finished.wait(40000));
    QVERIFY2(finished.first().at(1).toString().isEmpty(),
             qPrintable(finished.first().at(1).toString()));

    const auto& rows = search.results();
    QVERIFY2(!rows.isEmpty(), "a common keyword should match something");

    // Every row the GUI shows a column for has to actually arrive; a feed parsed
    // without extended=1 would give titles and nothing else.
    for (const auto& row : rows) {
        QVERIFY(!row.title.isEmpty());
        QVERIFY(!row.id.isEmpty());
        QVERIFY(!row.downloadUrl.isEmpty());
        QVERIFY2(row.size > 0, qPrintable(row.title));
        QVERIFY2(row.published.isValid(), qPrintable(row.title));
    }
}

void tst_IndexerLiveSearch::rejectsABadApiKeyWithTheIndexersOwnWords()
{
    if (const QString missing = missingSetting(); !missing.isEmpty())
        QSKIP(qPrintable(QStringLiteral("%1 is not set in .env").arg(missing)));

    // Through a search, not through t=caps. Several indexers serve their
    // capability document to anyone — the key is only checked once you ask for
    // results — so a caps probe with a bad key can come back perfectly fine and
    // would prove nothing about the credential path.
    IndexerConfig config = liveConfig();
    config.apiKey = QStringLiteral("definitely-not-a-valid-key");

    IndexerQuery query;
    query.text = env("EMULE_INDEXER_QUERY");
    query.limit = 10;

    IndexerClient client;
    bool done = false;
    bool ok = true;
    QString error;
    client.search(config, query, nullptr,
                  [&](bool o, const IndexerSearchPage& page, const QString& e) {
                      ok = o;
                      // The rejection usually arrives as HTTP 200 with an
                      // <error> document, so the page carries it, not the
                      // transport.
                      error = e.isEmpty() ? page.error : e;
                      done = true;
                  });

    QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
    QVERIFY2(!ok, "a bad key must not come back as a successful search");
    QVERIFY2(!error.isEmpty(), "a rejection must say something");
    // And whatever it says, it must not quote the key back at us.
    QVERIFY2(!error.contains(QStringLiteral("definitely-not-a-valid-key")),
             qPrintable(error));
}

QTEST_MAIN(tst_IndexerLiveSearch)
#include "tst_IndexerLiveSearch.moc"
