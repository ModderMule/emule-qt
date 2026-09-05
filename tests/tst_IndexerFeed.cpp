/// @file tst_IndexerFeed.cpp
/// @brief The newznab reader against whole recorded feeds, not hand-written ones.
///
/// tst_IndexerParse covers the traps with minimal fixtures. This covers the
/// thing those fixtures are a model of: a real indexer's answer, saved verbatim,
/// with its CDATA descriptions, its channel <image> block, its duplicate
/// reposts, its multi-gigabyte releases and its URLs that are not quite URLs.
///
/// Gated on EMULE_NZB_DIR, the same directory the live NZB download test reads
/// releases from -- feeds are saved next to the releases they came from, and
/// that tree is gitignored. Unset, every case skips: an unconfigured checkout
/// must not look broken.

#include "IndexerResult.h"
#include "UsenetLiveEnv.h"

#include <QDate>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QTime>
#include <QTimeZone>

using namespace eMule::indexer;
using namespace eMule::testing;
using namespace eMule::testing::usenet;

namespace {

/// Both are arbitrary -- the parser only stamps them onto every row -- but they
/// have to be asserted somewhere, because `id` is what a grab is addressed by.
const QString kIndexerName = QStringLiteral("recorded");
const QString kSlug = QStringLiteral("rec");

QByteArray readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("cannot read %s: %s", qPrintable(path), qPrintable(f.errorString()));
        return {};
    }
    return f.readAll();
}

} // namespace

class tst_IndexerFeed : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // -- true of any newznab feed ---------------------------------------------
    void parsesEveryItem_data();
    void parsesEveryItem();
    void keepsSizesWiderThan32Bits_data();
    void keepsSizesWiderThan32Bits();
    void keepsTheDownloadUrlVerbatim_data();
    void keepsTheDownloadUrlVerbatim();

    // -- the recorded usenet-crawler feed specifically -------------------------
    void readsTheUsenetCrawlerFeed();

private:
    static IndexerSearchPage parseFile(const QString& path, QByteArray& raw);
};

void tst_IndexerFeed::initTestCase()
{
    loadProjectEnv();

    if (nzbDir().isEmpty())
        QSKIP("Set EMULE_NZB_DIR to a directory holding recorded indexer feeds");

    QVERIFY2(QDir(nzbDir()).exists(),
             qPrintable(QStringLiteral("EMULE_NZB_DIR does not exist: %1").arg(nzbDir())));
}

void tst_IndexerFeed::parsesEveryItem_data()
{
    QTest::addColumn<QString>("path");
    if (addFeedRows() == 0)
        QSKIP("No .xml feeds in EMULE_NZB_DIR");
}

/// Nothing may be dropped silently. parseIndexerSearch() skips any row with no
/// title or no download URL, and a reader that walks the document wrongly loses
/// rows the same quiet way -- so the count is the assertion that matters.
void tst_IndexerFeed::parsesEveryItem()
{
    QFETCH(QString, path);

    QByteArray raw;
    const IndexerSearchPage page = parseFile(path, raw);

    QVERIFY2(page.error.isEmpty(), qPrintable(page.error));

    const qsizetype items = raw.count("<item>");
    QCOMPARE(raw.count("</item>"), items);   // no <item> hiding in a CDATA description
    QVERIFY(items > 0);
    QCOMPARE(page.results.size(), items);

    QSet<QString> ids;
    for (const IndexerResult& row : page.results) {
        QVERIFY(!row.title.isEmpty());
        QVERIFY(!row.guid.isEmpty());
        QVERIFY(!row.downloadUrl.isEmpty());
        QVERIFY2(row.downloadUrl.isValid(), qPrintable(row.downloadUrl.errorString()));
        QCOMPARE(row.indexerName, kIndexerName);
        QCOMPARE(row.id, kSlug + u'/' + row.guid);
        QVERIFY2(row.size > 0, qPrintable(row.title));
        QVERIFY2(row.published.isValid(), qPrintable(row.title));
        QVERIFY(row.ageDays() != -1);
        ids.insert(row.id);
    }

    // A grab is addressed by id, so two rows sharing one would fetch the same
    // release twice and never the other. dedupKey() is allowed to collide;
    // this is not.
    QCOMPARE(ids.size(), page.results.size());
}

void tst_IndexerFeed::keepsSizesWiderThan32Bits_data()
{
    QTest::addColumn<QString>("path");
    if (addFeedRows() == 0)
        QSKIP("No .xml feeds in EMULE_NZB_DIR");
}

/// Releases run past 4 GB routinely, and a size read into an int comes back
/// negative or wrapped rather than wrong-looking. Cross-checked against the
/// document instead of the parser, so a truncating read cannot agree with itself.
void tst_IndexerFeed::keepsSizesWiderThan32Bits()
{
    QFETCH(QString, path);

    QByteArray raw;
    const IndexerSearchPage page = parseFile(path, raw);
    QVERIFY(!page.results.isEmpty());

    // Custom delimiter: the pattern itself ends in )" .
    static const QRegularExpression re(QStringLiteral(R"RX(name="size"\s+value="(\d+)")RX"));
    qint64 documentMax = 0;
    auto it = re.globalMatch(QString::fromUtf8(raw));
    while (it.hasNext())
        documentMax = std::max(documentMax, it.next().captured(1).toLongLong());
    QVERIFY2(documentMax > 0, "the feed carries no size attributes to check");

    qint64 parsedMax = 0;
    for (const IndexerResult& row : page.results)
        parsedMax = std::max(parsedMax, row.size);

    QCOMPARE(parsedMax, documentMax);
}

void tst_IndexerFeed::keepsTheDownloadUrlVerbatim_data()
{
    QTest::addColumn<QString>("path");
    if (addFeedRows() == 0)
        QSKIP("No .xml feeds in EMULE_NZB_DIR");
}

/// usenet-crawler's getnzb links are malformed -- "…/HASH.nzb&i=482415&r=…",
/// with the query glued on and no '?' anywhere. They still fetch, but only if
/// nothing normalises them on the way through QUrl, so the parsed string has to
/// still be findable in the document it came from.
void tst_IndexerFeed::keepsTheDownloadUrlVerbatim()
{
    QFETCH(QString, path);

    QByteArray raw;
    const IndexerSearchPage page = parseFile(path, raw);
    QVERIFY(!page.results.isEmpty());

    const QString doc = QString::fromUtf8(raw);
    for (const IndexerResult& row : page.results) {
        QString escaped = row.downloadUrl.toString(QUrl::FullyEncoded);
        escaped.replace(u'&', QLatin1String("&amp;"));
        QVERIFY2(doc.contains(escaped),
                 qPrintable(QStringLiteral("URL was rewritten: %1").arg(escaped)));
    }
}

/// The exact values of the recorded usenet-crawler browse feed. Skips on a
/// machine whose EMULE_NZB_DIR holds some other feed.
void tst_IndexerFeed::readsTheUsenetCrawlerFeed()
{
    const QString path = QDir(nzbDir()).filePath(QStringLiteral("rss.xml"));
    if (!QFileInfo::exists(path))
        QSKIP("No rss.xml in EMULE_NZB_DIR");

    QByteArray raw;
    const IndexerSearchPage page = parseFile(path, raw);

    QVERIFY(page.error.isEmpty());
    QCOMPARE(page.results.size(), 100);

    // A browse feed carries no <newznab:response>, so there is no total to page
    // against and IndexerSearch has to keep asking until a page comes back short.
    QCOMPARE(page.total, -1);
    QCOMPARE(page.offset, 0);

    const IndexerResult& first = page.results.first();
    QCOMPARE(first.title,
             QStringLiteral("Shin Megami Tensei-Devil Survivor 2 Record Breaker EU ENG 3DS"));
    QCOMPARE(first.guid,
             QStringLiteral("https://www.usenet-crawler.com/details/"
                            "2475fb104b5567ebe8d5aaa90a095543"));
    QCOMPARE(first.downloadUrl.toString(),
             QStringLiteral("https://www.usenet-crawler.com/getnzb/"
                            "2475fb104b5567ebe8d5aaa90a095543.nzb"
                            "&i=482415&r=80bbe2015c1aa4177335743c18d55d3e"));
    QCOMPARE(first.size, Q_INT64_C(3239497497));
    QCOMPARE(first.files, 9);
    QCOMPARE(first.grabs, 2);
    QCOMPARE(first.group, QStringLiteral("alt.binaries.friends"));
    QCOMPARE(first.poster, QStringLiteral("WHY <omicron@highwinds.why>"));
    QCOMPARE(first.categoryIds, QList<int>({1000, 1010}));
    QVERIFY(first.isUsenet());

    // usenetdate wins over pubDate: the item was posted at 15:56 and indexed at
    // 16:01, and it is the posting that the Age column is about.
    QCOMPARE(first.published.toUTC(),
             QDateTime(QDate(2026, 8, 17), QTime(13, 56, 2), QTimeZone::UTC));

    // Largest release in the feed, well past 2^31.
    qint64 largest = 0;
    for (const IndexerResult& row : page.results)
        largest = std::max(largest, row.size);
    QCOMPARE(largest, Q_INT64_C(13652864194));

    // password="-1" is not "0", so it reads as protected. Three items use it.
    // Erring toward the warning costs a colour; erring the other way costs a
    // download that cannot be unpacked.
    int protectedRows = 0;
    for (const IndexerResult& row : page.results)
        protectedRows += row.passwordProtected ? 1 : 0;
    QCOMPARE(protectedRows, 3);

    // Reposts: 100 distinct guids, but only 82 distinct title+size pairs. This
    // is dedupKey() working as documented, not a bug -- assert the number so a
    // change to the heuristic has to be deliberate.
    QSet<QString> keys;
    for (const IndexerResult& row : page.results)
        keys.insert(row.dedupKey());
    QCOMPARE(keys.size(), 82);
}

IndexerSearchPage tst_IndexerFeed::parseFile(const QString& path, QByteArray& raw)
{
    raw = readAll(path);
    return parseIndexerSearch(raw, kIndexerName, kSlug);
}

QTEST_MAIN(tst_IndexerFeed)
#include "tst_IndexerFeed.moc"
