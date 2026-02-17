/// @file tst_KadMiscUtils.cpp
/// @brief Tests for KadMiscUtils.h — Kad utility functions.

#include "TestHelpers.h"

#include "kademlia/KadMiscUtils.h"
#include "kademlia/KadUInt128.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;

class tst_KadMiscUtils : public QObject {
    Q_OBJECT

private slots:
    void ipToString_basic();
    void getKeywordHash_deterministic();
    void getKeywordHash_different();
    void getWords_basic();
    void getWords_specialChars();
    void kadTagStrToLower_basic();
};

void tst_KadMiscUtils::ipToString_basic()
{
    QCOMPARE(ipToString(0x0A000001), QStringLiteral("10.0.0.1"));
    QCOMPARE(ipToString(0xC0A80164), QStringLiteral("192.168.1.100"));
    QCOMPARE(ipToString(0x00000000), QStringLiteral("0.0.0.0"));
    QCOMPARE(ipToString(0xFFFFFFFF), QStringLiteral("255.255.255.255"));
}

void tst_KadMiscUtils::getKeywordHash_deterministic()
{
    UInt128 hash1, hash2;
    getKeywordHash(QStringLiteral("test"), hash1);
    getKeywordHash(QStringLiteral("test"), hash2);
    QCOMPARE(hash1, hash2);
}

void tst_KadMiscUtils::getKeywordHash_different()
{
    UInt128 hash1, hash2;
    getKeywordHash(QStringLiteral("hello"), hash1);
    getKeywordHash(QStringLiteral("world"), hash2);
    QVERIFY(hash1 != hash2);
}

void tst_KadMiscUtils::getWords_basic()
{
    std::vector<QString> words;
    getWords(QStringLiteral("hello world test"), words);
    QCOMPARE(words.size(), size_t{3});
    QCOMPARE(words[0], QStringLiteral("hello"));
    QCOMPARE(words[1], QStringLiteral("world"));
    QCOMPARE(words[2], QStringLiteral("test"));
}

void tst_KadMiscUtils::getWords_specialChars()
{
    std::vector<QString> words;
    getWords(QStringLiteral("file_name-v2.0 (test)[final]"), words);
    // Should split on _ - . ( ) [ ]
    QVERIFY(words.size() >= 4); // "file", "name", "v2", "0", "test", "final"
    QCOMPARE(words[0], QStringLiteral("file"));
    QCOMPARE(words[1], QStringLiteral("name"));
}

void tst_KadMiscUtils::kadTagStrToLower_basic()
{
    QCOMPARE(kadTagStrToLower(QStringLiteral("Hello World")),
             QStringLiteral("hello world"));
    QCOMPARE(kadTagStrToLower(QStringLiteral("UPPERCASE")),
             QStringLiteral("uppercase"));
    QCOMPARE(kadTagStrToLower(QStringLiteral("already lower")),
             QStringLiteral("already lower"));
}

QTEST_GUILESS_MAIN(tst_KadMiscUtils)
#include "tst_KadMiscUtils.moc"
