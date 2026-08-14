/// @file tst_SearchParams.cpp
/// @brief Tests for search/SearchParams — construction, serialization, enum values.

#include "TestHelpers.h"
#include "search/SearchParams.h"
#include "utils/SafeFile.h"

#include <QTest>

using namespace eMule;

class tst_SearchParams : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void storeAndLoad_roundTrip();
    void searchType_values();
    void fields_initialValues();

    // Automatic method resolution
    void automatic_kadOnly_picksKad();
    void automatic_serverOnly_picksServer();
    void automatic_neitherNetwork_picksNothing();
    void automatic_bothNetworks_prefersKad();
    void automatic_staticServer_picksServer();
    void automatic_bigServerAndShortList_picksServer();
    void automatic_bigServerButLongList_picksKad();
    void automatic_serverThresholds_areExact();
};

void tst_SearchParams::construct_default()
{
    SearchParams p;
    QCOMPARE(p.searchID, UINT32_MAX);
    QCOMPARE(p.type, SearchType::Ed2kServer);
    QVERIFY(!p.clientSharedFiles);
    QVERIFY(!p.matchKeywords);
    QCOMPARE(p.minSize, uint64{0});
    QCOMPARE(p.maxSize, uint64{0});
    QCOMPARE(p.availability, uint32{0});
    QCOMPARE(p.completeSources, uint32{0});
    QVERIFY(p.expression.isEmpty());
    QVERIFY(p.fileType.isEmpty());
}

void tst_SearchParams::storeAndLoad_roundTrip()
{
    SearchParams original;
    original.searchID = 42;
    original.type = SearchType::Kademlia;
    original.clientSharedFiles = true;
    original.specialTitle = QStringLiteral("My Search");
    original.expression = QStringLiteral("test file AND audio");
    original.fileType = QStringLiteral("Audio");

    // Write to memory
    SafeMemFile mem;
    original.storePartially(mem);

    // Read back
    mem.seek(0, 0);
    SearchParams loaded(mem);

    QCOMPARE(loaded.searchID, uint32{42});
    QCOMPARE(loaded.type, SearchType::Kademlia);
    QVERIFY(loaded.clientSharedFiles);
    QCOMPARE(loaded.specialTitle, QStringLiteral("My Search"));
    QCOMPARE(loaded.expression, QStringLiteral("test file AND audio"));
    QCOMPARE(loaded.fileType, QStringLiteral("Audio"));
}

void tst_SearchParams::searchType_values()
{
    QCOMPARE(static_cast<uint8>(SearchType::Automatic), uint8{0});
    QCOMPARE(static_cast<uint8>(SearchType::Ed2kServer), uint8{1});
    QCOMPARE(static_cast<uint8>(SearchType::Ed2kGlobal), uint8{2});
    QCOMPARE(static_cast<uint8>(SearchType::Kademlia), uint8{3});
    QCOMPARE(static_cast<uint8>(SearchType::ContentDB), uint8{4});
}

void tst_SearchParams::fields_initialValues()
{
    SearchParams p;
    QVERIFY(p.searchTitle.isEmpty());
    QVERIFY(p.keyword.isEmpty());
    QVERIFY(p.booleanExpr.isEmpty());
    QVERIFY(p.extension.isEmpty());
    QVERIFY(p.minSizeStr.isEmpty());
    QVERIFY(p.maxSizeStr.isEmpty());
    QVERIFY(p.codec.isEmpty());
    QVERIFY(p.title.isEmpty());
    QVERIFY(p.album.isEmpty());
    QVERIFY(p.artist.isEmpty());
    QCOMPARE(p.minBitrate, uint32{0});
    QCOMPARE(p.minLength, uint32{0});
}

// ---------------------------------------------------------------------------
// Automatic method resolution — MFC CSearchResultsWnd::StartNewSearch
// (srchybrid/SearchResultsWnd.cpp:1134-1165)
// ---------------------------------------------------------------------------

namespace {

/// Both networks up, connected to a server that on its own would *not* tip the
/// choice away from Kad. Cases below move one field at a time from here.
AutoSearchState bothNetworks()
{
    AutoSearchState s;
    s.serverConnected = true;
    s.kadConnected = true;
    s.serverIsStatic = false;
    s.serverUsers = 1000;
    s.serverFiles = 100000;
    s.serverCount = 100;
    return s;
}

} // namespace

void tst_SearchParams::automatic_kadOnly_picksKad()
{
    AutoSearchState s;
    s.kadConnected = true;
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Kademlia);
}

void tst_SearchParams::automatic_serverOnly_picksServer()
{
    AutoSearchState s;
    s.serverConnected = true;
    // A server good enough to be preferred when both are up must not change the
    // answer here — there is no Kad to choose instead.
    s.serverIsStatic = true;
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Ed2kServer);
}

void tst_SearchParams::automatic_neitherNetwork_picksNothing()
{
    QVERIFY(!resolveAutomaticSearchType(AutoSearchState{}).has_value());
}

void tst_SearchParams::automatic_bothNetworks_prefersKad()
{
    QCOMPARE(resolveAutomaticSearchType(bothNetworks()), SearchType::Kademlia);
}

void tst_SearchParams::automatic_staticServer_picksServer()
{
    // A static server is one the user chose deliberately, so it wins outright —
    // no user/file/list-size test applies.
    AutoSearchState s = bothNetworks();
    s.serverIsStatic = true;
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Ed2kServer);
}

void tst_SearchParams::automatic_bigServerAndShortList_picksServer()
{
    AutoSearchState s = bothNetworks();
    s.serverUsers = 50000;
    s.serverFiles = 6000000;
    s.serverCount = 39;
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Ed2kServer);
}

void tst_SearchParams::automatic_bigServerButLongList_picksKad()
{
    // Same server, but a list long enough that eMule assumes it is polluted with
    // fakes — so the server's own numbers stop being evidence of anything.
    AutoSearchState s = bothNetworks();
    s.serverUsers = 50000;
    s.serverFiles = 6000000;
    s.serverCount = 40;
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Kademlia);
}

void tst_SearchParams::automatic_serverThresholds_areExact()
{
    // Each bound checked from both sides. These constants are copied from MFC
    // verbatim — including the 2,000,000 upper user bound that eMule's own comment
    // calls a copy & paste bug — so a later "cleanup" has to fail a test first.
    AutoSearchState s = bothNetworks();
    s.serverUsers = 50000;
    s.serverFiles = 6000000;
    s.serverCount = 30;
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Ed2kServer);

    s.serverUsers = 40000;                                  // must be > 40000
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Kademlia);
    s.serverUsers = 40001;
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Ed2kServer);

    s.serverUsers = 2000000;                                // must be < 2000000
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Kademlia);
    s.serverUsers = 1999999;
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Ed2kServer);

    s.serverFiles = 5000000;                                // must be > 5000000
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Kademlia);
    s.serverFiles = 5000001;
    QCOMPARE(resolveAutomaticSearchType(s), SearchType::Ed2kServer);
}

QTEST_MAIN(tst_SearchParams)
#include "tst_SearchParams.moc"
