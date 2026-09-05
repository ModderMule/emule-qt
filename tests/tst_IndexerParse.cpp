/// @file tst_IndexerParse.cpp
/// @brief The newznab / torznab readers, over recorded XML.
///
/// Every case here is a trap that cost something to find, not a smoke test:
/// an error document that arrives with HTTP 200, a `size` attribute that
/// disagrees with `<enclosure length>`, caps booleans spelled "yes", and an
/// attribute namespace under a prefix the spec never promised.

#include "IndexerCaps.h"
#include "IndexerResult.h"

#include <QTest>

using namespace eMule::indexer;


// NOTE: the XML fixtures live *below* the Q_OBJECT class on purpose. moc's
// preprocessor does not understand C++11 raw strings, and a raw string above a
// Q_OBJECT class makes it emit a zero-byte .moc — which fails at link time as
// "missing vtable", pointing nowhere near the cause.

class tst_IndexerParse : public QObject {
    Q_OBJECT

private slots:
    // -- caps -----------------------------------------------------------------
    void caps_readsLimitsModesAndCategories();
    void caps_treatsYesAsTrue();
    void caps_aModeWithNoParamsAcceptsTheBasics();
    void caps_resolvesASubcategoryName();

    // -- errors ---------------------------------------------------------------
    void error_isDetectedDespiteAnHttp200();
    void error_fallsBackToTheCodeWithNoDescription();
    void search_reportsAnErrorDocumentInsteadOfZeroRows();

    // -- search ---------------------------------------------------------------
    void search_readsTitleSizeDateAndAttrs();
    void search_prefersTheSizeAttrOverTheEnclosureLength();
    void search_readsAnRfc822PubDate();
    void search_readsAnIsoPubDate();
    void search_bindsAttrsUnderAnUnexpectedPrefix();
    void search_readsTorznabFields();
    void search_skipsAnItemWithNoDownloadUrl();
    void search_dedupKeyIgnoresCase();
};

namespace {

const QByteArray kCaps = R"(<?xml version="1.0" encoding="UTF-8"?>
<caps>
  <server version="1.1" title="Test Indexer"/>
  <limits max="100" default="50"/>
  <searching>
    <search available="yes" supportedParams="q"/>
    <tv-search available="yes" supportedParams="q,season,ep"/>
    <movie-search available="no" supportedParams="q,imdbid"/>
    <audio-search available="no" supportedParams=""/>
  </searching>
  <categories>
    <category id="2000" name="Movies">
      <subcat id="2040" name="HD"/>
      <subcat id="2030" name="SD"/>
    </category>
    <category id="5000" name="TV"/>
  </categories>
</caps>)";

/// A normal newznab answer. Note the deliberate disagreement on item 1: the
/// attr says 734003200, the enclosure says 12345 — real indexers put the size of
/// the .nzb itself in the enclosure.
const QByteArray kSearch = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:newznab="http://www.newznab.com/DTD/2010/feeds/attributes/">
  <channel>
    <newznab:response offset="0" total="217"/>
    <item>
      <title>Some.Release.2160p.WEB.H265</title>
      <guid isPermaLink="false">abc123</guid>
      <link>https://indexer.example/details/abc123</link>
      <pubDate>Mon, 25 Aug 2026 11:30:00 +0000</pubDate>
      <enclosure url="https://indexer.example/getnzb/abc123.nzb?apikey=SEKRIT"
                 length="12345" type="application/x-nzb"/>
      <newznab:attr name="size" value="734003200"/>
      <newznab:attr name="category" value="2000"/>
      <newznab:attr name="category" value="2040"/>
      <newznab:attr name="grabs" value="42"/>
      <newznab:attr name="files" value="97"/>
      <newznab:attr name="password" value="0"/>
      <newznab:attr name="group" value="alt.binaries.test"/>
    </item>
    <item>
      <title>Locked.Release</title>
      <guid isPermaLink="false">def456</guid>
      <enclosure url="https://indexer.example/getnzb/def456.nzb" length="900" type="application/x-nzb"/>
      <newznab:attr name="password" value="1"/>
    </item>
  </channel>
</rss>)";

} // namespace

// ---------------------------------------------------------------------------
// caps
// ---------------------------------------------------------------------------

void tst_IndexerParse::caps_readsLimitsModesAndCategories()
{
    QString error;
    const IndexerCaps caps = parseIndexerCaps(kCaps, error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(caps.serverTitle, QStringLiteral("Test Indexer"));
    QCOMPARE(caps.limitMax, 100);
    QCOMPARE(caps.limitDefault, 50);
    QCOMPARE(caps.categories.size(), 2);
    QCOMPARE(caps.categories.at(0).subcategories.size(), 2);
    QVERIFY(caps.probedAt.isValid());
}

void tst_IndexerParse::caps_treatsYesAsTrue()
{
    // The caps schema spells its booleans as words. A naive toBool() reads
    // available="yes" as false and greys out every search field the indexer
    // actually supports.
    QString error;
    const IndexerCaps caps = parseIndexerCaps(kCaps, error);

    QVERIFY(caps.supportsMode(kModeSearch));
    QVERIFY(caps.supportsMode(kModeTvSearch));
    QVERIFY(!caps.supportsMode(kModeMovieSearch));   // available="no"

    QVERIFY(caps.supportsParam(kModeTvSearch, u"season"));
    QVERIFY(!caps.supportsParam(kModeSearch, u"season"));
    // A mode that is not available supports nothing, whatever it lists.
    QVERIFY(!caps.supportsParam(kModeMovieSearch, u"imdbid"));
}

void tst_IndexerParse::caps_aModeWithNoParamsAcceptsTheBasics()
{
    // An indexer that advertises a mode but lists no supportedParams is saying
    // nothing, not saying no. Reading it as "supports nothing" would leave the
    // search box greyed out on several real indexers.
    const QByteArray xml = R"(<caps><searching><search available="yes"/></searching></caps>)";
    QString error;
    const IndexerCaps caps = parseIndexerCaps(xml, error);

    QVERIFY(caps.supportsMode(kModeSearch));
    QVERIFY(caps.supportsParam(kModeSearch, u"q"));
    QVERIFY(caps.supportsParam(kModeSearch, u"anythingElse"));
}

void tst_IndexerParse::caps_resolvesASubcategoryName()
{
    QString error;
    const IndexerCaps caps = parseIndexerCaps(kCaps, error);

    QCOMPARE(caps.categoryName(2000), QStringLiteral("Movies"));
    QCOMPARE(caps.categoryName(2040), QStringLiteral("HD"));
    QVERIFY(caps.categoryName(9999).isEmpty());
}

// ---------------------------------------------------------------------------
// errors — the ones that arrive looking like success
// ---------------------------------------------------------------------------

void tst_IndexerParse::error_isDetectedDespiteAnHttp200()
{
    // This is the whole reason parseIndexerError() exists and runs first.
    const QByteArray xml =
        R"(<?xml version="1.0"?><error code="100" description="Incorrect user credentials"/>)";
    QCOMPARE(parseIndexerError(xml), QStringLiteral("Incorrect user credentials"));
}

void tst_IndexerParse::error_fallsBackToTheCodeWithNoDescription()
{
    QCOMPARE(parseIndexerError(R"(<error code="910"/>)"),
             QStringLiteral("indexer error 910"));
    // A perfectly good feed is not an error.
    QVERIFY(parseIndexerError(kSearch).isEmpty());
    QVERIFY(parseIndexerError(kCaps).isEmpty());
}

void tst_IndexerParse::search_reportsAnErrorDocumentInsteadOfZeroRows()
{
    const QByteArray xml = R"(<error code="101" description="Account suspended"/>)";
    const IndexerSearchPage page =
        parseIndexerSearch(xml, QStringLiteral("Test"), QStringLiteral("test"));

    QCOMPARE(page.error, QStringLiteral("Account suspended"));
    QVERIFY(page.results.isEmpty());
}

// ---------------------------------------------------------------------------
// search
// ---------------------------------------------------------------------------

void tst_IndexerParse::search_readsTitleSizeDateAndAttrs()
{
    const IndexerSearchPage page =
        parseIndexerSearch(kSearch, QStringLiteral("Test"), QStringLiteral("test"));

    QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
    QCOMPARE(page.results.size(), 2);
    QCOMPARE(page.total, 217);
    QCOMPARE(page.offset, 0);

    const IndexerResult& row = page.results.at(0);
    QCOMPARE(row.title, QStringLiteral("Some.Release.2160p.WEB.H265"));
    QCOMPARE(row.guid, QStringLiteral("abc123"));
    QCOMPARE(row.grabs, 42);
    QCOMPARE(row.files, 97);
    QCOMPARE(row.group, QStringLiteral("alt.binaries.test"));
    QVERIFY(!row.passwordProtected);
    QCOMPARE(row.categoryIds, QList<int>({2000, 2040}));
    QCOMPARE(row.indexerName, QStringLiteral("Test"));
    QCOMPARE(row.id, QStringLiteral("test/abc123"));
    QVERIFY(row.isUsenet());

    // password="1" is protected; anything but "0" is.
    QVERIFY(page.results.at(1).passwordProtected);
}

void tst_IndexerParse::search_prefersTheSizeAttrOverTheEnclosureLength()
{
    // The two disagree in the fixture on purpose: 734003200 in the attr,
    // 12345 in the enclosure. Taking the enclosure would show a 700 MB release
    // as 12 KB.
    const IndexerSearchPage page =
        parseIndexerSearch(kSearch, QStringLiteral("Test"), QStringLiteral("test"));

    QCOMPARE(page.results.at(0).size, 734003200LL);

    // With no attr at all, the enclosure length is all there is.
    QCOMPARE(page.results.at(1).size, 900LL);
}

void tst_IndexerParse::search_readsAnRfc822PubDate()
{
    // The fixture says "Mon, 25 Aug 2026" and 25 August 2026 is a Tuesday. That
    // is deliberate: Qt's RFC2822 parser validates the day name against the date
    // and rejects the whole timestamp when they disagree, and feeds in the wild
    // get it wrong routinely. Rejecting the date over it blanks the Age column,
    // which is the most useful thing about a Usenet result.
    const IndexerSearchPage page =
        parseIndexerSearch(kSearch, QStringLiteral("Test"), QStringLiteral("test"));

    const IndexerResult& row = page.results.at(0);
    QVERIFY(row.published.isValid());
    QCOMPARE(row.published.toUTC().date(), QDate(2026, 8, 25));
    QVERIFY(row.ageDays() >= 0);

    // An item with no date at all reports an unknown age rather than "today".
    QCOMPARE(page.results.at(1).ageDays(), -1);
}

void tst_IndexerParse::search_readsAnIsoPubDate()
{
    // A few indexers emit ISO 8601 rather than RFC 822.
    const QByteArray xml = R"(<?xml version="1.0"?>
<rss version="2.0"><channel><item>
  <title>Iso.Release</title><guid>iso</guid>
  <pubDate>2026-08-25T11:30:00Z</pubDate>
  <enclosure url="https://x.example/iso.nzb" length="10"/>
</item></channel></rss>)";

    const IndexerSearchPage page =
        parseIndexerSearch(xml, QStringLiteral("Test"), QStringLiteral("test"));
    QCOMPARE(page.results.size(), 1);
    QVERIFY(page.results.at(0).published.isValid());
    QCOMPARE(page.results.at(0).published.toUTC().date(), QDate(2026, 8, 25));
}

void tst_IndexerParse::search_bindsAttrsUnderAnUnexpectedPrefix()
{
    // A prefix is a document-local label. Prowlarr, Jackett and NZBHydra2 do not
    // agree on it, and a reader that matched the literal "newznab:" would read
    // zero attributes from this — a feed with titles and no sizes.
    const QByteArray xml = R"(<?xml version="1.0"?>
<rss version="2.0" xmlns:nn="http://www.newznab.com/DTD/2010/feeds/attributes/">
  <channel>
    <item>
      <title>Prefixed.Release</title>
      <guid>zzz</guid>
      <enclosure url="https://indexer.example/z.nzb" length="1" type="application/x-nzb"/>
      <nn:attr name="size" value="4096"/>
      <nn:attr name="grabs" value="7"/>
    </item>
  </channel>
</rss>)";

    const IndexerSearchPage page =
        parseIndexerSearch(xml, QStringLiteral("Test"), QStringLiteral("test"));

    QCOMPARE(page.results.size(), 1);
    QCOMPARE(page.results.at(0).size, 4096LL);
    QCOMPARE(page.results.at(0).grabs, 7);
}

void tst_IndexerParse::search_readsTorznabFields()
{
    // Nothing consumes these yet. They are parsed now so that a BitTorrent
    // module inherits a working reader instead of a second one being written.
    const QByteArray xml = R"(<?xml version="1.0"?>
<rss version="2.0" xmlns:torznab="http://torznab.com/schemas/2015/feed">
  <channel>
    <item>
      <title>Torrent.Release</title>
      <guid>tor1</guid>
      <enclosure url="https://tracker.example/t.torrent" length="10" type="application/x-bittorrent"/>
      <torznab:attr name="size" value="2048"/>
      <torznab:attr name="seeders" value="123"/>
      <torznab:attr name="peers" value="140"/>
      <torznab:attr name="magneturl" value="magnet:?xt=urn:btih:0123456789abcdef"/>
      <torznab:attr name="infohash" value="0123456789abcdef"/>
    </item>
  </channel>
</rss>)";

    const IndexerSearchPage page =
        parseIndexerSearch(xml, QStringLiteral("Tracker"), QStringLiteral("tracker"));

    QCOMPARE(page.results.size(), 1);
    const IndexerResult& row = page.results.at(0);
    QCOMPARE(row.seeders, 123);
    QCOMPARE(row.peers, 140);
    QCOMPARE(row.infoHash, QStringLiteral("0123456789abcdef"));
    // A row with a torrent payload is not Usenet, whatever the account is
    // configured as — an aggregator answers one query with both kinds.
    QVERIFY(!row.isUsenet());
}

void tst_IndexerParse::search_skipsAnItemWithNoDownloadUrl()
{
    const QByteArray xml = R"(<?xml version="1.0"?>
<rss version="2.0"><channel>
  <item><title>No.Payload</title><guid>x</guid></item>
  <item><title/><enclosure url="https://x.example/a.nzb" length="1"/></item>
</channel></rss>)";

    const IndexerSearchPage page =
        parseIndexerSearch(xml, QStringLiteral("Test"), QStringLiteral("test"));
    QVERIFY(page.results.isEmpty());
}

void tst_IndexerParse::search_dedupKeyIgnoresCase()
{
    IndexerResult a;
    a.title = QStringLiteral("Some.Release");
    a.size = 100;
    IndexerResult b;
    b.title = QStringLiteral("some.release");
    b.size = 100;
    IndexerResult c;
    c.title = QStringLiteral("Some.Release");
    c.size = 101;

    QCOMPARE(a.dedupKey(), b.dedupKey());
    QVERIFY(a.dedupKey() != c.dedupKey());
}

QTEST_MAIN(tst_IndexerParse)
#include "tst_IndexerParse.moc"
