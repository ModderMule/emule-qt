/// @file tst_KadIndexed.cpp
/// @brief Tests for KadIndexed.h — keyword/source/notes index.

#include "TestHelpers.h"

#include "kademlia/KadEntry.h"
#include "kademlia/KadIndexed.h"
#include "kademlia/KadResultPacketWriter.h"
#include "kademlia/KadTypes.h"
#include "kademlia/KadUInt128.h"
#include "utils/MapKey.h"
#include "utils/SafeFile.h"

#include <QTest>

#include <ctime>

using namespace eMule;
using namespace eMule::kad;

class tst_KadIndexed : public QObject {
    Q_OBJECT

private slots:
    void construct_empty();
    void addKeyword_basic();
    void addKeyword_loadTracking();
    void addSources_basic();
    void addNotes_basic();
    void getFileKeyCount();
    void addKeyword_duplicateSource();
    void sendStoreRequest_check();

    // Refactor coverage: extracted KadResultPacketWriter.h helpers.
    void resultPacketSender_header();
    void resultPacketSender_fragments();
    void cleanIndex_srcHash();
    void cleanIndex_keyHash();
    void addSources_replacesDuplicate();
};

void tst_KadIndexed::construct_empty()
{
    Indexed indexed;
    QCOMPARE(indexed.m_totalIndexSource, uint32{0});
    QCOMPARE(indexed.m_totalIndexKeyword, uint32{0});
    QCOMPARE(indexed.m_totalIndexNotes, uint32{0});
    QCOMPARE(indexed.m_totalIndexLoad, uint32{0});
    QCOMPARE(indexed.getFileKeyCount(), uint32{0});
}

void tst_KadIndexed::addKeyword_basic()
{
    Indexed indexed;

    UInt128 keyID(uint32{100});
    UInt128 sourceID(uint32{200});
    auto* entry = new KeyEntry();
    entry->m_address = Address::fromHostOrder(0x0A000001);
    entry->setFileName(QStringLiteral("test.mp3"));

    uint8 load = 0;
    bool result = indexed.addKeyword(keyID, sourceID, entry, load);
    QVERIFY(result);
    QCOMPARE(indexed.m_totalIndexKeyword, uint32{1});
    QVERIFY(load < 100);
}

void tst_KadIndexed::addKeyword_loadTracking()
{
    Indexed indexed;

    UInt128 keyID(uint32{100});
    uint8 load = 0;

    // Add several keyword entries under different source IDs
    for (uint32 i = 0; i < 10; ++i) {
        UInt128 sourceID(i + 1);
        auto* entry = new KeyEntry();
        entry->m_address = Address::fromHostOrder(0x0A000001 + i);
        entry->setFileName(QStringLiteral("file_%1.txt").arg(i));
        indexed.addKeyword(keyID, sourceID, entry, load);
    }

    QCOMPARE(indexed.m_totalIndexKeyword, uint32{10});
    // Load is (count * 100) / KADEMLIAMAXINDEX — with 10 entries out of 50000 it rounds to 0
    QVERIFY(load == 0); // 10/50000 * 100 = 0 in integer math, which is correct
}

void tst_KadIndexed::addSources_basic()
{
    Indexed indexed;

    UInt128 keyID(uint32{300});
    UInt128 sourceID(uint32{400});
    auto* entry = new Entry();
    entry->m_address = Address::fromHostOrder(0x0A000001);
    entry->m_tcpPort = 4662;
    entry->m_udpPort = 4672;

    uint8 load = 0;
    bool result = indexed.addSources(keyID, sourceID, entry, load);
    QVERIFY(result);
    QCOMPARE(indexed.m_totalIndexSource, uint32{1});
}

void tst_KadIndexed::addNotes_basic()
{
    Indexed indexed;

    UInt128 keyID(uint32{500});
    UInt128 sourceID(uint32{600});
    auto* entry = new Entry();
    entry->m_address = Address::fromHostOrder(0x0A000001);
    entry->addTag(Tag(QByteArrayLiteral("comment"), QStringLiteral("Great file!")));

    uint8 load = 0;
    bool result = indexed.addNotes(keyID, sourceID, entry, load);
    QVERIFY(result);
    QCOMPARE(indexed.m_totalIndexNotes, uint32{1});
}

void tst_KadIndexed::getFileKeyCount()
{
    Indexed indexed;
    QCOMPARE(indexed.getFileKeyCount(), uint32{0});

    uint8 load = 0;

    // Add entries with different key IDs
    UInt128 keyID1(uint32{1});
    UInt128 sourceID1(uint32{10});
    auto* entry1 = new KeyEntry();
    entry1->setFileName(QStringLiteral("a.txt"));
    indexed.addKeyword(keyID1, sourceID1, entry1, load);

    UInt128 keyID2(uint32{2});
    UInt128 sourceID2(uint32{20});
    auto* entry2 = new KeyEntry();
    entry2->setFileName(QStringLiteral("b.txt"));
    indexed.addKeyword(keyID2, sourceID2, entry2, load);

    QCOMPARE(indexed.getFileKeyCount(), uint32{2});
}

void tst_KadIndexed::addKeyword_duplicateSource()
{
    Indexed indexed;

    UInt128 keyID(uint32{100});
    UInt128 sourceID(uint32{200});
    uint8 load = 0;

    // Add first entry
    auto* entry1 = new KeyEntry();
    entry1->m_address = Address::fromHostOrder(0x0A000001);
    entry1->setFileName(QStringLiteral("original.txt"));
    indexed.addKeyword(keyID, sourceID, entry1, load);
    QCOMPARE(indexed.m_totalIndexKeyword, uint32{1});

    // Add second entry with same key+source — should merge, not add new
    auto* entry2 = new KeyEntry();
    entry2->m_address = Address::fromHostOrder(0x0A000002);
    entry2->setFileName(QStringLiteral("updated.txt"));
    bool result = indexed.addKeyword(keyID, sourceID, entry2, load);
    QVERIFY(result);
    // Total should remain 1 since it merged
    QCOMPARE(indexed.m_totalIndexKeyword, uint32{1});
    delete entry2; // not owned by indexed since it was merged
}

void tst_KadIndexed::sendStoreRequest_check()
{
    Indexed indexed;

    UInt128 keyID(uint32{700});

    // With no load entry, store should be allowed
    QVERIFY(indexed.sendStoreRequest(keyID));

    // After adding a load entry with current time, store should be blocked
    indexed.addLoad(keyID, time(nullptr));
    QVERIFY(!indexed.sendStoreRequest(keyID));
}

void tst_KadIndexed::resultPacketSender_header()
{
    UInt128 kadId(uint32{0xAABBCCDD});
    UInt128 keyID(uint32{0x11223344});

    QList<QByteArray> sent;
    ResultPacketSender sender(kadId, keyID,
        [&](SafeMemFile& pkt) { sent.append(pkt.buffer()); });

    SafeMemFile body;
    body.writeUInt32(0xDEADBEEF); // 4-byte payload
    sender.addResult(body);
    sender.flush();

    QCOMPARE(sent.size(), 1);
    // header = kadId(16) + keyID(16) + count(2), then 4-byte body
    QCOMPARE(static_cast<int>(sent.first().size()), 16 + 16 + 2 + 4);

    SafeMemFile reader(sent.first());
    reader.seek(32, 0); // skip kadId + keyID to the count field
    QCOMPARE(reader.readUInt16(), uint16{1});
}

void tst_KadIndexed::resultPacketSender_fragments()
{
    UInt128 kadId(uint32{1});
    UInt128 keyID(uint32{2});

    QList<QByteArray> sent;
    ResultPacketSender sender(kadId, keyID,
        [&](SafeMemFile& pkt) { sent.append(pkt.buffer()); });

    // 500-byte bodies force multiple packets below UDP_KAD_MAXFRAGMENT (1420).
    constexpr int kResults = 10;
    const QByteArray payload(500, 'x');
    for (int i = 0; i < kResults; ++i) {
        SafeMemFile body;
        body.write(payload.constData(), payload.size());
        sender.addResult(body);
    }
    sender.flush();

    QVERIFY(sent.size() >= 2); // fragmented across packets

    int totalCount = 0;
    for (const QByteArray& pkt : sent) {
        SafeMemFile reader(pkt);
        reader.seek(32, 0);
        totalCount += reader.readUInt16();
    }
    QCOMPARE(totalCount, kResults); // every result accounted for exactly once
}

void tst_KadIndexed::cleanIndex_srcHash()
{
    SrcHashMap map;
    UInt128 keyID(uint32{42});
    HashKeyOwn key(keyID.getData());

    auto* srcHash = new SrcHash();
    srcHash->keyID = keyID;

    // Expired source: lifetime > 0 and < now -> pruned.
    auto* expiredSrc = new Source();
    expiredSrc->sourceID = UInt128(uint32{1});
    auto* expiredEntry = new Entry();
    expiredEntry->m_lifetime = 1;
    expiredSrc->entryList.push_back(expiredEntry);
    srcHash->sourceList.push_back(expiredSrc);

    // Live source: future lifetime -> kept.
    auto* liveSrc = new Source();
    liveSrc->sourceID = UInt128(uint32{2});
    auto* liveEntry = new Entry();
    liveEntry->m_lifetime = time(nullptr) + 100000;
    liveSrc->entryList.push_back(liveEntry);
    srcHash->sourceList.push_back(liveSrc);

    map[key] = srcHash;

    uint32 counter = 2;
    cleanIndex(map, time(nullptr), counter);

    QCOMPARE(counter, uint32{1});                                  // one entry removed
    QCOMPARE(static_cast<int>(map.size()), 1);                     // hash kept (live source)
    QCOMPARE(static_cast<int>(map[key]->sourceList.size()), 1);    // only live source left

    destroyIndex(map);
}

void tst_KadIndexed::cleanIndex_keyHash()
{
    KeyHashMap map;
    UInt128 keyID(uint32{7});
    HashKeyOwn key(keyID.getData());

    auto* keyHash = new KeyHash();
    keyHash->keyID = keyID;

    UInt128 srcA(uint32{1});
    auto* expiredSrc = new Source();
    expiredSrc->sourceID = srcA;
    auto* expiredEntry = new KeyEntry();
    expiredEntry->m_lifetime = 1;
    expiredSrc->entryList.push_back(expiredEntry);
    keyHash->mapSource[HashKeyOwn(srcA.getData())] = expiredSrc;

    UInt128 srcB(uint32{2});
    auto* liveSrc = new Source();
    liveSrc->sourceID = srcB;
    auto* liveEntry = new KeyEntry();
    liveEntry->m_lifetime = time(nullptr) + 100000;
    liveSrc->entryList.push_back(liveEntry);
    keyHash->mapSource[HashKeyOwn(srcB.getData())] = liveSrc;

    map[key] = keyHash;

    uint32 counter = 2;
    cleanIndex(map, time(nullptr), counter); // exercises the map/->second overload

    QCOMPARE(counter, uint32{1});
    QCOMPARE(static_cast<int>(map.size()), 1);
    QCOMPARE(static_cast<int>(map[key]->mapSource.size()), 1);

    destroyIndex(map);
}

void tst_KadIndexed::addSources_replacesDuplicate()
{
    Indexed indexed;
    UInt128 keyID(uint32{300});
    UInt128 sourceID(uint32{400});
    uint8 load = 0;

    auto* e1 = new Entry();
    e1->m_size = 111;
    QVERIFY(indexed.addSources(keyID, sourceID, e1, load));
    QCOMPARE(indexed.m_totalIndexSource, uint32{1});

    // Same key + source: addSourceEntry replaces (deletes e1), does not accumulate.
    auto* e2 = new Entry();
    e2->m_size = 222;
    QVERIFY(indexed.addSources(keyID, sourceID, e2, load));
    QCOMPARE(indexed.m_totalIndexSource, uint32{1});
}

QTEST_GUILESS_MAIN(tst_KadIndexed)
#include "tst_KadIndexed.moc"
