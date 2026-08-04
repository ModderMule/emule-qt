/// @file tst_KadSearchManager.cpp
/// @brief Tests for KadSearchManager.h — search lifecycle management.

#include "TestHelpers.h"

#include "kademlia/KadMiscUtils.h"
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
    void selectKeyword_picksFirstFreeWord();
    void selectKeyword_allActive();
    void selectKeyword_tooShort();
    void prepareFindKeywords_fallbackTarget();
    void findNodeFWCheckUDP_onlyOneSearch();
    void cancelNodeFWCheckUDPSearch_removesAll();

private:
    /// Start a keyword search for @p expression, asserting it starts.
    static void startKeywordSearch(const QString& expression);
    /// Number of NodeFwCheckUDP searches currently registered.
    static int countFWCheckSearches();
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

void tst_KadSearchManager::selectKeyword_picksFirstFreeWord()
{
    startKeywordSearch(QStringLiteral("ubuntu desktop"));

    // "ubuntu" is taken, so the next long-enough word becomes the target
    const auto sel = SearchManager::selectKeyword(QStringLiteral("ubuntu server"));
    QCOMPARE(sel.status, KeywordStatus::Ok);
    QCOMPARE(sel.keyword, QStringLiteral("server"));
    QCOMPARE(sel.primaryKeyword, QStringLiteral("ubuntu"));
    QVERIFY(sel.isFallback);
}

void tst_KadSearchManager::selectKeyword_allActive()
{
    startKeywordSearch(QStringLiteral("ubuntu desktop"));
    startKeywordSearch(QStringLiteral("server release"));

    const auto sel = SearchManager::selectKeyword(QStringLiteral("ubuntu server"));
    QCOMPARE(sel.status, KeywordStatus::AllActive);
    QCOMPARE(sel.primaryKeyword, QStringLiteral("ubuntu"));
    QVERIFY(sel.keyword.isEmpty());
}

void tst_KadSearchManager::selectKeyword_tooShort()
{
    const auto sel = SearchManager::selectKeyword(QStringLiteral("ab cd"));
    QCOMPARE(sel.status, KeywordStatus::TooShort);
    QVERIFY(sel.keyword.isEmpty());
    QVERIFY(sel.primaryKeyword.isEmpty());
}

void tst_KadSearchManager::prepareFindKeywords_fallbackTarget()
{
    startKeywordSearch(QStringLiteral("ubuntu desktop"));

    // Without a keyword hint prepareFindKeywords selects one itself
    auto* search = SearchManager::prepareFindKeywords(
        QStringLiteral("ubuntu server"), 0, nullptr);
    QVERIFY(search != nullptr);

    UInt128 expected;
    getKeywordHash(QStringLiteral("server"), expected);
    QCOMPARE(search->getTarget(), expected);
    QVERIFY(SearchManager::startSearch(search));
}

void tst_KadSearchManager::findNodeFWCheckUDP_onlyOneSearch()
{
    // The FW-check target is random, so alreadySearchingFor() cannot dedupe it;
    // repeated calls (the "Recheck Firewall" button) must not stack up lookups.
    QVERIFY(!SearchManager::isNodeFWCheckUDPSearchActive());

    for (int i = 0; i < 3; ++i)
        QVERIFY(SearchManager::findNodeFWCheckUDP());

    QCOMPARE(countFWCheckSearches(), 1);
    QVERIFY(SearchManager::isNodeFWCheckUDPSearchActive());
}

void tst_KadSearchManager::cancelNodeFWCheckUDPSearch_removesAll()
{
    // Plant two FW-check searches directly, bypassing the cancel-first in
    // findNodeFWCheckUDP(), plus an unrelated search that must survive.
    UInt128 fwTarget1(uint32{600});
    UInt128 fwTarget2(uint32{601});
    UInt128 nodeTarget(uint32{602});
    QVERIFY(SearchManager::prepareLookup(SearchType::NodeFwCheckUDP, true, fwTarget1));
    QVERIFY(SearchManager::prepareLookup(SearchType::NodeFwCheckUDP, true, fwTarget2));
    QVERIFY(SearchManager::prepareLookup(SearchType::Node, true, nodeTarget));
    QCOMPARE(countFWCheckSearches(), 2);

    SearchManager::cancelNodeFWCheckUDPSearch();

    QCOMPARE(countFWCheckSearches(), 0);
    QVERIFY(!SearchManager::isNodeFWCheckUDPSearchActive());
    QVERIFY(SearchManager::alreadySearchingFor(nodeTarget));
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void tst_KadSearchManager::startKeywordSearch(const QString& expression)
{
    auto* search = SearchManager::prepareFindKeywords(expression, 0, nullptr);
    QVERIFY(search != nullptr);
    QVERIFY(SearchManager::startSearch(search));
}

int tst_KadSearchManager::countFWCheckSearches()
{
    int count = 0;
    for (const auto& [target, search] : SearchManager::getSearches()) {
        if (search->getSearchType() == SearchType::NodeFwCheckUDP)
            ++count;
    }
    return count;
}

QTEST_GUILESS_MAIN(tst_KadSearchManager)
#include "tst_KadSearchManager.moc"
