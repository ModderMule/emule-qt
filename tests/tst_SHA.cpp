#include <QTest>
#include "crypto/AICHHashSet.h"
#include "crypto/SHAHash.h"
#include "utils/OtherFunctions.h"

#include <cstring>
#include <memory>

using namespace eMule;
using namespace Qt::Literals::StringLiterals;

class tst_SHA : public QObject {
    Q_OBJECT

private slots:
    void sha1_empty();
    void sha1_abc();
    void sha1_longString();
    void aichHashAlgoInterface();
    void base32Roundtrip();
    void hashFromString_valid();
    void hashFromString_invalid();
    void hashFromURN_valid();
    void hashFromURN_invalid();
    void isNull_true();
    void isNull_false();
    void hexString();
};

// SHA-1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
// SHA-1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
// SHA-1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") =
//   84983e441c3bd26ebaae4aa1f95129e5e54670f1

void tst_SHA::sha1_empty()
{
    ShaHasher h;
    h.add("", 0);
    h.finish();
    Sha1Digest d;
    h.getHash(&d);
    const QString hex = ShaHasher::hashToHexString(&d);
    QCOMPARE(hex, u"DA39A3EE5E6B4B0D3255BFEF95601890AFD80709");
}

void tst_SHA::sha1_abc()
{
    ShaHasher h;
    h.add("abc", 3);
    h.finish();
    Sha1Digest d;
    h.getHash(&d);
    QCOMPARE(ShaHasher::hashToHexString(&d),
             u"A9993E364706816ABA3E25717850C26C9CD0D89D");
}

void tst_SHA::sha1_longString()
{
    ShaHasher h;
    const char* input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    h.add(input, static_cast<uint32>(std::strlen(input)));
    h.finish();
    Sha1Digest d;
    h.getHash(&d);
    QCOMPARE(ShaHasher::hashToHexString(&d),
             u"84983E441C3BD26EBAAE4AA1F95129E5E54670F1");
}

void tst_SHA::aichHashAlgoInterface()
{
    // Test through AICHHashAlgo interface
    std::unique_ptr<AICHHashAlgo> algo(AICHRecoveryHashSet::getNewHashAlgo());
    algo->add("abc", 3);
    AICHHash hash;
    algo->finish(hash);

    // SHA-1("abc") base32 = QNRAU7BFPFOQPGQIJQGO7BGANSSRD6E5 (verify via known output)
    const QString b32 = hash.getString();
    QCOMPARE(b32.length(), 32); // Base32 of 20 bytes = 32 chars

    // Decode back and compare raw bytes
    std::array<uint8, 20> decoded{};
    const auto n = decodeBase32(b32, decoded.data(), 20);
    QCOMPARE(n, 20u);
    QVERIFY(std::memcmp(decoded.data(), hash.getRawHash(), 20) == 0);
}

void tst_SHA::base32Roundtrip()
{
    ShaHasher h;
    h.add("test data", 9);
    h.finish();
    Sha1Digest d;
    h.getHash(&d);

    // Base32 encode then decode
    const QString b32 = ShaHasher::hashToString(&d);
    QCOMPARE(b32.length(), 32);

    Sha1Digest decoded{};
    QVERIFY(ShaHasher::hashFromString(b32, &decoded));
    QCOMPARE(d, decoded);
}

void tst_SHA::hashFromString_valid()
{
    // Use a known SHA-1 hash in Base32
    ShaHasher h;
    h.add("abc", 3);
    h.finish();
    Sha1Digest original;
    h.getHash(&original);

    const QString b32 = ShaHasher::hashToString(&original);
    Sha1Digest parsed{};
    QVERIFY(ShaHasher::hashFromString(b32, &parsed));
    QCOMPARE(original, parsed);
}

void tst_SHA::hashFromString_invalid()
{
    Sha1Digest d{};
    // Too short
    QVERIFY(!ShaHasher::hashFromString(u"ABC"_s, &d));
    // All-A (null hash rejected)
    QVERIFY(!ShaHasher::hashFromString(u"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"_s, &d));
}

void tst_SHA::hashFromURN_valid()
{
    ShaHasher h;
    h.add("abc", 3);
    h.finish();
    Sha1Digest original;
    h.getHash(&original);
    const QString b32 = ShaHasher::hashToString(&original);

    Sha1Digest parsed{};
    QVERIFY(ShaHasher::hashFromURN(u"urn:sha1:" + b32, &parsed));
    QCOMPARE(original, parsed);

    Sha1Digest parsed2{};
    QVERIFY(ShaHasher::hashFromURN(u"sha1:" + b32, &parsed2));
    QCOMPARE(original, parsed2);
}

void tst_SHA::hashFromURN_invalid()
{
    Sha1Digest d{};
    QVERIFY(!ShaHasher::hashFromURN(u""_s, &d));
    QVERIFY(!ShaHasher::hashFromURN(u"invalid"_s, &d));
}

void tst_SHA::isNull_true()
{
    Sha1Digest d{};
    QVERIFY(ShaHasher::isNull(&d));
}

void tst_SHA::isNull_false()
{
    Sha1Digest d{};
    d.b[0] = 0x42;
    QVERIFY(!ShaHasher::isNull(&d));
}

void tst_SHA::hexString()
{
    ShaHasher h;
    h.add("abc", 3);
    h.finish();
    Sha1Digest d;
    h.getHash(&d);
    const QString hex = ShaHasher::hashToHexString(&d);
    QCOMPARE(hex.length(), 40);
    for (QChar c : hex) {
        QVERIFY(c.isDigit() || (c >= u'A' && c <= u'F'));
    }
}

QTEST_MAIN(tst_SHA)
#include "tst_SHA.moc"
