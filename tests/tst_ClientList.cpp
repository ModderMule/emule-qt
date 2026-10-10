/// @file tst_ClientList.cpp
/// @brief Tests for client/ClientList — client management, find operations, banning.

#include "TestFixtures.h"
#include "TestHelpers.h"
#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/PartFile.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadPrefs.h"
#include "transfer/DownloadQueue.h"
#include "utils/SafeFile.h"
#include "net/Address.h"
#include "net/ClientReqSocket.h"
#include "server/Server.h"
#include "server/ServerList.h"
#include "utils/Opcodes.h"
#include "kademlia/KadFirewallTester.h"
#include "utils/ByteOrder.h"
#include "utils/OtherFunctions.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>
#include <QtEndian>

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
    void handshakeFailures_banAtTheThreshold();
    void fruitlessLedger_secondSessionInTheWindowPauses();
    void fruitlessLedger_secondPauseQuarantines();
    void fruitlessLedger_tenEventsQuarantine();
    void fruitlessLedger_productiveSessionStartsOver();
    void fruitlessLedger_isBounded();
    void handshakeFailures_neverBanAListedServer();
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
    void requestBuddy_refusesFirewallCheckAndBannedNodes();
    void kadFirewallAck_oncePerAskedAddress();
    void requestTCP_reusesTheClientAndRefusesRepeats();
    void processKadList_releasesAStaleIncomingBuddy();
    void incomingBuddy_pendingClaimsAreBounded();
    void callbackPacket_onlyFromBuddyToAVettedTarget();
    void ban_coversTheIPv6Prefix();
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
    void helloID_followsKadWithoutAServer();
    void process_reapsABannedClientOnceTheBanIsOver();
    void processKadList_dropsAnOpenBuddy();
    void processKadList_firewalledKeepsItsOpenBuddy();
    void processKadList_dropsTheBuddyWhenKadLosesContact();
    void processKadList_detectsBuddyLoss();
    void servedBuddies_claimsUpToTheLimit();
    void servedBuddies_neverBecomeOurBuddy();
    void servedBuddies_loweredLimitDropsTheNewest();
    void servedBuddies_droppedWhenWeNeedABuddy();
    void servedBuddies_droppedWhenKadLosesContact();
    void servedBuddies_refusedWhileWeUseABuddy();

    // The reaper and the chat state — MFC CClientList::Process()
    void process_reapsAChatterOnceTheSessionEnds();
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

// MFC ClientList.cpp:700-703: a node we are running a firewall check with is no buddy.
void tst_ClientList::requestBuddy_refusesFirewallCheckAndBannedNodes()
{
    ClientList list;
    const uint8 id[16] = {0xB1};
    const uint32 checking = 0x4D020304, banned = 0x4D030405, fine = 0x4D040506;

    list.addKadFirewallRequest(qToBigEndian(checking));
    list.requestBuddy(checking, 4662, 4672, id, 0);
    QVERIFY(list.findByConnIP(qToBigEndian(checking), 4662) == nullptr);

    list.addBannedClient(Address::fromHostOrder(banned));
    list.requestBuddy(banned, 4662, 4672, id, 0);
    QVERIFY(list.findByConnIP(qToBigEndian(banned), 4662) == nullptr);

    list.requestBuddy(fine, 4662, 4672, id, 0);
    QVERIFY(list.findByConnIP(qToBigEndian(fine), 4662) != nullptr);
    list.deleteAll();
}

// MFC ClientList.cpp:662-692: one client per asking node, and no second check while
// the first is pending.
void tst_ClientList::requestTCP_reusesTheClientAndRefusesRepeats()
{
    ClientList list;
    const uint32 node = 0x4D060708;

    UpDownClient* first = list.requestTCP(node, 4662, 4672, nullptr, 0);
    QVERIFY(first != nullptr);
    QCOMPARE(first->kadState(), KadState::QueuedFwCheck);
    QCOMPARE(first->kadPort(), uint16{4672});
    QCOMPARE(list.clientCount(), 1);

    QVERIFY(list.requestTCP(node, 4662, 4672, nullptr, 0) == nullptr);
    QCOMPARE(list.clientCount(), 1);

    // Check done: the same client is taken again, not a new one.
    first->setKadState(KadState::None);
    QCOMPARE(list.requestTCP(node, 4662, 4672, nullptr, 0), first);
    QCOMPARE(list.clientCount(), 1);

    list.deleteAll();
}

void tst_ClientList::processKadList_releasesAStaleIncomingBuddy()
{
    eMule::testing::KadFixture kadFixture;
    ClientList list;
    const uint8 id[16] = {0xB2};

    QVERIFY(list.incomingBuddy(0x4D070809, 4662, 4672, id, id));
    UpDownClient* claimant = list.findByConnIP(qToBigEndian(uint32{0x4D070809}), 4662);
    QVERIFY(claimant != nullptr);

    list.processKadList();
    QCOMPARE(claimant->kadState(), KadState::IncomingBuddy);

    // Released after 5 minutes, not before
    claimant->setKadStateSince(std::time(nullptr) - 4 * 60);
    list.processKadList();
    QCOMPARE(claimant->kadState(), KadState::IncomingBuddy);

    claimant->setKadStateSince(std::time(nullptr) - 6 * 60);
    list.processKadList();
    QCOMPARE(claimant->kadState(), KadState::None);

    list.deleteAll();
}

void tst_ClientList::incomingBuddy_pendingClaimsAreBounded()
{
    eMule::testing::KadFixture kadFixture(eMule::testing::KadMode::Open);
    kadFixture.kadPrefs().setLastContact();

    ClientList list;
    const uint8 id[16] = {0xB3};
    const uint32 base = 0x4D080000;

    for (int i = 0; i < ClientList::kMaxPendingBuddyClaims; ++i)
        QVERIFY(list.incomingBuddy(base + static_cast<uint32>(i) + 1, 4662, 4672, id, id));
    QCOMPARE(list.pendingBuddyClaims(), ClientList::kMaxPendingBuddyClaims);

    UpDownClient* oldest = list.findByConnIP(qToBigEndian(base + 7), 4662);
    QVERIFY(oldest != nullptr);
    oldest->setKadStateSince(std::time(nullptr) - 60);

    // One more is accepted, not refused: the oldest claim makes room
    QVERIFY(list.incomingBuddy(base + 0x1000, 4662, 4672, id, id));
    QCOMPARE(list.pendingBuddyClaims(), ClientList::kMaxPendingBuddyClaims);
    QCOMPARE(oldest->kadState(), KadState::None);

    list.deleteAll();
}

void tst_ClientList::kadFirewallAck_oncePerAskedAddress()
{
    ClientList list;
    const uint32 ip = qToBigEndian(uint32{0x4D050607});
    QVERIFY(!list.takeKadFirewallAck(ip));
    list.addKadFirewallRequest(ip);
    list.addKadFirewallRequest(ip);   // asked twice is still one witness
    QVERIFY(list.takeKadFirewallAck(ip));
    QVERIFY(!list.takeKadFirewallAck(ip));
    QVERIFY(list.isKadFirewallCheckIP(ip));   // the callback is still let in
}

// OP_CALLBACK makes us dial an address taken from the packet. Its check value (our Kad
// ID inverted) and the file hash are public, so only our buddy may send it.
void tst_ClientList::callbackPacket_onlyFromBuddyToAVettedTarget()
{
    eMule::testing::KadFixture kadFixture;
    ClientList list;
    theApp.clientList = &list;
    DownloadQueue dq;
    theApp.downloadQueue = &dq;

    uint8 hash[16] = {0xCB, 1, 2, 3};
    auto* pf = new PartFile;
    pf->setFileName(QStringLiteral("callback.bin"));
    pf->setFileHash(hash);
    dq.addDownload(pf);

    const auto packet = [&](uint32 ipHost, uint16 port, const uint8* fileHash) {
        kad::UInt128 check(kadFixture.kadPrefs().kadId());
        check.xorWith(kad::UInt128(true));
        SafeMemFile io;
        kad::io::writeUInt128(io, check);
        kad::io::writeUInt128(io, kad::UInt128(fileHash));
        io.writeUInt32(ipHost);
        io.writeUInt16(port);
        return io.buffer();
    };
    const auto send = [](UpDownClient* from, const QByteArray& data) {
        from->processCallbackPacket(reinterpret_cast<const uint8*>(data.constData()),
                                    static_cast<uint32>(data.size()));
    };
    const uint32 target = 0x4D0A0B0C;   // 77.10.11.12
    const Address targetAddr = Address::fromHostOrder(target);
    const auto dialled = [&] { return !list.allowCallbackRequest(targetAddr); };

    auto* stranger = new UpDownClient();
    list.addClient(stranger);
    auto* buddy = new UpDownClient();
    buddy->setKadState(KadState::ConnectedBuddy);
    list.addClient(buddy);
    list.setBuddy(buddy, BuddyStatus::Connected);

    send(stranger, packet(target, 4662, hash));
    QVERIFY2(!dialled(), "a client that is not our buddy made us dial out");

    const uint8 unknown[16] = {0xEE};
    send(buddy, packet(target, 4662, unknown));
    QVERIFY(!dialled());

    send(buddy, packet(0xC0A80105, 4662, hash));   // 192.168.1.5
    QVERIFY(list.allowCallbackRequest(Address::fromHostOrder(0xC0A80105)));
    send(buddy, packet(target, 0, hash));
    QVERIFY(!dialled());

    list.addBannedClient(targetAddr);
    send(buddy, packet(target, 4662, hash));
    QVERIFY(!dialled());
    list.removeBannedClient(targetAddr);

    send(buddy, packet(target, 4662, hash));
    QVERIFY(dialled());

    list.setBuddy(nullptr, BuddyStatus::None);
    list.deleteAll();
    dq.deleteAll();
    theApp.downloadQueue = nullptr;
    theApp.clientList = nullptr;
}

// A banned IPv6 peer used to be back on the next address of its own prefix.
void tst_ClientList::ban_coversTheIPv6Prefix()
{
    ClientList list;
    const auto banned = Address::fromString(QStringLiteral("2606:4700:1:2::10"));
    const auto neighbour = Address::fromString(QStringLiteral("2606:4700:1:2:ffff::99"));
    const auto elsewhere = Address::fromString(QStringLiteral("2606:4700:1:3::10"));

    list.addBannedClient(banned);
    QVERIFY(list.isBannedClient(banned));
    QVERIFY(list.isBannedClient(neighbour));
    QVERIFY(!list.isBannedClient(elsewhere));
    QCOMPARE(list.bannedCount(), 1);

    list.removeBannedClient(neighbour);
    QVERIFY(!list.isBannedClient(banned));

    // The callback rate window follows the same key.
    list.addTrackCallbackRequests(banned);
    QVERIFY(!list.allowCallbackRequest(neighbour));
    QVERIFY(list.allowCallbackRequest(elsewhere));

    // IPv4 stays exact.
    list.addBannedClient(Address::fromHostOrder(0x4D010203));
    QVERIFY(!list.isBannedClient(Address::fromHostOrder(0x4D010204)));
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
    credits.verified(known.userAddress());
    QCOMPARE(credits.currentIdentState(known.userAddress()),
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
    kadFixture.kadPrefs().setLastContact();   // connected: a buddy is dropped otherwise

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

void tst_ClientList::helloID_followsKadWithoutAServer()
{
    // No server: the hello ID comes from Kad. 0 would read as HighID on the far side.
    QCOMPARE(theApp.getID(), 0u);
    {
        eMule::testing::KadFixture kadFixture(eMule::testing::KadMode::Firewalled);
        QCOMPARE(theApp.getID(), 0u);                 // running, not yet connected
        kadFixture.kadPrefs().setLastContact();
        QCOMPARE(theApp.getID(), 1u);
    }
    {
        eMule::testing::KadFixture kadFixture(eMule::testing::KadMode::Open);
        kadFixture.kadPrefs().setLastContact();
        kadFixture.kadPrefs().setIPAddress(0x01020304u);
        kadFixture.kadPrefs().setIPAddress(0x01020304u);
        QCOMPARE(kadFixture.kad().getIPAddress(), 0x01020304u);
        // eD2K ID form: first octet in the low byte.
        QCOMPARE(theApp.getID(), htonl(0x01020304u));
    }
}

void tst_ClientList::processKadList_dropsAnOpenBuddy()
{
    // We are reachable, so we are the relay: the buddy is the firewalled side, and one
    // that opened its port no longer needs us. MFC srchybrid/ClientList.cpp:611-618.
    eMule::testing::KadFixture kadFixture(eMule::testing::KadMode::Open);
    kadFixture.kadPrefs().setLastContact();

    ClientList list;

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

void tst_ClientList::processKadList_firewalledKeepsItsOpenBuddy()
{
    // The other direction: we are firewalled (TCP and UDP), and our buddy is an open
    // node — that is what makes it a relay. Applying the "buddy must be firewalled"
    // rule here too dropped every buddy in the pass that adopted it, and the buddy
    // search restarted forever.
    eMule::testing::KadFixture kadFixture;
    kadFixture.kadPrefs().setLastContact();
    kad::UDPFirewallTester::reset();
    kad::UDPFirewallTester::debugAddUsedTestClient(0x0A000001, 4672);
    kad::UDPFirewallTester::setUDPFWCheckResult(false, false, 0x0A000001, 4672);
    kad::UDPFirewallTester::debugAddUsedTestClient(0x0A000002, 4672);
    kad::UDPFirewallTester::setUDPFWCheckResult(false, false, 0x0A000002, 4672);
    const auto resetTester = qScopeGuard([] { kad::UDPFirewallTester::reset(); });
    QVERIFY(kadFixture.kad().isFirewalled());
    QVERIFY(kad::UDPFirewallTester::isFirewalledUDP(true));

    ClientList list;

    auto* buddy = new UpDownClient();
    buddy->setUserAddress(Address::fromString(QStringLiteral("10.7.0.5")));
    buddy->setUserIDHybrid(0x0A070005u);              // High ID
    buddy->setKadState(KadState::ConnectedBuddy);
    list.addClient(buddy);
    list.setBuddy(buddy, BuddyStatus::Connected);
    QVERIFY(!buddy->hasLowID());

    list.processKadList();
    list.processKadList();

    QCOMPARE(buddy->kadState(), KadState::ConnectedBuddy);
    QCOMPARE(list.getBuddy(), buddy);
    QCOMPARE(list.buddyStatus(), BuddyStatus::Connected);
}

void tst_ClientList::processKadList_dropsTheBuddyWhenKadLosesContact()
{
    // Running but no contact: MFC srchybrid/ClientList.cpp:620-623.
    eMule::testing::KadFixture kadFixture;
    QVERIFY(!kadFixture.kad().isConnected());

    ClientList list;

    auto* buddy = new UpDownClient();
    buddy->setUserAddress(Address::fromString(QStringLiteral("10.7.0.6")));
    buddy->setKadState(KadState::ConnectedBuddy);
    list.addClient(buddy);
    list.setBuddy(buddy, BuddyStatus::Connected);

    list.processKadList();
    QCOMPARE(buddy->kadState(), KadState::None);

    list.processKadList();
    QVERIFY(list.getBuddy() == nullptr);
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

// ---------------------------------------------------------------------------
// Served buddies — an open node relays for several firewalled nodes (not MFC)
// ---------------------------------------------------------------------------

namespace {

/// A firewalled node whose buddy connection to us is up.
UpDownClient* addServedBuddy(ClientList& list, uint32 ipHost, uint8 idByte, std::time_t since)
{
    auto* client = new UpDownClient();
    client->setConnectAddress(Address::fromHostOrder(ipHost));
    client->setUserAddress(Address::fromHostOrder(ipHost));
    client->setUserIDHybrid(1);                       // Low ID
    const uint8 id[16] = {idByte, 0x01, 0x02, 0x03};
    client->setBuddyID(id);
    client->setServedBuddy(true);
    client->setKadState(KadState::ConnectedBuddy);
    client->setKadStateSince(since);
    list.addClient(client);
    return client;
}

} // namespace

void tst_ClientList::servedBuddies_claimsUpToTheLimit()
{
    eMule::testing::KadFixture kadFixture(eMule::testing::KadMode::Open);
    kadFixture.kadPrefs().setLastContact();
    QCOMPARE(thePrefs.maxServedBuddies(), static_cast<uint16>(8));
    thePrefs.setMaxServedBuddies(3);
    const auto restore = qScopeGuard([] { thePrefs.setMaxServedBuddies(8); });

    ClientList list;
    const uint8 id[16] = {0xC1, 0x02};

    QVERIFY(list.incomingBuddy(0x4D070801, 4662, 4672, id, id));
    // One pending claim per IP, whatever the port
    QVERIFY(!list.incomingBuddy(0x4D070801, 4663, 4673, id, id));
    QVERIFY(list.incomingBuddy(0x4D070802, 4662, 4672, id, id));
    QCOMPARE(list.pendingBuddyClaims(), 2);

    // A claim reserves nothing (as MFC): 1 connected + 2 pending leaves the limit open
    addServedBuddy(list, 0x4D070803, 0xC3, std::time(nullptr));
    QCOMPARE(list.servedBuddyCount(), 1);
    QVERIFY(list.canServeAnotherBuddy());
    QVERIFY(list.incomingBuddy(0x4D070804, 4662, 4672, id, id));
    QCOMPARE(list.pendingBuddyClaims(), 3);

    // Claims stay through a pass although a buddy is connected (MFC drops them)
    list.processKadList();
    QCOMPARE(list.pendingBuddyClaims(), 3);
    QCOMPARE(list.servedBuddyCount(), 1);

    UpDownClient* claimant = list.findByConnIP(qToBigEndian(uint32{0x4D070801}), 4662);
    QVERIFY(claimant && claimant->isServedBuddy());

    // A claim that ran out is released
    claimant->setKadStateSince(std::time(nullptr) - 6 * 60);
    list.processKadList();
    QCOMPARE(claimant->kadState(), KadState::None);
    QVERIFY(!claimant->isServedBuddy());

    // Only connected buddies fill the limit
    addServedBuddy(list, 0x4D070805, 0xC5, std::time(nullptr));
    QVERIFY(list.canServeAnotherBuddy());
    addServedBuddy(list, 0x4D070806, 0xC6, std::time(nullptr));
    QCOMPARE(list.servedBuddyCount(), 3);
    QVERIFY(!list.canServeAnotherBuddy());
    QVERIFY(!list.incomingBuddy(0x4D070807, 4662, 4672, id, id));

    list.deleteAll();
}

void tst_ClientList::servedBuddies_neverBecomeOurBuddy()
{
    eMule::testing::KadFixture kadFixture(eMule::testing::KadMode::Open);
    kadFixture.kadPrefs().setLastContact();

    ClientList list;
    auto* a = addServedBuddy(list, 0x4D070811, 0xD1, std::time(nullptr));
    auto* b = addServedBuddy(list, 0x4D070812, 0xD2, std::time(nullptr));

    list.processKadList();
    list.processKadList();

    QCOMPARE(a->kadState(), KadState::ConnectedBuddy);
    QCOMPARE(b->kadState(), KadState::ConnectedBuddy);
    QCOMPARE(list.servedBuddyCount(), 2);
    QVERIFY(list.getBuddy() == nullptr);
    QCOMPARE(list.buddyStatus(), BuddyStatus::None);

    // No socket, no relay target
    const uint8 idA[16] = {0xD1, 0x01, 0x02, 0x03};
    QVERIFY(list.findServedBuddy(idA) == nullptr);

    // One that opened its port no longer needs us (MFC ClientList.cpp:618-622)
    b->setUserIDHybrid(0x4D070812u);
    QVERIFY(!b->hasLowID());
    list.processKadList();
    QCOMPARE(a->kadState(), KadState::ConnectedBuddy);
    QCOMPARE(b->kadState(), KadState::None);

    list.deleteAll();
}

void tst_ClientList::servedBuddies_loweredLimitDropsTheNewest()
{
    eMule::testing::KadFixture kadFixture(eMule::testing::KadMode::Open);
    kadFixture.kadPrefs().setLastContact();
    const auto restore = qScopeGuard([] { thePrefs.setMaxServedBuddies(8); });

    ClientList list;
    const std::time_t now = std::time(nullptr);
    auto* newest = addServedBuddy(list, 0x4D070821, 0xE1, now - 10);
    auto* oldest = addServedBuddy(list, 0x4D070822, 0xE2, now - 300);
    auto* middle = addServedBuddy(list, 0x4D070823, 0xE3, now - 100);

    list.processKadList();
    QCOMPARE(list.servedBuddyCount(), 3);
    QCOMPARE(list.servedBuddies().front(), oldest);

    thePrefs.setMaxServedBuddies(2);
    list.processKadList();
    QCOMPARE(oldest->kadState(), KadState::ConnectedBuddy);
    QCOMPARE(middle->kadState(), KadState::ConnectedBuddy);
    QCOMPARE(newest->kadState(), KadState::None);

    list.deleteAll();
}

void tst_ClientList::servedBuddies_droppedWhenWeNeedABuddy()
{
    // TCP and UDP firewalled: we cannot relay for anyone.
    eMule::testing::KadFixture kadFixture;
    kadFixture.kadPrefs().setLastContact();
    // reset() keeps the asked clients, so these two are not the ones used above
    kad::UDPFirewallTester::reset();
    kad::UDPFirewallTester::debugAddUsedTestClient(0x0A000011, 4672);
    kad::UDPFirewallTester::setUDPFWCheckResult(false, false, 0x0A000011, 4672);
    kad::UDPFirewallTester::debugAddUsedTestClient(0x0A000012, 4672);
    kad::UDPFirewallTester::setUDPFWCheckResult(false, false, 0x0A000012, 4672);
    const auto resetTester = qScopeGuard([] { kad::UDPFirewallTester::reset(); });
    QVERIFY(kadFixture.kad().isFirewalled());
    QVERIFY(kad::UDPFirewallTester::isFirewalledUDP(true));

    ClientList list;
    auto* served = addServedBuddy(list, 0x4D070831, 0xF1, std::time(nullptr));
    auto* claim = new UpDownClient();
    claim->setConnectAddress(Address::fromHostOrder(0x4D070832));
    claim->setServedBuddy(true);
    claim->setKadState(KadState::IncomingBuddy);
    list.addClient(claim);

    list.processKadList();
    QCOMPARE(served->kadState(), KadState::None);
    QCOMPARE(claim->kadState(), KadState::None);
    QCOMPARE(list.servedBuddyCount(), 0);

    list.deleteAll();
}

void tst_ClientList::servedBuddies_droppedWhenKadLosesContact()
{
    eMule::testing::KadFixture kadFixture(eMule::testing::KadMode::Open);
    QVERIFY(!kadFixture.kad().isConnected());

    ClientList list;
    auto* served = addServedBuddy(list, 0x4D070841, 0xA1, std::time(nullptr));
    list.processKadList();
    QCOMPARE(served->kadState(), KadState::None);

    list.deleteAll();
}

void tst_ClientList::servedBuddies_refusedWhileWeUseABuddy()
{
    eMule::testing::KadFixture kadFixture;
    kadFixture.kadPrefs().setLastContact();

    ClientList list;
    auto* buddy = new UpDownClient();
    buddy->setUserAddress(Address::fromString(QStringLiteral("10.7.0.9")));
    buddy->setKadState(KadState::ConnectedBuddy);
    list.addClient(buddy);
    list.setBuddy(buddy, BuddyStatus::Connected);

    const uint8 id[16] = {0xB7, 0x02};
    QVERIFY(!list.canServeAnotherBuddy());
    QVERIFY(!list.incomingBuddy(0x4D070851, 4662, 4672, id, id));

    list.deleteAll();
}

// A client kept alive only by its chat state is never collected (ClientList.cpp:465-466),
// and its socket timeout stays doubled. Before EndChatSession existed nothing ever put
// that state back, so every peer we had ever chatted with leaked for the daemon's life.
void tst_ClientList::process_reapsAChatterOnceTheSessionEnds()
{
    ClientList list;
    auto* client = new UpDownClient();   // reaped clients are deleteLater()d
    client->setChatState(ChatState::Chatting);
    list.addClient(client);

    list.process();
    QCOMPARE(list.clientCount(), 1);     // mid-conversation: kept, as MFC keeps it

    client->endChatSession();
    list.process();
    QCOMPARE(list.clientCount(), 0);
}

void tst_ClientList::process_reapsABannedClientOnceTheBanIsOver()
{
    ClientList list;
    auto* prev = theApp.clientList;
    theApp.clientList = &list;
    const auto restore = qScopeGuard([prev] { theApp.clientList = prev; });

    auto* client = new UpDownClient();   // reaped clients are deleteLater()d
    client->setConnectAddress(Address::fromString(QStringLiteral("10.9.0.7")));
    list.addClient(client);
    client->ban(QStringLiteral("test"));

    list.process();
    QCOMPARE(list.clientCount(), 1);     // banned: the object carries the state

    list.removeBannedClient(client->connectAddress());
    list.process();
    QCOMPARE(list.clientCount(), 0);
}

void tst_ClientList::handshakeFailures_banAtTheThreshold()
{
    ClientList list;
    const Address peer = Address::fromHostOrder(0x08080404);
    const Address other = Address::fromHostOrder(0x08080405);

    for (int i = 0; i < HANDSHAKEFAIL_BAN_COUNT - 1; ++i)
        list.noteHandshakeFailure(peer);
    list.noteHandshakeFailure(other);
    QVERIFY(!list.isBannedClient(peer));

    list.noteHandshakeFailure(peer);
    QVERIFY(list.isBannedClient(peer));
    QVERIFY(!list.isBannedClient(other));
}

void tst_ClientList::handshakeFailures_neverBanAListedServer()
{
    ServerList servers;
    QVERIFY(servers.addServer(std::make_unique<Server>(htonl(0x08080808), 4661)));
    theApp.serverList = &servers;
    const auto restore = qScopeGuard([] { theApp.serverList = nullptr; });

    ClientList list;
    const Address server = Address::fromHostOrder(0x08080808);
    for (int i = 0; i < 3 * HANDSHAKEFAIL_BAN_COUNT; ++i)
        list.noteHandshakeFailure(server);
    QVERIFY(!list.isBannedClient(server));
}

// ---------------------------------------------------------------------------
// FruitlessSessionLedger
// ---------------------------------------------------------------------------

namespace {
using Ledger = eMule::FruitlessSessionLedger;
constexpr uint64 kMin = 60 * 1000;

std::array<uint8, 16> ledgerHash(uint32 n)
{
    std::array<uint8, 16> h{};
    h[0] = 0x51;
    std::memcpy(h.data() + 4, &n, sizeof(n));
    return h;
}
} // namespace

void tst_ClientList::fruitlessLedger_secondSessionInTheWindowPauses()
{
    Ledger ledger;
    const auto peer = ledgerHash(1);
    const auto other = ledgerHash(2);
    uint64 now = 1'000'000;

    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now), Ledger::Verdict::Noted);
    QCOMPARE(ledger.heldBackFor(peer.data(), now), uint64{0});

    // outside the window the first one is forgotten
    now += Ledger::kWindowMs + 1;
    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now), Ledger::Verdict::Noted);
    now += kMin;
    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now), Ledger::Verdict::Paused);
    QCOMPARE(ledger.heldBackFor(peer.data(), now), Ledger::kPauseMs);
    QVERIFY(!ledger.isQuarantined(peer.data(), now));
    QCOMPARE(ledger.heldBackFor(other.data(), now), uint64{0});

    now += Ledger::kPauseMs;
    QCOMPARE(ledger.heldBackFor(peer.data(), now), uint64{0});
}

void tst_ClientList::fruitlessLedger_secondPauseQuarantines()
{
    Ledger ledger;
    const auto peer = ledgerHash(3);
    uint64 now = 1'000'000;

    ledger.noteFruitlessSession(peer.data(), now);
    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now + kMin), Ledger::Verdict::Paused);

    // an hour later, the same again
    now += 60 * kMin;
    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now), Ledger::Verdict::Noted);
    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now + kMin), Ledger::Verdict::Quarantined);
    now += kMin;
    QVERIFY(ledger.isQuarantined(peer.data(), now));
    QCOMPARE(ledger.heldBackFor(peer.data(), now), Ledger::kQuarantineMs);

    // a good session cannot buy it out
    ledger.noteProductiveSession(peer.data());
    QVERIFY(ledger.isQuarantined(peer.data(), now + kMin));

    // served: the peer starts clean
    now += Ledger::kQuarantineMs;
    QVERIFY(!ledger.isQuarantined(peer.data(), now));
    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now), Ledger::Verdict::Noted);
    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now + kMin), Ledger::Verdict::Paused);
}

void tst_ClientList::fruitlessLedger_tenEventsQuarantine()
{
    Ledger ledger;
    const auto peer = ledgerHash(4);
    uint64 now = 1'000'000;

    ledger.noteFruitlessSession(peer.data(), now);
    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now + 1000), Ledger::Verdict::Paused);
    // the peer keeps accepting us during the pause: events 3..9 change nothing
    for (uint32 i = 3; i < Ledger::kEventsToQuarantine; ++i)
        QCOMPARE(ledger.noteDeclinedAccept(peer.data(), now + i * 1000), Ledger::Verdict::Paused);
    QCOMPARE(ledger.noteDeclinedAccept(peer.data(), now + 10'000), Ledger::Verdict::Quarantined);
    QVERIFY(ledger.isQuarantined(peer.data(), now + 11'000));

    // spread over more than the window, declined accepts alone do nothing
    const auto slow = ledgerHash(5);
    for (uint32 i = 0; i < 3 * Ledger::kEventsToQuarantine; ++i)
        QCOMPARE(ledger.noteDeclinedAccept(slow.data(), now + i * kMin), Ledger::Verdict::Noted);
}

void tst_ClientList::fruitlessLedger_productiveSessionStartsOver()
{
    Ledger ledger;
    const auto peer = ledgerHash(6);
    uint64 now = 1'000'000;

    ledger.noteFruitlessSession(peer.data(), now);
    ledger.noteProductiveSession(peer.data());
    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now + kMin), Ledger::Verdict::Noted);

    // and it wipes an earlier pause from the count towards quarantine
    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now + 2 * kMin), Ledger::Verdict::Paused);
    now += 30 * kMin;
    ledger.noteProductiveSession(peer.data());
    ledger.noteFruitlessSession(peer.data(), now);
    QCOMPARE(ledger.noteFruitlessSession(peer.data(), now + kMin), Ledger::Verdict::Paused);
}

void tst_ClientList::fruitlessLedger_isBounded()
{
    Ledger ledger;
    uint64 now = 1'000'000;
    for (uint32 i = 0; i < Ledger::kMaxEntries; ++i)
        ledger.noteFruitlessSession(ledgerHash(100 + i).data(), now + i);
    QCOMPARE(ledger.count(), Ledger::kMaxEntries);

    // full, nothing expired: the least recently used one goes
    now += Ledger::kMaxEntries;
    ledger.noteFruitlessSession(ledgerHash(1).data(), now);
    QCOMPARE(ledger.count(), Ledger::kMaxEntries);
    QCOMPARE(ledger.noteFruitlessSession(ledgerHash(100).data(), now + 1), Ledger::Verdict::Noted);
    QCOMPARE(ledger.noteFruitlessSession(ledgerHash(101 + 5).data(), now + 2), Ledger::Verdict::Paused);

    // everything idle past the TTL is dropped when room is needed
    now += Ledger::kEntryTtlMs + Ledger::kMaxEntries;
    ledger.noteFruitlessSession(ledgerHash(2).data(), now);
    QCOMPARE(ledger.count(), std::size_t{1});
}

QTEST_MAIN(tst_ClientList)
#include "tst_ClientList.moc"
