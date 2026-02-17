/// @file tst_StatisticFile.cpp
/// @brief Tests for files/StatisticFile — counters, merge.

#include "TestHelpers.h"
#include "files/StatisticFile.h"

#include <QTest>

using namespace eMule;

class tst_StatisticFile : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void addRequest();
    void addAccepted();
    void addTransferred();
    void setAllTime_values();
    void mergeFileStats();
};

void tst_StatisticFile::construct_default()
{
    StatisticFile s;
    QCOMPARE(s.requests(), uint32{0});
    QCOMPARE(s.accepts(), uint32{0});
    QCOMPARE(s.transferred(), uint64{0});
    QCOMPARE(s.allTimeRequests(), uint32{0});
    QCOMPARE(s.allTimeAccepts(), uint32{0});
    QCOMPARE(s.allTimeTransferred(), uint64{0});
}

void tst_StatisticFile::addRequest()
{
    StatisticFile s;
    s.addRequest();
    s.addRequest();
    QCOMPARE(s.requests(), uint32{2});
    QCOMPARE(s.allTimeRequests(), uint32{2});
}

void tst_StatisticFile::addAccepted()
{
    StatisticFile s;
    s.addAccepted();
    QCOMPARE(s.accepts(), uint32{1});
    QCOMPARE(s.allTimeAccepts(), uint32{1});
}

void tst_StatisticFile::addTransferred()
{
    StatisticFile s;
    s.addTransferred(1000);
    s.addTransferred(500);
    QCOMPARE(s.transferred(), uint64{1500});
    QCOMPARE(s.allTimeTransferred(), uint64{1500});
}

void tst_StatisticFile::setAllTime_values()
{
    StatisticFile s;
    s.setAllTimeRequests(100);
    s.setAllTimeAccepts(50);
    s.setAllTimeTransferred(999999);
    QCOMPARE(s.allTimeRequests(), uint32{100});
    QCOMPARE(s.allTimeAccepts(), uint32{50});
    QCOMPARE(s.allTimeTransferred(), uint64{999999});
    // Session counters should be unaffected
    QCOMPARE(s.requests(), uint32{0});
    QCOMPARE(s.accepts(), uint32{0});
    QCOMPARE(s.transferred(), uint64{0});
}

void tst_StatisticFile::mergeFileStats()
{
    StatisticFile a;
    a.addRequest();
    a.addRequest();
    a.addAccepted();
    a.addTransferred(1000);
    a.setAllTimeRequests(10);
    a.setAllTimeAccepts(5);
    a.setAllTimeTransferred(50000);

    StatisticFile b;
    b.addRequest();
    b.addAccepted();
    b.addAccepted();
    b.addTransferred(2000);
    b.setAllTimeRequests(20);
    b.setAllTimeAccepts(15);
    b.setAllTimeTransferred(80000);

    a.mergeFileStats(b);

    QCOMPARE(a.requests(), uint32{3});
    QCOMPARE(a.accepts(), uint32{3});
    QCOMPARE(a.transferred(), uint64{3000});
    QCOMPARE(a.allTimeRequests(), uint32{30});
    QCOMPARE(a.allTimeAccepts(), uint32{20});
    QCOMPARE(a.allTimeTransferred(), uint64{130000});
}

QTEST_MAIN(tst_StatisticFile)
#include "tst_StatisticFile.moc"
