/// @file tst_Kademlia.cpp
/// @brief Tests for Kademlia.h — main DHT engine.

#include "TestHelpers.h"

#include "app/AppContext.h"
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
    void instanceAvailableBeforeStart();
    void startStop_lifecycle();
    void isConnected_requiresContact();
    void appContextIsConnected_satisfiedByKadAlone();
    void publicIP_kadOutranksServer();
    void publicIP_usesServerWhenKadHasNone();
    void publicIP_kadValueIsConvertedToED2KOrder();
    void publicIP_ignoreKadIPReadsServerValue();
    void publicIP_kadIgnoredUntilConnected();
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

// s_instance is set in the constructor (not in start()), so a manual connect
// from the GUI/IPC can reach Kademlia::instance() before Kad has been started —
// this is what stops the daemon returning 503 "Kademlia unavailable" when
// autoConnect is off. The constructed-but-not-running state must look exactly
// like the stopped state that every instance() caller already tolerates.
void tst_Kademlia::instanceAvailableBeforeStart()
{
    Kademlia kad;
    QCOMPARE(Kademlia::instance(), &kad);  // addressable without start()
    QVERIFY(!kad.isRunning());
    QVERIFY(!kad.isConnected());
    QVERIFY(kad.getPrefs() == nullptr);       // sub-objects still null, as when stopped
    QVERIFY(kad.getRoutingZone() == nullptr);
    QVERIFY(kad.getUDPListener() == nullptr);
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

// theApp.isConnected() means "connected to *any* network", matching MFC
// CemuleApp::IsConnected() (Emule.cpp:1122). Kad alone must satisfy it — that is
// what keeps server UDP alive (stat pings, global search) in Kad-only mode via
// ServerConnect::sendUDPPacket(). Lives here rather than in a ServerList test
// because the Kad half is the arm that changed and this fixture already has it.
void tst_Kademlia::appContextIsConnected_satisfiedByKadAlone()
{
    QVERIFY2(theApp.serverConnect == nullptr,
             "fixture assumes no ED2K side, so Kad is the only arm under test");
    QVERIFY(!theApp.isConnected());  // neither network up

    Kademlia kad;
    kad.start();
    QVERIFY(kad.isRunning());
    QVERIFY(kad.getPrefs() != nullptr);

    // Running is not enough — Kad is only "connected" once it has had contact.
    QVERIFY(!kad.isConnected());
    QVERIFY(!theApp.isConnected());

    kad.getPrefs()->setLastContact();
    QVERIFY(kad.isConnected());
    QVERIFY(theApp.isConnected());  // Kad alone, no ED2K server

    kad.stop();
    QVERIFY(!theApp.isConnected());
}

// ---------------------------------------------------------------------------
// theApp.publicIP() priority: Kad -> ED2K server -> peer.
//
// Kad wins because KadPrefs::setIPAddress() only commits an address two
// independent nodes agreed on, while a server's claim is one unverified
// assertion. This deliberately inverts MFC (Emule.cpp:1542), where the stored
// ED2K value wins and Kad is only consulted when it is 0 — so these cases are
// what stops a future "restore MFC parity" pass from silently flipping it back.
//
// The value feeds Server::setServerKeyUDP()/serverKeyUDP(), so a wrong answer
// here means server UDP keys never match and stat pings never get obfuscated.
// ---------------------------------------------------------------------------

namespace {
/// theApp.publicIP() is process-global; restore it so case order stays free.
/// Reads publicIP(true) because setPublicIP() writes the ED2K-only slot —
/// restoring the Kad-inclusive value would store Kad's address as an ED2K one.
class ScopedPublicIP {
public:
    explicit ScopedPublicIP(uint32 ip) : m_saved(theApp.publicIP(true)) { theApp.setPublicIP(ip); }
    ~ScopedPublicIP() { theApp.setPublicIP(m_saved); }
    ScopedPublicIP(const ScopedPublicIP&) = delete;
    ScopedPublicIP& operator=(const ScopedPublicIP&) = delete;
private:
    uint32 m_saved;
};

// 10.20.30.40 in each convention. Kad puts the first octet in the MSB
// (see ipToString, KadMiscUtils.cpp:20); ED2K puts it in the LSB (ipstr,
// OtherFunctions.cpp:199). Deliberately asymmetric so a missing or doubled
// byte swap cannot pass by coincidence.
constexpr uint32 kKadOrderIP  = 0x0A141E28;
constexpr uint32 kED2KOrderIP = 0x281E140A;
} // namespace

void tst_Kademlia::publicIP_kadOutranksServer()
{
    ScopedPublicIP serverIP(0x0100007F);  // an ED2K-derived value is present...

    Kademlia kad;
    kad.start();
    kad.getPrefs()->setLastContact();
    QVERIFY(kad.isConnected());

    // setIPAddress needs the same value twice before it commits (KadPrefs.cpp:80).
    kad.getPrefs()->setIPAddress(kKadOrderIP);
    kad.getPrefs()->setIPAddress(kKadOrderIP);
    QCOMPARE(kad.getIPAddress(), kKadOrderIP);

    // ...and Kad still wins.
    QCOMPARE(theApp.publicIP(), kED2KOrderIP);

    kad.stop();
}

void tst_Kademlia::publicIP_usesServerWhenKadHasNone()
{
    ScopedPublicIP serverIP(kED2KOrderIP);

    Kademlia kad;
    kad.start();
    kad.getPrefs()->setLastContact();
    QVERIFY(kad.isConnected());

    // Explicit, not assumed: KadPrefs persists to preferencesKad.dat and reloads
    // it on start(), so an address committed by an earlier case survives into
    // this one. Setting 0 commits immediately (the ip == 0 branch of the two-step
    // check in KadPrefs.cpp:80).
    kad.getPrefs()->setIPAddress(0);
    QCOMPARE(kad.getIPAddress(), uint32{0});  // connected, but no external IP

    // Rank 2 applies: connected-but-IP-less Kad must not mask the ED2K value.
    QCOMPARE(theApp.publicIP(), kED2KOrderIP);

    kad.stop();
}

void tst_Kademlia::publicIP_kadValueIsConvertedToED2KOrder()
{
    ScopedPublicIP noServerIP(0);

    Kademlia kad;
    kad.start();
    kad.getPrefs()->setLastContact();
    kad.getPrefs()->setIPAddress(kKadOrderIP);
    kad.getPrefs()->setIPAddress(kKadOrderIP);

    // The whole point: assert the exact value, not merely non-zero. A missing
    // htonl yields a plausible-looking address that silently never matches a
    // server UDP key — indistinguishable from the bug this all exists to fix.
    QCOMPARE(theApp.publicIP(), kED2KOrderIP);
    QVERIFY2(theApp.publicIP() != kKadOrderIP, "Kad host order leaked into an ED2K value");

    kad.stop();
}

void tst_Kademlia::publicIP_ignoreKadIPReadsServerValue()
{
    ScopedPublicIP serverIP(0x0100007F);

    Kademlia kad;
    kad.start();
    kad.getPrefs()->setLastContact();
    kad.getPrefs()->setIPAddress(kKadOrderIP);
    kad.getPrefs()->setIPAddress(kKadOrderIP);

    // The escape hatch for callers that must agree with what a specific server
    // sees — notably server UDP key stamping, if Kad and the server ever differ.
    QCOMPARE(theApp.publicIP(true), uint32{0x0100007F});

    kad.stop();
}

void tst_Kademlia::publicIP_kadIgnoredUntilConnected()
{
    ScopedPublicIP serverIP(kED2KOrderIP);

    Kademlia kad;
    kad.start();
    kad.getPrefs()->setIPAddress(kKadOrderIP);
    kad.getPrefs()->setIPAddress(kKadOrderIP);
    QCOMPARE(kad.getIPAddress(), kKadOrderIP);  // Kad *has* an address...
    QVERIFY(!kad.isConnected());                // ...but no contact yet

    // A stale IP from a Kad session that never established contact must not
    // outrank a live server's view.
    QCOMPARE(theApp.publicIP(), kED2KOrderIP);

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
