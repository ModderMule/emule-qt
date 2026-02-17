/// @file tst_Friend.cpp
/// @brief Tests for friends/Friend — construction, properties, serialization.

#include "TestHelpers.h"
#include "friends/Friend.h"
#include "utils/SafeFile.h"
#include "utils/OtherFunctions.h"

#include <QTest>

#include <array>
#include <cstring>
#include <ctime>

using namespace eMule;

class tst_Friend : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void construct_withHash();
    void construct_withoutHash();
    void hasUserhash_emptyIsFalse();
    void hasUserhash_nonEmptyIsTrue();
    void hasKadID_emptyIsFalse();
    void hasKadID_nonEmptyIsTrue();
    void setUserHash_nullClears();
    void setKadID_sets();
    void friendSlot_defaultFalse();
    void friendSlot_setAndGet();
    void nameAccessors();
    void ipPortAccessors();
    void timestampAccessors();
    void serialize_roundTrip();
    void serialize_withName();
    void serialize_withKadID();
    void serialize_empty();
};

// Helper: create a known hash
static std::array<uint8, 16> makeHash(uint8 fill)
{
    std::array<uint8, 16> h{};
    h.fill(fill);
    return h;
}

void tst_Friend::construct_default()
{
    Friend f;
    QVERIFY(!f.hasUserhash());
    QVERIFY(!f.hasKadID());
    QVERIFY(f.name().isEmpty());
    QCOMPARE(f.lastUsedIP(), 0u);
    QCOMPARE(f.lastUsedPort(), static_cast<uint16>(0));
    QCOMPARE(f.lastSeen(), std::time_t(0));
    QCOMPARE(f.lastChatted(), std::time_t(0));
    QVERIFY(!f.friendSlot());
}

void tst_Friend::construct_withHash()
{
    auto hash = makeHash(0xAB);
    Friend f(hash.data(), 1000, 0x7F000001, 4662, 500,
             QStringLiteral("Alice"), true);

    QVERIFY(f.hasUserhash());
    QVERIFY(md4equ(f.userHash().data(), hash.data()));
    QCOMPARE(f.name(), QStringLiteral("Alice"));
    QCOMPARE(f.lastUsedIP(), 0x7F000001u);
    QCOMPARE(f.lastUsedPort(), static_cast<uint16>(4662));
    QCOMPARE(f.lastSeen(), std::time_t(1000));
    QCOMPARE(f.lastChatted(), std::time_t(500));
}

void tst_Friend::construct_withoutHash()
{
    auto hash = makeHash(0xCD);
    Friend f(hash.data(), 2000, 0x0A000001, 4672, 0,
             QStringLiteral("Bob"), false);

    // hasHash=false means hash should NOT be stored
    QVERIFY(!f.hasUserhash());
    QCOMPARE(f.name(), QStringLiteral("Bob"));
    QCOMPARE(f.lastUsedIP(), 0x0A000001u);
}

void tst_Friend::hasUserhash_emptyIsFalse()
{
    Friend f;
    QVERIFY(!f.hasUserhash());
}

void tst_Friend::hasUserhash_nonEmptyIsTrue()
{
    auto hash = makeHash(0x01);
    Friend f(hash.data(), 0, 0, 0, 0, {}, true);
    QVERIFY(f.hasUserhash());
}

void tst_Friend::hasKadID_emptyIsFalse()
{
    Friend f;
    QVERIFY(!f.hasKadID());
}

void tst_Friend::hasKadID_nonEmptyIsTrue()
{
    Friend f;
    auto kadId = makeHash(0x55);
    f.setKadID(kadId.data());
    QVERIFY(f.hasKadID());
}

void tst_Friend::setUserHash_nullClears()
{
    auto hash = makeHash(0xAB);
    Friend f(hash.data(), 0, 0, 0, 0, {}, true);
    QVERIFY(f.hasUserhash());

    f.setUserHash(nullptr);
    QVERIFY(!f.hasUserhash());
}

void tst_Friend::setKadID_sets()
{
    Friend f;
    auto kadId = makeHash(0x77);
    f.setKadID(kadId.data());
    QVERIFY(f.hasKadID());
    QVERIFY(md4equ(f.kadID().data(), kadId.data()));

    f.setKadID(nullptr);
    QVERIFY(!f.hasKadID());
}

void tst_Friend::friendSlot_defaultFalse()
{
    Friend f;
    QVERIFY(!f.friendSlot());
}

void tst_Friend::friendSlot_setAndGet()
{
    Friend f;
    f.setFriendSlot(true);
    QVERIFY(f.friendSlot());
    f.setFriendSlot(false);
    QVERIFY(!f.friendSlot());
}

void tst_Friend::nameAccessors()
{
    Friend f;
    QVERIFY(f.name().isEmpty());
    f.setName(QStringLiteral("Charlie"));
    QCOMPARE(f.name(), QStringLiteral("Charlie"));
}

void tst_Friend::ipPortAccessors()
{
    Friend f;
    f.setLastUsedIP(0xC0A80001); // 192.168.0.1
    f.setLastUsedPort(4662);
    QCOMPARE(f.lastUsedIP(), 0xC0A80001u);
    QCOMPARE(f.lastUsedPort(), static_cast<uint16>(4662));
}

void tst_Friend::timestampAccessors()
{
    Friend f;
    f.setLastSeen(12345);
    f.setLastChatted(67890);
    QCOMPARE(f.lastSeen(), std::time_t(12345));
    QCOMPARE(f.lastChatted(), std::time_t(67890));
}

void tst_Friend::serialize_roundTrip()
{
    auto hash = makeHash(0xDE);
    Friend original(hash.data(), 1700000000, 0x0A0B0C0D, 4662,
                    1700001000, QStringLiteral("TestFriend"), true);
    auto kadId = makeHash(0xBE);
    original.setKadID(kadId.data());

    // Write to memory
    SafeMemFile mem;
    original.writeToFile(mem);

    // Read back
    mem.seek(0, 0);
    Friend loaded;
    loaded.loadFromFile(mem);

    QVERIFY(loaded.hasUserhash());
    QVERIFY(md4equ(loaded.userHash().data(), hash.data()));
    QCOMPARE(loaded.lastUsedIP(), 0x0A0B0C0Du);
    QCOMPARE(loaded.lastUsedPort(), static_cast<uint16>(4662));
    QCOMPARE(loaded.lastSeen(), std::time_t(1700000000));
    QCOMPARE(loaded.lastChatted(), std::time_t(1700001000));
    QCOMPARE(loaded.name(), QStringLiteral("TestFriend"));
    QVERIFY(loaded.hasKadID());
    QVERIFY(md4equ(loaded.kadID().data(), kadId.data()));
}

void tst_Friend::serialize_withName()
{
    Friend original;
    auto hash = makeHash(0x11);
    original.setUserHash(hash.data());
    original.setName(QStringLiteral("Named Friend"));

    SafeMemFile mem;
    original.writeToFile(mem);

    mem.seek(0, 0);
    Friend loaded;
    loaded.loadFromFile(mem);

    QCOMPARE(loaded.name(), QStringLiteral("Named Friend"));
}

void tst_Friend::serialize_withKadID()
{
    Friend original;
    auto hash = makeHash(0x22);
    original.setUserHash(hash.data());
    auto kadId = makeHash(0x99);
    original.setKadID(kadId.data());

    SafeMemFile mem;
    original.writeToFile(mem);

    mem.seek(0, 0);
    Friend loaded;
    loaded.loadFromFile(mem);

    QVERIFY(loaded.hasKadID());
    QVERIFY(md4equ(loaded.kadID().data(), kadId.data()));
}

void tst_Friend::serialize_empty()
{
    Friend original; // all defaults

    SafeMemFile mem;
    original.writeToFile(mem);

    mem.seek(0, 0);
    Friend loaded;
    loaded.loadFromFile(mem);

    QVERIFY(!loaded.hasUserhash());
    QVERIFY(!loaded.hasKadID());
    QVERIFY(loaded.name().isEmpty());
    QCOMPARE(loaded.lastUsedIP(), 0u);
}

QTEST_GUILESS_MAIN(tst_Friend)
#include "tst_Friend.moc"
