/// @file tst_KadNodeCensus.cpp
/// @brief Distinct Kad nodes by country: counting, persistence, reset and restore.

#include "kademlia/KadNodeCensus.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;
using namespace eMule::kad;

namespace {

using Scope = KadNodeCensus::Scope;

UInt128 node(uint32 n)
{
    UInt128 id(n);
    id.setBitNumber(3, 1);   // not all in the low word
    return id;
}

void contact(KadNodeCensus& census, uint32 from, uint32 to, const QString& cc)
{
    for (uint32 i = from; i < to; ++i)
        census.noteContacted(node(i), cc);
}

uint64 countOf(const KadNodeCensus& census, Scope scope, const QString& cc)
{
    for (const auto& country : census.countries(scope)) {
        if (country.cc == cc)
            return country.count;
    }
    return 0;
}

} // namespace

class tst_KadNodeCensus : public QObject {
    Q_OBJECT

private slots:
    void countsDistinctNodesPerCountry();
    void unresolvedAddressesAreTheUnknownCountry();
    void aNodeThatMovedCountsOnceInTheTotal();
    void listedNodesAreASeparateCount();
    void cumulativeSurvivesARestart();
    void savingTwiceChangesNothing();
    void aLostOrBrokenFileStartsClean();
    void resetBacksUpAndRestoreSwaps();
    void restoreWithoutABackupFails();
    void readsTheFileLayoutOfVersion1();
};

void tst_KadNodeCensus::countsDistinctNodesPerCountry()
{
    KadNodeCensus census;
    contact(census, 0, 30, QStringLiteral("DE"));
    contact(census, 0, 30, QStringLiteral("DE"));    // the same nodes again
    contact(census, 100, 110, QStringLiteral("IT"));
    contact(census, 200, 203, QStringLiteral("FR"));

    const auto countries = census.countries(Scope::Session);
    QCOMPARE(countries.size(), 3);
    QCOMPARE(countries.at(0).cc, QStringLiteral("DE"));   // most nodes first
    QCOMPARE(countries.at(0).count, uint64{30});
    QCOMPARE(countries.at(1).cc, QStringLiteral("IT"));
    QCOMPARE(countries.at(1).count, uint64{10});
    QCOMPARE(countries.at(2).count, uint64{3});
    QCOMPARE(census.contacted(Scope::Session), uint64{43});
    QCOMPARE(census.contacted(Scope::Cumulative), uint64{43});
}

void tst_KadNodeCensus::unresolvedAddressesAreTheUnknownCountry()
{
    KadNodeCensus census;
    // No GeoIP database in a unit test: every address resolves to nothing.
    census.noteContacted(node(1), Address::fromHostOrder(0x58010203));
    census.noteContacted(node(2), QStringLiteral("not a code"));
    census.noteContacted(node(3), QStringLiteral("de"));   // codes are upper-case

    const auto countries = census.countries(Scope::Session);
    QCOMPARE(countries.size(), 1);
    QVERIFY(countries.at(0).cc.isEmpty());
    QCOMPARE(countries.at(0).count, uint64{3});
}

void tst_KadNodeCensus::aNodeThatMovedCountsOnceInTheTotal()
{
    KadNodeCensus census;
    contact(census, 0, 20, QStringLiteral("ES"));
    contact(census, 10, 20, QStringLiteral("PT"));   // ten of them seen again elsewhere

    QCOMPARE(countOf(census, Scope::Session, QStringLiteral("ES")), uint64{20});
    QCOMPARE(countOf(census, Scope::Session, QStringLiteral("PT")), uint64{10});
    QCOMPARE(census.contacted(Scope::Session), uint64{20});
}

void tst_KadNodeCensus::listedNodesAreASeparateCount()
{
    KadNodeCensus census;
    for (uint32 i = 0; i < 50; ++i)
        census.noteListed(node(i));
    census.noteContacted(node(1), QStringLiteral("DE"));

    QCOMPARE(census.listed(Scope::Session), uint64{50});
    QCOMPARE(census.contacted(Scope::Session), uint64{1});
    QCOMPARE(census.countries(Scope::Session).size(), 1);
}

void tst_KadNodeCensus::cumulativeSurvivesARestart()
{
    QTemporaryDir dir;
    {
        KadNodeCensus census(dir.path());
        contact(census, 0, 1000, QStringLiteral("DE"));    // dense
        contact(census, 2000, 2040, QStringLiteral("IT")); // sparse
        census.noteListed(node(9));
        QVERIFY(census.save());
    }

    KadNodeCensus next(dir.path());
    QCOMPARE(next.contacted(Scope::Session), uint64{0});
    QVERIFY(next.countries(Scope::Session).isEmpty());
    QCOMPARE(countOf(next, Scope::Cumulative, QStringLiteral("IT")), uint64{40});
    const uint64 de = countOf(next, Scope::Cumulative, QStringLiteral("DE"));
    QVERIFY2(de > 950 && de < 1050, qPrintable(QString::number(de)));
    QCOMPARE(next.listed(Scope::Cumulative), uint64{1});

    // The salt came back with the file: the same nodes are recognised, new ones add.
    contact(next, 2000, 2040, QStringLiteral("IT"));
    QCOMPARE(countOf(next, Scope::Cumulative, QStringLiteral("IT")), uint64{40});
    contact(next, 2040, 2050, QStringLiteral("IT"));
    QCOMPARE(countOf(next, Scope::Cumulative, QStringLiteral("IT")), uint64{50});
    QCOMPARE(countOf(next, Scope::Session, QStringLiteral("IT")), uint64{50});
}

void tst_KadNodeCensus::savingTwiceChangesNothing()
{
    QTemporaryDir dir;
    KadNodeCensus census(dir.path());
    contact(census, 0, 500, QStringLiteral("DE"));
    QVERIFY(census.save());

    QFile file(census.filePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray first = file.readAll();
    file.close();

    census.noteContacted(node(7), QStringLiteral("DE"));   // seen before: dirty, same content
    QVERIFY(census.save());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), first);
}

void tst_KadNodeCensus::aLostOrBrokenFileStartsClean()
{
    QTemporaryDir dir;
    QString path;
    {
        KadNodeCensus census(dir.path());
        contact(census, 0, 10, QStringLiteral("DE"));
        QVERIFY(census.save());
        path = census.filePath();
    }

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("not a census");
    file.close();

    KadNodeCensus broken(dir.path());
    QCOMPARE(broken.contacted(Scope::Cumulative), uint64{0});
    contact(broken, 0, 4, QStringLiteral("FR"));
    QVERIFY(broken.save());

    KadNodeCensus after(dir.path());
    QCOMPARE(after.contacted(Scope::Cumulative), uint64{4});
}

// The statistics menu's Reset and Restore, as Preferences does them for the counters.
void tst_KadNodeCensus::resetBacksUpAndRestoreSwaps()
{
    QTemporaryDir dir;
    {
        KadNodeCensus earlier(dir.path());
        contact(earlier, 0, 100, QStringLiteral("DE"));
        QVERIFY(earlier.save());
    }

    KadNodeCensus census(dir.path());
    contact(census, 500, 520, QStringLiteral("IT"));   // this session
    QCOMPARE(census.contacted(Scope::Cumulative), uint64{120});

    census.reset();
    QVERIFY(QFile::exists(census.backupPath()));
    // The banked half is gone; the session keeps counting, as the counters do.
    QCOMPARE(census.contacted(Scope::Cumulative), uint64{20});
    QCOMPARE(census.contacted(Scope::Session), uint64{20});

    QVERIFY(census.restore());
    QCOMPARE(census.contacted(Scope::Cumulative), uint64{120});
    QCOMPARE(countOf(census, Scope::Cumulative, QStringLiteral("DE")), uint64{100});

    // Restoring again undoes the restore: the backup now holds the post-reset totals.
    QVERIFY(census.restore());
    QCOMPARE(census.contacted(Scope::Cumulative), uint64{20});
    QVERIFY(census.restore());
    QCOMPARE(census.contacted(Scope::Cumulative), uint64{120});

    // And what was restored is what a restart finds.
    KadNodeCensus next(dir.path());
    QCOMPARE(next.contacted(Scope::Cumulative), uint64{120});
}

void tst_KadNodeCensus::restoreWithoutABackupFails()
{
    QTemporaryDir dir;
    KadNodeCensus census(dir.path());
    contact(census, 0, 5, QStringLiteral("DE"));
    QVERIFY(!census.restore());
    QCOMPARE(census.contacted(Scope::Cumulative), uint64{5});

    KadNodeCensus memoryOnly;
    QVERIFY(!memoryOnly.restore());
    QVERIFY(memoryOnly.save());   // nothing to write is not a failure
}

// kadcensus.dat as documented in docs/kad-statistics.md, written here by hand:
// a census file has to survive changes to the classes that read it.
void tst_KadNodeCensus::readsTheFileLayoutOfVersion1()
{
    QTemporaryDir dir;
    {
        QFile file(dir.filePath(QStringLiteral("kadcensus.dat")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream out(&file);
        out.setByteOrder(QDataStream::LittleEndian);
        out << quint32{0x4B4E4331} << quint8{1} << quint8{12}        // "KNC1", version, precision
            << quint64{0x1111} << quint64{0x2222};                   // salt
        out << quint8{0} << quint16{2} << quint64{5} << quint64{9};  // listed: exact, two hashes
        out << quint16{2};                                           // countries
        out << quint16{0} << quint8{0} << quint16{1} << quint64{77};                    // unknown
        out << quint16{('D' << 8) | 'E'} << quint8{0} << quint16{3}
            << quint64{1} << quint64{2} << quint64{3};
    }

    KadNodeCensus census(dir.path());
    QCOMPARE(census.listed(Scope::Cumulative), uint64{2});
    QCOMPARE(census.contacted(Scope::Cumulative), uint64{4});
    QCOMPARE(countOf(census, Scope::Cumulative, QStringLiteral("DE")), uint64{3});
    QCOMPARE(countOf(census, Scope::Cumulative, QString()), uint64{1});
    QCOMPARE(census.contacted(Scope::Session), uint64{0});
}

QTEST_APPLESS_MAIN(tst_KadNodeCensus)
#include "tst_KadNodeCensus.moc"
