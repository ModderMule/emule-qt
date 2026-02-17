/// @file tst_KadSearchManager.cpp
/// @brief Tests for KadSearchManager.h — search lifecycle management.

#include "TestHelpers.h"

#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUInt128.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;

class tst_KadSearchManager : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void prepareLookup_createsSearch();
    void startSearch_addsToMap();
    void isSearching_afterStart();
    void stopSearch_removesFromMap();
    void alreadySearchingFor_duplicate();
    void prepareFindKeywords_splits();
};

void tst_KadSearchManager::cleanup()
{
    SearchManager::stopAllSearches();
}

void tst_KadSearchManager::prepareLookup_createsSearch()
{
    UInt128 target(uint32{100});
    auto* search = SearchManager::prepareLookup(SearchType::Node, false, target);
    QVERIFY(search != nullptr);
    QCOMPARE(search->getSearchType(), SearchType::Node);
    QCOMPARE(search->getTarget(), target);
    delete search;
}

void tst_KadSearchManager::startSearch_addsToMap()
{
    UInt128 target(uint32{200});
    auto* search = SearchManager::prepareLookup(SearchType::Keyword, false, target);
    QVERIFY(search != nullptr);

    bool started = SearchManager::startSearch(search);
    QVERIFY(started);

    // Verify it's in the search map
    QVERIFY(SearchManager::alreadySearchingFor(target));
}

void tst_KadSearchManager::isSearching_afterStart()
{
    UInt128 target(uint32{300});
    auto* search = SearchManager::prepareLookup(SearchType::File, true, target);
    QVERIFY(search != nullptr);

    QVERIFY(SearchManager::isSearching(search->getSearchID()));
}

void tst_KadSearchManager::stopSearch_removesFromMap()
{
    UInt128 target(uint32{400});
    auto* search = SearchManager::prepareLookup(SearchType::Node, true, target);
    QVERIFY(search != nullptr);

    uint32 searchID = search->getSearchID();
    QVERIFY(SearchManager::isSearching(searchID));

    SearchManager::stopSearch(searchID, false);
    QVERIFY(!SearchManager::isSearching(searchID));
    QVERIFY(!SearchManager::alreadySearchingFor(target));
}

void tst_KadSearchManager::alreadySearchingFor_duplicate()
{
    UInt128 target(uint32{500});
    auto* search1 = SearchManager::prepareLookup(SearchType::Node, true, target);
    QVERIFY(search1 != nullptr);

    // Trying to create another search for same target should fail
    auto* search2 = SearchManager::prepareLookup(SearchType::Node, true, target);
    QVERIFY(search2 == nullptr);
}

void tst_KadSearchManager::prepareFindKeywords_splits()
{
    auto* search = SearchManager::prepareFindKeywords(
        QStringLiteral("hello world"), 0, nullptr);
    QVERIFY(search != nullptr);
    QCOMPARE(search->getSearchType(), SearchType::Keyword);
    QCOMPARE(search->getGUIName(), QStringLiteral("hello world"));

    // The target should be the MD4 hash of "hello world" (lowercased keyword)
    QVERIFY(search->getTarget() != UInt128());
    delete search;
}

QTEST_GUILESS_MAIN(tst_KadSearchManager)
#include "tst_KadSearchManager.moc"
