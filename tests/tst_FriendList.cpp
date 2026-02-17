/// @file tst_FriendList.cpp
/// @brief Tests for friends/FriendList — add/remove, load/save, search, signals.

#include "TestHelpers.h"
#include "friends/FriendList.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QSignalSpy>
#include <QTest>

#include <array>

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
        QCOMPARE(f1->lastUsedIP(), 0x0A000001u);
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

QTEST_GUILESS_MAIN(tst_FriendList)
#include "tst_FriendList.moc"
