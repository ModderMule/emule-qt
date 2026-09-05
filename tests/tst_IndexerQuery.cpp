/// @file tst_IndexerQuery.cpp
/// @brief URL construction, capability gating, and API-key redaction.
///
/// The redaction cases are the important ones. An indexer API key travels as a
/// *query parameter*, so every log line, error string and status message that
/// carries a URL carries the key with it. There is no second layer catching
/// that, which is why it is tested rather than reviewed.

#include "IndexerQuery.h"

#include <QTest>
#include <QUrlQuery>

using namespace eMule::indexer;

namespace {

IndexerConfig makeConfig(const QString& url = QStringLiteral("https://api.example.com/"))
{
    IndexerConfig config;
    config.name = QStringLiteral("Test Indexer");
    config.url = url;
    config.apiKey = QStringLiteral("s3cr3tk3y");
    return config;
}

QString param(const QUrl& url, const char* name)
{
    return QUrlQuery(url.query()).queryItemValue(QLatin1String(name));
}

} // namespace

class tst_IndexerQuery : public QObject {
    Q_OBJECT

private slots:
    // -- URL normalisation ----------------------------------------------------
    void apiUrl_completesABareHost();
    void apiUrl_leavesAnExplicitPathAlone();
    void apiUrl_rejectsGarbage();
    void slug_isFilenameSafe();

    // -- request building -----------------------------------------------------
    void caps_asksForCapsAndCarriesTheKey();
    void search_carriesTheBasicParameters();
    void search_preservesAQueryTheUserTyped();
    void search_clampsTheLimitToWhatTheIndexerAllows();
    void search_pagesWithAnOffset();

    // -- capability gating ----------------------------------------------------
    void gating_dropsAParameterTheModeDoesNotAdvertise();
    void gating_sendsEverythingWhenNothingHasBeenProbed();

    // -- redaction ------------------------------------------------------------
    void redact_removesTheKeyFromAUrl();
    void redact_removesTheKeyFromProse();
    void redact_leavesAKeylessUrlAlone();
};

void tst_IndexerQuery::apiUrl_completesABareHost()
{
    // A bare host is the one form we can safely complete: append /api.
    QCOMPARE(makeConfig(QStringLiteral("https://api.example.org/")).apiUrl().path(),
             QStringLiteral("/api"));
    QCOMPARE(makeConfig(QStringLiteral("https://api.example.org")).apiUrl().path(),
             QStringLiteral("/api"));
}

void tst_IndexerQuery::apiUrl_leavesAnExplicitPathAlone()
{
    // A Jackett endpoint already ends in /api. "Always append" would turn a
    // working URL into a 404 the user has no way to correct, because the
    // completion happens after they stop typing.
    const QString jackett =
        QStringLiteral("http://localhost:9117/api/v2.0/indexers/nzb/results/torznab/api");
    QCOMPARE(makeConfig(jackett).apiUrl().path(),
             QStringLiteral("/api/v2.0/indexers/nzb/results/torznab/api"));

    // An NZBHydra2 mount point, likewise.
    QCOMPARE(makeConfig(QStringLiteral("http://box:5076/api")).apiUrl().path(),
             QStringLiteral("/api"));
}

void tst_IndexerQuery::apiUrl_rejectsGarbage()
{
    QVERIFY(!makeConfig(QStringLiteral("not a url")).apiUrl().isValid());
    QVERIFY(!makeConfig(QString{}).apiUrl().isValid());
    QVERIFY(!makeConfig(QStringLiteral("relative/path")).apiUrl().isValid());
}

void tst_IndexerQuery::slug_isFilenameSafe()
{
    IndexerConfig config = makeConfig();
    config.name = QStringLiteral("NZB Geek / Main!");
    const QString slug = config.slug();

    QVERIFY(!slug.contains(u'/'));
    QVERIFY(!slug.contains(u' '));
    QVERIFY(!slug.endsWith(u'_'));
    QCOMPARE(slug, QStringLiteral("nzb_geek_main"));

    config.name = QStringLiteral("!!!");
    QCOMPARE(config.slug(), QStringLiteral("indexer"));
}

void tst_IndexerQuery::caps_asksForCapsAndCarriesTheKey()
{
    const QUrl url = buildIndexerCapsUrl(makeConfig());

    QCOMPARE(param(url, "t"), QStringLiteral("caps"));
    QCOMPARE(param(url, "apikey"), QStringLiteral("s3cr3tk3y"));
}

void tst_IndexerQuery::search_carriesTheBasicParameters()
{
    IndexerQuery query;
    query.text = QStringLiteral("ubuntu 24.04");
    query.categories = {2000, 5000};
    query.limit = 50;

    const QUrl url = buildIndexerSearchUrl(makeConfig(), query, nullptr);

    QCOMPARE(param(url, "t"), QStringLiteral("search"));
    QCOMPARE(param(url, "q"), QStringLiteral("ubuntu 24.04"));
    QCOMPARE(param(url, "cat"), QStringLiteral("2000,5000"));
    QCOMPARE(param(url, "limit"), QStringLiteral("50"));
    // extended=1 is what turns the newznab:attr elements on; without it the feed
    // carries a title and a link and nothing worth showing.
    QCOMPARE(param(url, "extended"), QStringLiteral("1"));
    QCOMPARE(param(url, "apikey"), QStringLiteral("s3cr3tk3y"));
}

void tst_IndexerQuery::search_preservesAQueryTheUserTyped()
{
    // A self-hosted endpoint can need a parameter we know nothing about.
    IndexerQuery query;
    query.text = QStringLiteral("x");

    const QUrl url = buildIndexerSearchUrl(
        makeConfig(QStringLiteral("http://host:9117/api?extra=keepme")), query, nullptr);

    QCOMPARE(param(url, "extra"), QStringLiteral("keepme"));
    QCOMPARE(param(url, "t"), QStringLiteral("search"));
}

void tst_IndexerQuery::search_clampsTheLimitToWhatTheIndexerAllows()
{
    // Asking for more than limits/@max is not an error — the indexer silently
    // truncates, and the offsets we page with then stop lining up.
    IndexerCaps caps;
    caps.limitMax = 100;
    caps.modes.insert(kModeSearch.toString(), IndexerSearchMode{true, {}});

    IndexerQuery query;
    query.text = QStringLiteral("x");
    query.limit = 500;

    QCOMPARE(param(buildIndexerSearchUrl(makeConfig(), query, &caps), "limit"),
             QStringLiteral("100"));
}

void tst_IndexerQuery::search_pagesWithAnOffset()
{
    IndexerQuery query;
    query.text = QStringLiteral("x");
    query.offset = 0;
    QVERIFY(param(buildIndexerSearchUrl(makeConfig(), query, nullptr), "offset").isEmpty());

    query.offset = 100;
    QCOMPARE(param(buildIndexerSearchUrl(makeConfig(), query, nullptr), "offset"),
             QStringLiteral("100"));
}

void tst_IndexerQuery::gating_dropsAParameterTheModeDoesNotAdvertise()
{
    IndexerCaps caps;
    caps.limitMax = 100;
    caps.modes.insert(kModeTvSearch.toString(),
                      IndexerSearchMode{true, {QStringLiteral("q"), QStringLiteral("season")}});

    IndexerQuery query;
    query.mode = kModeTvSearch.toString();
    query.text = QStringLiteral("show");
    query.season = QStringLiteral("2");
    query.episode = QStringLiteral("5");     // "ep" is not advertised
    query.imdbId = QStringLiteral("tt123");  // nor is "imdbid"

    const QUrl url = buildIndexerSearchUrl(makeConfig(), query, &caps);

    QCOMPARE(param(url, "season"), QStringLiteral("2"));
    QVERIFY(param(url, "ep").isEmpty());
    QVERIFY(param(url, "imdbid").isEmpty());
}

void tst_IndexerQuery::gating_sendsEverythingWhenNothingHasBeenProbed()
{
    // No caps is not "supports nothing" — a search must still work before the
    // first probe, and every newznab implementation accepts the basics.
    IndexerQuery query;
    query.text = QStringLiteral("show");
    query.season = QStringLiteral("2");

    QCOMPARE(param(buildIndexerSearchUrl(makeConfig(), query, nullptr), "season"),
             QStringLiteral("2"));

    const IndexerCaps empty;
    QCOMPARE(param(buildIndexerSearchUrl(makeConfig(), query, &empty), "season"),
             QStringLiteral("2"));
}

void tst_IndexerQuery::redact_removesTheKeyFromAUrl()
{
    const QUrl url = buildIndexerSearchUrl(makeConfig(), IndexerQuery{}, nullptr);
    const QString redacted = redactApiKey(url);

    QVERIFY2(!redacted.contains(QStringLiteral("s3cr3tk3y")), qPrintable(redacted));
    QVERIFY(redacted.contains(QStringLiteral("apikey=%3Credacted%3E"))
            || redacted.contains(QStringLiteral("apikey=<redacted>")));
    // Everything else must survive, or the redacted URL is useless for debugging.
    QVERIFY(redacted.contains(QStringLiteral("t=search")));
}

void tst_IndexerQuery::redact_removesTheKeyFromProse()
{
    // Qt's own network error strings embed the request URL, so the key arrives
    // wrapped in prose that no QUrl parse will reach.
    const QString error = QStringLiteral(
        "Error transferring https://api.example.com/api?t=search&apikey=s3cr3tk3y&limit=100"
        " - server replied: Unauthorized");
    const QString redacted = redactApiKey(error);

    QVERIFY2(!redacted.contains(QStringLiteral("s3cr3tk3y")), qPrintable(redacted));
    QVERIFY(redacted.contains(QStringLiteral("limit=100")));
    QVERIFY(redacted.contains(QStringLiteral("Unauthorized")));
}

void tst_IndexerQuery::redact_leavesAKeylessUrlAlone()
{
    const QString plain = QStringLiteral("https://api.example.com/api?t=caps");
    QCOMPARE(redactApiKey(QUrl(plain)), plain);
    QCOMPARE(redactApiKey(plain), plain);
}

QTEST_MAIN(tst_IndexerQuery)
#include "tst_IndexerQuery.moc"
