/// @file tst_Tags.cpp
/// @brief Tests for protocol/Tag — construction, serialization round-trips, edge cases.

#include "TestHelpers.h"
#include "protocol/Tag.h"
#include "utils/SafeFile.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QTest>

#include <array>
#include <cstring>

using namespace eMule;

class tst_Tags : public QObject {
    Q_OBJECT

private slots:
    // Construction variants
    void construct_uint32();
    void construct_uint64();
    void construct_string();
    void construct_float();
    void construct_hash();
    void construct_blob();
    void construct_stringName_uint32();
    void construct_stringName_uint64();
    void construct_stringName_string();

    // Deserialization round-trip (old format)
    void roundTrip_uint32();
    void roundTrip_uint64();
    void roundTrip_string();
    void roundTrip_float();
    void roundTrip_hash();
    void roundTrip_blob();

    // New ED2K format round-trip
    void newFormat_smallInt_optimized();
    void newFormat_mediumInt_optimized();
    void newFormat_fullUint32();
    void newFormat_uint64();
    void newFormat_shortString_optimized();
    void newFormat_longString();

    // String name vs ID name through serialization
    void roundTrip_numericIdName();
    void roundTrip_stringName();

    // Edge cases
    void edgeCase_emptyString();
    void edgeCase_zeroBlob();
    void edgeCase_maxUint64();

    // Mutators
    void mutator_setInt();
    void mutator_setInt64();
    void mutator_setStr();
};

// ---------------------------------------------------------------------------
// Construction tests
// ---------------------------------------------------------------------------

void tst_Tags::construct_uint32()
{
    Tag tag(FT_FILESIZE, uint32{12345});
    QCOMPARE(tag.type(), static_cast<uint8>(TAGTYPE_UINT32));
    QCOMPARE(tag.nameId(), static_cast<uint8>(FT_FILESIZE));
    QVERIFY(!tag.hasName());
    QVERIFY(tag.isInt());
    QCOMPARE(tag.intValue(), uint32{12345});
}

void tst_Tags::construct_uint64()
{
    Tag tag(FT_FILESIZE, uint64{UINT64_C(0x100000000)});
    QCOMPARE(tag.type(), static_cast<uint8>(TAGTYPE_UINT64));
    QCOMPARE(tag.nameId(), static_cast<uint8>(FT_FILESIZE));
    QVERIFY(tag.isInt64());
    QCOMPARE(tag.int64Value(), UINT64_C(0x100000000));
}

void tst_Tags::construct_string()
{
    Tag tag(FT_FILENAME, QStringLiteral("test.mp3"));
    QCOMPARE(tag.type(), static_cast<uint8>(TAGTYPE_STRING));
    QVERIFY(tag.isStr());
    QCOMPARE(tag.strValue(), QStringLiteral("test.mp3"));
}

void tst_Tags::construct_float()
{
    Tag tag(uint8{0x10}, 3.14f);
    QCOMPARE(tag.type(), static_cast<uint8>(TAGTYPE_FLOAT32));
    QVERIFY(tag.isFloat());
    QCOMPARE(tag.floatValue(), 3.14f);
}

void tst_Tags::construct_hash()
{
    const std::array<uint8, 16> hash = {
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10
    };
    Tag tag(FT_FILEHASH, hash.data());
    QCOMPARE(tag.type(), static_cast<uint8>(TAGTYPE_HASH));
    QVERIFY(tag.isHash());
    QVERIFY(md4equ(tag.hashValue(), hash.data()));
}

void tst_Tags::construct_blob()
{
    QByteArray data("blobdata", 8);
    Tag tag(uint8{0x20}, data);
    QCOMPARE(tag.type(), static_cast<uint8>(TAGTYPE_BLOB));
    QVERIFY(tag.isBlob());
    QCOMPARE(tag.blobValue(), data);
}

void tst_Tags::construct_stringName_uint32()
{
    Tag tag(QByteArray("testname"), uint32{42});
    QVERIFY(tag.hasName());
    QCOMPARE(tag.name(), QByteArray("testname"));
    QCOMPARE(tag.nameId(), uint8{0});
    QVERIFY(tag.isInt());
    QCOMPARE(tag.intValue(), uint32{42});
}

void tst_Tags::construct_stringName_uint64()
{
    Tag tag(QByteArray("big"), uint64{UINT64_C(0xFFFFFFFFFF)});
    QVERIFY(tag.hasName());
    QVERIFY(tag.isInt64());
    QCOMPARE(tag.int64Value(), UINT64_C(0xFFFFFFFFFF));
}

void tst_Tags::construct_stringName_string()
{
    Tag tag(QByteArray("key"), QStringLiteral("value"));
    QVERIFY(tag.hasName());
    QVERIFY(tag.isStr());
    QCOMPARE(tag.strValue(), QStringLiteral("value"));
}

// ---------------------------------------------------------------------------
// Deserialization round-trip (old format via writeTagToFile)
// ---------------------------------------------------------------------------

void tst_Tags::roundTrip_uint32()
{
    Tag original(FT_FILESIZE, uint32{99999});
    SafeMemFile f;
    original.writeTagToFile(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_UINT32));
    QCOMPARE(restored.nameId(), static_cast<uint8>(FT_FILESIZE));
    QCOMPARE(restored.intValue(), uint32{99999});
}

void tst_Tags::roundTrip_uint64()
{
    Tag original(FT_FILESIZE, uint64{UINT64_C(0x123456789ABCDEF0)});
    SafeMemFile f;
    original.writeTagToFile(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_UINT64));
    QCOMPARE(restored.int64Value(), UINT64_C(0x123456789ABCDEF0));
}

void tst_Tags::roundTrip_string()
{
    Tag original(FT_FILENAME, QStringLiteral("hello world"));
    SafeMemFile f;
    original.writeTagToFile(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_STRING));
    QCOMPARE(restored.strValue(), QStringLiteral("hello world"));
}

void tst_Tags::roundTrip_float()
{
    Tag original(uint8{0x10}, 2.718f);
    SafeMemFile f;
    original.writeTagToFile(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_FLOAT32));
    QCOMPARE(restored.floatValue(), 2.718f);
}

void tst_Tags::roundTrip_hash()
{
    const std::array<uint8, 16> hash = {
        0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,0x11,
        0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99
    };
    Tag original(FT_FILEHASH, hash.data());
    SafeMemFile f;
    original.writeTagToFile(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_HASH));
    QVERIFY(md4equ(restored.hashValue(), hash.data()));
}

void tst_Tags::roundTrip_blob()
{
    QByteArray data(32, '\xAB');
    Tag original(uint8{0x20}, data);
    SafeMemFile f;
    original.writeTagToFile(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_BLOB));
    QCOMPARE(restored.blobValue(), data);
}

// ---------------------------------------------------------------------------
// New ED2K format (writeNewEd2kTag) — size optimization tests
// ---------------------------------------------------------------------------

void tst_Tags::newFormat_smallInt_optimized()
{
    // Small value (<=255) should be written as UINT8
    Tag tag(FT_FILESIZE, uint32{42});
    SafeMemFile f;
    tag.writeNewEd2kTag(f);
    f.seek(0, 0);
    Tag restored(f, false);

    // Deserialized UINT8 normalizes to UINT32
    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_UINT32));
    QCOMPARE(restored.intValue(), uint32{42});
}

void tst_Tags::newFormat_mediumInt_optimized()
{
    // Medium value (256–65535) should be written as UINT16
    Tag tag(FT_FILESIZE, uint32{1000});
    SafeMemFile f;
    tag.writeNewEd2kTag(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_UINT32));
    QCOMPARE(restored.intValue(), uint32{1000});
}

void tst_Tags::newFormat_fullUint32()
{
    Tag tag(FT_FILESIZE, uint32{0x12345678});
    SafeMemFile f;
    tag.writeNewEd2kTag(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_UINT32));
    QCOMPARE(restored.intValue(), uint32{0x12345678});
}

void tst_Tags::newFormat_uint64()
{
    Tag tag(FT_FILESIZE, uint64{UINT64_C(0xABCDEF0123456789)});
    SafeMemFile f;
    tag.writeNewEd2kTag(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_UINT64));
    QCOMPARE(restored.int64Value(), UINT64_C(0xABCDEF0123456789));
}

void tst_Tags::newFormat_shortString_optimized()
{
    // Short string (1–16 chars) should use STR1–STR16 compact type
    Tag tag(FT_FILENAME, QStringLiteral("Hi"));
    SafeMemFile f;
    tag.writeNewEd2kTag(f);

    // Verify size: 1 byte (type|0x80) + 1 byte (nameId) + 2 bytes (string) = 4
    QCOMPARE(f.length(), qint64{4});

    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_STRING));
    QCOMPARE(restored.strValue(), QStringLiteral("Hi"));
}

void tst_Tags::newFormat_longString()
{
    // String longer than 16 chars uses standard STRING type
    const QString longStr = QStringLiteral("This is a longer string that exceeds 16");
    Tag tag(FT_FILENAME, longStr);
    SafeMemFile f;
    tag.writeNewEd2kTag(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.type(), static_cast<uint8>(TAGTYPE_STRING));
    QCOMPARE(restored.strValue(), longStr);
}

// ---------------------------------------------------------------------------
// Name format round-trips
// ---------------------------------------------------------------------------

void tst_Tags::roundTrip_numericIdName()
{
    Tag original(FT_FILENAME, QStringLiteral("test.txt"));
    SafeMemFile f;
    original.writeNewEd2kTag(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.nameId(), static_cast<uint8>(FT_FILENAME));
    QVERIFY(!restored.hasName());
}

void tst_Tags::roundTrip_stringName()
{
    Tag original(QByteArray("Artist"), QStringLiteral("Mozart"));
    SafeMemFile f;
    original.writeNewEd2kTag(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QCOMPARE(restored.name(), QByteArray("Artist"));
    QCOMPARE(restored.nameId(), uint8{0});
    QCOMPARE(restored.strValue(), QStringLiteral("Mozart"));
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

void tst_Tags::edgeCase_emptyString()
{
    Tag tag(FT_FILENAME, QStringLiteral(""));
    SafeMemFile f;
    tag.writeTagToFile(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QVERIFY(restored.isStr());
    QCOMPARE(restored.strValue(), QStringLiteral(""));
}

void tst_Tags::edgeCase_zeroBlob()
{
    Tag tag(uint8{0x20}, QByteArray());
    SafeMemFile f;
    tag.writeTagToFile(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QVERIFY(restored.isBlob());
    QVERIFY(restored.blobValue().isEmpty());
}

void tst_Tags::edgeCase_maxUint64()
{
    Tag tag(FT_FILESIZE, uint64{UINT64_MAX});
    SafeMemFile f;
    tag.writeNewEd2kTag(f);
    f.seek(0, 0);
    Tag restored(f, false);

    QVERIFY(restored.isInt64());
    QCOMPARE(restored.int64Value(), UINT64_MAX);
}

// ---------------------------------------------------------------------------
// Mutator tests
// ---------------------------------------------------------------------------

void tst_Tags::mutator_setInt()
{
    Tag tag(FT_FILESIZE, uint32{100});
    tag.setInt(200);
    QCOMPARE(tag.intValue(), uint32{200});
    QVERIFY(tag.isInt());
}

void tst_Tags::mutator_setInt64()
{
    Tag tag(FT_FILESIZE, uint32{100});
    tag.setInt64(UINT64_C(0xFFFFFFFFFF));
    QVERIFY(tag.isInt64(false));
    QCOMPARE(tag.int64Value(), UINT64_C(0xFFFFFFFFFF));
}

void tst_Tags::mutator_setStr()
{
    Tag tag(FT_FILENAME, QStringLiteral("old"));
    tag.setStr(QStringLiteral("new"));
    QVERIFY(tag.isStr());
    QCOMPARE(tag.strValue(), QStringLiteral("new"));
}

QTEST_MAIN(tst_Tags)
#include "tst_Tags.moc"
