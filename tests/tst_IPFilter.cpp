/// @file tst_IPFilter.cpp
/// @brief Tests for ipfilter/IPFilter — loading, parsing, sort/merge, save/load roundtrip.

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

class tst_IPFilter : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void addIPRange_basic();
    void loadFromFile_filterDat();
    void loadFromFile_peerGuardian();
    void loadFromFile_nonexistent();
    void loadFromFile_commentsIgnored();
    void sortAndMerge_overlapping();
    void sortAndMerge_adjacent_sameLevel();
    void sortAndMerge_adjacent_differentLevel();
    void sortAndMerge_duplicate_keepsLowestLevel();
    void saveAndLoad_roundTrip();
};

void tst_IPFilter::construct_default()
{
    IPFilter filter;
    QCOMPARE(filter.entryCount(), 0);
    QVERIFY(filter.isEmpty());
    QVERIFY(!filter.isModified());
    QCOMPARE(filter.lastHitDescription(), QStringLiteral("Not available"));
}

void tst_IPFilter::addIPRange_basic()
{
    IPFilter filter;
    filter.addIPRange(0x0A000000, 0x0A0000FF, 100, "test range");
    QCOMPARE(filter.entryCount(), 1);
    QVERIFY(filter.isModified());

    filter.addIPRange(0x0B000000, 0x0B0000FF, 50, "another range");
    QCOMPARE(filter.entryCount(), 2);
}

void tst_IPFilter::loadFromFile_filterDat()
{
    IPFilter filter;
    QSignalSpy spy(&filter, &IPFilter::filterLoaded);

    const QString path = eMule::testing::testDataDir()
                         + QStringLiteral("/ipfilter_sample.dat");
    const int count = filter.loadFromFile(path);

    QCOMPARE(count, 3);
    QCOMPARE(filter.entryCount(), 3);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 3);

    // Verify entries are sorted by start IP
    const auto& entries = filter.entries();
    QVERIFY(entries[0].start < entries[1].start);
    QVERIFY(entries[1].start < entries[2].start);

    // Check the level=50 entry
    bool foundLevel50 = false;
    for (const auto& e : entries) {
        if (e.level == 50) {
            foundLevel50 = true;
            QCOMPARE(e.desc, std::string("Test range 2 low level"));
        }
    }
    QVERIFY(foundLevel50);
}

void tst_IPFilter::loadFromFile_peerGuardian()
{
    IPFilter filter;
    const QString path = eMule::testing::testDataDir()
                         + QStringLiteral("/ipfilter_peerguardian.txt");
    const int count = filter.loadFromFile(path);

    QCOMPARE(count, 2);

    // PeerGuardian lines all get level=100
    for (const auto& e : filter.entries()) {
        QCOMPARE(e.level, static_cast<uint32>(100));
    }

    // Check first entry description
    const auto& first = filter.entries().front();
    QCOMPARE(first.desc, std::string("Bogon range"));
}

void tst_IPFilter::loadFromFile_nonexistent()
{
    IPFilter filter;
    const int count = filter.loadFromFile(QStringLiteral("/nonexistent/ipfilter.dat"));
    QCOMPARE(count, 0);
    QCOMPARE(filter.entryCount(), 0);
}

void tst_IPFilter::loadFromFile_commentsIgnored()
{
    eMule::testing::TempDir tmpDir;
    const QString path = tmpDir.filePath(QStringLiteral("comments.dat"));

    // Write a file with comments and one valid line
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("# This is a comment\n");
    f.write("// Another comment\n");
    f.write("# short\n");
    f.write("001.002.003.000 - 001.002.003.255 , 100 , Valid entry\n");
    f.close();

    IPFilter filter;
    const int count = filter.loadFromFile(path);
    QCOMPARE(count, 1);
}

void tst_IPFilter::sortAndMerge_overlapping()
{
    IPFilter filter;
    // Two overlapping ranges: [10-20] and [15-30]
    filter.addIPRange(10, 20, 100, "range A");
    filter.addIPRange(15, 30, 100, "range B");
    filter.sortAndMerge();

    QCOMPARE(filter.entryCount(), 1);
    QCOMPARE(filter.entries()[0].start, static_cast<uint32>(10));
    QCOMPARE(filter.entries()[0].end, static_cast<uint32>(30));
}

void tst_IPFilter::sortAndMerge_adjacent_sameLevel()
{
    IPFilter filter;
    // Adjacent ranges with same level: [10-20] and [21-30]
    filter.addIPRange(10, 20, 100, "range A");
    filter.addIPRange(21, 30, 100, "range B");
    filter.sortAndMerge();

    QCOMPARE(filter.entryCount(), 1);
    QCOMPARE(filter.entries()[0].start, static_cast<uint32>(10));
    QCOMPARE(filter.entries()[0].end, static_cast<uint32>(30));
}

void tst_IPFilter::sortAndMerge_adjacent_differentLevel()
{
    IPFilter filter;
    // Adjacent ranges with different levels: should NOT merge
    filter.addIPRange(10, 20, 100, "range A");
    filter.addIPRange(21, 30, 50, "range B");
    filter.sortAndMerge();

    QCOMPARE(filter.entryCount(), 2);
}

void tst_IPFilter::sortAndMerge_duplicate_keepsLowestLevel()
{
    IPFilter filter;
    // Identical ranges with different levels
    filter.addIPRange(10, 20, 100, "range A");
    filter.addIPRange(10, 20, 50, "range B");
    filter.sortAndMerge();

    QCOMPARE(filter.entryCount(), 1);
    QCOMPARE(filter.entries()[0].level, static_cast<uint32>(50));
}

void tst_IPFilter::saveAndLoad_roundTrip()
{
    eMule::testing::TempDir tmpDir;
    const QString path = tmpDir.filePath(QStringLiteral("roundtrip.dat"));

    // Create and save
    {
        IPFilter filter;
        filter.addIPRange(0x01020300, 0x010203FF, 100, "Range 1");
        filter.addIPRange(0x0A000000, 0x0A0000FF, 50, "Range 2");
        filter.sortAndMerge();
        QVERIFY(filter.saveToFile(path));
    }

    // Reload and verify
    {
        IPFilter filter;
        const int count = filter.loadFromFile(path);
        QCOMPARE(count, 2);

        const auto& entries = filter.entries();
        // First entry: 1.2.3.0 - 1.2.3.255
        QCOMPARE(entries[0].start, static_cast<uint32>(0x01020300));
        QCOMPARE(entries[0].end, static_cast<uint32>(0x010203FF));
        QCOMPARE(entries[0].level, static_cast<uint32>(100));

        // Second entry: 10.0.0.0 - 10.0.0.255
        QCOMPARE(entries[1].start, static_cast<uint32>(0x0A000000));
        QCOMPARE(entries[1].end, static_cast<uint32>(0x0A0000FF));
        QCOMPARE(entries[1].level, static_cast<uint32>(50));
    }
}

QTEST_GUILESS_MAIN(tst_IPFilter)
#include "tst_IPFilter.moc"
