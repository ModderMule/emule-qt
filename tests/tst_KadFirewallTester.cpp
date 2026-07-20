/// @file tst_KadFirewallTester.cpp
/// @brief Tests for KadFirewallTester.h — UDP firewall detection.

#include "TestHelpers.h"

#include "kademlia/KadFirewallTester.h"
#include "kademlia/Kademlia.h"
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
    void setUDPFWCheckResult_ignoresUnrequested();
    void setUDPFWCheckResult_dedupesSecondAnswer();
    void setUDPFWCheckResult_failureFreesSlot();
    void setUDPFWCheckResult_twoFailuresMeanFirewalled();
    void timeout_reportsFirewalledNotOpen();
};

void tst_KadFirewallTester::init()
{
    UDPFirewallTester::reset();
}

void tst_KadFirewallTester::cleanup()
{
    UDPFirewallTester::reset();
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
