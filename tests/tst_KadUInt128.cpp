/// @file tst_KadUInt128.cpp
/// @brief Tests for KadUInt128.h — 128-bit integer for Kademlia.

#include "TestHelpers.h"

#include "kademlia/KadUInt128.h"

#include <QTest>

#include <array>
#include <cstring>

using namespace eMule;
using namespace eMule::kad;

class tst_KadUInt128 : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void construct_fillFalse();
    void construct_fillTrue();
    void construct_value();
    void construct_BE();
    void construct_copy();

    void setValue_random();
    void setValue_GUID();

    void toHexString();
    void toBinaryString_noTrim();
    void toBinaryString_trim();
    void toByteArray_roundTrip();

    void getBitNumber();
    void setBitNumber();

    void add_basic();
    void add_overflow();
    void subtract_basic();
    void xorWith();
    void xorBE();
    void shiftLeft();
    void shiftLeft_zeroAndLarge();

    void compareTo_UInt128();
    void compareTo_uint32();

    void operators_comparison();
    void operators_arithmetic();
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void tst_KadUInt128::construct_default()
{
    UInt128 v;
    QCOMPARE(v.get32BitChunk(0), uint32{0});
    QCOMPARE(v.get32BitChunk(1), uint32{0});
    QCOMPARE(v.get32BitChunk(2), uint32{0});
    QCOMPARE(v.get32BitChunk(3), uint32{0});
}

void tst_KadUInt128::construct_fillFalse()
{
    UInt128 v(false);
    QCOMPARE(v, uint32{0});
}

void tst_KadUInt128::construct_fillTrue()
{
    UInt128 v(true);
    QCOMPARE(v.get32BitChunk(0), uint32{0xFFFFFFFF});
    QCOMPARE(v.get32BitChunk(1), uint32{0xFFFFFFFF});
    QCOMPARE(v.get32BitChunk(2), uint32{0xFFFFFFFF});
    QCOMPARE(v.get32BitChunk(3), uint32{0xFFFFFFFF});
}

void tst_KadUInt128::construct_value()
{
    UInt128 v(uint32{42});
    QCOMPARE(v.get32BitChunk(3), uint32{42});
    QCOMPARE(v.get32BitChunk(0), uint32{0});
    QCOMPARE(v.get32BitChunk(1), uint32{0});
    QCOMPARE(v.get32BitChunk(2), uint32{0});
}

void tst_KadUInt128::construct_BE()
{
    // Big-endian bytes: 0x00112233 44556677 8899AABB CCDDEEFF
    const uint8 be[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
    };
    UInt128 v(be);
    QCOMPARE(v.get32BitChunk(0), uint32{0x00112233});
    QCOMPARE(v.get32BitChunk(1), uint32{0x44556677});
    QCOMPARE(v.get32BitChunk(2), uint32{0x8899AABB});
    QCOMPARE(v.get32BitChunk(3), uint32{0xCCDDEEFF});
}

void tst_KadUInt128::construct_copy()
{
    UInt128 original;
    original.setValueRandom();
    UInt128 copy(original, 64); // copy top 64 bits, randomise rest

    // Top 64 bits must match
    QCOMPARE(copy.get32BitChunk(0), original.get32BitChunk(0));
    QCOMPARE(copy.get32BitChunk(1), original.get32BitChunk(1));
    // Lower bits may differ (random)
}

// ---------------------------------------------------------------------------
// Random / GUID
// ---------------------------------------------------------------------------

void tst_KadUInt128::setValue_random()
{
    UInt128 v;
    v.setValueRandom();
    // Extremely unlikely to be zero
    QVERIFY(v != uint32{0});
}

void tst_KadUInt128::setValue_GUID()
{
    UInt128 v;
    v.setValueGUID();
    QVERIFY(v != uint32{0});

    // Two GUIDs should be different
    UInt128 v2;
    v2.setValueGUID();
    QVERIFY(v != v2);
}

// ---------------------------------------------------------------------------
// String conversion
// ---------------------------------------------------------------------------

void tst_KadUInt128::toHexString()
{
    UInt128 v(uint32{0xFF});
    // Should be 00000000000000000000000000000FF
    QString hex = v.toHexString();
    QCOMPARE(hex.length(), 32);
    QCOMPARE(hex, QStringLiteral("000000000000000000000000000000FF"));
}

void tst_KadUInt128::toBinaryString_noTrim()
{
    UInt128 v(uint32{5}); // binary: ...101
    QString bin = v.toBinaryString(false);
    QCOMPARE(bin.length(), 128);
    QVERIFY(bin.endsWith(QStringLiteral("101")));
    // First 125 chars should be 0
    QCOMPARE(bin.left(125), QString(125, u'0'));
}

void tst_KadUInt128::toBinaryString_trim()
{
    UInt128 v(uint32{5}); // binary: 101
    QString bin = v.toBinaryString(true);
    QCOMPARE(bin, QStringLiteral("101"));
}

// ---------------------------------------------------------------------------
// Byte array round-trip
// ---------------------------------------------------------------------------

void tst_KadUInt128::toByteArray_roundTrip()
{
    const uint8 be[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    UInt128 v(be);

    uint8 out[16] = {};
    v.toByteArray(out);
    QVERIFY(std::memcmp(be, out, 16) == 0);
}

// ---------------------------------------------------------------------------
// Bit manipulation
// ---------------------------------------------------------------------------

void tst_KadUInt128::getBitNumber()
{
    UInt128 v(uint32{1}); // only bit 127 set
    QCOMPARE(v.getBitNumber(127), uint32{1});
    QCOMPARE(v.getBitNumber(126), uint32{0});
    QCOMPARE(v.getBitNumber(0), uint32{0});

    // Out of range
    QCOMPARE(v.getBitNumber(128), uint32{0});
}

void tst_KadUInt128::setBitNumber()
{
    UInt128 v;
    v.setBitNumber(0, 1); // Set MSB
    QCOMPARE(v.getBitNumber(0), uint32{1});
    QCOMPARE(v.get32BitChunk(0), uint32{0x80000000});

    v.setBitNumber(0, 0); // Clear MSB
    QCOMPARE(v.getBitNumber(0), uint32{0});
    QCOMPARE(v, uint32{0});
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

void tst_KadUInt128::add_basic()
{
    UInt128 a(uint32{100});
    UInt128 b(uint32{200});
    a.add(b);
    QCOMPARE(a.get32BitChunk(3), uint32{300});
}

void tst_KadUInt128::add_overflow()
{
    UInt128 a(uint32{0xFFFFFFFF});
    a.add(uint32{1});
    // Should carry into chunk[2]
    QCOMPARE(a.get32BitChunk(3), uint32{0});
    QCOMPARE(a.get32BitChunk(2), uint32{1});
}

void tst_KadUInt128::subtract_basic()
{
    UInt128 a(uint32{300});
    UInt128 b(uint32{100});
    a.subtract(b);
    QCOMPARE(a.get32BitChunk(3), uint32{200});
}

void tst_KadUInt128::xorWith()
{
    UInt128 a(uint32{0xFF00FF00});
    UInt128 b(uint32{0x0F0F0F0F});
    a.xorWith(b);
    QCOMPARE(a.get32BitChunk(3), uint32{0xF00FF00F});
}

void tst_KadUInt128::xorBE()
{
    UInt128 a;
    a.setValue(uint32{0});

    const uint8 be[16] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    a.xorBE(be);
    QCOMPARE(a.get32BitChunk(0), uint32{0xAABBCCDD});
}

void tst_KadUInt128::shiftLeft()
{
    UInt128 v(uint32{1});
    v.shiftLeft(1);
    QCOMPARE(v.get32BitChunk(3), uint32{2});

    v.shiftLeft(31);
    // Was 2 (bit 126), shift left 31 → bit 95 → chunk[2] MSB
    QCOMPARE(v.get32BitChunk(2), uint32{1});
    QCOMPARE(v.get32BitChunk(3), uint32{0});
}

void tst_KadUInt128::shiftLeft_zeroAndLarge()
{
    UInt128 v(uint32{42});
    v.shiftLeft(0);
    QCOMPARE(v.get32BitChunk(3), uint32{42});

    v.shiftLeft(128);
    QCOMPARE(v, uint32{0});
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

void tst_KadUInt128::compareTo_UInt128()
{
    UInt128 a(uint32{100});
    UInt128 b(uint32{200});
    QCOMPARE(a.compareTo(b), -1);
    QCOMPARE(b.compareTo(a), 1);
    QCOMPARE(a.compareTo(a), 0);
}

void tst_KadUInt128::compareTo_uint32()
{
    UInt128 v(uint32{100});
    QCOMPARE(v.compareTo(uint32{50}), 1);
    QCOMPARE(v.compareTo(uint32{100}), 0);
    QCOMPARE(v.compareTo(uint32{200}), -1);

    // Value with high bits set
    UInt128 big;
    big.setBitNumber(0, 1);
    QCOMPARE(big.compareTo(uint32{0xFFFFFFFF}), 1);
}

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

void tst_KadUInt128::operators_comparison()
{
    UInt128 a(uint32{100});
    UInt128 b(uint32{200});

    QVERIFY(a < b);
    QVERIFY(b > a);
    QVERIFY(a <= b);
    QVERIFY(b >= a);
    QVERIFY(a == a);
    QVERIFY(a != b);

    // Against uint32
    QVERIFY(a == uint32{100});
    QVERIFY(a != uint32{200});
    QVERIFY(a < uint32{200});
    QVERIFY(a > uint32{50});
}

void tst_KadUInt128::operators_arithmetic()
{
    UInt128 a(uint32{100});
    a += UInt128(uint32{50});
    QCOMPARE(a, uint32{150});

    a -= uint32{25};
    QCOMPARE(a, uint32{125});
}

QTEST_MAIN(tst_KadUInt128)
#include "tst_KadUInt128.moc"
