#include <QTest>
#include "crypto/MD4Hash.h"
#include "utils/OtherFunctions.h"

#include <QCryptographicHash>
#include <array>
#include <cstring>

using namespace eMule;

class tst_MD4 : public QObject {
    Q_OBJECT

private slots:
    void rfc1320_empty();
    void rfc1320_a();
    void rfc1320_abc();
    void rfc1320_messageDigest();
    void rfc1320_alphabet();
    void resetAndReuse();
    void chunkedAdd();
};

// RFC 1320 test vectors for MD4
// MD4("") = 31d6cfe0d16ae931b73c59d7e0c089c0
// MD4("a") = bde52cb31de33e46245e05fbdbd6fb24
// MD4("abc") = a448017aaf21d8525fc10ae87aa6729d
// MD4("message digest") = d9130a8164549fe818874806e1c7014b
// MD4("abcdefghijklmnopqrstuvwxyz") = d79e1c308aa5bbcdeea8ed63df412da9

static QString hashToHex(const uint8* hash)
{
    return encodeBase16(std::span<const uint8>(hash, 16));
}

void tst_MD4::rfc1320_empty()
{
    MD4Hasher h;
    h.add(nullptr, 0);
    h.finish();
    QCOMPARE(hashToHex(h.getHash()), u"31D6CFE0D16AE931B73C59D7E0C089C0");
}

void tst_MD4::rfc1320_a()
{
    MD4Hasher h;
    const char data[] = "a";
    h.add(data, 1);
    h.finish();
    QCOMPARE(hashToHex(h.getHash()), u"BDE52CB31DE33E46245E05FBDBD6FB24");
}

void tst_MD4::rfc1320_abc()
{
    MD4Hasher h;
    const char data[] = "abc";
    h.add(data, 3);
    h.finish();
    QCOMPARE(hashToHex(h.getHash()), u"A448017AAF21D8525FC10AE87AA6729D");
}

void tst_MD4::rfc1320_messageDigest()
{
    MD4Hasher h;
    const char data[] = "message digest";
    h.add(data, 14);
    h.finish();
    QCOMPARE(hashToHex(h.getHash()), u"D9130A8164549FE818874806E1C7014B");
}

void tst_MD4::rfc1320_alphabet()
{
    MD4Hasher h;
    const char data[] = "abcdefghijklmnopqrstuvwxyz";
    h.add(data, 26);
    h.finish();
    QCOMPARE(hashToHex(h.getHash()), u"D79E1C308AA5BBCDEEA8ED63DF412DA9");
}

void tst_MD4::resetAndReuse()
{
    MD4Hasher h;
    h.add("a", 1);
    h.finish();
    const QString first = hashToHex(h.getHash());
    QCOMPARE(first, u"BDE52CB31DE33E46245E05FBDBD6FB24");

    h.reset();
    h.add("abc", 3);
    h.finish();
    QCOMPARE(hashToHex(h.getHash()), u"A448017AAF21D8525FC10AE87AA6729D");
}

void tst_MD4::chunkedAdd()
{
    // "message digest" split across multiple add() calls
    MD4Hasher h;
    h.add("message", 7);
    h.add(" ", 1);
    h.add("digest", 6);
    h.finish();
    QCOMPARE(hashToHex(h.getHash()), u"D9130A8164549FE818874806E1C7014B");
}

QTEST_MAIN(tst_MD4)
#include "tst_MD4.moc"
