/// @file tst_KadContact.cpp
/// @brief Tests for KadContact.h — Kademlia DHT node contact.

#include "TestHelpers.h"

#include "kademlia/KadContact.h"
#include "kademlia/KadUInt128.h"
#include "kademlia/KadUDPKey.h"

#include <QTest>

#include <ctime>

using namespace eMule;
using namespace eMule::kad;

class tst_KadContact : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void construct_withId();
    void construct_withTarget();
    void setClientId_recomputesDistance();
    void setIPAddress_clearsVerified();
    void updateType_progression();
    void checkingType_increment();
    void expire_marksType4();
    void incDecUse();
    void copy_semantics();
    void getLastSeen_reverseCalc();
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void tst_KadContact::construct_default()
{
    Contact c;
    QCOMPARE(c.getIPAddress(), uint32{0});
    QCOMPARE(c.getTCPPort(), uint16{0});
    QCOMPARE(c.getUDPPort(), uint16{0});
    QCOMPARE(c.getType(), uint8{3});
    QCOMPARE(c.getVersion(), uint8{0});
    QVERIFY(!c.inUse());
    QVERIFY(!c.isIpVerified());
    QVERIFY(!c.getReceivedHelloPacket());
    QVERIFY(!c.isBootstrapContact());
}

void tst_KadContact::construct_withId()
{
    UInt128 localId;
    localId.setValueRandom();
    UInt128 clientId;
    clientId.setValueRandom();

    Contact c(clientId, 0x0A000001, 4672, 4662, 8, KadUDPKey(0x1234, 0x0A000001), true, localId);

    QCOMPARE(c.getClientID(), clientId);
    QCOMPARE(c.getIPAddress(), uint32{0x0A000001});
    QCOMPARE(c.getUDPPort(), uint16{4672});
    QCOMPARE(c.getTCPPort(), uint16{4662});
    QCOMPARE(c.getVersion(), uint8{8});
    QVERIFY(c.isIpVerified());
    QCOMPARE(c.getType(), uint8{3});

    // Distance = localId XOR clientId
    UInt128 expectedDist(localId);
    expectedDist.xorWith(clientId);
    QCOMPARE(c.getDistance(), expectedDist);
}

void tst_KadContact::construct_withTarget()
{
    UInt128 clientId(uint32{100});
    UInt128 target(uint32{200});

    Contact c(clientId, 0x0A000001, 4672, 4662, target, 8, KadUDPKey(), false);

    // Distance = target XOR clientId
    UInt128 expectedDist(target);
    expectedDist.xorWith(clientId);
    QCOMPARE(c.getDistance(), expectedDist);
}

void tst_KadContact::setClientId_recomputesDistance()
{
    UInt128 localId(uint32{0xFF});
    UInt128 clientId1(uint32{0x0F});

    Contact c(clientId1, 0x01020304, 1000, 2000, 8, KadUDPKey(), false, localId);

    UInt128 clientId2(uint32{0xAA});
    c.setClientID(clientId2, localId);

    UInt128 expectedDist(localId);
    expectedDist.xorWith(clientId2);
    QCOMPARE(c.getClientID(), clientId2);
    QCOMPARE(c.getDistance(), expectedDist);
}

void tst_KadContact::setIPAddress_clearsVerified()
{
    UInt128 localId;
    localId.setValueRandom();
    UInt128 clientId;
    clientId.setValueRandom();

    Contact c(clientId, 0x0A000001, 4672, 4662, 8, KadUDPKey(), true, localId);
    QVERIFY(c.isIpVerified());

    c.setIPAddress(0x0A000002);
    QVERIFY(!c.isIpVerified());
    QCOMPARE(c.getIPAddress(), uint32{0x0A000002});

    // Setting same IP should NOT clear verified
    c.setIpVerified(true);
    c.setIPAddress(0x0A000002);
    QVERIFY(c.isIpVerified()); // unchanged because same IP
}

void tst_KadContact::updateType_progression()
{
    Contact c;
    // Just created → updateType should set type 2 (< 1 hour old)
    c.updateType();
    QCOMPARE(c.getType(), uint8{2});
    QVERIFY(c.getExpireTime() > 0);
}

void tst_KadContact::checkingType_increment()
{
    Contact c;
    // checkingType only works if lastTypeSet was >= 10 seconds ago.
    // We can't easily control time, but we can verify the initial call
    // doesn't change anything (since it was just created < 10s ago)
    uint8 typeBefore = c.getType();
    c.checkingType();
    QCOMPARE(c.getType(), typeBefore); // shouldn't change — too recent
}

void tst_KadContact::expire_marksType4()
{
    Contact c;
    c.expire();
    QCOMPARE(c.getType(), uint8{4});
    QCOMPARE(c.getExpireTime(), time_t{1});
}

void tst_KadContact::incDecUse()
{
    Contact c;
    QVERIFY(!c.inUse());

    c.incUse();
    QVERIFY(c.inUse());

    c.incUse();
    c.decUse();
    QVERIFY(c.inUse());

    c.decUse();
    QVERIFY(!c.inUse());
}

void tst_KadContact::copy_semantics()
{
    UInt128 localId(uint32{0xFF});
    UInt128 clientId(uint32{42});

    Contact original(clientId, 0x0A000001, 4672, 4662, 8, KadUDPKey(0x5678, 0x0A000001), true, localId);
    original.setReceivedHelloPacket();

    Contact copy(original);
    QCOMPARE(copy.getClientID(), original.getClientID());
    QCOMPARE(copy.getDistance(), original.getDistance());
    QCOMPARE(copy.getIPAddress(), original.getIPAddress());
    QCOMPARE(copy.getUDPPort(), original.getUDPPort());
    QCOMPARE(copy.getTCPPort(), original.getTCPPort());
    QCOMPARE(copy.getVersion(), original.getVersion());
    QCOMPARE(copy.getType(), original.getType());
    QCOMPARE(copy.isIpVerified(), original.isIpVerified());
    QCOMPARE(copy.getReceivedHelloPacket(), original.getReceivedHelloPacket());
}

void tst_KadContact::getLastSeen_reverseCalc()
{
    Contact c;
    c.updateType(); // sets type=2, expires = now + HR2S(1)
    QCOMPARE(c.getType(), uint8{2});

    time_t lastSeen = c.getLastSeen();
    time_t now = time(nullptr);
    // lastSeen = expires - HR2S(1) ≈ now
    QVERIFY(lastSeen >= now - 2 && lastSeen <= now + 2);
}

QTEST_MAIN(tst_KadContact)
#include "tst_KadContact.moc"
