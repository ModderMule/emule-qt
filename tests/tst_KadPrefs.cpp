/// @file tst_KadPrefs.cpp
/// @brief Tests for KadPrefs — Kademlia runtime preferences.

#include "TestHelpers.h"

#include "kademlia/KadPrefs.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadUInt128.h"
#include "prefs/Preferences.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"

#include <QTest>

#include <cstring>

using namespace eMule;
using namespace eMule::kad;
using namespace eMule::testing;

class tst_KadPrefs : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void construction_generatesRandomKadId();
    void construction_clientHashFromUserHash();
    void persistRoundTrip();
    void setIPAddress_twoStepVerification();
    void firewalled_counterBehavior();
    void firewalled_recheckPreservesLastState();
    void findBuddy_oneShotFlag();
    void setKademliaFiles_minimumAverage();
    void setExternKadPort_consensusCheck();
    void getUDPVerifyKey_nonZero();
    void getUDPVerifyKey_deterministic();
    void statsFirewalledRatio_withSamples();
    void statsFirewalledRatio_noData();
};

void tst_KadPrefs::initTestCase()
{
    // Ensure thePrefs has a user hash and kadUDPKey
    TempDir tmp;
    thePrefs.load(tmp.filePath(QStringLiteral("prefs_init.yaml")));
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

void tst_KadPrefs::construction_generatesRandomKadId()
{
    TempDir tmp;
    KadPrefs prefs(tmp.path());

    UInt128 zero(uint32{0});
    QVERIFY(prefs.kadId() != zero);
}

void tst_KadPrefs::construction_clientHashFromUserHash()
{
    TempDir tmp;
    KadPrefs prefs(tmp.path());

    // clientHash should be derived from thePrefs.userHash()
    auto userHash = thePrefs.userHash();
    UInt128 expected(userHash.data());
    QCOMPARE(prefs.clientHash(), expected);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void tst_KadPrefs::persistRoundTrip()
{
    TempDir tmp;
    UInt128 savedId;

    // Create and destroy to trigger writeFile
    {
        KadPrefs p1(tmp.path());
        p1.setIPAddress(0x0A000101);
        p1.setIPAddress(0x0A000101); // second call to pass two-step
        savedId = p1.kadId();
    }

    // Reload
    KadPrefs p2(tmp.path());

    QCOMPARE(p2.kadId(), savedId);
    QCOMPARE(p2.ipAddress(), uint32{0x0A000101});
}

// ---------------------------------------------------------------------------
// IP two-step verification
// ---------------------------------------------------------------------------

void tst_KadPrefs::setIPAddress_twoStepVerification()
{
    TempDir tmp;
    KadPrefs prefs(tmp.path());

    // First call with a new IP — should NOT set m_ip yet (only m_ipLast)
    prefs.setIPAddress(0x0A000001);
    // Second call with same IP — should set m_ip
    prefs.setIPAddress(0x0A000001);
    QCOMPARE(prefs.ipAddress(), uint32{0x0A000001});

    // Now set a different IP — should only update m_ipLast, not m_ip
    prefs.setIPAddress(0x0B000001);
    // m_ip should still be the old value since the new IP hasn't been confirmed
    // Actually, according to the logic: if ip != m_ipLast, m_ipLast = ip
    // So m_ip stays 0x0A000001
    QCOMPARE(prefs.ipAddress(), uint32{0x0A000001});

    // Confirm the new IP
    prefs.setIPAddress(0x0B000001);
    QCOMPARE(prefs.ipAddress(), uint32{0x0B000001});
}

// ---------------------------------------------------------------------------
// Firewall
// ---------------------------------------------------------------------------

void tst_KadPrefs::firewalled_counterBehavior()
{
    TempDir tmp;
    KadPrefs prefs(tmp.path());

    // Initial state: recheckIP is true (counter < KADEMLIAFIREWALLCHECKS)
    // So firewalled() should return true (assume firewalled during recheck)
    QVERIFY(prefs.firewalled());

    // Increment firewall checks: each incRecheckIP() + incFirewalled() combo
    // After 2 incFirewalled(), firewalled() returns false
    prefs.incFirewalled();
    QVERIFY(prefs.firewalled()); // counter = 1, still < 2
    prefs.incFirewalled();
    QVERIFY(!prefs.firewalled()); // counter = 2, >= 2 → not firewalled
}

void tst_KadPrefs::firewalled_recheckPreservesLastState()
{
    TempDir tmp;
    KadPrefs prefs(tmp.path());

    // Get past the initial firewalled state
    prefs.incFirewalled();
    prefs.incFirewalled();
    QVERIFY(!prefs.firewalled()); // not firewalled

    // Now setFirewalled() should snapshot current state (not firewalled) and reset counter
    prefs.setFirewalled();
    // After reset, counter is 0, recheckIP is still true → returns true (recheck mode)
    QVERIFY(prefs.firewalled());

    // Complete enough recheck IPs to exit recheck mode
    for (uint32 i = 0; i < KADEMLIAFIREWALLCHECKS; ++i)
        prefs.incRecheckIP();

    // Now recheckIP() returns false → returns m_lastFirewallState (which was false)
    QVERIFY(!prefs.firewalled());
}

// ---------------------------------------------------------------------------
// Find buddy
// ---------------------------------------------------------------------------

void tst_KadPrefs::findBuddy_oneShotFlag()
{
    TempDir tmp;
    KadPrefs prefs(tmp.path());

    prefs.setFindBuddy(true);
    QVERIFY(prefs.findBuddy());  // first call returns true
    QVERIFY(!prefs.findBuddy()); // second call returns false (one-shot)
}

// ---------------------------------------------------------------------------
// Kademlia files
// ---------------------------------------------------------------------------

void tst_KadPrefs::setKademliaFiles_minimumAverage()
{
    TempDir tmp;
    KadPrefs prefs(tmp.path());

    prefs.setKademliaUsers(100);

    // avg < 108 → uses 108
    prefs.setKademliaFiles(50);
    QCOMPARE(prefs.kademliaFiles(), uint32{108 * 100});

    // avg >= 108 → uses actual avg
    prefs.setKademliaFiles(200);
    QCOMPARE(prefs.kademliaFiles(), uint32{200 * 100});
}

// ---------------------------------------------------------------------------
// External port consensus
// ---------------------------------------------------------------------------

void tst_KadPrefs::setExternKadPort_consensusCheck()
{
    TempDir tmp;
    KadPrefs prefs(tmp.path());

    // 2 of 3 must agree
    prefs.setExternKadPort(5000, 0x01010101);
    QCOMPARE(prefs.externalKadPort(), uint16{0}); // not enough votes

    prefs.setExternKadPort(5000, 0x02020202);
    QCOMPARE(prefs.externalKadPort(), uint16{5000}); // 2 of 2 agree → set

    // Different port from a third IP should not change it
    prefs.setExternKadPort(6000, 0x03030303);
    QCOMPARE(prefs.externalKadPort(), uint16{5000}); // still 5000
}

// ---------------------------------------------------------------------------
// UDP verify key
// ---------------------------------------------------------------------------

void tst_KadPrefs::getUDPVerifyKey_nonZero()
{
    uint32 key = KadPrefs::getUDPVerifyKey(0x0A000001);
    QVERIFY(key != 0);
}

void tst_KadPrefs::getUDPVerifyKey_deterministic()
{
    uint32 key1 = KadPrefs::getUDPVerifyKey(0x0A000001);
    uint32 key2 = KadPrefs::getUDPVerifyKey(0x0A000001);
    QCOMPARE(key1, key2);

    // Different target IP → likely different key
    uint32 key3 = KadPrefs::getUDPVerifyKey(0x0B000001);
    QVERIFY(key1 != key3);
}

// ---------------------------------------------------------------------------
// Firewall stats
// ---------------------------------------------------------------------------

void tst_KadPrefs::statsFirewalledRatio_withSamples()
{
    TempDir tmp;
    KadPrefs prefs(tmp.path());

    prefs.statsIncUDPFirewalledNodes(true);
    prefs.statsIncUDPFirewalledNodes(true);
    prefs.statsIncUDPFirewalledNodes(false);

    // 2 firewalled out of 3 total
    float ratio = prefs.statsGetFirewalledRatio(true);
    QVERIFY(qFuzzyCompare(ratio, 2.0f / 3.0f));
}

void tst_KadPrefs::statsFirewalledRatio_noData()
{
    TempDir tmp;
    KadPrefs prefs(tmp.path());

    QCOMPARE(prefs.statsGetFirewalledRatio(true), 0.0f);
    QCOMPARE(prefs.statsGetFirewalledRatio(false), 0.0f);
}

QTEST_MAIN(tst_KadPrefs)
#include "tst_KadPrefs.moc"
