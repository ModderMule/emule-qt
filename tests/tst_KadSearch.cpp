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
#include "kademlia/KadIO.h"
#include "kademlia/KadUInt128.h"
#include "net/Address.h"
#include "httpcache/HttpCacheOffer.h"
#include "protocol/Tag.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QTest>

#include <array>

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
    void sourceTags_directCallbackSetsTheCallbackBit();
    void sourceTags_buddyBranchDoesNotClaimDirectCallback();
    void sourceTags_buddyIpTravelsInNetworkOrder();

    // HTTP Cache chunk descriptors riding the source record
    void httpCacheTags_absentWithoutChunks();
    void httpCacheTags_roundTripThroughTheKadCodec();
    void httpCacheTags_cappedAndScreened();
    void httpCacheTags_carryTheRealFileSize();

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

void tst_KadSearch::sourceTags_directCallbackSetsTheCallbackBit()
{
    // Regression: the publish path built FT_ENCRYPTION from the three crypt bits and
    // never set bit 3, so every type-6 record we published was dropped on arrival —
    // that bit is the whole of the "reach me by direct UDP callback" claim, and a
    // receiver rejects the source without it. Nothing else on a type-6 record
    // distinguishes it from an unreachable one.
    Search::SourcePublishParams p;
    p.firewalled        = true;
    p.directUDPCallback = true;
    p.tcpPort           = 4662;
    p.cryptOptions      = 0x01;   // crypt-layer supported, callback bit clear

    bool canPublish = false;
    const auto tags = Search::buildSourcePublishTags(p, canPublish);
    QVERIFY(canPublish);

    const Tag* sourceType = findTag(tags, FT_SOURCETYPE);
    QVERIFY(sourceType != nullptr);
    QCOMPARE(sourceType->intValue(), uint32{6});

    const Tag* encryption = findTag(tags, FT_ENCRYPTION);
    QVERIFY(encryption != nullptr);
    QVERIFY((encryption->intValue() & 0x08) != 0);
    // The caller's own bits survive: the branch adds to the byte, it does not replace it.
    QVERIFY((encryption->intValue() & 0x01) != 0);

    // Type 6 carries no buddy at all — it is the alternative to having one.
    QVERIFY(findTag(tags, FT_SERVERIP) == nullptr);
    QVERIFY(findTag(tags, FT_SERVERPORT) == nullptr);
    QVERIFY(findTag(tags, FT_BUDDYHASH) == nullptr);

    // The TCP port still travels, so a downloader knows where to come back to.
    const Tag* sourcePort = findTag(tags, FT_SOURCEPORT);
    QVERIFY(sourcePort != nullptr);
    QCOMPARE(static_cast<uint16>(sourcePort->intValue()), uint16{4662});
}

void tst_KadSearch::sourceTags_buddyBranchDoesNotClaimDirectCallback()
{
    // The forced bit belongs to type 6 only. A buddy-relayed source is reached through
    // the buddy, and claiming a direct callback it cannot answer would cost every
    // downloader a UDP round trip into a hole.
    Search::SourcePublishParams p;
    p.firewalled   = true;
    p.hasBuddy     = true;
    p.buddyIP      = testIP(6);
    p.buddyUDPPort = 5555;
    p.tcpPort      = 4662;
    p.cryptOptions = 0x01;

    bool canPublish = false;
    const auto tags = Search::buildSourcePublishTags(p, canPublish);
    QVERIFY(canPublish);

    const Tag* encryption = findTag(tags, FT_ENCRYPTION);
    QVERIFY(encryption != nullptr);
    QCOMPARE(encryption->intValue(), uint32{0x01});
}

void tst_KadSearch::sourceTags_buddyIpTravelsInNetworkOrder()
{
    // FT_SERVERIP is the one IP tag in this packet that is not host order, matching the
    // reference implementation. Asserting the exact integer rather than a round trip is
    // deliberate: a symmetric address would make a swap invisible in either direction.
    const Address buddy = Address::fromString(QStringLiteral("11.22.33.44"));
    QVERIFY(!buddy.isNull());
    QVERIFY(buddy.toNetworkUint32() != buddy.toUint32());   // asymmetric, so order shows

    Search::SourcePublishParams p;
    p.firewalled   = true;
    p.hasBuddy     = true;
    p.buddyIP      = buddy.toNetworkUint32();
    p.buddyUDPPort = 5555;
    p.tcpPort      = 4662;

    bool canPublish = false;
    const auto tags = Search::buildSourcePublishTags(p, canPublish);
    QVERIFY(canPublish);

    const Tag* serverIP = findTag(tags, FT_SERVERIP);
    QVERIFY(serverIP != nullptr);
    QCOMPARE(serverIP->intValue(), buddy.toNetworkUint32());
    QVERIFY(Address::fromNetworkOrder(serverIP->intValue()) == buddy);
}

// ---------------------------------------------------------------------------
// HTTP Cache chunk descriptors on the source record
//
// These ride the ordinary Kad source record rather than a record of their own,
// because a storing node keys one bucket per publisher IP+port and a brand new Kad
// opcode would be dropped unread by the stock clients that do the storing. That makes
// the exact tag encoding a compatibility surface: it has to survive a round trip
// through the real Kad tag codec, and it must not disturb anything a legacy client
// reads out of the same record.
// ---------------------------------------------------------------------------

namespace {

eMule::HttpCacheOffer makeChunk(uint32 part, const QString& url, uint32 expires)
{
    eMule::HttpCacheOffer offer;
    offer.fileHash.fill(0x11);
    offer.partIndex = part;
    offer.plainLength = eMule::kHttpCachePublishPartSize;
    offer.cipherLength = eMule::kHttpCacheCipherMax;
    offer.url = url;
    offer.key = QByteArray(32, static_cast<char>(0xA0 + part));
    offer.iv = QByteArray(16, static_cast<char>(0xB0 + part));
    offer.cipherSha256 = QByteArray(32, static_cast<char>(0xC0 + part));
    offer.expiresAt = expires;
    return offer;
}

/// Put a tag list through the real Kad wire codec, which is the only way to know the
/// tags survive as the type they were written as — a BSOB in particular.
TagList reserialize(const std::vector<Tag>& tags)
{
    SafeMemFile out;
    io::writeKadTagList(out, tags);
    out.seek(0, 0);
    return io::readKadTagList(out);
}

} // namespace

void tst_KadSearch::httpCacheTags_absentWithoutChunks()
{
    Search::SourcePublishParams p;
    p.firewalled = false;
    p.tcpPort    = 4662;

    bool canPublish = false;
    const auto tags = Search::buildSourcePublishTags(p, canPublish);
    QVERIFY(canPublish);

    // Nothing to advertise, so not a byte is spent — and a record with no chunk tags
    // is exactly what every client that does not publish them emits.
    for (const auto& tag : tags)
        QVERIFY2(!tag.name().startsWith("hc"), "chunk tags emitted with no chunks");

    uint8 fileHash[16];
    std::memset(fileHash, 0x11, sizeof fileHash);
    QVERIFY(Search::parseHttpCacheChunkTags(reserialize(tags), fileHash).empty());
}

void tst_KadSearch::httpCacheTags_roundTripThroughTheKadCodec()
{
    Search::SourcePublishParams p;
    p.firewalled = false;
    p.tcpPort    = 4662;
    p.httpCacheChunks = {
        makeChunk(0, QStringLiteral("https://cache.example.com/v1/chunks/aa11"), 1800000000),
        makeChunk(7, QStringLiteral("http://other.example.org/v1/chunks/bb22"),  1800000900),
    };

    bool canPublish = false;
    const auto tags = Search::buildSourcePublishTags(p, canPublish);
    QVERIFY(canPublish);

    // Through the real codec, not straight back out of the vector: the key and the
    // digest travel as BSOB, which is a distinct wire type, and "it worked in memory"
    // says nothing about what a storing node hands back.
    uint8 fileHash[16];
    std::memset(fileHash, 0x11, sizeof fileHash);
    const auto chunks = Search::parseHttpCacheChunkTags(reserialize(tags), fileHash);

    QCOMPARE(chunks.size(), size_t{2});

    QCOMPARE(chunks[0].partIndex, uint32{0});
    QCOMPARE(chunks[0].url, QStringLiteral("https://cache.example.com/v1/chunks/aa11"));
    QCOMPARE(chunks[0].key, QByteArray(32, static_cast<char>(0xA0)));
    QCOMPARE(chunks[0].iv, QByteArray(16, static_cast<char>(0xB0)));
    QCOMPARE(chunks[0].cipherSha256, QByteArray(32, static_cast<char>(0xC0)));
    QCOMPARE(chunks[0].expiresAt, uint32{1800000000});

    QCOMPARE(chunks[1].partIndex, uint32{7});
    QCOMPARE(chunks[1].url, QStringLiteral("http://other.example.org/v1/chunks/bb22"));

    // The lengths are never sent — only whole parts are published, so the reader
    // derives them and a publisher has nothing to lie about.
    QCOMPARE(chunks[0].plainLength, eMule::kHttpCachePublishPartSize);
    QCOMPARE(chunks[0].cipherLength, eMule::kHttpCacheCipherMax);
    QVERIFY(chunks[0].isWellFormed());

    // The source itself is untouched: a client that knows nothing about these tags
    // still reads the record it came for.
    const Tag* sourceType = findTag(tags, FT_SOURCETYPE);
    QVERIFY(sourceType != nullptr);
    QCOMPARE(sourceType->intValue(), uint32{1});
    const Tag* sourcePort = findTag(tags, FT_SOURCEPORT);
    QVERIFY(sourcePort != nullptr);
    QCOMPARE(static_cast<uint16>(sourcePort->intValue()), uint16{4662});
}

void tst_KadSearch::httpCacheTags_cappedAndScreened()
{
    Search::SourcePublishParams p;
    p.firewalled = false;
    p.tcpPort    = 4662;

    const QString goodUrl = QStringLiteral("https://cache.example.com/v1/chunks/ok");

    // A URL past the Kad cap, a chunk that is not a whole part, and one with no stated
    // expiry are each dropped rather than published — a stored record outlives the
    // chunk, so an unbounded lifetime is the one thing that cannot be guessed.
    auto tooLongUrl = makeChunk(1, QStringLiteral("https://x.example/")
                                       + QString(KADHC_MAX_URL_LEN, QLatin1Char('u')), 1800000000);
    auto shortPart = makeChunk(2, goodUrl, 1800000000);
    shortPart.plainLength = eMule::kHttpCachePublishPartSize - 1;
    auto noExpiry = makeChunk(3, goodUrl, 0);

    p.httpCacheChunks = {tooLongUrl, shortPart, noExpiry,
                         makeChunk(4, goodUrl, 1800000000),
                         makeChunk(5, goodUrl, 1800000001),
                         makeChunk(6, goodUrl, 1800000002),
                         makeChunk(8, goodUrl, 1800000003)};

    bool canPublish = false;
    const auto tags = Search::buildSourcePublishTags(p, canPublish);
    QVERIFY(canPublish);

    uint8 fileHash[16];
    std::memset(fileHash, 0x11, sizeof fileHash);
    const auto chunks = Search::parseHttpCacheChunkTags(reserialize(tags), fileHash);

    // Three screened out, and the remaining four capped at KADHC_MAX_CHUNKS. The cap is
    // not cosmetic: a stock node serialises one record into a 2 KB buffer and throws on
    // overflow, which aborts the serve for every result in that packet, not just ours.
    QCOMPARE(chunks.size(), size_t{KADHC_MAX_CHUNKS});
    QCOMPARE(chunks[0].partIndex, uint32{4});
    QCOMPARE(chunks[1].partIndex, uint32{5});
    QCOMPARE(chunks[2].partIndex, uint32{6});

    // And the whole record stays well under that wall.
    SafeMemFile out;
    io::writeKadTagList(out, tags);
    QVERIFY2(out.length() < 2048,
             qPrintable(QStringLiteral("record is %1 bytes; a stock node refuses at 2048")
                            .arg(out.length())));
}

void tst_KadSearch::httpCacheTags_carryTheRealFileSize()
{
    // A stored record carrying a size is only served by a stock node when the searcher
    // asked for that exact size (srchybrid/kademlia/kademlia/Indexed.cpp:728). We do
    // publish one, and that is safe only because these tags annotate our real source
    // record: the size is the file's own, so the searcher's number matches. Pinned here
    // because the day that stops being true, chunk records silently stop coming back.
    Search::SourcePublishParams p;
    p.firewalled  = false;
    p.tcpPort     = 4662;
    p.hasFileSize = true;
    p.fileSize    = 123456789;
    p.httpCacheChunks = {
        makeChunk(0, QStringLiteral("https://cache.example.com/v1/chunks/aa11"), 1800000000)};

    bool canPublish = false;
    const auto tags = Search::buildSourcePublishTags(p, canPublish);
    QVERIFY(canPublish);

    const Tag* size = findTag(tags, FT_FILESIZE);
    QVERIFY2(size != nullptr, "FT_FILESIZE dropped — chunk records would still be served "
                              "by this port's storer but filtered out by a stock one");
    QCOMPARE(size->int64Value(), p.fileSize);
}

QTEST_GUILESS_MAIN(tst_KadSearch)
#include "tst_KadSearch.moc"
