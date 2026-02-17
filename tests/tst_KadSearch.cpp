/// @file tst_KadSearch.cpp
/// @brief Tests for KadSearch.h — Kademlia search state machine.

#include "TestHelpers.h"

#include "kademlia/KadLookupHistory.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUInt128.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;

class tst_KadSearch : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void construct_default();
    void setSearchType_basic();
    void addFileID_tracked();
    void getTypeName_allTypes();
    void updateNodeLoad_accumulates();
    void stopping_flag();

private:
    void cleanupSearchManager();
};

void tst_KadSearch::cleanupSearchManager()
{
    SearchManager::stopAllSearches();
}

void tst_KadSearch::cleanup()
{
    cleanupSearchManager();
}

void tst_KadSearch::construct_default()
{
    UInt128 target(uint32{42});
    auto* search = SearchManager::prepareLookup(SearchType::Node, false, target);
    QVERIFY(search != nullptr);
    QVERIFY(search->getSearchID() > 0);
    QCOMPARE(search->getSearchType(), SearchType::Node);
    QCOMPARE(search->getAnswers(), uint32{0});
    QCOMPARE(search->getKadPacketSent(), uint32{0});
    QVERIFY(!search->stopping());
    QVERIFY(search->getLookupHistory() != nullptr);
    delete search;
}

void tst_KadSearch::setSearchType_basic()
{
    UInt128 target(uint32{100});
    auto* search = SearchManager::prepareLookup(SearchType::Node, false, target);
    QVERIFY(search != nullptr);

    search->setSearchType(SearchType::Keyword);
    QCOMPARE(search->getSearchType(), SearchType::Keyword);

    search->setSearchType(SearchType::StoreFile);
    QCOMPARE(search->getSearchType(), SearchType::StoreFile);
    delete search;
}

void tst_KadSearch::addFileID_tracked()
{
    UInt128 target(uint32{200});
    auto* search = SearchManager::prepareLookup(SearchType::File, false, target);
    QVERIFY(search != nullptr);

    UInt128 fileID1(uint32{1});
    UInt128 fileID2(uint32{2});
    search->addFileID(fileID1);
    search->addFileID(fileID2);

    // No public accessor for file IDs count, but we verify it doesn't crash
    delete search;
}

void tst_KadSearch::getTypeName_allTypes()
{
    QCOMPARE(Search::getTypeName(SearchType::Node), QStringLiteral("Node"));
    QCOMPARE(Search::getTypeName(SearchType::NodeComplete), QStringLiteral("NodeComplete"));
    QCOMPARE(Search::getTypeName(SearchType::File), QStringLiteral("File"));
    QCOMPARE(Search::getTypeName(SearchType::Keyword), QStringLiteral("Keyword"));
    QCOMPARE(Search::getTypeName(SearchType::Notes), QStringLiteral("Notes"));
    QCOMPARE(Search::getTypeName(SearchType::StoreFile), QStringLiteral("StoreFile"));
    QCOMPARE(Search::getTypeName(SearchType::StoreKeyword), QStringLiteral("StoreKeyword"));
    QCOMPARE(Search::getTypeName(SearchType::StoreNotes), QStringLiteral("StoreNotes"));
    QCOMPARE(Search::getTypeName(SearchType::FindBuddy), QStringLiteral("FindBuddy"));
    QCOMPARE(Search::getTypeName(SearchType::FindSource), QStringLiteral("FindSource"));
    QCOMPARE(Search::getTypeName(SearchType::NodeSpecial), QStringLiteral("NodeSpecial"));
    QCOMPARE(Search::getTypeName(SearchType::NodeFwCheckUDP), QStringLiteral("NodeFwCheckUDP"));
}

void tst_KadSearch::updateNodeLoad_accumulates()
{
    UInt128 target(uint32{300});
    auto* search = SearchManager::prepareLookup(SearchType::Keyword, false, target);
    QVERIFY(search != nullptr);

    QCOMPARE(search->getNodeLoad(), uint32{0});

    search->updateNodeLoad(50);
    search->updateNodeLoad(70);

    // Average load: (50 + 70) / 2 = 60
    QCOMPARE(search->getNodeLoad(), uint32{60});
    QCOMPARE(search->getNodeLoadResponse(), uint32{2});
    QCOMPARE(search->getNodeLoadTotal(), uint32{120});
    delete search;
}

void tst_KadSearch::stopping_flag()
{
    UInt128 target(uint32{400});
    // Use prepareLookup without start — the search won't call go() or prepareToStop()
    auto* search = SearchManager::prepareLookup(SearchType::Node, false, target);
    QVERIFY(search != nullptr);
    QVERIFY(!search->stopping());

    // Start and then stop through SearchManager
    bool started = SearchManager::startSearch(search);
    QVERIFY(started);

    uint32 searchID = search->getSearchID();
    SearchManager::stopSearch(searchID, false);
    // After stopSearch, the search is deleted — we just verify no crash
}

QTEST_GUILESS_MAIN(tst_KadSearch)
#include "tst_KadSearch.moc"
