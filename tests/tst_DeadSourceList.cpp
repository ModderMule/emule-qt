/// @file tst_DeadSourceList.cpp
/// @brief Tests for client/DeadSourceList — dead source tracking with expiry.

#include "TestHelpers.h"
#include "client/DeadSourceList.h"
#include "utils/OtherFunctions.h"

#include <QTest>

using namespace eMule;

class tst_DeadSourceList : public QObject {
    Q_OBJECT

private slots:
    void addAndCheck();
    void notDead();
    void remove();
    void globalVsLocal();
    void highID_matching();
    void lowID_matching();
    void lowID_hashMatching();
    void count();
    void hashFunction();
};

static DeadSourceKey makeHighIDKey(uint32 id, uint16 port, uint16 kadPort = 0)
{
    DeadSourceKey k;
    k.userID = id;
    k.port = port;
    k.kadPort = kadPort;
    return k;
}

static DeadSourceKey makeLowIDKey(uint32 id, uint16 port, uint32 serverIP)
{
    DeadSourceKey k;
    k.userID = id;
    k.port = port;
    k.serverIP = serverIP;
    return k;
}

static DeadSourceKey makeLowIDHashKey(const uint8* hash)
{
    DeadSourceKey k;
    std::memcpy(k.hash.data(), hash, 16);
    // userID=0 → low ID without server, use hash matching
    return k;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void tst_DeadSourceList::addAndCheck()
{
    DeadSourceList list;
    list.init(false);  // local list
    auto key = makeHighIDKey(0x01020304, 4662);
    list.addDeadSource(key, false);
    QVERIFY(list.isDeadSource(key));
}

void tst_DeadSourceList::notDead()
{
    DeadSourceList list;
    list.init(false);
    auto key = makeHighIDKey(0x01020304, 4662);
    QVERIFY(!list.isDeadSource(key));
}

void tst_DeadSourceList::remove()
{
    DeadSourceList list;
    list.init(false);
    auto key = makeHighIDKey(0x01020304, 4662);
    list.addDeadSource(key, false);
    QVERIFY(list.isDeadSource(key));
    list.removeDeadSource(key);
    QVERIFY(!list.isDeadSource(key));
}

void tst_DeadSourceList::globalVsLocal()
{
    // Both global and local lists should accept and track sources
    DeadSourceList global;
    global.init(true);
    DeadSourceList local;
    local.init(false);

    auto key = makeHighIDKey(0x01020304, 4662);
    global.addDeadSource(key, false);
    local.addDeadSource(key, false);

    QVERIFY(global.isDeadSource(key));
    QVERIFY(local.isDeadSource(key));
}

void tst_DeadSourceList::highID_matching()
{
    DeadSourceList list;
    list.init(false);

    auto key = makeHighIDKey(0x01020304, 4662);
    list.addDeadSource(key, false);

    // Same ID + port, different server IP → should still match (high ID ignores server)
    DeadSourceKey lookup;
    lookup.userID = 0x01020304;
    lookup.port = 4662;
    lookup.serverIP = 0xAAAAAAAA;  // different server
    QVERIFY(list.isDeadSource(lookup));
}

void tst_DeadSourceList::lowID_matching()
{
    DeadSourceList list;
    list.init(false);

    auto key = makeLowIDKey(100, 4662, 0xC0A80001);
    list.addDeadSource(key, true);

    // Same low ID + port + server → match
    QVERIFY(list.isDeadSource(key));

    // Same low ID + port, different server → no match
    auto differentServer = makeLowIDKey(100, 4662, 0xC0A80099);
    QVERIFY(!list.isDeadSource(differentServer));
}

void tst_DeadSourceList::lowID_hashMatching()
{
    DeadSourceList list;
    list.init(false);

    uint8 hash[16];
    std::memset(hash, 0xAB, 16);
    auto key = makeLowIDHashKey(hash);
    list.addDeadSource(key, true);

    // Same hash → match
    auto lookup = makeLowIDHashKey(hash);
    QVERIFY(list.isDeadSource(lookup));

    // Different hash → no match
    uint8 hash2[16];
    std::memset(hash2, 0xCD, 16);
    auto different = makeLowIDHashKey(hash2);
    QVERIFY(!list.isDeadSource(different));
}

void tst_DeadSourceList::count()
{
    DeadSourceList list;
    list.init(false);
    QCOMPARE(list.count(), std::size_t{0});

    list.addDeadSource(makeHighIDKey(0x01020304, 4662), false);
    QCOMPARE(list.count(), std::size_t{1});

    list.addDeadSource(makeHighIDKey(0x05060708, 4663), false);
    QCOMPARE(list.count(), std::size_t{2});

    list.removeDeadSource(makeHighIDKey(0x01020304, 4662));
    QCOMPARE(list.count(), std::size_t{1});
}

void tst_DeadSourceList::hashFunction()
{
    // Verify that the hash function produces consistent results
    std::hash<DeadSourceKey> hasher;
    auto key1 = makeHighIDKey(0x01020304, 4662);
    auto key2 = makeHighIDKey(0x01020304, 4662);
    QCOMPARE(hasher(key1), hasher(key2));

    // Low-ID hash-based key
    uint8 hash[16];
    std::memset(hash, 0xAB, 16);
    auto hk1 = makeLowIDHashKey(hash);
    auto hk2 = makeLowIDHashKey(hash);
    QCOMPARE(hasher(hk1), hasher(hk2));
}

QTEST_MAIN(tst_DeadSourceList)
#include "tst_DeadSourceList.moc"
