/// @file tst_IPFilterMatch.cpp
/// @brief Tests for ipfilter/IPFilter — IP matching, level checks, hits, signals.

#include "TestHelpers.h"
#include "ipfilter/IPFilter.h"

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
    void isFiltered_hitsCounter();
    void lastHitDescription_afterMatch();
    void removeFilter_basic();
    void removeAllFilters_clears();
    void signal_filterLoaded();
    void signal_ipBlocked();
    void multipleRanges_correctMatch();
};

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
    QCOMPARE(spy.at(0).at(0).value<uint32>(), ip);
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

QTEST_GUILESS_MAIN(tst_IPFilterMatch)
#include "tst_IPFilterMatch.moc"
