/// @file tst_FriendList.cpp
/// @brief Tests for friends/FriendList — add/remove, load/save, search, signals.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "client/UpDownClient.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QSignalSpy>
#include <QTest>

#include <array>
#include <cstring>
#include <memory>

using namespace eMule;

class tst_FriendList : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void addFriend_basic();
    void addFriend_duplicateRejected();
    void addFriend_noHashNoIP_rejected();
    void addFriend_ipOnly();
    void removeFriend_basic();
    void removeFriend_notFound();
    void removeAll();
    void searchFriend_byHash();
    void searchFriend_byIP();
    void searchFriend_notFound();
    void isAlreadyFriend_byHexHash();
    void isValid_true();
    void isValid_false();
    void removeAllFriendSlots();
    void saveAndLoad_roundTrip();
    void load_emptyFile();
    void load_badHeader();
    void load_nonexistent();
    void signal_friendAdded();
    void signal_friendRemoved();
    void signal_listLoaded();

    // Friend <-> client link — MFC srchybrid/Friend.cpp:171-198, FriendList.cpp:203-219
    void linkedClient_carriesTheSlotBothWays();
    void linkedClient_relinkingClearsTheOldClient();
    void removeFriend_clearsSlotAndPointerOnTheClient();
    void removeAllFriendSlots_reachesLinkedClients();
    void clientDestroyed_leavesNoDanglingLink();
};

static std::array<uint8, 16> makeHash(uint8 fill)
{
    std::array<uint8, 16> h{};
    h.fill(fill);
    return h;
}

void tst_FriendList::construct_default()
{
    FriendList list;
    QCOMPARE(list.count(), 0);
    QVERIFY(list.friends().empty());
}

void tst_FriendList::addFriend_basic()
{
    FriendList list;
    auto hash = makeHash(0xAA);
    Friend* f = list.addFriend(hash.data(), 0x0A000001, 4662,
                               QStringLiteral("Alice"), true);
    QVERIFY(f != nullptr);
    QCOMPARE(list.count(), 1);
    QCOMPARE(f->name(), QStringLiteral("Alice"));
    QVERIFY(f->hasUserhash());
}

void tst_FriendList::addFriend_duplicateRejected()
{
    FriendList list;
    auto hash = makeHash(0xBB);
    QVERIFY(list.addFriend(hash.data(), 0x0A000001, 4662, {}, true));
    // Same hash → duplicate
    QVERIFY(!list.addFriend(hash.data(), 0x0A000002, 4663, {}, true));
    QCOMPARE(list.count(), 1);
}

void tst_FriendList::addFriend_noHashNoIP_rejected()
{
    FriendList list;
    // No hash and no IP → rejected
    QVERIFY(!list.addFriend(nullptr, 0, 0, {}, false));
    QCOMPARE(list.count(), 0);
}

void tst_FriendList::addFriend_ipOnly()
{
    FriendList list;
    Friend* f = list.addFriend(nullptr, 0x0A000001, 4662,
                               QStringLiteral("IPOnly"), false);
    QVERIFY(f != nullptr);
    QCOMPARE(list.count(), 1);
    QVERIFY(!f->hasUserhash());
}

void tst_FriendList::removeFriend_basic()
{
    FriendList list;
    auto hash = makeHash(0xCC);
    Friend* f = list.addFriend(hash.data(), 0x0A000001, 4662,
                               QStringLiteral("ToRemove"), true);
    QVERIFY(f);
    QCOMPARE(list.count(), 1);

    QVERIFY(list.removeFriend(f));
    QCOMPARE(list.count(), 0);
}

void tst_FriendList::removeFriend_notFound()
{
    FriendList list;
    Friend bogus;
    QVERIFY(!list.removeFriend(&bogus));
}

void tst_FriendList::removeAll()
{
    FriendList list;
    auto h1 = makeHash(0x01);
    auto h2 = makeHash(0x02);
    list.addFriend(h1.data(), 0x01, 100, {}, true);
    list.addFriend(h2.data(), 0x02, 200, {}, true);
    QCOMPARE(list.count(), 2);

    list.removeAll();
    QCOMPARE(list.count(), 0);
}

void tst_FriendList::searchFriend_byHash()
{
    FriendList list;
    auto hash = makeHash(0xDD);
    list.addFriend(hash.data(), 0x0A000001, 4662,
                   QStringLiteral("Found"), true);

    Friend* f = list.searchFriend(hash.data());
    QVERIFY(f);
    QCOMPARE(f->name(), QStringLiteral("Found"));
}

void tst_FriendList::searchFriend_byIP()
{
    FriendList list;
    list.addFriend(nullptr, 0x0A000099, 1234,
                   QStringLiteral("IPFriend"), false);

    Friend* f = list.searchFriend(nullptr, 0x0A000099, 1234);
    QVERIFY(f);
    QCOMPARE(f->name(), QStringLiteral("IPFriend"));
}

void tst_FriendList::searchFriend_notFound()
{
    FriendList list;
    auto hash = makeHash(0xFF);
    QVERIFY(!list.searchFriend(hash.data()));
}

void tst_FriendList::isAlreadyFriend_byHexHash()
{
    FriendList list;
    auto hash = makeHash(0xAB);
    list.addFriend(hash.data(), 0x01, 100, {}, true);

    const QString hexHash = md4str(hash.data());
    QVERIFY(list.isAlreadyFriend(hexHash));

    auto otherHash = makeHash(0xCD);
    QVERIFY(!list.isAlreadyFriend(md4str(otherHash.data())));
}

void tst_FriendList::isValid_true()
{
    FriendList list;
    auto hash = makeHash(0x11);
    Friend* f = list.addFriend(hash.data(), 0x01, 100, {}, true);
    QVERIFY(list.isValid(f));
}

void tst_FriendList::isValid_false()
{
    FriendList list;
    Friend bogus;
    QVERIFY(!list.isValid(&bogus));
}

void tst_FriendList::removeAllFriendSlots()
{
    FriendList list;
    auto h1 = makeHash(0x01);
    auto h2 = makeHash(0x02);
    Friend* f1 = list.addFriend(h1.data(), 0x01, 100, {}, true);
    Friend* f2 = list.addFriend(h2.data(), 0x02, 200, {}, true);
    f1->setFriendSlot(true);
    f2->setFriendSlot(true);

    list.removeAllFriendSlots();
    QVERIFY(!f1->friendSlot());
    QVERIFY(!f2->friendSlot());
}

void tst_FriendList::saveAndLoad_roundTrip()
{
    eMule::testing::TempDir tmp;

    // Populate and save
    {
        FriendList list;
        auto h1 = makeHash(0xAA);
        auto h2 = makeHash(0xBB);
        Friend* f1 = list.addFriend(h1.data(), 0x0A000001, 4662,
                                    QStringLiteral("Alice"), true);
        QVERIFY(f1);
        auto kadId = makeHash(0xCC);
        f1->setKadID(kadId.data());

        Friend* f2 = list.addFriend(h2.data(), 0x0A000002, 4672,
                                    QStringLiteral("Bob"), true);
        QVERIFY(f2);
        f2->setFriendSlot(true);

        list.save(tmp.path());
    }

    // Load into fresh list
    {
        FriendList list;
        QVERIFY(list.load(tmp.path()));
        QCOMPARE(list.count(), 2);

        auto h1 = makeHash(0xAA);
        Friend* f1 = list.searchFriend(h1.data());
        QVERIFY(f1);
        QCOMPARE(f1->name(), QStringLiteral("Alice"));
        QCOMPARE(f1->lastUsedAddress().toNetworkUint32(), 0x0A000001u);
        QCOMPARE(f1->lastUsedPort(), static_cast<uint16>(4662));
        QVERIFY(f1->hasKadID());
        auto kadId = makeHash(0xCC);
        QVERIFY(md4equ(f1->kadID().data(), kadId.data()));

        auto h2 = makeHash(0xBB);
        Friend* f2 = list.searchFriend(h2.data());
        QVERIFY(f2);
        QCOMPARE(f2->name(), QStringLiteral("Bob"));
    }
}

void tst_FriendList::load_emptyFile()
{
    eMule::testing::TempDir tmp;

    // Save an empty list
    {
        FriendList list;
        list.save(tmp.path());
    }

    // Load it back
    {
        FriendList list;
        QVERIFY(list.load(tmp.path()));
        QCOMPARE(list.count(), 0);
    }
}

void tst_FriendList::load_badHeader()
{
    eMule::testing::TempDir tmp;
    const QString filePath = tmp.filePath(
        QString::fromLatin1(kFriendsMetFilename));

    // Write a file with a bad header byte
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    char bad = 0xFF;
    f.write(&bad, 1);
    f.close();

    FriendList list;
    QVERIFY(!list.load(tmp.path()));
    QCOMPARE(list.count(), 0);
}

void tst_FriendList::load_nonexistent()
{
    FriendList list;
    QVERIFY(!list.load(QStringLiteral("/nonexistent/path")));
    QCOMPARE(list.count(), 0);
}

void tst_FriendList::signal_friendAdded()
{
    FriendList list;
    QSignalSpy spy(&list, &FriendList::friendAdded);

    auto hash = makeHash(0xEE);
    list.addFriend(hash.data(), 0x01, 100, QStringLiteral("Sig"), true);

    QCOMPARE(spy.count(), 1);
}

void tst_FriendList::signal_friendRemoved()
{
    FriendList list;
    auto hash = makeHash(0xFF);
    Friend* f = list.addFriend(hash.data(), 0x01, 100,
                               QStringLiteral("ToGo"), true);

    QSignalSpy spy(&list, &FriendList::friendRemoved);
    list.removeFriend(f);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy[0][0].toString(), QStringLiteral("ToGo"));
}

void tst_FriendList::signal_listLoaded()
{
    eMule::testing::TempDir tmp;

    {
        FriendList list;
        auto hash = makeHash(0x11);
        list.addFriend(hash.data(), 0x01, 100, {}, true);
        list.save(tmp.path());
    }

    FriendList list;
    QSignalSpy spy(&list, &FriendList::listLoaded);
    list.load(tmp.path());

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy[0][0].toInt(), 1);
}


// ===========================================================================
// Friend <-> client link
//
// The port dropped MFC's bidirectional CFriend::m_LinkedClient, and with it every path
// that revokes a friend slot. The flag the upload queue actually scores lives on
// UpDownClient, so without the link a slot could outlive its friend, be held by two
// clients at once, or point at freed memory.
// ===========================================================================

namespace {

/// A client with a hash and address, enough for searchFriend()/addFriend() to match it.
std::unique_ptr<eMule::UpDownClient> makeLinkableClient(uint8 hashByte, const QString& ip,
                                                        uint16 port = 4662)
{
    auto client = std::make_unique<eMule::UpDownClient>();
    client->setUserAddress(eMule::Address::fromString(ip));
    client->setUserPort(port);
    uint8 hash[16];
    std::memset(hash, hashByte, sizeof(hash));
    client->setUserHash(hash);
    return client;
}

} // namespace

void tst_FriendList::linkedClient_carriesTheSlotBothWays()
{
    FriendList list;
    auto client = makeLinkableClient(0x41, QStringLiteral("10.9.0.1"));

    auto* f = list.addFriend(client->userHash(), client->userAddress(), client->userPort(),
                             QStringLiteral("alice"), true);
    QVERIFY(f);

    // Granting on the Friend reaches the client, which is what makes the flag mean anything
    // to the upload queue. MFC srchybrid/Friend.cpp:158-163.
    f->setLinkedClient(client.get());
    QCOMPARE(client->friendPtr(), f);
    f->setFriendSlot(true);
    QVERIFY(client->friendSlot());
    QVERIFY(f->friendSlot());

    // Unlinking takes the flag back, so the friend keeps its slot until it reconnects
    // rather than losing it to a client going offline.
    f->setLinkedClient(nullptr);
    QVERIFY2(!client->friendSlot(), "a client that is no longer the friend must lose the slot");
    QVERIFY(client->friendPtr() == nullptr);
    QVERIFY2(f->friendSlot(), "the friend keeps the grant across a disconnect");

    // ...and hands it straight back on reconnect.
    f->setLinkedClient(client.get());
    QVERIFY(client->friendSlot());
}

void tst_FriendList::linkedClient_relinkingClearsTheOldClient()
{
    FriendList list;
    auto first  = makeLinkableClient(0x42, QStringLiteral("10.9.0.2"));
    auto second = makeLinkableClient(0x42, QStringLiteral("10.9.0.3"), 4663);

    auto* f = list.addFriend(first->userHash(), first->userAddress(), first->userPort(),
                             QStringLiteral("bob"), true);
    QVERIFY(f);
    f->setLinkedClient(first.get());
    f->setFriendSlot(true);
    QVERIFY(first->friendSlot());

    // The same friend reconnecting as a new client instance — the old one must not keep a
    // slot it no longer earns. MFC srchybrid/Friend.cpp:191-193.
    f->setLinkedClient(second.get());
    QVERIFY2(!first->friendSlot(), "the displaced client must lose the slot");
    QVERIFY(first->friendPtr() == nullptr);
    QVERIFY(second->friendSlot());
    QCOMPARE(second->friendPtr(), f);
}

void tst_FriendList::removeFriend_clearsSlotAndPointerOnTheClient()
{
    FriendList list;
    auto client = makeLinkableClient(0x43, QStringLiteral("10.9.0.4"));

    auto* f = list.addFriend(client->userHash(), client->userAddress(), client->userPort(),
                             QStringLiteral("carol"), true);
    QVERIFY(f);
    f->setLinkedClient(client.get());
    f->setFriendSlot(true);
    QVERIFY(client->friendSlot());

    QVERIFY(list.removeFriend(f));

    // Both halves matter. The slot must go, and friendPtr() must not survive as a pointer
    // into a destroyed Friend — UploadQueue's soft-limit bypass dereferences it.
    QVERIFY2(!client->friendSlot(), "a removed friend must not leave a slot behind");
    QVERIFY2(client->friendPtr() == nullptr, "friendPtr() must not outlive the friend entry");
}

void tst_FriendList::removeAllFriendSlots_reachesLinkedClients()
{
    FriendList list;
    auto a = makeLinkableClient(0x44, QStringLiteral("10.9.0.5"));
    auto b = makeLinkableClient(0x45, QStringLiteral("10.9.0.6"), 4664);

    auto* fa = list.addFriend(a->userHash(), a->userAddress(), a->userPort(),
                              QStringLiteral("dave"), true);
    auto* fb = list.addFriend(b->userHash(), b->userAddress(), b->userPort(),
                              QStringLiteral("erin"), true);
    QVERIFY(fa && fb);
    fa->setLinkedClient(a.get());
    fb->setLinkedClient(b.get());
    fa->setFriendSlot(true);
    QVERIFY(a->friendSlot());

    // Moving the slot: this is the one-at-a-time rule, and it only holds because the clear
    // propagates to the clients. Clearing the Friend objects alone left the previously
    // slotted client holding a slot forever.
    list.removeAllFriendSlots();
    QVERIFY(!a->friendSlot());
    fb->setFriendSlot(true);
    QVERIFY(b->friendSlot());
    QVERIFY2(!a->friendSlot(), "only one client may hold the friend slot");
}

void tst_FriendList::clientDestroyed_leavesNoDanglingLink()
{
    FriendList list;
    auto client = makeLinkableClient(0x46, QStringLiteral("10.9.0.7"));

    auto* f = list.addFriend(client->userHash(), client->userAddress(), client->userPort(),
                             QStringLiteral("frank"), true);
    QVERIFY(f);
    f->setLinkedClient(client.get());
    f->setFriendSlot(true);

    client.reset();      // MFC srchybrid/BaseClient.cpp:269-273 unlinks from this side

    QVERIFY(f->linkedClient() == nullptr);
    QVERIFY2(f->friendSlot(), "the grant survives the client, ready for its next connection");
}

QTEST_GUILESS_MAIN(tst_FriendList)
#include "tst_FriendList.moc"
