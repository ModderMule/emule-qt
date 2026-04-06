/// @file tst_SafeKadFastKad.cpp
/// @brief Tests for SafeKad (node reputation) and FastKad (adaptive timeout).

#include "TestHelpers.h"

#include "kademlia/KadSafeKad.h"
#include "kademlia/KadFastKad.h"
#include "kademlia/KadUInt128.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;
using namespace eMule::testing;

// Helper: create a UInt128 from a single uint32 seed
static UInt128 makeID(uint32 seed)
{
    UInt128 id(seed);
    return id;
}

// ---------------------------------------------------------------------------
// SafeKad Tests
// ---------------------------------------------------------------------------

class tst_SafeKadFastKad : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // SafeKad
    void trackNode_basicTracking();
    void trackNode_sameIdNoBan();
    void trackNode_rapidIdChange_bans();
    void isBadNode_bannedIpRejected();
    void isBadNode_onePerIP();
    void isBadNode_unverifiedIdChange_rejected();
    void trackProblematicNode_basic();
    void isProblematic_bannedFallthrough();
    void shutdownCleanup_clearsAll();
    void safeKad_disabled_prefReturnsEarly();

    // FastKad
    void initialEstimate_isDefault();
    void addResponseTime_lowersEstimate();
    void addResponseTime_highRaisesEstimate();
    void capacityLimit_evictsOldest();
    void fastKad_shutdownCleanup_resetsState();
    void fastKad_disabled_prefReturnsEarly();
};

void tst_SafeKadFastKad::initTestCase()
{
    TempDir tmp;
    thePrefs.load(tmp.filePath(QStringLiteral("prefs_init.yaml")));
}

// ---------------------------------------------------------------------------
// SafeKad — trackNode
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::trackNode_basicTracking()
{
    SafeKad sk;
    UInt128 id1 = makeID(1);
    sk.trackNode(0x0A000001, 5000, id1, true);

    // Node should not be banned or problematic
    QVERIFY(!sk.isBanned(0x0A000001));
    QVERIFY(!sk.isProblematic(0x0A000001, 5000));
}

void tst_SafeKadFastKad::trackNode_sameIdNoBan()
{
    SafeKad sk;
    UInt128 id1 = makeID(42);

    // Track the same node many times with the same ID — should never ban
    for (int i = 0; i < 100; ++i)
        sk.trackNode(0x0A000002, 6000, id1, true);

    QVERIFY(!sk.isBanned(0x0A000002));
}

void tst_SafeKadFastKad::trackNode_rapidIdChange_bans()
{
    SafeKad sk;
    UInt128 id1 = makeID(100);
    UInt128 id2 = makeID(200);

    // First track with ID1 (verified)
    sk.trackNode(0x0A000003, 7000, id1, true);
    QVERIFY(!sk.isBanned(0x0A000003));

    // Immediately change to ID2 (verified) — within the 1-hour window → ban
    sk.trackNode(0x0A000003, 7000, id2, true);
    QVERIFY(sk.isBanned(0x0A000003));
}

// ---------------------------------------------------------------------------
// SafeKad — isBadNode
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::isBadNode_bannedIpRejected()
{
    SafeKad sk;
    UInt128 id1 = makeID(300);

    sk.banIP(0x0A000004);
    QVERIFY(sk.isBadNode(0x0A000004, 8000, id1, KADEMLIA_VERSION8_49b, true, false));
}

void tst_SafeKadFastKad::isBadNode_onePerIP()
{
    SafeKad sk;
    UInt128 id1 = makeID(400);
    UInt128 id2 = makeID(500);

    // Track a node at IP:port1
    sk.trackNode(0x0A000005, 9000, id1, true);

    // isBadNode at same IP, different port, onePerIP=true → should reject
    QVERIFY(sk.isBadNode(0x0A000005, 9001, id2, KADEMLIA_VERSION8_49b, true, true));

    // isBadNode at same IP, different port, onePerIP=false → should accept
    QVERIFY(!sk.isBadNode(0x0A000005, 9001, id2, KADEMLIA_VERSION8_49b, true, false));
}

void tst_SafeKadFastKad::isBadNode_unverifiedIdChange_rejected()
{
    SafeKad sk;
    UInt128 id1 = makeID(600);
    UInt128 id2 = makeID(700);

    // Track with verified ID
    sk.trackNode(0x0A000006, 1000, id1, true);

    // Present a different ID as unverified → should be rejected
    // (won't downgrade from verified to unverified)
    QVERIFY(sk.isBadNode(0x0A000006, 1000, id2, KADEMLIA_VERSION8_49b, false, false));
}

// ---------------------------------------------------------------------------
// SafeKad — problematic nodes
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::trackProblematicNode_basic()
{
    SafeKad sk;
    sk.trackProblematicNode(0x0A000007, 2000);
    QVERIFY(sk.isProblematic(0x0A000007, 2000));

    // Different port at same IP is NOT problematic (tracked by IP:port)
    QVERIFY(!sk.isProblematic(0x0A000007, 2001));
}

void tst_SafeKadFastKad::isProblematic_bannedFallthrough()
{
    SafeKad sk;
    sk.banIP(0x0A000008);

    // Banned IPs are always considered problematic
    QVERIFY(sk.isProblematic(0x0A000008, 3000));
}

// ---------------------------------------------------------------------------
// SafeKad — cleanup
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::shutdownCleanup_clearsAll()
{
    SafeKad sk;
    UInt128 id1 = makeID(800);

    sk.trackNode(0x0A000009, 4000, id1, true);
    sk.trackProblematicNode(0x0A00000A, 4001);
    sk.banIP(0x0A00000B);

    sk.shutdownCleanup();

    QVERIFY(!sk.isBanned(0x0A000009));
    QVERIFY(!sk.isBanned(0x0A00000B));
    QVERIFY(!sk.isProblematic(0x0A00000A, 4001));
}

// ---------------------------------------------------------------------------
// SafeKad — disabled preference
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::safeKad_disabled_prefReturnsEarly()
{
    thePrefs.setUseSafeKad(false);

    SafeKad sk;
    UInt128 id1 = makeID(900);

    // trackNode should be a no-op
    sk.trackNode(0x0A00000C, 5000, id1, true);

    // isBadNode should return false when disabled
    QVERIFY(!sk.isBadNode(0x0A00000C, 5000, id1, KADEMLIA_VERSION8_49b, true, true));

    // isBanned should return false when disabled
    sk.banIP(0x0A00000C); // no-op when disabled
    QVERIFY(!sk.isBanned(0x0A00000C));

    thePrefs.setUseSafeKad(true); // restore
}

// ---------------------------------------------------------------------------
// FastKad — initial state
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::initialEstimate_isDefault()
{
    FastKad fk;

    // Seeded with one 1000ms default entry → estimate should be around 1000ms
    double est = fk.getEstMaxResponseTimeMs();
    QVERIFY2(est > 500.0 && est <= 3000.0,
             qPrintable(QStringLiteral("Initial estimate %1 ms out of expected range").arg(est)));
}

// ---------------------------------------------------------------------------
// FastKad — response time tracking
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::addResponseTime_lowersEstimate()
{
    FastKad fk;

    // Fill the entire 100-sample pool with fast responses (50ms each)
    // to eliminate the 1000ms padding for missing samples
    for (uint32 i = 1; i <= 100; ++i)
        fk.addResponseTime(i, 50.0);

    double est = fk.getEstMaxResponseTimeMs();
    // With 100 uniform 50ms samples: mean≈50, variance≈0, est≈150ms (50+0+100)
    QVERIFY2(est < 500.0,
             qPrintable(QStringLiteral("Estimate %1 ms should be < 500 with 100 fast responses").arg(est)));
    QVERIFY2(est > 0.0, "Estimate should be positive");
}

void tst_SafeKadFastKad::addResponseTime_highRaisesEstimate()
{
    FastKad fk;

    // Add 50 slow responses at 2500ms each
    for (uint32 i = 1; i <= 50; ++i)
        fk.addResponseTime(i, 2500.0);

    double est = fk.getEstMaxResponseTimeMs();
    // Should be capped at 3000ms
    QVERIFY2(est <= 3000.0,
             qPrintable(QStringLiteral("Estimate %1 ms should be capped at 3000").arg(est)));
    QVERIFY2(est > 1000.0,
             qPrintable(QStringLiteral("Estimate %1 ms should be > 1000 with slow responses").arg(est)));
}

// ---------------------------------------------------------------------------
// FastKad — capacity
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::capacityLimit_evictsOldest()
{
    FastKad fk;

    // Add 150 entries (exceeds 100 capacity) — should not crash
    for (uint32 i = 1; i <= 150; ++i)
        fk.addResponseTime(i, 100.0 + static_cast<double>(i));

    double est = fk.getEstMaxResponseTimeMs();
    QVERIFY2(est > 0.0 && est <= 3000.0,
             qPrintable(QStringLiteral("Estimate %1 ms should be in valid range after eviction").arg(est)));
}

// ---------------------------------------------------------------------------
// FastKad — cleanup
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::fastKad_shutdownCleanup_resetsState()
{
    FastKad fk;
    for (uint32 i = 1; i <= 10; ++i)
        fk.addResponseTime(i, 200.0);

    fk.shutdownCleanup();
    QCOMPARE(fk.getEstMaxResponseTimeMs(), 0.0);
}

// ---------------------------------------------------------------------------
// FastKad — disabled preference
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::fastKad_disabled_prefReturnsEarly()
{
    thePrefs.setUseFastKad(false);

    FastKad fk;
    // Constructor calls addResponseTime(0, 1000) which is a no-op when disabled
    // So estimate stays at default member init value
    double initial = fk.getEstMaxResponseTimeMs();

    // Additional calls should also be no-ops
    fk.addResponseTime(1, 50.0);
    fk.addResponseTime(2, 50.0);
    QCOMPARE(fk.getEstMaxResponseTimeMs(), initial);

    thePrefs.setUseFastKad(true); // restore
}

QTEST_MAIN(tst_SafeKadFastKad)
#include "tst_SafeKadFastKad.moc"
