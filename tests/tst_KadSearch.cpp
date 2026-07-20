/// @file tst_KadSearch.cpp
/// @brief Tests for KadSearch.h — Kademlia search state machine.

#include "TestHelpers.h"

#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadLookupHistory.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUInt128.h"
#include "protocol/Tag.h"
#include "utils/Opcodes.h"

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

    // Contact ownership (audit item #2)
    void processResponse_freesAllResultContacts();
    void processResponse_freesRejectedDuplicates();

    // Source publishing (audit item #4)
    void sourceTags_publishBuddyUdpPort();
    void sourceTags_notFirewalledHasNoBuddyTags();
    void sourceTags_firewalledWithoutBuddyCannotPublish();

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

// ---------------------------------------------------------------------------
// Contact ownership (audit item #2)
//
// Contacts handed to a search by the UDP listener are raw pointers the search
// takes ownership of. They used to be leaked: m_deleteList existed but nothing
// ever populated it, so every response leaked one Contact per result.
// ---------------------------------------------------------------------------

namespace {

uint32 testIP(uint32 seed)
{
    // Distinct /24s so the anti-spam subnet limit doesn't reject them.
    return (77u << 24) | ((seed & 0xFF) << 16) | (1u << 8) | 1u;
}

ContactArray makeResults(const UInt128& target, uint32 count, bool sameIP = false)
{
    ContactArray results;
    for (uint32 i = 1; i <= count; ++i) {
        results.push_back(new Contact(UInt128(i * 1013u), testIP(sameIP ? 1 : i),
                                      static_cast<uint16>(4672 + i),
                                      static_cast<uint16>(4662 + i),
                                      target, KADEMLIA_VERSION, KadUDPKey(), false));
    }
    return results;
}

} // namespace

void tst_KadSearch::processResponse_freesAllResultContacts()
{
    const uint64 baseline = Contact::liveInstanceCount();

    UInt128 target(uint32{9001});
    auto* search = SearchManager::prepareLookup(SearchType::Node, false, target);
    QVERIFY(search != nullptr);
    QVERIFY(SearchManager::startSearch(search));
    const uint32 searchID = search->getSearchID();

    ContactArray results = makeResults(target, 3);
    QCOMPARE(Contact::liveInstanceCount(), baseline + 3);

    SearchManager::processResponse(target, testIP(200), 4672, results);

    // The search owns them now and must have recorded all three for deletion.
    QCOMPARE(search->deleteListSize(), std::size_t{3});
    QCOMPARE(Contact::liveInstanceCount(), baseline + 3);

    SearchManager::stopSearch(searchID, false);

    // Destroying the search must free every contact it was handed.
    QCOMPARE(Contact::liveInstanceCount(), baseline);
}

void tst_KadSearch::processResponse_freesRejectedDuplicates()
{
    // Contacts rejected by the dedup / anti-spam filters are no longer deleted
    // inline — m_deleteList owns them all. Deleting inline *and* recording them
    // would be a double free; not recording them at all was the original leak.
    const uint64 baseline = Contact::liveInstanceCount();

    UInt128 target(uint32{9002});
    auto* search = SearchManager::prepareLookup(SearchType::Node, false, target);
    QVERIFY(search != nullptr);
    QVERIFY(SearchManager::startSearch(search));
    const uint32 searchID = search->getSearchID();

    // All three share one IP, so two get rejected as duplicates.
    ContactArray results = makeResults(target, 3, /*sameIP*/ true);
    QCOMPARE(Contact::liveInstanceCount(), baseline + 3);

    SearchManager::processResponse(target, testIP(200), 4672, results);
    QCOMPARE(search->deleteListSize(), std::size_t{3});

    SearchManager::stopSearch(searchID, false);
    QCOMPARE(Contact::liveInstanceCount(), baseline);
}

// ---------------------------------------------------------------------------
// Source publish tags (audit item #4)
// ---------------------------------------------------------------------------

namespace {

const Tag* findTag(const std::vector<Tag>& tags, uint8 nameId)
{
    for (const auto& t : tags)
        if (t.nameId() == nameId)
            return &t;
    return nullptr;
}

} // namespace

void tst_KadSearch::sourceTags_publishBuddyUdpPort()
{
    // Regression: FT_SERVERPORT carried the buddy's ED2K *TCP* port. The Kad
    // buddy-callback packet is UDP, so downloaders sent it to a port nothing was
    // listening on and every callback to a firewalled source failed.
    Search::SourcePublishParams p;
    p.firewalled   = true;
    p.hasBuddy     = true;
    p.buddyIP      = testIP(5);
    p.buddyUDPPort = 5555;   // deliberately different from any TCP port
    p.tcpPort      = 4662;
    p.largeFile    = false;

    bool canPublish = false;
    const auto tags = Search::buildSourcePublishTags(p, canPublish);
    QVERIFY(canPublish);

    const Tag* serverPort = findTag(tags, FT_SERVERPORT);
    QVERIFY(serverPort != nullptr);
    QCOMPARE(static_cast<uint16>(serverPort->intValue()), uint16{5555});

    const Tag* serverIP = findTag(tags, FT_SERVERIP);
    QVERIFY(serverIP != nullptr);
    QCOMPARE(serverIP->intValue(), p.buddyIP);

    // Source type 3 = firewalled with buddy, file <= 4GB.
    const Tag* sourceType = findTag(tags, FT_SOURCETYPE);
    QVERIFY(sourceType != nullptr);
    QCOMPARE(sourceType->intValue(), uint32{3});

    // Our own TCP port still travels as FT_SOURCEPORT and must not be confused
    // with the buddy's port.
    const Tag* sourcePort = findTag(tags, FT_SOURCEPORT);
    QVERIFY(sourcePort != nullptr);
    QCOMPARE(static_cast<uint16>(sourcePort->intValue()), uint16{4662});
}

void tst_KadSearch::sourceTags_notFirewalledHasNoBuddyTags()
{
    Search::SourcePublishParams p;
    p.firewalled = false;
    p.tcpPort    = 4662;
    p.largeFile  = true;

    bool canPublish = false;
    const auto tags = Search::buildSourcePublishTags(p, canPublish);
    QVERIFY(canPublish);

    QVERIFY(findTag(tags, FT_SERVERIP) == nullptr);
    QVERIFY(findTag(tags, FT_SERVERPORT) == nullptr);

    // Source type 4 = reachable directly, file > 4GB.
    const Tag* sourceType = findTag(tags, FT_SOURCETYPE);
    QVERIFY(sourceType != nullptr);
    QCOMPARE(sourceType->intValue(), uint32{4});
}

void tst_KadSearch::sourceTags_firewalledWithoutBuddyCannotPublish()
{
    // Publishing a source nobody can reach is worse than publishing none.
    Search::SourcePublishParams p;
    p.firewalled        = true;
    p.directUDPCallback = false;
    p.hasBuddy          = false;

    bool canPublish = true;
    const auto tags = Search::buildSourcePublishTags(p, canPublish);
    QVERIFY(!canPublish);
    QVERIFY(tags.empty());
}

QTEST_GUILESS_MAIN(tst_KadSearch)
#include "tst_KadSearch.moc"
