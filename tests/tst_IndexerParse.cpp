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
#include <QTimeZone>

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
    void search_classifiesThePasswordAttr();
    void search_takesABracedPasswordOutOfTheTitle();

    // -- usenet-crawler -------------------------------------------------------
    void usenetCrawler_capsIsFullyReadable();
    void usenetCrawler_searchIsFullyReadable();
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

/// The two documents usenet-crawler actually emits, copied verbatim from
/// usenet-crawler/pkg/newznab/testdata/.
///
/// They are here so that a change on either side breaks a build rather than a
/// user's search. The Go side pins them with a golden test; when that test fails,
/// the new bytes are regenerated with `go test ./pkg/newznab -update` and pasted
/// below. This is the only thing that crosses between the two projects: a test
/// vector, parsed here by a different language and a different compiler from the
/// one that wrote it — which is what makes it capable of disagreeing with us.
const QByteArray kUsenetCrawlerCaps = R"(<?xml version="1.0" encoding="UTF-8"?>
<caps>
  <server version="1.0" title="usenet-crawler" strapline="a crawl of the binary groups" email="nobody@example.invalid" url="http://indexer.example.invalid"/>
  <limits max="100" default="100"/>
  <retention days="3000"/>
  <registration available="no" open="no"/>
  <searching>
    <search available="yes" supportedParams="q,cat,limit,offset,maxage,minsize,maxsize,group,extended"/>
    <tv-search available="yes" supportedParams="q,cat,limit,offset,maxage,season,ep,extended"/>
    <movie-search available="yes" supportedParams="q,cat,limit,offset,maxage,extended"/>
    <audio-search available="no"/>
    <book-search available="no"/>
  </searching>
  <categories>
    <category id="2000" name="Movies">
      <subcat id="2040" name="HD"/>
    </category>
    <category id="5000" name="TV">
      <subcat id="5040" name="HD"/>
    </category>
    <category id="6000" name="XXX">
      <subcat id="6040" name="x264"/>
    </category>
  </categories>
</caps>)";

const QByteArray kUsenetCrawlerSearch = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:atom="http://www.w3.org/2005/Atom" xmlns:newznab="http://www.newznab.com/DTD/2010/feeds/attributes/">
<channel>
  <atom:link href="http://indexer.example.invalid/api" rel="self" type="application/rss+xml"/>
  <title>usenet-crawler</title>
  <description>usenet-crawler API results</description>
  <link>http://indexer.example.invalid/</link>
  <language>en-gb</language>
  <webMaster>nobody@example.invalid (usenet-crawler)</webMaster>
  <newznab:response offset="0" total="1"/>
  <item>
    <title>A.Tv.Show.S06E05.1080p.WEB-DL "special" &amp; &lt;friends&gt;</title>
    <guid isPermaLink="false">ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789</guid>
    <link>http://indexer.example.invalid/getnzb/ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789.nzb</link>
    <pubDate>Tue, 23 Jun 2026 09:30:00 +0000</pubDate>
    <category>TV &gt; HD</category>
    <description>A.Tv.Show.S06E05.1080p.WEB-DL "special" &amp; &lt;friends&gt;</description>
    <enclosure url="http://indexer.example.invalid/getnzb/ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789.nzb" length="4294967295" type="application/x-nzb"/>
    <newznab:attr name="category" value="5000"/>
    <newznab:attr name="category" value="5040"/>
    <newznab:attr name="size" value="4294967295"/>
    <newznab:attr name="files" value="42"/>
    <newznab:attr name="poster" value="yenc@power-post"/>
    <newznab:attr name="group" value="alt.binaries.teevee"/>
    <newznab:attr name="grabs" value="7"/>
    <newznab:attr name="comments" value="0"/>
    <newznab:attr name="password" value="0"/>
    <newznab:attr name="usenetdate" value="Mon, 22 Jun 2026 06:54:22 +0000"/>
    <newznab:attr name="completion" value="100"/>
    <newznab:attr name="parts" value="12345"/>
    <newznab:attr name="catalogid" value="nzb:ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"/>
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

void tst_IndexerParse::search_classifiesThePasswordAttr()
{
    // The attribute is specified as a flag — 0/1/2 — and "-1" turns up in the
    // wild. But indexers also put the real passphrase in it, so the value has to
    // be classified rather than just compared against "0".
    //
    // The rule: anything that parses as an integer is a flag and never a
    // password ("1" is nobody's passphrase, and using it as one turns a release
    // that would have asked the user into one that fails with "wrong password").
    // Everything else is a candidate, with a length floor for the "n/a" and "?"
    // placeholders.
    const auto rowFor = [](const QString& value) {
        const QString xml =
            QStringLiteral(R"(<?xml version="1.0"?>
<rss xmlns:newznab="http://www.newznab.com/DTD/2010/feeds/attributes/">
 <channel><item>
  <title>Release.Name</title>
  <guid>g1</guid>
  <enclosure url="https://x/1.nzb" length="10"/>
  <newznab:attr name="password" value="%1"/>
 </item></channel></rss>)").arg(value);
        const IndexerSearchPage page = parseIndexerSearch(
            xml.toUtf8(), QStringLiteral("Test"), QStringLiteral("test"));
        return page.results.value(0);
    };

    // Flags: never a password, whatever they say about protection.
    QVERIFY(!rowFor(QStringLiteral("0")).passwordProtected);
    QVERIFY(rowFor(QStringLiteral("0")).password.isEmpty());

    for (const QString& flag : {QStringLiteral("1"), QStringLiteral("2"),
                               QStringLiteral("-1")}) {
        const IndexerResult row = rowFor(flag);
        QVERIFY2(row.passwordProtected, qPrintable(flag));
        QVERIFY2(row.password.isEmpty(), qPrintable(flag));
    }

    // Too short to be anything but a placeholder: protected, but nothing usable.
    for (const QString& junk : {QStringLiteral("ab"), QStringLiteral("?"),
                               QStringLiteral("n/a")}) {
        const IndexerResult row = rowFor(junk);
        QVERIFY2(row.passwordProtected, qPrintable(junk));
        QVERIFY2(row.password.isEmpty(), qPrintable(junk));
    }

    // A real one.
    const IndexerResult real = rowFor(QStringLiteral("s3cretpw"));
    QVERIFY(real.passwordProtected);
    QCOMPARE(real.password, QStringLiteral("s3cretpw"));
}

void tst_IndexerParse::search_takesABracedPasswordOutOfTheTitle()
{
    // Some indexers carry the passphrase in the title with NZBGet's marker
    // rather than in an attribute. Taken *out* of the title as well as read, or
    // the release is queued under a name with its own password showing.
    const QByteArray xml = R"(<?xml version="1.0"?>
<rss xmlns:newznab="http://www.newznab.com/DTD/2010/feeds/attributes/">
 <channel><item>
  <title>Some.Release.2160p{{letmein}}</title>
  <guid>g1</guid>
  <enclosure url="https://x/1.nzb" length="10"/>
 </item></channel></rss>)";

    const IndexerSearchPage page =
        parseIndexerSearch(xml, QStringLiteral("Test"), QStringLiteral("test"));
    QCOMPARE(page.results.size(), 1);
    QCOMPARE(page.results.at(0).title, QStringLiteral("Some.Release.2160p"));
    QCOMPARE(page.results.at(0).password, QStringLiteral("letmein"));
    QVERIFY(page.results.at(0).passwordProtected);
}

// ---------------------------------------------------------------------------
// usenet-crawler — the documents the sibling crawler emits
// ---------------------------------------------------------------------------

void tst_IndexerParse::usenetCrawler_capsIsFullyReadable()
{
    QString error;
    const IndexerCaps caps = parseIndexerCaps(kUsenetCrawlerCaps, error);

    QVERIFY2(error.isEmpty(), qPrintable(error));

    QCOMPARE(caps.serverTitle, QStringLiteral("usenet-crawler"));
    QCOMPARE(caps.limitMax, 100);
    QCOMPARE(caps.limitDefault, 100);

    // The modes are keyed by the element name, which is also what we send as t=.
    QVERIFY(caps.supportsMode(kModeSearch));
    QVERIFY(caps.supportsMode(kModeTvSearch));
    QVERIFY(caps.supportsMode(kModeMovieSearch));
    QVERIFY(!caps.supportsMode(kModeAudioSearch));
    QVERIFY(!caps.supportsMode(kModeBookSearch));

    // And the parameters our query builder gates on are advertised.
    QVERIFY(caps.supportsParam(kModeTvSearch, u"season"));
    QVERIFY(caps.supportsParam(kModeTvSearch, u"ep"));
    QVERIFY(caps.supportsParam(kModeSearch, u"cat"));
    QVERIFY(caps.supportsParam(kModeSearch, u"maxage"));
    // Not advertised because that crawler does not filter on it, so we must not
    // send it and believe the result was narrowed.
    QVERIFY(!caps.supportsParam(kModeTvSearch, u"imdbid"));

    // The tree resolves both levels, which is what a results list needs to show
    // a heading without a second round trip.
    QCOMPARE(caps.categoryName(5000), QStringLiteral("TV"));
    QCOMPARE(caps.categoryName(5040), QStringLiteral("HD"));
}

void tst_IndexerParse::usenetCrawler_searchIsFullyReadable()
{
    const IndexerSearchPage page = parseIndexerSearch(
        kUsenetCrawlerSearch, QStringLiteral("usenet-crawler"),
        QStringLiteral("usenet_crawler"));

    QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
    QCOMPARE(page.total, 1);
    QCOMPARE(page.offset, 0);
    QCOMPARE(page.results.size(), 1);

    const IndexerResult& row = page.results.at(0);

    // The title round-trips through XML escaping with its punctuation intact.
    QCOMPARE(row.title,
             QStringLiteral("A.Tv.Show.S06E05.1080p.WEB-DL \"special\" & <friends>"));

    // The size attribute and the enclosure length agree, so the pick between
    // them cannot go wrong.
    QCOMPARE(row.size, 4294967295LL);

    QCOMPARE(row.files, 42);
    QCOMPARE(row.grabs, 7);
    QCOMPARE(row.poster, QStringLiteral("yenc@power-post"));
    QCOMPARE(row.group, QStringLiteral("alt.binaries.teevee"));

    // Parent then leaf, both present.
    QCOMPARE(row.categoryIds.size(), 2);
    QCOMPARE(row.categoryIds.at(0), 5000);
    QCOMPARE(row.categoryIds.at(1), 5040);

    // password="0" is a flag and never a passphrase.
    QVERIFY(!row.passwordProtected);
    QVERIFY(row.password.isEmpty());

    // usenetdate wins over pubDate, and both parse: the day names are generated
    // from the timestamps rather than separately, so the strict RFC 2822 reader
    // accepts them without the fallback.
    QVERIFY(row.published.isValid());
    QCOMPARE(row.published.toUTC(),
             QDateTime(QDate(2026, 6, 22), QTime(6, 54, 22), QTimeZone::UTC));

    // The download URL is the enclosure's, and it names the release by its own
    // digest — so a row we cached stays valid across a re-crawl of that catalogue.
    QVERIFY(row.downloadUrl.isValid());
    QVERIFY(row.downloadUrl.path().endsWith(QStringLiteral(".nzb")));
    QCOMPARE(row.guid,
             QStringLiteral("ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"));
}

QTEST_MAIN(tst_IndexerParse)
#include "tst_IndexerParse.moc"
