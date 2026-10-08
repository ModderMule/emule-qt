/// @file tst_ClientCensus.cpp
/// @brief Distinct eD2K clients by user hash and country.
///
/// Counting, persistence, reset and restore are CountryCensus and covered by
/// tst_KadNodeCensus; this is what the client census adds.

#include "client/ClientCensus.h"
#include "kademlia/KadNodeCensus.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <array>

using namespace eMule;

namespace {

using Scope = CountryCensus::Scope;
using Hash = std::array<uint8, 16>;

Hash hashOf(uint32 n)
{
    Hash hash{};
    hash[0] = static_cast<uint8>(n);
    hash[1] = static_cast<uint8>(n >> 8);
    hash[15] = 0x6F;   // both halves in use
    return hash;
}

uint64 countOf(const ClientCensus& census, Scope scope, const QString& cc)
{
    for (const auto& country : census.countries(scope)) {
        if (country.cc == cc)
            return country.count;
    }
    return 0;
}

} // namespace

class tst_ClientCensus : public QObject {
    Q_OBJECT

private slots:
    void countsDistinctHashesPerCountry();
    void theNullHashIsNotAClient();
    void identifiedIsASeparateCount();
    void hashesDifferingInOneHalfAreDifferentClients();
    void survivesARestartInItsOwnFile();
    void doesNotReadTheKadCensus();
};

void tst_ClientCensus::countsDistinctHashesPerCountry()
{
    ClientCensus census;
    for (int round = 0; round < 2; ++round) {    // every client connects twice
        for (uint32 i = 0; i < 30; ++i)
            census.noteSeen(hashOf(i).data(), QStringLiteral("ES"));
    }
    for (uint32 i = 20; i < 35; ++i)             // ten of them from another country too
        census.noteSeen(hashOf(i).data(), QStringLiteral("FR"));

    QCOMPARE(countOf(census, Scope::Session, QStringLiteral("ES")), uint64{30});
    QCOMPARE(countOf(census, Scope::Session, QStringLiteral("FR")), uint64{15});
    QCOMPARE(census.seen(Scope::Session), uint64{35});
    QCOMPARE(census.countries(Scope::Session).at(0).cc, QStringLiteral("ES"));

    // No GeoIP database in a unit test: an address is the unknown country.
    census.noteSeen(hashOf(99).data(), Address::fromHostOrder(0x58010203));
    QCOMPARE(countOf(census, Scope::Session, QString()), uint64{1});
}

void tst_ClientCensus::theNullHashIsNotAClient()
{
    ClientCensus census;
    const Hash null{};
    census.noteSeen(null.data(), QStringLiteral("DE"));
    census.noteSeen(nullptr, QStringLiteral("DE"));
    census.noteIdentified(null.data());

    QCOMPARE(census.seen(Scope::Session), uint64{0});
    QCOMPARE(census.identified(Scope::Session), uint64{0});
    QVERIFY(census.countries(Scope::Session).isEmpty());
}

void tst_ClientCensus::identifiedIsASeparateCount()
{
    ClientCensus census;
    for (uint32 i = 0; i < 10; ++i)
        census.noteSeen(hashOf(i).data(), QStringLiteral("IT"));
    for (uint32 i = 0; i < 7; ++i) {
        census.noteIdentified(hashOf(i).data());
        census.noteIdentified(hashOf(i).data());   // again from a new address
    }

    QCOMPARE(census.seen(Scope::Session), uint64{10});
    QCOMPARE(census.identified(Scope::Session), uint64{7});
    QCOMPARE(census.identified(Scope::Cumulative), uint64{7});
}

void tst_ClientCensus::hashesDifferingInOneHalfAreDifferentClients()
{
    ClientCensus census;
    Hash a = hashOf(1);
    Hash b = a;
    b[12] ^= 0x01;   // upper half only
    Hash c = a;
    c[3] ^= 0x01;    // lower half only
    for (const Hash& hash : {a, b, c})
        census.noteSeen(hash.data(), QStringLiteral("DE"));
    QCOMPARE(census.seen(Scope::Session), uint64{3});
}

void tst_ClientCensus::survivesARestartInItsOwnFile()
{
    QTemporaryDir dir;
    {
        ClientCensus census(dir.path());
        for (uint32 i = 0; i < 40; ++i)
            census.noteSeen(hashOf(i).data(), QStringLiteral("PL"));
        census.noteIdentified(hashOf(3).data());
        QVERIFY(census.save());
        QVERIFY(census.filePath().endsWith(QStringLiteral("/clientcensus.dat")));
        QVERIFY(census.backupPath().endsWith(QStringLiteral("/clientcensus.bak")));
    }

    ClientCensus next(dir.path());
    QCOMPARE(next.seen(Scope::Session), uint64{0});
    QCOMPARE(next.seen(Scope::Cumulative), uint64{40});
    QCOMPARE(next.identified(Scope::Cumulative), uint64{1});
    next.noteSeen(hashOf(5).data(), QStringLiteral("PL"));   // known: the salt came back
    QCOMPARE(countOf(next, Scope::Cumulative, QStringLiteral("PL")), uint64{40});
}

// Both live in the same directory; neither may take the other's file for its own.
void tst_ClientCensus::doesNotReadTheKadCensus()
{
    QTemporaryDir dir;
    {
        kad::KadNodeCensus kadCensus(dir.path());
        kadCensus.noteContacted(kad::UInt128(uint32{7}), QStringLiteral("DE"));
        QVERIFY(kadCensus.save());
        QVERIFY(QFile::copy(kadCensus.filePath(), dir.filePath(QStringLiteral("clientcensus.dat"))));
    }

    ClientCensus census(dir.path());
    QCOMPARE(census.seen(Scope::Cumulative), uint64{0});

    kad::KadNodeCensus kadAgain(dir.path());
    QCOMPARE(kadAgain.contacted(Scope::Cumulative), uint64{1});
}

QTEST_APPLESS_MAIN(tst_ClientCensus)
#include "tst_ClientCensus.moc"
