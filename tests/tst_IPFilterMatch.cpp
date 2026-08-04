/// @file tst_IPFilterMatch.cpp
/// @brief Tests for ipfilter/IPFilter — IP matching, level checks, hits, signals.

#include "TestHelpers.h"
#include "ipfilter/IPFilter.h"
#include "prefs/Preferences.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

using namespace eMule;

/// Helper: convert a dotted-decimal IP to network byte order uint32.
static uint32 makeIP(uint8 a, uint8 b, uint8 c, uint8 d)
{
    return htonl((static_cast<uint32>(a) << 24)
               | (static_cast<uint32>(b) << 16)
               | (static_cast<uint32>(c) << 8)
               | static_cast<uint32>(d));
}

class tst_IPFilterMatch : public QObject {
    Q_OBJECT

private slots:
    void isFiltered_emptyFilter();
    void isFiltered_zeroIP();
    void isFiltered_matchInRange();
    void isFiltered_startBoundary();
    void isFiltered_endBoundary();
    void isFiltered_outsideRange();
    void isFiltered_levelCheck();
    void defaultLevel_comesFromPreferences();
    void isFiltered_hitsCounter();
    void lastHitDescription_afterMatch();
    void removeFilter_basic();
    void removeAllFilters_clears();
    void signal_filterLoaded();
    void signal_ipBlocked();
    void multipleRanges_correctMatch();

    // IPv6 range table
    void v6_isFiltered_matchAndBoundaries();
    void v6_isFiltered_familiesAreIsolated();
    void v6_parse_cidrPrefix();
    void v6_publicCidrList_blocksWithDefaultPrefs();
    void v6_parse_explicitRangeAndDescription();
    void v6_parse_bareLiteralPrefersLongestMatch();
    void v6_sortAndMerge_overlapWithDifferentLevels();
    void v6_saveLoad_roundTrip();
    void v6_signal_ipBlocked();
};

/// RAII: pin thePrefs.ipFilterLevel() for one test and put the old value back, so a test
/// that exercises the level-less isFiltered() overloads cannot leak into its neighbours.
class FilterLevelGuard {
public:
    explicit FilterLevelGuard(uint32 level)
        : m_saved(thePrefs.ipFilterLevel()) { thePrefs.setIpFilterLevel(level); }
    ~FilterLevelGuard() { thePrefs.setIpFilterLevel(m_saved); }

    FilterLevelGuard(const FilterLevelGuard&) = delete;
    FilterLevelGuard& operator=(const FilterLevelGuard&) = delete;

private:
    uint32 m_saved;
};

/// Helper: an IPv6 Address from a literal.
static Address v6(const char* literal)
{
    return Address::fromString(QString::fromLatin1(literal));
}

/// Helper: the 16 network-order bytes of an IPv6 literal.
static std::array<uint8, 16> v6Bytes(const char* literal)
{
    return v6(literal).ipv6Bytes();
}

/// Helper: write a filter list to @p dir and return its path.
static QString writeListFile(const eMule::testing::TempDir& dir, const QString& name,
                             const QString& content)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    file.write(content.toLatin1());
    file.close();
    return path;
}

void tst_IPFilterMatch::isFiltered_emptyFilter()
{
    IPFilter filter;
    QVERIFY(!filter.isFiltered(makeIP(10, 0, 0, 1)));
}

void tst_IPFilterMatch::isFiltered_zeroIP()
{
    IPFilter filter;
    filter.addIPRange(0, 0xFFFFFFFF, 50, "block all");
    filter.sortAndMerge();
    // IP=0 is always not filtered (early return)
    QVERIFY(!filter.isFiltered(0));
}

void tst_IPFilterMatch::isFiltered_matchInRange()
{
    IPFilter filter;
    // Range: 10.0.0.0 - 10.0.0.255 at level 50
    filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "test block");
    filter.sortAndMerge();

    // IP 10.0.0.100 should be filtered (level 50 < default 100)
    QVERIFY(filter.isFiltered(makeIP(10, 0, 0, 100)));
}

void tst_IPFilterMatch::isFiltered_startBoundary()
{
    IPFilter filter;
    filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "boundary test");
    filter.sortAndMerge();

    // Exact start: 10.0.0.0
    QVERIFY(filter.isFiltered(makeIP(10, 0, 0, 0)));
}

void tst_IPFilterMatch::isFiltered_endBoundary()
{
    IPFilter filter;
    filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "boundary test");
    filter.sortAndMerge();

    // Exact end: 10.0.0.255
    QVERIFY(filter.isFiltered(makeIP(10, 0, 0, 255)));
}

void tst_IPFilterMatch::isFiltered_outsideRange()
{
    IPFilter filter;
    filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "range test");
    filter.sortAndMerge();

    // Just before range: 9.255.255.255
    QVERIFY(!filter.isFiltered(makeIP(9, 255, 255, 255)));
    // Just after range: 10.0.1.0
    QVERIFY(!filter.isFiltered(makeIP(10, 0, 1, 0)));
}

void tst_IPFilterMatch::isFiltered_levelCheck()
{
    IPFilter filter;
    // Range at level 80
    filter.addIPRange(0x0A000000, 0x0A0000FF, 80, "level test");
    filter.sortAndMerge();

    const auto ip = makeIP(10, 0, 0, 1);

    // filterLevel=100: range level 80 < 100, should be filtered
    QVERIFY(filter.isFiltered(ip, 100));

    // filterLevel=80: range level 80 is NOT < 80, should NOT be filtered
    QVERIFY(!filter.isFiltered(ip, 80));

    // filterLevel=50: range level 80 is NOT < 50, should NOT be filtered
    QVERIFY(!filter.isFiltered(ip, 50));
}

void tst_IPFilterMatch::defaultLevel_comesFromPreferences()
{
    // Regression: the level-less overloads used to pass kDefaultFilterLevel as the
    // threshold. That is also the level every level-less list line is parsed at, so the
    // comparison read `100 < 100` and a list of bare CIDR / PeerGuardian lines loaded,
    // reported its count, and then blocked nothing at all.
    IPFilter filter;
    filter.addIPRange(0x0A000000, 0x0A0000FF, kDefaultFilterLevel, "level-less v4 entry");
    filter.addIPRange6(v6Bytes("2a01:4f8::"), v6Bytes("2a01:4f8::ffff"),
                       kDefaultFilterLevel, "level-less v6 entry");
    filter.sortAndMerge();

    const auto ip = makeIP(10, 0, 0, 1);

    // The shipped default (127) is above the entry default, so both families block.
    QCOMPARE(thePrefs.ipFilterLevel(), 127u);
    QVERIFY(filter.isFiltered(ip));
    QVERIFY(filter.isFiltered(v6("2a01:4f8::1")));

    // And the overloads really do read the preference, rather than a baked-in constant.
    {
        const FilterLevelGuard guard(kDefaultFilterLevel);
        QVERIFY(!filter.isFiltered(ip));
        QVERIFY(!filter.isFiltered(v6("2a01:4f8::1")));
    }

    QVERIFY(filter.isFiltered(ip));
}

void tst_IPFilterMatch::isFiltered_hitsCounter()
{
    IPFilter filter;
    filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "hits test");
    filter.sortAndMerge();

    const auto ip = makeIP(10, 0, 0, 1);
    QVERIFY(filter.isFiltered(ip));
    QVERIFY(filter.isFiltered(ip));
    QVERIFY(filter.isFiltered(ip));

    QCOMPARE(filter.entries()[0].hits, static_cast<uint32>(3));
}

void tst_IPFilterMatch::lastHitDescription_afterMatch()
{
    IPFilter filter;
    filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "my description");
    filter.sortAndMerge();

    QCOMPARE(filter.lastHitDescription(), QStringLiteral("Not available"));

    QVERIFY(filter.isFiltered(makeIP(10, 0, 0, 1)));
    QCOMPARE(filter.lastHitDescription(), QStringLiteral("my description"));
}

void tst_IPFilterMatch::removeFilter_basic()
{
    IPFilter filter;
    filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "range 1");
    filter.addIPRange(0x0B000000, 0x0B0000FF, 50, "range 2");
    filter.sortAndMerge();
    QCOMPARE(filter.entryCount(), 2);

    QVERIFY(filter.removeFilter(0));
    QCOMPARE(filter.entryCount(), 1);
    QCOMPARE(filter.entries()[0].desc, std::string("range 2"));

    // Invalid index
    QVERIFY(!filter.removeFilter(5));
    QVERIFY(!filter.removeFilter(-1));
}

void tst_IPFilterMatch::removeAllFilters_clears()
{
    IPFilter filter;
    filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "range 1");
    filter.addIPRange(0x0B000000, 0x0B0000FF, 50, "range 2");
    filter.sortAndMerge();

    // Trigger a hit so m_lastHit is set
    QVERIFY(filter.isFiltered(makeIP(10, 0, 0, 1)));
    QVERIFY(filter.lastHitDescription() != QStringLiteral("Not available"));

    filter.removeAllFilters();
    QCOMPARE(filter.entryCount(), 0);
    QVERIFY(filter.isEmpty());
    QCOMPARE(filter.lastHitDescription(), QStringLiteral("Not available"));
}

void tst_IPFilterMatch::signal_filterLoaded()
{
    IPFilter filter;
    QSignalSpy spy(&filter, &IPFilter::filterLoaded);

    const QString path = eMule::testing::testDataDir()
                         + QStringLiteral("/ipfilter_sample.dat");
    filter.loadFromFile(path);

    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toInt() > 0);
}

void tst_IPFilterMatch::signal_ipBlocked()
{
    IPFilter filter;
    filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "blocked range");
    filter.sortAndMerge();

    QSignalSpy spy(&filter, &IPFilter::ipBlocked);
    const auto ip = makeIP(10, 0, 0, 42);
    QVERIFY(filter.isFiltered(ip));

    QCOMPARE(spy.count(), 1);
    // Address-typed since the filter gained IPv6 ranges — a uint32 cannot carry a v6 hit.
    QCOMPARE(spy.at(0).at(0).value<Address>(), Address::fromNetworkOrder(ip));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("blocked range"));
}

void tst_IPFilterMatch::multipleRanges_correctMatch()
{
    IPFilter filter;
    // Three non-overlapping ranges
    filter.addIPRange(0x01000000, 0x010000FF, 50, "range A");  // 1.0.0.x
    filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "range B");  // 10.0.0.x
    filter.addIPRange(0xC0A80100, 0xC0A801FF, 50, "range C");  // 192.168.1.x
    filter.sortAndMerge();

    // Match in range A
    QVERIFY(filter.isFiltered(makeIP(1, 0, 0, 50)));
    QCOMPARE(filter.lastHitDescription(), QStringLiteral("range A"));

    // Match in range B
    QVERIFY(filter.isFiltered(makeIP(10, 0, 0, 200)));
    QCOMPARE(filter.lastHitDescription(), QStringLiteral("range B"));

    // Match in range C
    QVERIFY(filter.isFiltered(makeIP(192, 168, 1, 1)));
    QCOMPARE(filter.lastHitDescription(), QStringLiteral("range C"));

    // No match between ranges
    QVERIFY(!filter.isFiltered(makeIP(5, 0, 0, 1)));
    QVERIFY(!filter.isFiltered(makeIP(192, 168, 2, 1)));
}

// ---------------------------------------------------------------------------
// IPv6 range table
// ---------------------------------------------------------------------------

void tst_IPFilterMatch::v6_isFiltered_matchAndBoundaries()
{
    IPFilter filter;
    filter.addIPRange6(v6Bytes("2a01:4f8::"),
                       v6Bytes("2a01:4f8::ffff"), 50, "block v6");
    filter.sortAndMerge();

    QVERIFY(filter.isFiltered(v6("2a01:4f8::")));          // start boundary
    QVERIFY(filter.isFiltered(v6("2a01:4f8::ffff")));      // end boundary
    QVERIFY(filter.isFiltered(v6("2a01:4f8::abcd")));      // inside

    // One below the start and one above the end — proves the 128-bit comparison is
    // numeric across a byte boundary, not a truncated prefix test.
    QVERIFY(!filter.isFiltered(v6("2a01:4f7:ffff:ffff:ffff:ffff:ffff:ffff")));
    QVERIFY(!filter.isFiltered(v6("2a01:4f8::1:0")));

    QCOMPARE(filter.lastHitDescription(), QStringLiteral("block v6"));
}

void tst_IPFilterMatch::v6_isFiltered_familiesAreIsolated()
{
    // The two tables must never see each other's entries: an IPv4 range must not block a
    // v6 address that happens to share leading bytes, and vice versa.
    IPFilter filter;
    filter.addIPRange(0x00000000, 0xFFFFFFFF, 50, "all v4");
    filter.addIPRange6(v6Bytes("2a01:4f8::"), v6Bytes("2a01:4f8::ffff"), 50, "some v6");
    filter.sortAndMerge();

    QCOMPARE(filter.entryCountV4(), 1);
    QCOMPARE(filter.entryCountV6(), 1);
    QCOMPARE(filter.entryCount(), 2);

    QVERIFY(filter.isFiltered(makeIP(8, 8, 8, 8)));        // caught by the v4 range
    QVERIFY(!filter.isFiltered(v6("2001:4860:4860::8888"))); // not caught by "all v4"
    QVERIFY(filter.isFiltered(v6("2a01:4f8::5")));

    IPFilter v6Only;
    v6Only.addIPRange6(v6Bytes("::"), v6Bytes("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
                       50, "all v6");
    v6Only.sortAndMerge();
    QVERIFY(!v6Only.isFiltered(makeIP(8, 8, 8, 8)));
}

void tst_IPFilterMatch::v6_parse_cidrPrefix()
{
    // Real-world v6 lists ship prefixes, not start-end pairs.
    const eMule::testing::TempDir dir;
    const QString path = writeListFile(dir, QStringLiteral("v6cidr.dat"),
        QStringLiteral("2a01:4f8::/32 , 50 , hoster\n"));

    IPFilter filter;
    QCOMPARE(filter.loadFromFile(path), 1);
    QCOMPARE(filter.entryCountV6(), 1);

    // /32 covers the whole 2a01:4f8:: block and nothing outside it.
    QVERIFY(filter.isFiltered(v6("2a01:4f8::1")));
    QVERIFY(filter.isFiltered(
        v6("2a01:4f8:ffff:ffff:ffff:ffff:ffff:ffff")));
    QVERIFY(!filter.isFiltered(v6("2a01:4f9::1")));
    QVERIFY(!filter.isFiltered(v6("2a01:4f7:ffff:ffff:ffff:ffff:ffff:ffff")));
    QCOMPARE(filter.lastHitDescription(), QStringLiteral("hoster"));
}

void tst_IPFilterMatch::v6_publicCidrList_blocksWithDefaultPrefs()
{
    // End-to-end shape of a public IPv6 blocklist: a comment header and bare prefixes,
    // no level column and no description — nothing in the eMule/PeerGuardian ecosystem
    // publishes v6, so these lists are what a user actually imports. Loading one must
    // block through the level-less overload, on stock preferences.
    const eMule::testing::TempDir dir;
    const QString path = writeListFile(dir, QStringLiteral("v6public.dat"),
        QStringLiteral("# fullbogons-ipv6, generated\n"
                       "#\n"
                       "2001:db8::/32\n"
                       "2a01:4f8::/32\n"
                       "3fff::/20\n"));

    IPFilter filter;
    QCOMPARE(filter.loadFromFile(path), 3);
    QCOMPARE(filter.entryCountV6(), 3);
    QCOMPARE(filter.entryCountV4(), 0);

    QVERIFY(filter.isFiltered(v6("2001:db8::1")));
    QVERIFY(filter.isFiltered(v6("2a01:4f8:dead:beef::1")));
    QVERIFY(filter.isFiltered(v6("3fff:0:0:1::1")));

    QVERIFY(!filter.isFiltered(v6("2606:4700:4700::1111")));
}

void tst_IPFilterMatch::v6_parse_explicitRangeAndDescription()
{
    // Explicit bounds, and the PeerGuardian "description:range" shape whose description
    // may itself contain colons.
    const eMule::testing::TempDir dir;
    const QString path = writeListFile(dir, QStringLiteral("v6range.p2p"),
        QStringLiteral("2a01:4f8:: - 2a01:4f8::ff , 20 , explicit\n"
                       "Some Corp:2a02:26f0::/32\n"
                       "Corp:Ltd:2a03:2880::/32\n"));

    IPFilter filter;
    QCOMPARE(filter.loadFromFile(path), 3);

    QVERIFY(filter.isFiltered(v6("2a01:4f8::80")));
    QCOMPARE(filter.lastHitDescription(), QStringLiteral("explicit"));
    QVERIFY(!filter.isFiltered(v6("2a01:4f8::100")));

    // The two description-prefixed lines carry no level column, so they land on
    // kDefaultFilterLevel — and isFiltered requires level < filterLevel, so whether they
    // block is decided by the configured level. Pass it explicitly here; the level-less
    // overload's use of the preference is covered by defaultLevel_comesFromPreferences.
    QVERIFY(filter.isFiltered(v6("2a02:26f0::1"), kDefaultFilterLevel + 1));
    QVERIFY(!filter.isFiltered(v6("2a02:26f0::1"), kDefaultFilterLevel));
    QCOMPARE(filter.lastHitDescription(), QStringLiteral("Some Corp"));

    QVERIFY(filter.isFiltered(v6("2a03:2880::1"), kDefaultFilterLevel + 1));
    QCOMPARE(filter.lastHitDescription(), QStringLiteral("Corp:Ltd"));
}

void tst_IPFilterMatch::v6_parse_bareLiteralPrefersLongestMatch()
{
    // A bare "2a01:4f8::/32" must not be split at its first colon: the suffix
    // "4f8::/32" is *also* a valid address, and taking it would filter the wrong block.
    const eMule::testing::TempDir dir;
    const QString path = writeListFile(dir, QStringLiteral("v6bare.dat"),
        QStringLiteral("2a01:4f8::/32\n"));

    IPFilter filter;
    QCOMPARE(filter.loadFromFile(path), 1);
    QCOMPARE(filter.entries6().size(), std::size_t{1});
    QCOMPARE(filter.entries6()[0].start, v6Bytes("2a01:4f8::"));
    QCOMPARE(filter.entries6()[0].end,
             v6Bytes("2a01:4f8:ffff:ffff:ffff:ffff:ffff:ffff"));
    // No level on the line, so it sits at kDefaultFilterLevel — query above it.
    QVERIFY(filter.isFiltered(v6("2a01:4f8::1"), kDefaultFilterLevel + 1));
    QVERIFY(!filter.isFiltered(v6("4f8::1"), kDefaultFilterLevel + 1));
}

void tst_IPFilterMatch::v6_sortAndMerge_overlapWithDifferentLevels()
{
    // The lookup inspects only the entry with the largest start <= the address, so an
    // entry nested inside another must be split out rather than left hidden.
    IPFilter filter;
    filter.addIPRange6(v6Bytes("2a01::"), v6Bytes("2a01::ff"), 100, "outer");
    filter.addIPRange6(v6Bytes("2a01::10"), v6Bytes("2a01::1f"), 20, "inner");
    filter.sortAndMerge();

    // Below the inner range: only the outer, permissive level applies.
    QVERIFY(filter.isFiltered(v6("2a01::5"), 150));
    QVERIFY(!filter.isFiltered(v6("2a01::5"), 50));

    // Inside the inner range: the stricter level wins.
    QVERIFY(filter.isFiltered(v6("2a01::18"), 50));

    // Above the inner range — this is the address a naive merge loses.
    QVERIFY(filter.isFiltered(v6("2a01::80"), 150));
    QVERIFY(!filter.isFiltered(v6("2a01::100"), 150));
}

void tst_IPFilterMatch::v6_saveLoad_roundTrip()
{
    IPFilter filter;
    filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "v4 range");
    filter.addIPRange6(v6Bytes("2a01:4f8::"), v6Bytes("2a01:4f8::ffff"), 60, "v6 range");
    filter.sortAndMerge();

    const eMule::testing::TempDir dir;
    const QString path = dir.filePath(QStringLiteral("v6roundtrip.dat"));
    QVERIFY(filter.saveToFile(path));

    IPFilter reloaded;
    QCOMPARE(reloaded.loadFromFile(path), 2);
    QCOMPARE(reloaded.entryCountV4(), 1);
    QCOMPARE(reloaded.entryCountV6(), 1);
    QCOMPARE(reloaded.entries6()[0].start, v6Bytes("2a01:4f8::"));
    QCOMPARE(reloaded.entries6()[0].end, v6Bytes("2a01:4f8::ffff"));
    QCOMPARE(reloaded.entries6()[0].level, 60u);
    QVERIFY(reloaded.isFiltered(makeIP(10, 0, 0, 1)));
    QVERIFY(reloaded.isFiltered(v6("2a01:4f8::1")));
}

void tst_IPFilterMatch::v6_signal_ipBlocked()
{
    IPFilter filter;
    filter.addIPRange6(v6Bytes("2a01:4f8::"), v6Bytes("2a01:4f8::ffff"), 50, "blocked v6");
    filter.sortAndMerge();

    QSignalSpy spy(&filter, &IPFilter::ipBlocked);
    QVERIFY(filter.isFiltered(v6("2a01:4f8::7")));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<Address>(), v6("2a01:4f8::7"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("blocked v6"));
}

QTEST_GUILESS_MAIN(tst_IPFilterMatch)
#include "tst_IPFilterMatch.moc"
