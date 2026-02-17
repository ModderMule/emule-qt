/// @file tst_OtherFunctions.cpp
/// @brief Tests for OtherFunctions.h — encoding, IP helpers, RC4, file types, etc.

#include "TestHelpers.h"

#include "utils/OtherFunctions.h"

#include <QTest>

#include <array>
#include <cstring>

using namespace eMule;

class tst_OtherFunctions : public QObject {
    Q_OBJECT

private slots:
    // MD4 helpers
    void md4equ_identical();
    void md4equ_different();
    void isnulmd4_zero();
    void isnulmd4_nonzero();
    void md4clr_zeros();
    void md4cpy_copies();
    void md4str_roundtrip();
    void strmd4_invalidLength();

    // Base16
    void encodeBase16_knownVector();
    void decodeBase16_roundtrip();
    void decodeBase16_invalidChar();
    void decodeBase16_oddLength();

    // Base32
    void encodeBase32_knownVector();
    void decodeBase32_roundtrip();
    void decodeBase32_invalidChar();

    // URL encode/decode
    void urlEncode_spaces();
    void urlDecode_roundtrip();

    // IP helpers
    void isLowID_boundary();
    void isLanIP_private();
    void isLanIP_public();
    void isGoodIP_zero();
    void ipstr_format();
    void ipstr_withPort();

    // Random
    void randomUInt16_range();
    void randomUInt32_notConstant();

    // RC4
    void rc4_encryptDecrypt();
    void rc4_inPlace();

    // File types
    void ed2kFileType_audio();
    void ed2kFileType_video();
    void ed2kFileType_unknown();

    // Comparison
    void compareUnsigned_ordering();

    // String helpers
    void stripInvalidFilenameChars_cleans();
    void stringLimit_short();
    void stringLimit_long();
    void levenshteinDistance_identical();
    void levenshteinDistance_different();

    // Peek / Poke
    void peekPoke_uint8();
    void peekPoke_uint16();
    void peekPoke_uint32();
    void peekPoke_uint64();
};

// ---------------------------------------------------------------------------
// MD4 helpers
// ---------------------------------------------------------------------------

void tst_OtherFunctions::md4equ_identical()
{
    const std::array<uint8, 16> h = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    QVERIFY(md4equ(h.data(), h.data()));
}

void tst_OtherFunctions::md4equ_different()
{
    const std::array<uint8, 16> a = {};
    std::array<uint8, 16> b = {};
    b[15] = 1;
    QVERIFY(!md4equ(a.data(), b.data()));
}

void tst_OtherFunctions::isnulmd4_zero()
{
    const std::array<uint8, 16> z = {};
    QVERIFY(isnulmd4(z.data()));
}

void tst_OtherFunctions::isnulmd4_nonzero()
{
    std::array<uint8, 16> h = {};
    h[0] = 0xFF;
    QVERIFY(!isnulmd4(h.data()));
}

void tst_OtherFunctions::md4clr_zeros()
{
    std::array<uint8, 16> h;
    h.fill(0xFF);
    md4clr(h.data());
    QVERIFY(isnulmd4(h.data()));
}

void tst_OtherFunctions::md4cpy_copies()
{
    const std::array<uint8, 16> src = {0xDE,0xAD,0xBE,0xEF, 1,2,3,4, 5,6,7,8, 9,10,11,12};
    std::array<uint8, 16> dst = {};
    md4cpy(dst.data(), src.data());
    QVERIFY(md4equ(src.data(), dst.data()));
}

void tst_OtherFunctions::md4str_roundtrip()
{
    const std::array<uint8, 16> hash = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
                                         0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10};
    const QString hex = md4str(hash.data());
    QCOMPARE(hex, QStringLiteral("0123456789ABCDEFFEDCBA9876543210"));

    std::array<uint8, 16> parsed = {};
    QVERIFY(strmd4(hex, parsed.data()));
    QVERIFY(md4equ(hash.data(), parsed.data()));
}

void tst_OtherFunctions::strmd4_invalidLength()
{
    std::array<uint8, 16> h = {};
    QVERIFY(!strmd4(QStringLiteral("ABC"), h.data()));
}

// ---------------------------------------------------------------------------
// Base16
// ---------------------------------------------------------------------------

void tst_OtherFunctions::encodeBase16_knownVector()
{
    const std::array<uint8, 4> data = {0xDE, 0xAD, 0xBE, 0xEF};
    QCOMPARE(encodeBase16(data), QStringLiteral("DEADBEEF"));
}

void tst_OtherFunctions::decodeBase16_roundtrip()
{
    const std::array<uint8, 5> original = {0x00, 0xFF, 0x42, 0x7F, 0x80};
    const QString hex = encodeBase16(original);

    std::array<uint8, 5> decoded = {};
    QCOMPARE(decodeBase16(hex, decoded.data(), decoded.size()), 5u);
    QVERIFY(std::memcmp(original.data(), decoded.data(), 5) == 0);
}

void tst_OtherFunctions::decodeBase16_invalidChar()
{
    std::array<uint8, 2> buf = {};
    QCOMPARE(decodeBase16(QStringLiteral("ZZZZ"), buf.data(), buf.size()), 0u);
}

void tst_OtherFunctions::decodeBase16_oddLength()
{
    std::array<uint8, 2> buf = {};
    QCOMPARE(decodeBase16(QStringLiteral("ABC"), buf.data(), buf.size()), 0u);
}

// ---------------------------------------------------------------------------
// Base32
// ---------------------------------------------------------------------------

void tst_OtherFunctions::encodeBase32_knownVector()
{
    // "Hello" in base32 = "JBSWY3DP"
    const std::array<uint8, 5> data = {'H', 'e', 'l', 'l', 'o'};
    QCOMPARE(encodeBase32(data), QStringLiteral("JBSWY3DP"));
}

void tst_OtherFunctions::decodeBase32_roundtrip()
{
    const std::array<uint8, 8> original = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    const QString encoded = encodeBase32(original);

    std::array<uint8, 8> decoded = {};
    QCOMPARE(decodeBase32(encoded, decoded.data(), decoded.size()), 8u);
    QVERIFY(std::memcmp(original.data(), decoded.data(), 8) == 0);
}

void tst_OtherFunctions::decodeBase32_invalidChar()
{
    std::array<uint8, 2> buf = {};
    QCOMPARE(decodeBase32(QStringLiteral("!!!"), buf.data(), buf.size()), 0u);
}

// ---------------------------------------------------------------------------
// URL encode/decode
// ---------------------------------------------------------------------------

void tst_OtherFunctions::urlEncode_spaces()
{
    const QString result = urlEncode(QStringLiteral("hello world"));
    QVERIFY(result.contains(QStringLiteral("%20")));
    QVERIFY(!result.contains(QChar(u' ')));
}

void tst_OtherFunctions::urlDecode_roundtrip()
{
    const QString original = QStringLiteral("a=1&b=hello world");
    const QString encoded = urlEncode(original);
    const QString decoded = urlDecode(encoded);
    QCOMPARE(decoded, original);
}

// ---------------------------------------------------------------------------
// IP helpers
// ---------------------------------------------------------------------------

void tst_OtherFunctions::isLowID_boundary()
{
    QVERIFY(isLowID(0));
    QVERIFY(isLowID(0x00FFFFFFu));
    QVERIFY(!isLowID(0x01000000u));
    QVERIFY(!isLowID(0xFFFFFFFFu));
}

void tst_OtherFunctions::isLanIP_private()
{
    // 10.0.0.1 in network byte order (little-endian): 10 is the first byte
    QVERIFY(isLanIP(10));           // 10.0.0.0
    QVERIFY(isLanIP(127));          // 127.0.0.0
    QVERIFY(isLanIP(192 | (168 << 8)));   // 192.168.0.0
}

void tst_OtherFunctions::isLanIP_public()
{
    // 8.8.8.8 in network byte order
    const uint32 ip = 8 | (8 << 8) | (8 << 16) | (8 << 24);
    QVERIFY(!isLanIP(ip));
}

void tst_OtherFunctions::isGoodIP_zero()
{
    QVERIFY(!isGoodIP(0));
}

void tst_OtherFunctions::ipstr_format()
{
    // 192.168.1.100 in network byte order
    const uint32 ip = 192 | (168 << 8) | (1 << 16) | (100 << 24);
    QCOMPARE(ipstr(ip), QStringLiteral("192.168.1.100"));
}

void tst_OtherFunctions::ipstr_withPort()
{
    const uint32 ip = 192 | (168 << 8) | (1 << 16) | (100 << 24);
    QCOMPARE(ipstr(ip, 4662), QStringLiteral("192.168.1.100:4662"));
}

// ---------------------------------------------------------------------------
// Random
// ---------------------------------------------------------------------------

void tst_OtherFunctions::randomUInt16_range()
{
    for (int i = 0; i < 100; ++i) {
        const uint16 val = getRandomUInt16();
        QVERIFY(val <= 0xFFFF);
    }
}

void tst_OtherFunctions::randomUInt32_notConstant()
{
    const uint32 first = getRandomUInt32();
    bool different = false;
    for (int i = 0; i < 10; ++i) {
        if (getRandomUInt32() != first) {
            different = true;
            break;
        }
    }
    QVERIFY(different);
}

// ---------------------------------------------------------------------------
// RC4
// ---------------------------------------------------------------------------

void tst_OtherFunctions::rc4_encryptDecrypt()
{
    const std::array<uint8, 5> keyData = {0x01, 0x02, 0x03, 0x04, 0x05};
    const std::array<uint8, 8> plaintext = {'T', 'e', 's', 't', 'D', 'a', 't', 'a'};

    auto encKey = rc4CreateKey(keyData, true);
    std::array<uint8, 8> ciphertext = {};
    rc4Crypt(plaintext.data(), ciphertext.data(), 8, encKey);

    // Ciphertext should differ from plaintext
    QVERIFY(std::memcmp(plaintext.data(), ciphertext.data(), 8) != 0);

    // Decrypt with fresh key
    auto decKey = rc4CreateKey(keyData, true);
    std::array<uint8, 8> decrypted = {};
    rc4Crypt(ciphertext.data(), decrypted.data(), 8, decKey);

    QVERIFY(std::memcmp(plaintext.data(), decrypted.data(), 8) == 0);
}

void tst_OtherFunctions::rc4_inPlace()
{
    const std::array<uint8, 4> keyData = {0xAB, 0xCD, 0xEF, 0x01};
    std::array<uint8, 6> data = {'A', 'B', 'C', 'D', 'E', 'F'};
    const auto original = data;

    auto key1 = rc4CreateKey(keyData, true);
    rc4Crypt(data.data(), 6, key1);
    QVERIFY(std::memcmp(data.data(), original.data(), 6) != 0);

    auto key2 = rc4CreateKey(keyData, true);
    rc4Crypt(data.data(), 6, key2);
    QVERIFY(std::memcmp(data.data(), original.data(), 6) == 0);
}

// ---------------------------------------------------------------------------
// File types
// ---------------------------------------------------------------------------

void tst_OtherFunctions::ed2kFileType_audio()
{
    QCOMPARE(getED2KFileTypeID(QStringLiteral("song.mp3")), ED2KFileType::Audio);
    QCOMPARE(getED2KFileTypeID(QStringLiteral("track.FLAC")), ED2KFileType::Audio);
}

void tst_OtherFunctions::ed2kFileType_video()
{
    QCOMPARE(getED2KFileTypeID(QStringLiteral("movie.avi")), ED2KFileType::Video);
    QCOMPARE(getED2KFileTypeID(QStringLiteral("clip.MKV")), ED2KFileType::Video);
}

void tst_OtherFunctions::ed2kFileType_unknown()
{
    QCOMPARE(getED2KFileTypeID(QStringLiteral("readme.xyz")), ED2KFileType::Any);
    QCOMPARE(getED2KFileTypeID(QStringLiteral("noextension")), ED2KFileType::Any);
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

void tst_OtherFunctions::compareUnsigned_ordering()
{
    QCOMPARE(compareUnsigned(1u, 2u), -1);
    QCOMPARE(compareUnsigned(2u, 2u), 0);
    QCOMPARE(compareUnsigned(3u, 2u), 1);
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

void tst_OtherFunctions::stripInvalidFilenameChars_cleans()
{
    const QString dirty = QStringLiteral("file<name>:test");
    const QString clean = stripInvalidFilenameChars(dirty);
    QVERIFY(!clean.contains(QChar(u'<')));
    QVERIFY(!clean.contains(QChar(u'>')));
    QVERIFY(!clean.contains(QChar(u':')));
    QVERIFY(clean.contains(QChar(u'_')));
}

void tst_OtherFunctions::stringLimit_short()
{
    const QString s = QStringLiteral("Hello");
    QCOMPARE(stringLimit(s, 10), s);
}

void tst_OtherFunctions::stringLimit_long()
{
    const QString s = QStringLiteral("This is a very long string");
    const QString limited = stringLimit(s, 10);
    QCOMPARE(limited.size(), 10);
    QVERIFY(limited.endsWith(QStringLiteral("...")));
}

void tst_OtherFunctions::levenshteinDistance_identical()
{
    QCOMPARE(levenshteinDistance(QStringLiteral("hello"), QStringLiteral("hello")), 0u);
}

void tst_OtherFunctions::levenshteinDistance_different()
{
    QCOMPARE(levenshteinDistance(QStringLiteral("kitten"), QStringLiteral("sitting")), 3u);
}

// ---------------------------------------------------------------------------
// Peek / Poke
// ---------------------------------------------------------------------------

void tst_OtherFunctions::peekPoke_uint8()
{
    uint8 buf = 0;
    pokeUInt8(&buf, 0x42);
    QCOMPARE(peekUInt8(&buf), uint8{0x42});
}

void tst_OtherFunctions::peekPoke_uint16()
{
    std::array<uint8, 2> buf = {};
    pokeUInt16(buf.data(), 0x1234);
    QCOMPARE(peekUInt16(buf.data()), uint16{0x1234});
}

void tst_OtherFunctions::peekPoke_uint32()
{
    std::array<uint8, 4> buf = {};
    pokeUInt32(buf.data(), 0xDEADBEEF);
    QCOMPARE(peekUInt32(buf.data()), uint32{0xDEADBEEF});
}

void tst_OtherFunctions::peekPoke_uint64()
{
    std::array<uint8, 8> buf = {};
    pokeUInt64(buf.data(), UINT64_C(0x0102030405060708));
    QCOMPARE(peekUInt64(buf.data()), uint64{UINT64_C(0x0102030405060708)});
}

QTEST_MAIN(tst_OtherFunctions)
#include "tst_OtherFunctions.moc"
