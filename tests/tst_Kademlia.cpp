/// @file tst_Kademlia.cpp
/// @brief Tests for Kademlia.h — main DHT engine.

#include "TestHelpers.h"

#include "kademlia/Kademlia.h"
#include "kademlia/KadIndexed.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPListener.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace eMule::kad;

class tst_Kademlia : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void construct_notRunning();
    void startStop_lifecycle();
    void isConnected_requiresContact();
    void bootstrap_delegatesToListener();
    void processPacket_dispatches();
};

void tst_Kademlia::cleanup()
{
    SearchManager::stopAllSearches();
}

void tst_Kademlia::construct_notRunning()
{
    Kademlia kad;
    QVERIFY(!kad.isRunning());
    QVERIFY(!kad.isConnected());
    QVERIFY(kad.isFirewalled()); // not running → considered firewalled
    QCOMPARE(kad.getKademliaUsers(), uint32{0});
    QCOMPARE(kad.getKademliaFiles(), uint32{0});
    QCOMPARE(kad.getTotalStoreKey(), uint32{0});
    QCOMPARE(kad.getTotalStoreSrc(), uint32{0});
    QCOMPARE(kad.getTotalStoreNotes(), uint32{0});
    QVERIFY(kad.getPrefs() == nullptr);
    QVERIFY(kad.getRoutingZone() == nullptr);
    QVERIFY(kad.getUDPListener() == nullptr);
    QVERIFY(kad.getIndexed() == nullptr);
}

void tst_Kademlia::startStop_lifecycle()
{
    Kademlia kad;
    QSignalSpy startedSpy(&kad, &Kademlia::started);
    QSignalSpy stoppedSpy(&kad, &Kademlia::stopped);

    kad.start();  // port 0 = OS-assigned random port
    QVERIFY(kad.isRunning());
    QVERIFY(kad.getPrefs() != nullptr);
    QVERIFY(kad.getRoutingZone() != nullptr);
    QVERIFY(kad.getUDPListener() != nullptr);
    QVERIFY(kad.getIndexed() != nullptr);
    QCOMPARE(startedSpy.count(), 1);

    kad.stop();
    QVERIFY(!kad.isRunning());
    QVERIFY(kad.getRoutingZone() == nullptr);
    QVERIFY(kad.getUDPListener() == nullptr);
    QVERIFY(kad.getIndexed() == nullptr);
    QCOMPARE(stoppedSpy.count(), 1);
}

void tst_Kademlia::isConnected_requiresContact()
{
    Kademlia kad;
    kad.start();
    QVERIFY(kad.isRunning());

    // Not connected until hasHadContact() is true
    QVERIFY(!kad.isConnected());

    kad.stop();
}

void tst_Kademlia::bootstrap_delegatesToListener()
{
    Kademlia kad;
    kad.start();

    QVERIFY(kad.getUDPListener() != nullptr);

    // Bootstrap sends a packet via the socket — verify it does not crash.
    kad.bootstrap(0x0A000001, 4672);

    kad.stop();
}

void tst_Kademlia::processPacket_dispatches()
{
    Kademlia kad;
    kad.start();

    // Send an unknown opcode — should not crash
    uint8 data[] = {0xFF};
    KadUDPKey senderKey(0);
    kad.processPacket(data, sizeof(data), 0x0A000001, 4672, false, senderKey);

    QVERIFY(true); // no crash

    kad.stop();
}

QTEST_GUILESS_MAIN(tst_Kademlia)
#include "tst_Kademlia.moc"
