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
    void onePerIP_port65535();
    void banIP_evictsWhenFull();
    void shutdownCleanup_clearsAll();
    void safeKad_disabled_prefReturnsEarly();

    // FastKad
    void initialEstimate_isDefault();
    void addResponseTime_lowersEstimate();
    void addResponseTime_highRaisesEstimate();
    void variancePadding_notCollapsed();
    void churn_keepsUpdating();
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
// SafeKad — onePerIP boundary (S2): a node squatting on port 65535 must still
// be caught by the one-node-per-IP scan.
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::onePerIP_port65535()
{
    SafeKad sk;
    UInt128 id1 = makeID(6553);
    UInt128 id2 = makeID(6554);

    // Track a node on the maximum port
    sk.trackNode(0x0A00000D, 65535, id1, true);

    // A second node from the same IP on a different port must be rejected under onePerIP —
    // the tracked :65535 entry has to fall inside the scan range (upper_bound, not lower).
    QVERIFY(sk.isBadNode(0x0A00000D, 1234, id2, KADEMLIA_VERSION8_49b, true, true));
}

// ---------------------------------------------------------------------------
// SafeKad — a ban must take effect even when the ban table is full (S3).
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::banIP_evictsWhenFull()
{
    SafeKad sk;

    // Fill the ban table to capacity (kMaxBannedIPs == 1000).
    for (uint32 i = 0; i < 1000; ++i)
        sk.banIP(0x0B000000 + i);

    // Banning one more IP must still succeed (evict the oldest ban to make room),
    // otherwise an ID-flipper whose IP can't be banned is simply re-admitted.
    const uint32 newIP = 0x0C123456;
    sk.banIP(newIP);
    QVERIFY(sk.isBanned(newIP));
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

    // Empty pool (no ctor placeholder) reports the 1000ms member-init default
    double est = fk.getEstMaxResponseTimeMs();
    QCOMPARE(est, 1000.0);
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
// FastKad — variance padding is dimensionally correct (F2). With a half-full
// pool of fast samples, the missing slots (assumed slow @1000ms) must inflate
// the variance so the estimate reflects real uncertainty. The old linear
// padding (missingCount * 1000) collapsed this: 50@100ms yielded ~1290ms;
// the correct squared padding yields ~1554ms.
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::variancePadding_notCollapsed()
{
    FastKad fk;

    // 50 fast samples (100ms) → 50 real, 50 "missing" slots padded at 1000ms.
    for (uint32 i = 1; i <= 50; ++i)
        fk.addResponseTime(i, 100.0);

    double est = fk.getEstMaxResponseTimeMs();
    // Correct padding puts this well above the old ~1290ms; assert the fix's regime.
    QVERIFY2(est > 1400.0 && est <= 3000.0,
             qPrintable(QStringLiteral("Estimate %1 ms should be > 1400 with correct variance padding").arg(est)));
}

// ---------------------------------------------------------------------------
// FastKad — the estimator keeps updating under heavy churn (F4). If the pool
// is full and every entry is younger than the 5-min protect window, the old
// code dropped new samples (estimator froze). The global-oldest fallback must
// keep admitting them.
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::churn_keepsUpdating()
{
    FastKad fk;

    // Fill the 100-slot pool with slow responses, then churn in 100 fresh fast ones.
    // All entries are young (added rapidly), so only the global-oldest fallback lets the
    // fast samples in — otherwise the pool stays all-slow and the estimate never drops.
    for (uint32 i = 1; i <= 100; ++i)
        fk.addResponseTime(i, 1000.0);
    for (uint32 i = 101; i <= 200; ++i)
        fk.addResponseTime(i, 50.0);

    double est = fk.getEstMaxResponseTimeMs();
    QVERIFY2(est < 500.0,
             qPrintable(QStringLiteral("Estimate %1 ms should drop below 500 once fast samples are admitted").arg(est)));
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
    // Restores the safe 1000ms default (not 0ms, which would collapse the timeout window
    // if FastKad is re-enabled in-process).
    QCOMPARE(fk.getEstMaxResponseTimeMs(), 1000.0);
}

// ---------------------------------------------------------------------------
// FastKad — disabled preference
// ---------------------------------------------------------------------------

void tst_SafeKadFastKad::fastKad_disabled_prefReturnsEarly()
{
    thePrefs.setUseFastKad(false);

    FastKad fk;
    // No ctor seed; the estimate stays at the 1000ms member-init default and
    // addResponseTime() is a no-op while disabled.
    double initial = fk.getEstMaxResponseTimeMs();

    // Additional calls should also be no-ops
    fk.addResponseTime(1, 50.0);
    fk.addResponseTime(2, 50.0);
    QCOMPARE(fk.getEstMaxResponseTimeMs(), initial);

    thePrefs.setUseFastKad(true); // restore
}

QTEST_MAIN(tst_SafeKadFastKad)
#include "tst_SafeKadFastKad.moc"
