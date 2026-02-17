/// @file tst_SafeFile.cpp
/// @brief Tests for SafeFile.h — FileDataIO, SafeFile, SafeMemFile.

#include "TestHelpers.h"

#include "utils/SafeFile.h"
#include "utils/OtherFunctions.h"

#include <QTest>

#include <array>
#include <cstring>

using namespace eMule;

class tst_SafeFile : public QObject {
    Q_OBJECT

private slots:
    // SafeMemFile typed I/O
    void memFile_writeReadUInt8();
    void memFile_writeReadUInt16();
    void memFile_writeReadUInt32();
    void memFile_writeReadUInt64();
    void memFile_writeReadHash16();
    void memFile_writeReadString_latin1();
    void memFile_writeReadString_utf8BOM();
    void memFile_writeReadString_rawUTF8();
    void memFile_writeLongString();
    void memFile_seekAndPosition();
    void memFile_readFromExistingData();
    void memFile_takeBuffer();

    // SafeFile (file-backed)
    void safeFile_writeAndRead();
    void safeFile_seekModes();

    // Error handling
    void memFile_readPastEnd();
};

// ---------------------------------------------------------------------------
// SafeMemFile typed I/O
// ---------------------------------------------------------------------------

void tst_SafeFile::memFile_writeReadUInt8()
{
    SafeMemFile f;
    f.writeUInt8(0x42);
    f.seek(0, 0);
    QCOMPARE(f.readUInt8(), uint8{0x42});
}

void tst_SafeFile::memFile_writeReadUInt16()
{
    SafeMemFile f;
    f.writeUInt16(0xBEEF);
    f.seek(0, 0);
    QCOMPARE(f.readUInt16(), uint16{0xBEEF});
}

void tst_SafeFile::memFile_writeReadUInt32()
{
    SafeMemFile f;
    f.writeUInt32(0xDEADBEEF);
    f.seek(0, 0);
    QCOMPARE(f.readUInt32(), uint32{0xDEADBEEF});
}

void tst_SafeFile::memFile_writeReadUInt64()
{
    SafeMemFile f;
    f.writeUInt64(UINT64_C(0x0102030405060708));
    f.seek(0, 0);
    QCOMPARE(f.readUInt64(), uint64{UINT64_C(0x0102030405060708)});
}

void tst_SafeFile::memFile_writeReadHash16()
{
    const std::array<uint8, 16> hash = {
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10
    };

    SafeMemFile f;
    f.writeHash16(hash.data());
    f.seek(0, 0);

    std::array<uint8, 16> readBack = {};
    f.readHash16(readBack.data());
    QVERIFY(md4equ(hash.data(), readBack.data()));
}

void tst_SafeFile::memFile_writeReadString_latin1()
{
    SafeMemFile f;
    f.writeString(QStringLiteral("Hello"), UTF8Mode::None);
    f.seek(0, 0);

    const QString result = f.readString(false);
    QCOMPARE(result, QStringLiteral("Hello"));
}

void tst_SafeFile::memFile_writeReadString_utf8BOM()
{
    // Non-ASCII triggers BOM+UTF-8
    const QString original = QStringLiteral("caf\u00E9");  // "café"
    SafeMemFile f;
    f.writeString(original, UTF8Mode::OptBOM);
    f.seek(0, 0);

    const QString result = f.readString(true);
    QCOMPARE(result, original);
}

void tst_SafeFile::memFile_writeReadString_rawUTF8()
{
    const QString original = QStringLiteral("\u00FC\u00F6\u00E4");  // "üöä"
    SafeMemFile f;
    f.writeString(original, UTF8Mode::Raw);
    f.seek(0, 0);

    const QString result = f.readString(true);
    QCOMPARE(result, original);
}

void tst_SafeFile::memFile_writeLongString()
{
    const QString original = QStringLiteral("A longer string with 32-bit length prefix");
    SafeMemFile f;
    f.writeLongString(original, UTF8Mode::None);
    f.seek(0, 0);

    // Read the uint32 length prefix manually, then read string
    const uint32 len = f.readUInt32();
    QCOMPARE(len, static_cast<uint32>(original.size()));

    const QString result = f.readString(false, len);
    QCOMPARE(result, original);
}

void tst_SafeFile::memFile_seekAndPosition()
{
    SafeMemFile f;
    f.writeUInt32(0x11111111);
    f.writeUInt32(0x22222222);
    f.writeUInt32(0x33333333);

    QCOMPARE(f.position(), qint64{12});
    QCOMPARE(f.length(), qint64{12});

    // SEEK_SET
    f.seek(4, 0);
    QCOMPARE(f.position(), qint64{4});
    QCOMPARE(f.readUInt32(), uint32{0x22222222});

    // SEEK_CUR (now at offset 8, seek back 4)
    f.seek(-4, 1);
    QCOMPARE(f.position(), qint64{4});

    // SEEK_END
    f.seek(-4, 2);
    QCOMPARE(f.readUInt32(), uint32{0x33333333});
}

void tst_SafeFile::memFile_readFromExistingData()
{
    // Build a buffer with known content: uint16(5) + "Hello"
    QByteArray data;
    data.append('\x05');
    data.append('\x00');  // uint16 LE = 5
    data.append("Hello");

    SafeMemFile f(data);
    const QString result = f.readString(false);
    QCOMPARE(result, QStringLiteral("Hello"));
}

void tst_SafeFile::memFile_takeBuffer()
{
    SafeMemFile f;
    f.writeUInt32(0xCAFE);
    QCOMPARE(f.length(), qint64{4});

    QByteArray buf = f.takeBuffer();
    QCOMPARE(buf.size(), qsizetype{4});

    // After take, the file should be empty
    QCOMPARE(f.length(), qint64{0});
}

// ---------------------------------------------------------------------------
// SafeFile (file-backed)
// ---------------------------------------------------------------------------

void tst_SafeFile::safeFile_writeAndRead()
{
    eMule::testing::TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("test.bin"));

    {
        SafeFile writer(path, QIODevice::WriteOnly);
        writer.writeUInt32(0xDEADBEEF);
        writer.writeUInt16(0x1234);
        writer.writeUInt8(0xFF);
    }

    {
        SafeFile reader(path, QIODevice::ReadOnly);
        QCOMPARE(reader.readUInt32(), uint32{0xDEADBEEF});
        QCOMPARE(reader.readUInt16(), uint16{0x1234});
        QCOMPARE(reader.readUInt8(), uint8{0xFF});
    }
}

void tst_SafeFile::safeFile_seekModes()
{
    eMule::testing::TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("seek.bin"));

    {
        SafeFile writer(path, QIODevice::WriteOnly);
        writer.writeUInt32(0xAAAAAAAA);
        writer.writeUInt32(0xBBBBBBBB);
    }

    {
        SafeFile reader(path, QIODevice::ReadOnly);
        QCOMPARE(reader.length(), qint64{8});

        // SEEK_END to read last 4 bytes
        reader.seek(-4, 2);
        QCOMPARE(reader.readUInt32(), uint32{0xBBBBBBBB});

        // SEEK_SET to beginning
        reader.seek(0, 0);
        QCOMPARE(reader.readUInt32(), uint32{0xAAAAAAAA});
    }
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

void tst_SafeFile::memFile_readPastEnd()
{
    SafeMemFile f;
    f.writeUInt8(0x42);
    f.seek(0, 0);

    f.readUInt8(); // OK
    QVERIFY_THROWS_EXCEPTION(FileException, f.readUInt8());
}

QTEST_MAIN(tst_SafeFile)
#include "tst_SafeFile.moc"
