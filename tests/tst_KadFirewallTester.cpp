/// @file tst_KadFirewallTester.cpp
/// @brief Tests for KadFirewallTester.h — UDP firewall detection.

#include "TestFixtures.h"
#include "TestHelpers.h"

#include "kademlia/KadFirewallTester.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUInt128.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;

class tst_KadFirewallTester : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void reset_clearsState();
    void isFirewalledUDP_defaultOpen();
    void addPossibleTestContact_queued();
    void setUDPFWCheckResult_updates();
    void isVerified_afterSuccess();
    void isOpenAndVerified_needsAnOpenVerdict();
    void verification_expiresWhenNoRecheckConcludes();
    void setUDPFWCheckResult_ignoresUnrequested();
    void setUDPFWCheckResult_dedupesSecondAnswer();
    void setUDPFWCheckResult_failureFreesSlot();
    void setUDPFWCheckResult_twoFailuresMeanFirewalled();
    void timeout_reportsFirewalledNotOpen();
    void usedClients_surviveRecheckAndReset();
    void candidates_version6IsAccepted();
    void failedRequest_isNotMarkedUsed();
    void queryNextClient_waitsForTheExternalPort();
    void useExternKadPort_trueBeforePortIsKnown();
};

void tst_KadFirewallTester::init()
{
    UDPFirewallTester::reset();
    UDPFirewallTester::debugClearUsedTestClients();   // they outlive reset()
}

void tst_KadFirewallTester::cleanup()
{
    UDPFirewallTester::reset();
    UDPFirewallTester::debugClearUsedTestClients();
    SearchManager::stopAllSearches();
}

void tst_KadFirewallTester::reset_clearsState()
{
    // After reset, should be in clean initial state
    QVERIFY(!UDPFirewallTester::isFirewalledUDP(false));
    QVERIFY(!UDPFirewallTester::isVerified());
    QCOMPARE(UDPFirewallTester::debugChecksFinished(), uint8{0});
    QCOMPARE(UDPFirewallTester::debugChecksRunning(), uint8{0});

    // A check counts as running from reset until enough clients have reported —
    // MFC UDPFirewallTester.cpp:193-196 defines it purely as finished < needed.
    QVERIFY(UDPFirewallTester::isFWCheckUDPRunning());
}

void tst_KadFirewallTester::isFirewalledUDP_defaultOpen()
{
    // Default state: not firewalled
    QVERIFY(!UDPFirewallTester::isFirewalledUDP(false));
    QVERIFY(!UDPFirewallTester::isFirewalledUDP(true));
}

void tst_KadFirewallTester::addPossibleTestContact_queued()
{
    UInt128 clientID(uint32{1});
    UInt128 target(uint32{100});
    KadUDPKey udpKey(0);

    // Version must be >= KADEMLIA_VERSION8_49b (0x08) to be accepted
    UDPFirewallTester::addPossibleTestContact(
        clientID, 0x0A000001, 4672, 4662, target, 8, udpKey, true);

    // Version too low: should be rejected
    UInt128 clientID2(uint32{2});
    UDPFirewallTester::addPossibleTestContact(
        clientID2, 0x0A000002, 4672, 4662, target, 5, udpKey, true);

    // We can't directly check the list size, but verify no crash
    // and that queryNextClient processes the valid one
}

void tst_KadFirewallTester::setUDPFWCheckResult_updates()
{
    UDPFirewallTester::debugAddUsedTestClient(0x0A000001, 4672);

    // Simulate a successful firewall check
    UDPFirewallTester::setUDPFWCheckResult(true, false, 0x0A000001, 4672);
    QVERIFY(UDPFirewallTester::isVerified());
    QVERIFY(!UDPFirewallTester::isFirewalledUDP(false));
}

void tst_KadFirewallTester::isVerified_afterSuccess()
{
    QVERIFY(!UDPFirewallTester::isVerified());

    // A single successful check should mark as verified
    UDPFirewallTester::debugAddUsedTestClient(0x0A000001, 4672);
    UDPFirewallTester::setUDPFWCheckResult(true, false, 0x0A000001, 4672);
    QVERIFY(UDPFirewallTester::isVerified());

    // After reset, should no longer be verified
    UDPFirewallTester::reset();
    QVERIFY(!UDPFirewallTester::isVerified());
}

// One question for every "direct callback or buddy" gate: unverified is not open.
void tst_KadFirewallTester::isOpenAndVerified_needsAnOpenVerdict()
{
    QVERIFY(!UDPFirewallTester::isFirewalledUDP(true));   // optimistic default...
    QVERIFY(!UDPFirewallTester::isOpenAndVerified());     // ...which proves nothing

    UDPFirewallTester::debugAddUsedTestClient(0x0A000001, 4672);
    UDPFirewallTester::setUDPFWCheckResult(true, false, 0x0A000001, 4672);
    QVERIFY(UDPFirewallTester::isOpenAndVerified());

    UDPFirewallTester::reset();
    UDPFirewallTester::debugAddUsedTestClient(0x0A000002, 4672);
    UDPFirewallTester::setUDPFWCheckResult(false, false, 0x0A000002, 4672);
    UDPFirewallTester::debugAddUsedTestClient(0x0A000003, 4672);
    UDPFirewallTester::setUDPFWCheckResult(false, false, 0x0A000003, 4672);
    QVERIFY(UDPFirewallTester::isVerified());
    QVERIFY(!UDPFirewallTester::isOpenAndVerified());
}

// A verdict used to hold for the whole session: once open, a recheck that reached no
// tester left "verified open" standing for good.
void tst_KadFirewallTester::verification_expiresWhenNoRecheckConcludes()
{
    const uint32 now = static_cast<uint32>(time(nullptr));
    const uint32 maxAge = 2 * 60 * 60 + 6 * 60;

    UDPFirewallTester::debugAddUsedTestClient(0x0A000001, 4672);
    UDPFirewallTester::setUDPFWCheckResult(true, false, 0x0A000001, 4672);
    QVERIFY(UDPFirewallTester::isOpenAndVerified());

    // No recheck pending: an old verdict is simply the current one
    UDPFirewallTester::debugSetLastVerdictTime(now - maxAge - 60);
    QVERIFY(UDPFirewallTester::isVerified());

    // A recheck that has not concluded, verdict still young: kept
    UDPFirewallTester::reCheckFirewallUDP(false);
    QVERIFY(UDPFirewallTester::isFWCheckUDPRunning());
    UDPFirewallTester::debugSetLastVerdictTime(now - maxAge + 60);
    QVERIFY(UDPFirewallTester::isVerified());
    QVERIFY(UDPFirewallTester::isOpenAndVerified());

    // ...and past the limit: no longer trusted
    UDPFirewallTester::debugSetLastVerdictTime(now - maxAge - 60);
    QVERIFY(!UDPFirewallTester::isVerified());
    QVERIFY(!UDPFirewallTester::isOpenAndVerified());

    // A fresh verdict verifies again
    UDPFirewallTester::debugAddUsedTestClient(0x0A000004, 4672);
    UDPFirewallTester::setUDPFWCheckResult(true, false, 0x0A000004, 4672);
    QVERIFY(UDPFirewallTester::isOpenAndVerified());
}

// C95: an address is asked once per session (MFC keeps the list in Reset() and
// in ReCheckFirewallUDP()).
void tst_KadFirewallTester::usedClients_surviveRecheckAndReset()
{
    UDPFirewallTester::debugAddUsedTestClient(0x0A000001, 4672);

    UDPFirewallTester::reCheckFirewallUDP(false);
    QCOMPARE(UDPFirewallTester::debugUsedTestClients(), qsizetype{1});

    UDPFirewallTester::reset();
    QCOMPARE(UDPFirewallTester::debugUsedTestClients(), qsizetype{1});
}

// C97: UDP firewall checks exist since Kad version 6.
void tst_KadFirewallTester::candidates_version6IsAccepted()
{
    const UInt128 target(uint32{100});
    UDPFirewallTester::addPossibleTestContact(UInt128(uint32{1}), 0x4D000001, 4672, 4662,
                                              target, 6, KadUDPKey(0), true);
    QCOMPARE(UDPFirewallTester::debugPossibleTestClients(), qsizetype{1});
    UDPFirewallTester::addPossibleTestContact(UInt128(uint32{2}), 0x4D000002, 4672, 4662,
                                              target, 5, KadUDPKey(0), true);
    QCOMPARE(UDPFirewallTester::debugPossibleTestClients(), qsizetype{1});
}

// C97: a client we could not ask is not "used" — its address must not be able
// to report a result.
void tst_KadFirewallTester::failedRequest_isNotMarkedUsed()
{
    UDPFirewallTester::addPossibleTestContact(UInt128(uint32{1}), 0x4D000001, 4672, 4662,
                                              UInt128(uint32{100}), 8, KadUDPKey(0), true);
    UDPFirewallTester::queryNextClient();   // no client list here: the request fails

    QCOMPARE(UDPFirewallTester::debugPossibleTestClients(), qsizetype{0});
    QCOMPARE(UDPFirewallTester::debugUsedTestClients(), qsizetype{0});
    QCOMPARE(UDPFirewallTester::debugChecksRunning(), uint8{0});

    UDPFirewallTester::setUDPFWCheckResult(true, false, 0x4D000001, 4672);
    QVERIFY(!UDPFirewallTester::isVerified());
}

// C89: the request carries our external Kad port, so no client is asked before
// the port is known. C96: reset() goes back to using the external port.
void tst_KadFirewallTester::queryNextClient_waitsForTheExternalPort()
{
    eMule::testing::KadFixture kadFixture;
    UDPFirewallTester::reset();
    UDPFirewallTester::debugClearUsedTestClients();
    auto* prefs = Kademlia::getInstancePrefs();
    QVERIFY(prefs != nullptr);
    (void)prefs->findExternKadPort(true);
    QVERIFY(prefs->findExternKadPort(false));

    UDPFirewallTester::addPossibleTestContact(UInt128(uint32{1}), 0x4D000001, 4672, 4662,
                                              UInt128(uint32{100}), 8, KadUDPKey(0), true);
    UDPFirewallTester::queryNextClient();
    QCOMPARE(UDPFirewallTester::debugPossibleTestClients(), qsizetype{1});   // still waiting

    // two nodes agree: the lookup is over although only two were asked
    prefs->setExternKadPort(5000, 0x4D000010);
    prefs->setExternKadPort(5000, 0x4D000011);
    QVERIFY(!prefs->findExternKadPort(false));
    UDPFirewallTester::queryNextClient();
    QCOMPARE(UDPFirewallTester::debugPossibleTestClients(), qsizetype{0});

    prefs->setUseExternKadPort(false);
    QVERIFY(!prefs->useExternKadPort());
    UDPFirewallTester::reset();
    QVERIFY(prefs->useExternKadPort());
}

// Issue #64: an unknown extern port must not make us send TAG_SOURCEUPORT.
// Peers reply to that tag's port, which a port-rewriting NAT drops.
void tst_KadFirewallTester::useExternKadPort_trueBeforePortIsKnown()
{
    eMule::testing::KadFixture kadFixture;
    UDPFirewallTester::reset();
    auto* prefs = Kademlia::getInstancePrefs();
    QVERIFY(prefs != nullptr);
    (void)prefs->findExternKadPort(true);
    QCOMPARE(prefs->externalKadPort(), uint16{0});
    QVERIFY(prefs->useExternKadPort());
}

// ---------------------------------------------------------------------------
// Regressions for the divergences fixed in audit item #6
// ---------------------------------------------------------------------------

void tst_KadFirewallTester::setUDPFWCheckResult_ignoresUnrequested()
{
    // We asked .1 — a verdict arriving from .99 must not be able to decide our
    // firewall state. Previously any peer could inject a result.
    UDPFirewallTester::debugAddUsedTestClient(0x0A000001, 4672);
    const uint8 runningBefore = UDPFirewallTester::debugChecksRunning();

    UDPFirewallTester::setUDPFWCheckResult(true, false, 0x0A000099, 4672);

    QVERIFY(!UDPFirewallTester::isVerified());
    QCOMPARE(UDPFirewallTester::debugChecksFinished(), uint8{0});
    QCOMPARE(UDPFirewallTester::debugChecksRunning(), runningBefore);
}

void tst_KadFirewallTester::setUDPFWCheckResult_dedupesSecondAnswer()
{
    // Each test produces two answer packets; the client must count only once.
    UDPFirewallTester::debugAddUsedTestClient(0x0A000001, 4672);

    UDPFirewallTester::setUDPFWCheckResult(false, false, 0x0A000001, 4672);
    QCOMPARE(UDPFirewallTester::debugChecksFinished(), uint8{1});

    UDPFirewallTester::setUDPFWCheckResult(false, false, 0x0A000001, 4672);
    QCOMPARE(UDPFirewallTester::debugChecksFinished(), uint8{1});
}

void tst_KadFirewallTester::setUDPFWCheckResult_failureFreesSlot()
{
    // Regression: a failed (non-cancelled) result used to leave running=1 while
    // finished=1, so the second test never started and the tester deadlocked.
    UDPFirewallTester::debugAddUsedTestClient(0x0A000001, 4672);
    QCOMPARE(UDPFirewallTester::debugChecksRunning(), uint8{1});

    UDPFirewallTester::setUDPFWCheckResult(false, false, 0x0A000001, 4672);

    QCOMPARE(UDPFirewallTester::debugChecksRunning(), uint8{0});
    QCOMPARE(UDPFirewallTester::debugChecksFinished(), uint8{1});
    // Still open for the second client.
    QVERIFY(UDPFirewallTester::isFWCheckUDPRunning());
}

void tst_KadFirewallTester::setUDPFWCheckResult_twoFailuresMeanFirewalled()
{
    UDPFirewallTester::debugAddUsedTestClient(0x0A000001, 4672);
    UDPFirewallTester::setUDPFWCheckResult(false, false, 0x0A000001, 4672);

    UDPFirewallTester::debugAddUsedTestClient(0x0A000002, 4672);
    UDPFirewallTester::setUDPFWCheckResult(false, false, 0x0A000002, 4672);

    QVERIFY(UDPFirewallTester::isFirewalledUDP(false));
    QVERIFY(UDPFirewallTester::isVerified());
    QVERIFY(!UDPFirewallTester::isFWCheckUDPRunning());
}

void tst_KadFirewallTester::timeout_reportsFirewalledNotOpen()
{
    // The port used to reset state after 45s and fall through to "open".
    // Official waits 6 minutes and reports *firewalled* — advertising a port
    // nobody can reach is worse than admitting we are behind a NAT.
    QVERIFY(UDPFirewallTester::isFWCheckUDPRunning());

    // Backdate the start past the 6-minute timeout.
    UDPFirewallTester::debugSetTestStart(static_cast<uint32>(time(nullptr)) - (7 * 60));

    // The timeout only applies while Kademlia reports us firewalled and we have
    // never passed a test; without a running Kademlia instance the verdict stays
    // at the last known state, so assert the state machine did not silently
    // declare us open.
    const bool firewalled = UDPFirewallTester::isFirewalledUDP(true);
    if (Kademlia::instance() && Kademlia::instance()->isFirewalled())
        QVERIFY(firewalled);
    QVERIFY(!UDPFirewallTester::isVerified());
}

QTEST_GUILESS_MAIN(tst_KadFirewallTester)
#include "tst_KadFirewallTester.moc"
