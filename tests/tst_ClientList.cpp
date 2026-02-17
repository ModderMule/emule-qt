/// @file tst_ClientList.cpp
/// @brief Tests for client/ClientList — client management, find operations, banning.

#include "TestHelpers.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "utils/OtherFunctions.h"

#include <QSignalSpy>
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
    client.setIP(0xC0A80001);
    list.addClient(&client);
    QCOMPARE(list.findByIP(0xC0A80001u), &client);
}

void tst_ClientList::findByIP_withPort()
{
    ClientList list;
    UpDownClient a, b;
    a.setIP(0xC0A80001);
    a.setUserPort(4662);
    b.setIP(0xC0A80001);
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
    client.setConnectIP(0xC0A80002);
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
    a.setIP(0xC0A80001);
    a.setUserPort(4662);
    b.setUserHash(hash);
    b.setIP(0xC0A80002);
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
    a.setIP(0xC0A80001);
    a.setUserPort(4662);
    list.addClient(&a);

    // No IP+port match → should fall back to hash-only
    QCOMPARE(list.findByUserHash(hash, 0xDEADBEEF, 9999), &a);
}

void tst_ClientList::findByIP_UDP()
{
    ClientList list;
    UpDownClient client;
    client.setIP(0xC0A80001);
    client.setUDPPort(4672);
    list.addClient(&client);
    QCOMPARE(list.findByIP_UDP(0xC0A80001u, 4672), &client);
}

void tst_ClientList::findByServerID()
{
    ClientList list;
    UpDownClient client;
    // Server ID search converts ED2K user ID to hybrid with ntohl
    const uint32 ed2kId = 0x0D0C0B0A;
    client.setServerIP(0x01020304);
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
    client.setIP(0xC0A80001);
    client.setKadPort(4672);
    list.addClient(&client);
    QCOMPARE(list.findByIP_KadPort(0xC0A80001u, 4672), &client);
}

void tst_ClientList::findByIP_notFound()
{
    ClientList list;
    UpDownClient client;
    client.setIP(0xC0A80001);
    list.addClient(&client);
    QCOMPARE(list.findByIP(0xDEADBEEFu), nullptr);
}

void tst_ClientList::addBannedClient()
{
    ClientList list;
    list.addBannedClient(0xC0A80001);
    QCOMPARE(list.bannedCount(), 1);
}

void tst_ClientList::isBannedClient_true()
{
    ClientList list;
    list.addBannedClient(0xC0A80001);
    QVERIFY(list.isBannedClient(0xC0A80001));
    QVERIFY(!list.isBannedClient(0xC0A80002));
}

void tst_ClientList::removeBannedClient()
{
    ClientList list;
    list.addBannedClient(0xC0A80001);
    QVERIFY(list.isBannedClient(0xC0A80001));
    list.removeBannedClient(0xC0A80001);
    QVERIFY(!list.isBannedClient(0xC0A80001));
    QCOMPARE(list.bannedCount(), 0);
}

void tst_ClientList::bannedCount()
{
    ClientList list;
    QCOMPARE(list.bannedCount(), 0);
    list.addBannedClient(0xC0A80001);
    list.addBannedClient(0xC0A80002);
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

QTEST_MAIN(tst_ClientList)
#include "tst_ClientList.moc"
