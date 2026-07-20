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

namespace {
/// A source entry as a real PUBLISH_SOURCE_REQ would produce it: routable
/// address, both ports, and at least one tag. Anything less is rejected as
/// malformed, so tests that want a source stored must supply all of it.
Entry* makeSourceEntry(uint32 ip, uint16 tcpPort = 4662, uint16 udpPort = 4672)
{
    auto* e = new Entry();
    e->m_address = Address::fromHostOrder(ip);
    e->m_tcpPort = tcpPort;
    e->m_udpPort = udpPort;
    e->addTag(Tag(uint8{FT_SOURCETYPE}, uint32{1}));
    return e;
}

/// A keyword entry as a real PUBLISH_KEY_REQ would produce it. A name and a
/// non-zero size are both mandatory — an entry missing either is unservable and
/// is rejected on ingest.
KeyEntry* makeKeyEntry(uint32 ip, const QString& name, uint64 size = 1024)
{
    auto* e = new KeyEntry();
    e->m_address = Address::fromHostOrder(ip);
    e->setFileName(name);
    e->m_size = size;
    return e;
}
} // namespace

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

    // Per-IP flood limits — MFC Indexed.cpp AddSources/AddNotes
    void addSources_rotatingSourceIdOccupiesOneSlot();
    void addSources_distinctHostsGetDistinctSlots();
    void addSources_rejectsIncompleteEntries();
    void addNotes_dedupePerIpOrSourceId();
    void addNotes_acceptEntryWithoutPorts();
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
    auto* entry = makeKeyEntry(0x0A000001, QStringLiteral("test.mp3"));

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
        auto* entry = makeKeyEntry(0x0A000001 + i, QStringLiteral("file_%1.txt").arg(i));
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
    // A source publish always carries at least TAG_SOURCETYPE; entries with no
    // tags at all are rejected as malformed.
    entry->addTag(Tag(uint8{FT_SOURCETYPE}, uint32{1}));

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
    auto* entry1 = makeKeyEntry(0x0A000001, QStringLiteral("a.txt"));
    indexed.addKeyword(keyID1, sourceID1, entry1, load);

    UInt128 keyID2(uint32{2});
    UInt128 sourceID2(uint32{20});
    auto* entry2 = makeKeyEntry(0x0A000002, QStringLiteral("b.txt"));
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
    auto* entry1 = makeKeyEntry(0x0A000001, QStringLiteral("original.txt"));
    indexed.addKeyword(keyID, sourceID, entry1, load);
    QCOMPARE(indexed.m_totalIndexKeyword, uint32{1});

    // Same key+source and same size — merges rather than adding a second entry.
    // The merge runs new-absorbs-old: entry2 becomes the stored entry and entry1
    // is destroyed, so the index owns entry2 and the caller must NOT delete it.
    // (It used to be the other way round, which leaked one KeyEntry per refresh.)
    auto* entry2 = makeKeyEntry(0x0A000002, QStringLiteral("updated.txt"));
    bool result = indexed.addKeyword(keyID, sourceID, entry2, load);
    QVERIFY(result);
    QCOMPARE(indexed.m_totalIndexKeyword, uint32{1});

    // Both publishers are now tracked against the surviving entry, and two
    // distinct /24 blocks means it scores as trusted.
    QVERIFY(entry2->getTrustValue() >= 1.0f);
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

    // Sources are deduped by IP + port, not by sourceID, so both publishes must
    // describe the same host to collapse onto one slot.
    auto* e1 = makeSourceEntry(0x0A000001, 4662, 4672);
    e1->m_size = 111;
    QVERIFY(indexed.addSources(keyID, sourceID, e1, load));
    QCOMPARE(indexed.m_totalIndexSource, uint32{1});

    // Same publisher: addSourceEntry replaces (deletes e1), does not accumulate.
    auto* e2 = makeSourceEntry(0x0A000001, 4662, 4672);
    e2->m_size = 222;
    QVERIFY(indexed.addSources(keyID, sourceID, e2, load));
    QCOMPARE(indexed.m_totalIndexSource, uint32{1});
}

// ---------------------------------------------------------------------------
// Per-IP source/note flood limits
//
// The index used to dedupe on sourceID alone. sourceID is the publisher's ED2K
// user hash — fully attacker-chosen — so one host could claim every slot for a
// file simply by varying it. MFC keys sources on IP + port and notes on IP or
// sourceID, which collapses all of one host's publishes onto a single slot.
// ---------------------------------------------------------------------------

void tst_KadIndexed::addSources_rotatingSourceIdOccupiesOneSlot()
{
    Indexed indexed;
    UInt128 keyID(uint32{700});
    uint8 load = 0;

    // Same host, 50 different sourceIDs — the core item-F attack.
    for (uint32 i = 0; i < 50; ++i) {
        auto* e = makeSourceEntry(0x0A000001);
        QVERIFY(indexed.addSources(keyID, UInt128(i), e, load));
    }

    QCOMPARE(indexed.m_totalIndexSource, uint32{1});
}

void tst_KadIndexed::addSources_distinctHostsGetDistinctSlots()
{
    Indexed indexed;
    UInt128 keyID(uint32{701});
    uint8 load = 0;

    // Genuinely different hosts must still each get a slot, otherwise the
    // dedupe would have broken normal operation rather than just the abuse.
    for (uint32 i = 0; i < 5; ++i) {
        auto* e = makeSourceEntry(0x0A000001 + i);
        QVERIFY(indexed.addSources(keyID, UInt128(i), e, load));
    }

    QCOMPARE(indexed.m_totalIndexSource, uint32{5});
}

void tst_KadIndexed::addSources_rejectsIncompleteEntries()
{
    Indexed indexed;
    UInt128 keyID(uint32{702});
    UInt128 sourceID(uint32{1});
    uint8 load = 0;

    // No address.
    auto* noIP = makeSourceEntry(0x0A000001);
    noIP->m_address = Address();
    QVERIFY(!indexed.addSources(keyID, sourceID, noIP, load));
    delete noIP;

    // No TCP port — unusable as a download source.
    auto* noTCP = makeSourceEntry(0x0A000001, 0, 4672);
    QVERIFY(!indexed.addSources(keyID, sourceID, noTCP, load));
    delete noTCP;

    // No UDP port.
    auto* noUDP = makeSourceEntry(0x0A000001, 4662, 0);
    QVERIFY(!indexed.addSources(keyID, sourceID, noUDP, load));
    delete noUDP;

    // No tags at all.
    auto* noTags = new Entry();
    noTags->m_address = Address::fromHostOrder(0x0A000001);
    noTags->m_tcpPort = 4662;
    noTags->m_udpPort = 4672;
    QVERIFY(!indexed.addSources(keyID, sourceID, noTags, load));
    delete noTags;

    QCOMPARE(indexed.m_totalIndexSource, uint32{0});
}

void tst_KadIndexed::addNotes_dedupePerIpOrSourceId()
{
    Indexed indexed;
    UInt128 keyID(uint32{703});
    uint8 load = 0;

    // m_sourceID must be set exactly as process_KADEMLIA2_PUBLISH_NOTES_REQ
    // does — the note dedupe reads it off the entry, so leaving it at its
    // default would make every note look like the same publisher.
    auto makeNote = [](uint32 ip, uint32 sourceID) {
        auto* e = new Entry();
        e->m_address = Address::fromHostOrder(ip);
        e->m_sourceID = UInt128(sourceID);
        e->addTag(Tag(QByteArrayLiteral("comment"), QStringLiteral("spam")));
        return e;
    };

    // One IP, many sourceIDs — collapses to a single note.
    for (uint32 i = 0; i < 20; ++i)
        QVERIFY(indexed.addNotes(keyID, UInt128(i), makeNote(0x0A000001, i), load));
    QCOMPARE(indexed.m_totalIndexNotes, uint32{1});

    // A different IP with an unseen sourceID earns its own slot.
    QVERIFY(indexed.addNotes(keyID, UInt128(uint32{999}), makeNote(0x0A000002, 999), load));
    QCOMPARE(indexed.m_totalIndexNotes, uint32{2});

    // ...but the same sourceID from yet another IP collapses onto an existing
    // slot, since notes match on IP *or* sourceID.
    QVERIFY(indexed.addNotes(keyID, UInt128(uint32{999}), makeNote(0x0A000003, 999), load));
    QCOMPARE(indexed.m_totalIndexNotes, uint32{2});
}

void tst_KadIndexed::addNotes_acceptEntryWithoutPorts()
{
    // Notes carry no ports and no lifetime requirement — the stricter source
    // gate must not leak across to them.
    Indexed indexed;
    uint8 load = 0;

    auto* note = new Entry();
    note->m_address = Address::fromHostOrder(0x0A000001);
    note->addTag(Tag(QByteArrayLiteral("comment"), QStringLiteral("nice")));

    QVERIFY(indexed.addNotes(UInt128(uint32{704}), UInt128(uint32{1}), note, load));
    QCOMPARE(indexed.m_totalIndexNotes, uint32{1});
}

QTEST_GUILESS_MAIN(tst_KadIndexed)
#include "tst_KadIndexed.moc"
