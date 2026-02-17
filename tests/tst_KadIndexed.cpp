/// @file tst_KadIndexed.cpp
/// @brief Tests for KadIndexed.h — keyword/source/notes index.

#include "TestHelpers.h"

#include "kademlia/KadEntry.h"
#include "kademlia/KadIndexed.h"
#include "kademlia/KadUInt128.h"

#include <QTest>

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
    entry->m_ip = 0x0A000001;
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
        entry->m_ip = 0x0A000001 + i;
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
    entry->m_ip = 0x0A000001;
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
    entry->m_ip = 0x0A000001;
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
    entry1->m_ip = 0x0A000001;
    entry1->setFileName(QStringLiteral("original.txt"));
    indexed.addKeyword(keyID, sourceID, entry1, load);
    QCOMPARE(indexed.m_totalIndexKeyword, uint32{1});

    // Add second entry with same key+source — should merge, not add new
    auto* entry2 = new KeyEntry();
    entry2->m_ip = 0x0A000002;
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

QTEST_GUILESS_MAIN(tst_KadIndexed)
#include "tst_KadIndexed.moc"
