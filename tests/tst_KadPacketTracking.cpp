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
    void addLegacyChallenge_roundTrip();
    void hasActiveLegacyChallenge_check();

    // Flood limiter — MFC PacketTracking.cpp:99-208 semantics
    void flood_returnZeroWhenWithinBudget();
    void flood_responsesAreNeverThrottled();
    void flood_validReceiverKeyDoesNotBypass();
    void flood_overBudgetReturnsOne();
    void flood_massiveFloodReturnsTwo();
    void flood_budgetsArePerOpcodeAndPerIP();
    void flood_firewalled2SharesFirewalledBucket();
    void flood_cleanupDoesNotResetActiveFlooder();
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

// ---------------------------------------------------------------------------
// Flood limiter
//
// Return codes are MFC's: 0 = allowed, 1 = flood (drop), 2 = massive flood
// (drop + ban + expire the contact). This is the OPPOSITE polarity to the
// pre-hardening port code, which returned 1 for "allowed" — these tests exist
// partly to pin the new convention down.
//
// Budgets are packets/minute charged as milliseconds against a 60000 ms bucket
// that starts full, so the Nth packet in a tight burst leaves
// 60000 - N*(60000/budget) tokens. Nothing here sleeps: the bucket only refills
// by elapsed wall-clock, so a tight loop is a deterministic drain.
// ---------------------------------------------------------------------------

namespace {
/// Send `count` back-to-back requests, returning the last verdict.
int drain(TestablePacketTracking& pt, uint32 ip, uint8 opcode, int count)
{
    int rc = 0;
    for (int i = 0; i < count; ++i)
        rc = pt.inTrackListIsAllowedPacket(ip, opcode, false);
    return rc;
}
} // namespace

void tst_KadPacketTracking::flood_returnZeroWhenWithinBudget()
{
    TestablePacketTracking pt;
    // KADEMLIA2_REQ is the most generous request budget at 10/min.
    for (int i = 0; i < 10; ++i)
        QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA2_REQ, false), 0);
}

void tst_KadPacketTracking::flood_responsesAreNeverThrottled()
{
    TestablePacketTracking pt;
    // Responses hit the `default:` arm. Throttling these would make a busy
    // search rate-limit its own answers, which is why MFC exempts them.
    const uint8 responses[] = {KADEMLIA2_RES, KADEMLIA2_SEARCH_RES,
                               KADEMLIA2_PUBLISH_RES, KADEMLIA2_PONG,
                               KADEMLIA2_HELLO_RES};
    for (uint8 op : responses)
        QCOMPARE(drain(pt, 0x0A000001, op, 500), 0);
}

void tst_KadPacketTracking::flood_validReceiverKeyDoesNotBypass()
{
    TestablePacketTracking pt;
    // Regression: a valid receiver key used to short-circuit the limiter
    // entirely, handing unlimited request rate to any handshaked peer.
    // CALLBACK_REQ has the tightest budget (1/min), so packet 2 is over.
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA_CALLBACK_REQ, true), 0);
    QVERIFY(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA_CALLBACK_REQ, true) != 0);
}

void tst_KadPacketTracking::flood_overBudgetReturnsOne()
{
    TestablePacketTracking pt;
    // BOOTSTRAP_REQ: 2/min. Two are free, the third overdraws the bucket but is
    // nowhere near the 3-minute deficit that triggers a ban.
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA2_BOOTSTRAP_REQ, false), 0);
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA2_BOOTSTRAP_REQ, false), 0);
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA2_BOOTSTRAP_REQ, false), 1);
}

void tst_KadPacketTracking::flood_massiveFloodReturnsTwo()
{
    TestablePacketTracking pt;
    // Deficit must exceed MIN2MS(3) = 180000 ms. BOOTSTRAP_REQ costs 30000 ms a
    // packet against a 60000 ms bucket, so it takes 2 free + 6 to reach -180000
    // and the 9th is the first strictly beyond it.
    QCOMPARE(drain(pt, 0x0A000001, KADEMLIA2_BOOTSTRAP_REQ, 8), 1);
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA2_BOOTSTRAP_REQ, false), 2);
}

void tst_KadPacketTracking::flood_budgetsArePerOpcodeAndPerIP()
{
    TestablePacketTracking pt;
    // Exhaust one opcode from one IP...
    QVERIFY(drain(pt, 0x0A000001, KADEMLIA2_BOOTSTRAP_REQ, 4) != 0);
    // ...a different opcode from the same IP is unaffected...
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA2_REQ, false), 0);
    // ...and so is the same opcode from a different IP.
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000002, KADEMLIA2_BOOTSTRAP_REQ, false), 0);
}

void tst_KadPacketTracking::flood_firewalled2SharesFirewalledBucket()
{
    TestablePacketTracking pt;
    // MFC folds FIREWALLED2 onto FIREWALLED so a peer cannot get double the
    // budget by alternating the two request forms. Both are 2/min.
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA_FIREWALLED_REQ, false), 0);
    QCOMPARE(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA_FIREWALLED2_REQ, false), 0);
    QVERIFY(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA_FIREWALLED2_REQ, false) != 0);
}

void tst_KadPacketTracking::flood_cleanupDoesNotResetActiveFlooder()
{
    TestablePacketTracking pt;
    // Regression: the old cleanup compared against a stamp fixed at entry
    // creation, so a sustained flooder had its counters wiped every 5 minutes
    // and the limit could never accumulate. lastExpire now tracks when the
    // bucket would actually be refilled, so a peer in deficit survives cleanup.
    QVERIFY(drain(pt, 0x0A000001, KADEMLIA2_BOOTSTRAP_REQ, 4) != 0);
    pt.inTrackListCleanup();
    QVERIFY(pt.inTrackListIsAllowedPacket(0x0A000001, KADEMLIA2_BOOTSTRAP_REQ, false) != 0);
}

QTEST_GUILESS_MAIN(tst_KadPacketTracking)
#include "tst_KadPacketTracking.moc"
