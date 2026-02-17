/// @file tst_KadPacketTracking.cpp
/// @brief Tests for KadPacketTracking.h — token bucket rate limiting and challenge tracking.

#include "TestHelpers.h"

#include "kademlia/KadPacketTracking.h"
#include "kademlia/KadUInt128.h"
#include "utils/Opcodes.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;

/// Expose protected methods for testing via a derived helper.
class TestablePacketTracking : public PacketTracking {
public:
    using PacketTracking::addTrackedOutPacket;
    using PacketTracking::isOnOutTrackList;
    using PacketTracking::inTrackListIsAllowedPacket;
    using PacketTracking::inTrackListCleanup;
    using PacketTracking::addLegacyChallenge;
    using PacketTracking::isLegacyChallenge;
    using PacketTracking::hasActiveLegacyChallenge;
};

class tst_KadPacketTracking : public QObject {
    Q_OBJECT

private slots:
    void addTrackedOutPacket_basic();
    void isOnOutTrackList_found();
    void isOnOutTrackList_notFound();
    void inTrackListIsAllowedPacket_privileged();
    void addLegacyChallenge_roundTrip();
    void hasActiveLegacyChallenge_check();
};

void tst_KadPacketTracking::addTrackedOutPacket_basic()
{
    TestablePacketTracking pt;
    pt.addTrackedOutPacket(0x0A000001, KADEMLIA2_REQ);
    // Should be on the track list now
    QVERIFY(pt.isOnOutTrackList(0x0A000001, KADEMLIA2_REQ, true /* dontRemove */));
}

void tst_KadPacketTracking::isOnOutTrackList_found()
{
    TestablePacketTracking pt;
    pt.addTrackedOutPacket(0x0A000001, KADEMLIA2_HELLO_REQ);
    // Find and remove
    QVERIFY(pt.isOnOutTrackList(0x0A000001, KADEMLIA2_HELLO_REQ));
    // After removal, should not be found
    QVERIFY(!pt.isOnOutTrackList(0x0A000001, KADEMLIA2_HELLO_REQ));
}

void tst_KadPacketTracking::isOnOutTrackList_notFound()
{
    TestablePacketTracking pt;
    pt.addTrackedOutPacket(0x0A000001, KADEMLIA2_REQ);
    // Different IP or opcode should not be found
    QVERIFY(!pt.isOnOutTrackList(0x0A000002, KADEMLIA2_REQ));
    QVERIFY(!pt.isOnOutTrackList(0x0A000001, KADEMLIA2_HELLO_REQ));
}

void tst_KadPacketTracking::inTrackListIsAllowedPacket_privileged()
{
    TestablePacketTracking pt;
    // Bootstrap and hello requests are always allowed (privileged)
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA2_BOOTSTRAP_REQ, false), 1);
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA2_HELLO_REQ, false), 1);

    // Regular requests should also be allowed initially (have tokens)
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA2_REQ, false), 1);
}

void tst_KadPacketTracking::addLegacyChallenge_roundTrip()
{
    TestablePacketTracking pt;

    UInt128 contactID(uint32{42});
    UInt128 challengeID(uint32{99});
    uint32 ip = 0x0A000001;
    uint8 opcode = KADEMLIA2_REQ;

    pt.addLegacyChallenge(contactID, challengeID, ip, opcode);

    // Should be retrievable
    UInt128 outContactID;
    QVERIFY(pt.isLegacyChallenge(challengeID, ip, opcode, outContactID));
    QCOMPARE(outContactID, contactID);

    // After retrieval, should be consumed
    QVERIFY(!pt.isLegacyChallenge(challengeID, ip, opcode, outContactID));
}

void tst_KadPacketTracking::hasActiveLegacyChallenge_check()
{
    TestablePacketTracking pt;

    uint32 ip = 0x0A000001;
    QVERIFY(!pt.hasActiveLegacyChallenge(ip));

    UInt128 contactID(uint32{1});
    UInt128 challengeID(uint32{2});
    pt.addLegacyChallenge(contactID, challengeID, ip, KADEMLIA2_REQ);

    QVERIFY(pt.hasActiveLegacyChallenge(ip));
    QVERIFY(!pt.hasActiveLegacyChallenge(0x0A000002));
}

QTEST_GUILESS_MAIN(tst_KadPacketTracking)
#include "tst_KadPacketTracking.moc"
