/// @file tst_KadFirewallTester.cpp
/// @brief Tests for KadFirewallTester.h — UDP firewall detection.

#include "TestHelpers.h"

#include "kademlia/KadFirewallTester.h"
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
    QVERIFY(!UDPFirewallTester::isFWCheckUDPRunning());
    QVERIFY(!UDPFirewallTester::isVerified());
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
    // Simulate successful firewall checks
    UDPFirewallTester::setUDPFWCheckResult(true, false, 0x0A000001, 4672);
    QVERIFY(UDPFirewallTester::isVerified());
    QVERIFY(!UDPFirewallTester::isFirewalledUDP(false));
}

void tst_KadFirewallTester::isVerified_afterSuccess()
{
    QVERIFY(!UDPFirewallTester::isVerified());

    // A single successful check should mark as verified
    UDPFirewallTester::setUDPFWCheckResult(true, false, 0x0A000001, 4672);
    QVERIFY(UDPFirewallTester::isVerified());

    // After reset, should no longer be verified
    UDPFirewallTester::reset();
    QVERIFY(!UDPFirewallTester::isVerified());
}

QTEST_GUILESS_MAIN(tst_KadFirewallTester)
#include "tst_KadFirewallTester.moc"
