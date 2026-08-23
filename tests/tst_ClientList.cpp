/// @file tst_ClientList.cpp
/// @brief Tests for client/ClientList — client management, find operations, banning.

#include "TestFixtures.h"
#include "TestHelpers.h"
#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "net/Address.h"
#include "net/ClientReqSocket.h"
#include "utils/ByteOrder.h"
#include "utils/OtherFunctions.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>

#include <cstring>

Q_DECLARE_METATYPE(eMule::UpDownClient*)

using namespace eMule;

class tst_ClientList : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<eMule::UpDownClient*>("UpDownClient*");
    }
    void addClient_basic();
    void addClient_duplicate();
    void addClient_skipDupTest();
    void removeClient_basic();
    void removeClient_notInList();
    void isValidClient_true();
    void isValidClient_false();
    void deleteAll();
    void findByIP_single();
    void findByIP_withPort();
    void findByConnIP();
    void findByUserHash_exact();
    void findByUserHash_fallback();
    void findByIP_UDP();
    void findByServerID();
    void findByUserID_KadPort();
    void findByIP_KadPort();
    void findByIP_notFound();
    void addBannedClient();
    void isBannedClient_true();
    void removeBannedClient();
    void bannedCount();
    void signal_clientAdded();
    void signal_clientRemoved();
    void globalDeadSourceList_initialized();

    // attachToAlreadyKnown — MFC CClientList::AttachToAlreadyKnown
    void attach_matchesByUserHash();
    void attach_matchesByAddressWithoutHash();
    void attach_noMatchReturnsNull();
    void attach_skipsTheNewClientItself();
    void attach_leavesNewClientInList();
    void attach_rehomesSocketToKnownClient();
    void attach_refusesAndBansIdentifiedImpostor();
    void attach_refusesUnidentifiedCollisionWithoutBanning();

    // Kad state machine — MFC srchybrid/ClientList.cpp:470-620
    void processKadList_clearsEveryStateWhenKadIsNotRunning();
    void processKadList_adoptsConnectedBuddyAndDropsOthers();
    void processKadList_dropsAnOpenBuddy();
    void processKadList_detectsBuddyLoss();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void fillHash(uint8* hash, uint8 pattern)
{
    std::memset(hash, pattern, 16);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void tst_ClientList::addClient_basic()
{
    ClientList list;
    UpDownClient client;
    list.addClient(&client);
    QCOMPARE(list.clientCount(), 1);
}

void tst_ClientList::addClient_duplicate()
{
    ClientList list;
    UpDownClient client;
    list.addClient(&client);
    list.addClient(&client);  // same pointer, should not add twice
    QCOMPARE(list.clientCount(), 1);
}

void tst_ClientList::addClient_skipDupTest()
{
    ClientList list;
    UpDownClient client;
    list.addClient(&client);
    list.addClient(&client, true);  // skipDupTest=true
    QCOMPARE(list.clientCount(), 2);
}

void tst_ClientList::removeClient_basic()
{
    ClientList list;
    UpDownClient client;
    list.addClient(&client);
    QCOMPARE(list.clientCount(), 1);
    list.removeClient(&client);
    QCOMPARE(list.clientCount(), 0);
}

void tst_ClientList::removeClient_notInList()
{
    ClientList list;
    UpDownClient client;
    list.removeClient(&client);  // should be no-op
    QCOMPARE(list.clientCount(), 0);
}

void tst_ClientList::isValidClient_true()
{
    ClientList list;
    UpDownClient client;
    list.addClient(&client);
    QVERIFY(list.isValidClient(&client));
}

void tst_ClientList::isValidClient_false()
{
    ClientList list;
    UpDownClient client;
    QVERIFY(!list.isValidClient(&client));
}

void tst_ClientList::deleteAll()
{
    ClientList list;
    UpDownClient a, b, c;
    list.addClient(&a);
    list.addClient(&b);
    list.addClient(&c);
    QCOMPARE(list.clientCount(), 3);
    list.deleteAll();
    QCOMPARE(list.clientCount(), 0);
}

void tst_ClientList::findByIP_single()
{
    ClientList list;
    UpDownClient client;
    client.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    list.addClient(&client);
    QCOMPARE(list.findByIP(0xC0A80001u), &client);
}

void tst_ClientList::findByIP_withPort()
{
    ClientList list;
    UpDownClient a, b;
    a.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    a.setUserPort(4662);
    b.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    b.setUserPort(4663);
    list.addClient(&a);
    list.addClient(&b);

    QCOMPARE(list.findByIP(0xC0A80001u, 4663), &b);
    QCOMPARE(list.findByIP(0xC0A80001u, 9999), nullptr);
}

void tst_ClientList::findByConnIP()
{
    ClientList list;
    UpDownClient client;
    client.setConnectAddress(Address::fromNetworkOrder(0xC0A80002));
    client.setUserPort(4662);
    list.addClient(&client);
    QCOMPARE(list.findByConnIP(0xC0A80002u, 4662), &client);
}

void tst_ClientList::findByUserHash_exact()
{
    ClientList list;
    UpDownClient a, b;
    uint8 hash[16];
    fillHash(hash, 0xAA);
    a.setUserHash(hash);
    a.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    a.setUserPort(4662);
    b.setUserHash(hash);
    b.setUserAddress(Address::fromNetworkOrder(0xC0A80002));
    b.setUserPort(4663);
    list.addClient(&a);
    list.addClient(&b);

    // Should find exact IP+port match
    QCOMPARE(list.findByUserHash(hash, 0xC0A80002, 4663), &b);
}

void tst_ClientList::findByUserHash_fallback()
{
    ClientList list;
    UpDownClient a;
    uint8 hash[16];
    fillHash(hash, 0xBB);
    a.setUserHash(hash);
    a.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    a.setUserPort(4662);
    list.addClient(&a);

    // No IP+port match → should fall back to hash-only
    QCOMPARE(list.findByUserHash(hash, 0xDEADBEEF, 9999), &a);
}

void tst_ClientList::findByIP_UDP()
{
    ClientList list;
    UpDownClient v4;
    const Address addr = Address::fromString(QStringLiteral("10.20.30.40"));
    v4.setUserAddress(addr);
    v4.setUDPPort(4672);
    list.addClient(&v4);

    // Deliberately non-palindromic: the old uint32 API compared network order while
    // every caller passed the host-order value out of an Endpoint, so this never matched.
    QCOMPARE(list.findByEndpoint_UDP(addr, 4672), &v4);
    QCOMPARE(list.findByEndpoint_UDP(addr, 4673), nullptr);

    // An IPv6 peer is findable at all — the uint32 form could not represent one.
    UpDownClient v6;
    const Address addr6 = Address::fromString(QStringLiteral("2001:db8::1"));
    v6.setUserAddress(addr6);
    v6.setUDPPort(4672);
    list.addClient(&v6);
    QCOMPARE(list.findByEndpoint_UDP(addr6, 4672), &v6);
    // Same port, different family — must not cross-match.
    QCOMPARE(list.findByEndpoint_UDP(addr, 4672), &v4);
}

void tst_ClientList::findByServerID()
{
    ClientList list;
    UpDownClient client;
    // Server ID search converts ED2K user ID to hybrid with ntohl
    const uint32 ed2kId = 0x0D0C0B0A;
    client.setServerAddress(Address::fromNetworkOrder(0x01020304));
    client.setUserIDHybrid(ntohl(ed2kId));
    list.addClient(&client);
    QCOMPARE(list.findByServerID(0x01020304u, ed2kId), &client);
}

void tst_ClientList::findByUserID_KadPort()
{
    ClientList list;
    UpDownClient client;
    client.setUserIDHybrid(0x0A0B0C0D);
    client.setKadPort(4672);
    list.addClient(&client);
    QCOMPARE(list.findByUserID_KadPort(0x0A0B0C0D, 4672), &client);
}

void tst_ClientList::findByIP_KadPort()
{
    ClientList list;
    UpDownClient client;
    client.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    client.setKadPort(4672);
    list.addClient(&client);
    QCOMPARE(list.findByIP_KadPort(0xC0A80001u, 4672), &client);
}

void tst_ClientList::findByIP_notFound()
{
    ClientList list;
    UpDownClient client;
    client.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    list.addClient(&client);
    QCOMPARE(list.findByIP(0xDEADBEEFu), nullptr);
}

void tst_ClientList::addBannedClient()
{
    ClientList list;
    list.addBannedClient(Address::fromNetworkOrder(0xC0A80001));
    QCOMPARE(list.bannedCount(), 1);
}

void tst_ClientList::isBannedClient_true()
{
    ClientList list;
    list.addBannedClient(Address::fromNetworkOrder(0xC0A80001));
    QVERIFY(list.isBannedClient(Address::fromNetworkOrder(0xC0A80001)));
    QVERIFY(!list.isBannedClient(Address::fromNetworkOrder(0xC0A80002)));
}

void tst_ClientList::removeBannedClient()
{
    ClientList list;
    list.addBannedClient(Address::fromNetworkOrder(0xC0A80001));
    QVERIFY(list.isBannedClient(Address::fromNetworkOrder(0xC0A80001)));
    list.removeBannedClient(Address::fromNetworkOrder(0xC0A80001));
    QVERIFY(!list.isBannedClient(Address::fromNetworkOrder(0xC0A80001)));
    QCOMPARE(list.bannedCount(), 0);
}

void tst_ClientList::bannedCount()
{
    ClientList list;
    QCOMPARE(list.bannedCount(), 0);
    list.addBannedClient(Address::fromNetworkOrder(0xC0A80001));
    list.addBannedClient(Address::fromNetworkOrder(0xC0A80002));
    QCOMPARE(list.bannedCount(), 2);
    list.removeAllBannedClients();
    QCOMPARE(list.bannedCount(), 0);
}

void tst_ClientList::signal_clientAdded()
{
    ClientList list;
    UpDownClient client;
    QSignalSpy spy(&list, &ClientList::clientAdded);
    list.addClient(&client);
    QCOMPARE(spy.count(), 1);
}

void tst_ClientList::signal_clientRemoved()
{
    ClientList list;
    UpDownClient client;
    list.addClient(&client);
    QSignalSpy spy(&list, &ClientList::clientRemoved);
    list.removeClient(&client);
    QCOMPARE(spy.count(), 1);
}

void tst_ClientList::globalDeadSourceList_initialized()
{
    ClientList list;
    // The public DeadSourceList member should be usable
    DeadSourceKey key;
    key.userID = 0x01020304;
    key.port = 4662;
    list.globalDeadSourceList.addDeadSource(key, false);
    QVERIFY(list.globalDeadSourceList.isDeadSource(key));
}

// ---------------------------------------------------------------------------
// attachToAlreadyKnown
//
// These drive the matcher directly. A null `sender` exercises matching alone, which is
// why the function deliberately does not delete or de-list anything: stack-allocated
// clients stay valid and the socket-free cases need no networking at all.
// ---------------------------------------------------------------------------

void tst_ClientList::attach_matchesByUserHash()
{
    ClientList list;
    UpDownClient known, incoming;
    uint8 hash[16];
    fillHash(hash, 0xAA);

    // Same identity, different address/port — only the user hash can match these two.
    known.setUserHash(hash);
    known.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    known.setUserPort(4662);
    incoming.setUserHash(hash);
    incoming.setUserAddress(Address::fromNetworkOrder(0x0A0A0A0A));
    incoming.setUserPort(5000);

    list.addClient(&known);
    list.addClient(&incoming);

    QCOMPARE(list.attachToAlreadyKnown(&incoming, nullptr), &known);
}

void tst_ClientList::attach_matchesByAddressWithoutHash()
{
    ClientList list;
    UpDownClient known, incoming;

    // No user hashes at all, so the address/port branch is the only one that can fire.
    known.setUserAddress(Address::fromNetworkOrder(0xC0A80005));
    known.setUserPort(4662);
    incoming.setUserAddress(Address::fromNetworkOrder(0xC0A80005));
    incoming.setUserPort(4662);

    list.addClient(&known);
    list.addClient(&incoming);

    QCOMPARE(list.attachToAlreadyKnown(&incoming, nullptr), &known);
}

void tst_ClientList::attach_noMatchReturnsNull()
{
    ClientList list;
    UpDownClient known, incoming;
    uint8 hashA[16], hashB[16];
    fillHash(hashA, 0xAA);
    fillHash(hashB, 0xBB);

    known.setUserHash(hashA);
    known.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    known.setUserPort(4662);
    incoming.setUserHash(hashB);
    incoming.setUserAddress(Address::fromNetworkOrder(0x0A0A0A0A));
    incoming.setUserPort(5000);

    list.addClient(&known);
    list.addClient(&incoming);

    QCOMPARE(list.attachToAlreadyKnown(&incoming, nullptr), nullptr);
}

void tst_ClientList::attach_skipsTheNewClientItself()
{
    // handleIncomingConnection() adds the throwaway before its hello arrives, so it is
    // always in the list when we look. It must never match itself — otherwise the scan
    // would short-circuit on its own address and never reach the real client.
    ClientList list;
    UpDownClient incoming;
    uint8 hash[16];
    fillHash(hash, 0xCC);
    incoming.setUserHash(hash);
    incoming.setUserAddress(Address::fromNetworkOrder(0xC0A80009));
    incoming.setUserPort(4662);

    list.addClient(&incoming);
    QCOMPARE(list.clientCount(), 1);

    QCOMPARE(list.attachToAlreadyKnown(&incoming, nullptr), nullptr);
}

void tst_ClientList::attach_leavesNewClientInList()
{
    // Contract: unlike MFC, this never deletes or de-lists newClient — the caller does,
    // because the only production caller runs inside newClient's own signal handler.
    ClientList list;
    UpDownClient known, incoming;
    uint8 hash[16];
    fillHash(hash, 0xDD);
    known.setUserHash(hash);
    known.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    known.setUserPort(4662);
    incoming.setUserHash(hash);
    incoming.setUserAddress(Address::fromNetworkOrder(0x0A0A0A0A));
    incoming.setUserPort(5000);

    list.addClient(&known);
    list.addClient(&incoming);

    QCOMPARE(list.attachToAlreadyKnown(&incoming, nullptr), &known);
    QCOMPARE(list.clientCount(), 2);
    QVERIFY(list.isValidClient(&incoming));
}

void tst_ClientList::attach_rehomesSocketToKnownClient()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    auto* sender = new ClientReqSocket();
    sender->connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(sender->waitForConnected(5000));

    ClientList list;
    UpDownClient known, incoming;
    uint8 hash[16];
    fillHash(hash, 0xEE);
    known.setUserHash(hash);
    known.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    known.setUserPort(4662);
    incoming.setUserHash(hash);
    incoming.setUserAddress(Address::fromNetworkOrder(0x0A0A0A0A));
    incoming.setUserPort(5000);
    incoming.wireIncomingSocket(sender);
    QCOMPARE(incoming.socket(), sender);

    list.addClient(&known);
    list.addClient(&incoming);

    QCOMPARE(list.attachToAlreadyKnown(&incoming, sender), &known);
    QCOMPARE(known.socket(), sender);          // the survivor now owns the connection
    QCOMPARE(incoming.socket(), nullptr);      // and the throwaway has let go

    known.setSocket(nullptr);
    sender->deleteLater();
    QCoreApplication::processEvents();
}

void tst_ClientList::attach_refusesAndBansIdentifiedImpostor()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    auto* knownSocket = new ClientReqSocket();
    knownSocket->connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(knownSocket->waitForConnected(5000));
    auto* sender = new ClientReqSocket();
    sender->connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(sender->waitForConnected(5000));

    ClientList list;
    theApp.clientList = &list;   // UpDownClient::ban() posts the ban through theApp

    UpDownClient known, incoming;
    uint8 hash[16];
    fillHash(hash, 0xAB);

    const uint32 knownIPNet = 0xC0A80001;
    known.setUserHash(hash);
    known.setUserAddress(Address::fromNetworkOrder(knownIPNet));
    known.setUserPort(4662);
    known.setSocket(knownSocket);

    // Secure identification pins the hash to this address, so a same-hash peer arriving
    // from anywhere else is the forger.
    ClientCredits credits(hash);
    uint8 pubKey[10];
    std::memset(pubKey, 0xBB, sizeof(pubKey));
    QVERIFY(credits.setSecureIdent(pubKey, 10));
    credits.verified(known.userAddress().toNetworkUint32());
    QCOMPARE(credits.currentIdentState(known.userAddress().toNetworkUint32()),
             IdentState::Identified);
    known.setCredits(&credits);

    incoming.setUserHash(hash);
    incoming.setUserAddress(Address::fromNetworkOrder(0x0A0A0A0A));
    incoming.setUserPort(5000);
    incoming.wireIncomingSocket(sender);

    list.addClient(&known);
    list.addClient(&incoming);

    QCOMPARE(list.attachToAlreadyKnown(&incoming, sender), nullptr);   // merge refused
    QCOMPARE(incoming.uploadState(), UploadState::Banned);
    QVERIFY(list.isBannedClient(incoming.connectAddress()));
    QCOMPARE(known.socket(), knownSocket);     // the identified client keeps its socket

    known.setSocket(nullptr);
    incoming.setSocket(nullptr);
    theApp.clientList = nullptr;
    knownSocket->deleteLater();
    sender->deleteLater();
    QCoreApplication::processEvents();
}

void tst_ClientList::attach_refusesUnidentifiedCollisionWithoutBanning()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    auto* knownSocket = new ClientReqSocket();
    knownSocket->connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(knownSocket->waitForConnected(5000));
    auto* sender = new ClientReqSocket();
    sender->connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(sender->waitForConnected(5000));

    ClientList list;
    theApp.clientList = &list;

    UpDownClient known, incoming;
    uint8 hash[16];
    fillHash(hash, 0xAC);

    // Same collision as above, but without secure identification there is no way to tell
    // which side is lying — so refuse the merge and ban nobody.
    known.setUserHash(hash);
    known.setUserAddress(Address::fromNetworkOrder(0xC0A80001));
    known.setUserPort(4662);
    known.setSocket(knownSocket);

    incoming.setUserHash(hash);
    incoming.setUserAddress(Address::fromNetworkOrder(0x0A0A0A0A));
    incoming.setUserPort(5000);
    incoming.wireIncomingSocket(sender);

    list.addClient(&known);
    list.addClient(&incoming);

    QCOMPARE(list.attachToAlreadyKnown(&incoming, sender), nullptr);
    QVERIFY(incoming.uploadState() != UploadState::Banned);
    QVERIFY(!list.isBannedClient(incoming.connectAddress()));

    known.setSocket(nullptr);
    incoming.setSocket(nullptr);
    theApp.clientList = nullptr;
    knownSocket->deleteLater();
    sender->deleteLater();
    QCoreApplication::processEvents();
}


// ===========================================================================
// Kad state machine — MFC srchybrid/ClientList.cpp:470-620
//
// The port kept every KadState and set them from the Kad handlers, but nothing ever drove
// the transitions: ClientList::process() only kept such clients alive. The buddy leg was
// therefore inert, and an established buddy link had no keep-alive.
// ===========================================================================

void tst_ClientList::processKadList_clearsEveryStateWhenKadIsNotRunning()
{
    ClientList list;

    auto* fwCheck = new UpDownClient();
    fwCheck->setKadState(KadState::QueuedFwCheck);
    list.addClient(fwCheck);

    auto* buddy = new UpDownClient();
    buddy->setKadState(KadState::ConnectedBuddy);
    list.addClient(buddy);

    // No Kad instance in this fixture, so nothing Kad-related can be pending.
    list.processKadList();

    QCOMPARE(fwCheck->kadState(), KadState::None);
    QCOMPARE(buddy->kadState(), KadState::None);
}

void tst_ClientList::processKadList_adoptsConnectedBuddyAndDropsOthers()
{
    // Kad has to be running, or the very first thing processKadList() does is clear every
    // pending Kad interaction — see the test above.
    eMule::testing::KadFixture kadFixture;

    ClientList list;

    auto* buddy = new UpDownClient();
    buddy->setUserAddress(Address::fromString(QStringLiteral("10.7.0.1")));
    buddy->setKadState(KadState::ConnectedBuddy);
    list.addClient(buddy);

    // A second candidate that arrived while the first was completing. One buddy at a time,
    // so this one is dropped rather than left queued behind a link we already have.
    auto* alsoWants = new UpDownClient();
    alsoWants->setUserAddress(Address::fromString(QStringLiteral("10.7.0.2")));
    alsoWants->setKadState(KadState::IncomingBuddy);
    list.addClient(alsoWants);

    list.setBuddy(buddy, BuddyStatus::Connected);
    list.processKadList();

    QCOMPARE(list.getBuddy(), buddy);
    QCOMPARE(list.buddyStatus(), BuddyStatus::Connected);
    QCOMPARE(alsoWants->kadState(), KadState::None);
}

void tst_ClientList::processKadList_dropsAnOpenBuddy()
{
    eMule::testing::KadFixture kadFixture;

    ClientList list;

    // A buddy relays callbacks for firewalled peers, which only makes sense while it is
    // itself firewalled. One that opened its port is no longer a relay.
    // MFC srchybrid/ClientList.cpp:614-617.
    auto* buddy = new UpDownClient();
    buddy->setUserAddress(Address::fromString(QStringLiteral("10.7.0.3")));
    buddy->setUserIDHybrid(0x0A070003u);              // High ID
    buddy->setKadState(KadState::ConnectedBuddy);
    list.addClient(buddy);
    list.setBuddy(buddy, BuddyStatus::Connected);

    QVERIFY(!buddy->hasLowID());
    list.processKadList();

    QCOMPARE(buddy->kadState(), KadState::None);
}

void tst_ClientList::processKadList_detectsBuddyLoss()
{
    eMule::testing::KadFixture kadFixture;

    ClientList list;

    auto* buddy = new UpDownClient();
    buddy->setUserAddress(Address::fromString(QStringLiteral("10.7.0.4")));
    buddy->setKadState(KadState::ConnectedBuddy);
    list.addClient(buddy);
    list.setBuddy(buddy, BuddyStatus::Connected);
    QCOMPARE(list.buddyStatus(), BuddyStatus::Connected);

    // The buddy went away. Loss is detected from the list itself, so it cannot depend on
    // whichever call site happened to clear m_buddy last.
    buddy->setKadState(KadState::None);
    list.processKadList();

    QCOMPARE(list.buddyStatus(), BuddyStatus::None);
    QVERIFY(list.getBuddy() == nullptr);
}

QTEST_MAIN(tst_ClientList)
#include "tst_ClientList.moc"
