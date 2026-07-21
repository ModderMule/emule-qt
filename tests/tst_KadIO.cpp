/// @file tst_KadIO.cpp
/// @brief Tests for KadIO.h — Kad-specific I/O serialization.

#include "TestHelpers.h"

#include "kademlia/KadIO.h"
#include "kademlia/KadUInt128.h"
#include "protocol/Tag.h"
#include "utils/SafeFile.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;

class tst_KadIO : public QObject {
    Q_OBJECT

private slots:
    void readWriteUInt128_roundTrip();
    void readWriteKadTag_string();
    void readWriteKadTag_uint64();
    void readWriteKadTag_uint32();
    void readWriteKadTag_uint8_autosize();
    void readWriteKadTag_float();
    void readKadTag_unknownTypeThrows();
    void readWriteKadTag_bsobRoundTrip();
    void readWriteKadTagList_roundTrip();
    void readWriteBsob_roundTrip();
};

void tst_KadIO::readWriteUInt128_roundTrip()
{
    UInt128 original;
    original.setValueRandom();

    SafeMemFile file;
    io::writeUInt128(file, original);

    file.seek(0, SEEK_SET);
    UInt128 read = io::readUInt128(file);
    QCOMPARE(read, original);
}

void tst_KadIO::readWriteKadTag_string()
{
    Tag original(QByteArrayLiteral("filename"), QStringLiteral("test_file.mp3"));

    SafeMemFile file;
    io::writeKadTag(file, original);

    file.seek(0, SEEK_SET);
    Tag read = io::readKadTag(file);

    QVERIFY(read.isStr());
    QCOMPARE(read.name(), QByteArrayLiteral("filename"));
    QCOMPARE(read.strValue(), QStringLiteral("test_file.mp3"));
}

void tst_KadIO::readWriteKadTag_uint64()
{
    Tag original(QByteArrayLiteral("filesize"), uint64{0x123456789ABCDEF0ULL});

    SafeMemFile file;
    io::writeKadTag(file, original);

    file.seek(0, SEEK_SET);
    Tag read = io::readKadTag(file);

    QVERIFY(read.isInt64());
    QCOMPARE(read.int64Value(), uint64{0x123456789ABCDEF0ULL});
}

void tst_KadIO::readWriteKadTag_uint32()
{
    Tag original(QByteArrayLiteral("type"), uint32{42000});

    SafeMemFile file;
    io::writeKadTag(file, original);

    file.seek(0, SEEK_SET);
    Tag read = io::readKadTag(file);

    QVERIFY(read.isInt());
    QCOMPARE(read.intValue(), uint32{42000});
}

void tst_KadIO::readWriteKadTag_uint8_autosize()
{
    // Value fits in uint8, should auto-size on write
    Tag original(QByteArrayLiteral("rating"), uint32{5});

    SafeMemFile file;
    io::writeKadTag(file, original);

    file.seek(0, SEEK_SET);
    Tag read = io::readKadTag(file);

    // Auto-sized to uint8 on write, normalized back to uint32 on read
    QVERIFY(read.isInt());
    QCOMPARE(read.intValue(), uint32{5});
}

void tst_KadIO::readWriteKadTag_float()
{
    // A FLOAT32 tag must round-trip as a real float, not be reinterpreted as the
    // integer holding its IEEE-754 bits (which the old reader did, so a rating of
    // 3.0 came back as 1077936128).
    Tag original(QByteArrayLiteral("trust"), 3.5f);

    SafeMemFile file;
    io::writeKadTag(file, original);

    file.seek(0, SEEK_SET);
    Tag read = io::readKadTag(file);

    QVERIFY(read.isFloat());
    QVERIFY(!read.isInt());
    QCOMPARE(read.floatValue(), 3.5f);
}

void tst_KadIO::readKadTag_unknownTypeThrows()
{
    // An unsupported tag type must throw FileException (which the packet handlers
    // catch and drop), NOT log-and-return a dummy value that leaves the value
    // bytes unread and desyncs the rest of the stream.
    SafeMemFile file;
    file.writeUInt8(0x99);         // bogus tag type
    file.writeUInt16(1);           // 1-byte name
    file.writeUInt8(0x01);
    file.seek(0, SEEK_SET);

    QVERIFY_EXCEPTION_THROWN(io::readKadTag(file), FileException);
}

void tst_KadIO::readWriteKadTag_bsobRoundTrip()
{
    // A BSOB tag must survive a write→read cycle as BSOB and must NOT be re-emitted
    // as an (illegal-in-Kad) BLOB. The old code collapsed BSOB onto TAGTYPE_BLOB.
    const QByteArray payload = QByteArrayLiteral("\x0A\x0B\x0C\x0D");
    Tag original = Tag::makeBsob(QByteArrayLiteral("aich"), payload);

    SafeMemFile file;
    io::writeKadTag(file, original);

    // The wire type byte must be BSOB (0x0A), never BLOB (0x07).
    file.seek(0, SEEK_SET);
    QCOMPARE(file.readUInt8(), static_cast<uint8>(TAGTYPE_BSOB));

    file.seek(0, SEEK_SET);
    Tag read = io::readKadTag(file);
    QVERIFY(read.isBsob());
    QVERIFY(!read.isBlob());
    QCOMPARE(read.blobValue(), payload);
}

void tst_KadIO::readWriteKadTagList_roundTrip()
{
    std::vector<Tag> tags;
    tags.emplace_back(QByteArrayLiteral("name"), QStringLiteral("hello.txt"));
    tags.emplace_back(QByteArrayLiteral("size"), uint32{1024});
    tags.emplace_back(QByteArrayLiteral("big"), uint64{0xFFFFFFFFFFULL});

    SafeMemFile file;
    io::writeKadTagList(file, tags);

    file.seek(0, SEEK_SET);
    auto readTags = io::readKadTagList(file);

    QCOMPARE(readTags.size(), tags.size());
    QVERIFY(readTags[0].isStr());
    QCOMPARE(readTags[0].strValue(), QStringLiteral("hello.txt"));
    QVERIFY(readTags[1].isInt());
    QCOMPARE(readTags[1].intValue(), uint32{1024});
}

void tst_KadIO::readWriteBsob_roundTrip()
{
    QByteArray original = QByteArrayLiteral("\x01\x02\x03\x04\x05");

    SafeMemFile file;
    io::writeBsob(file, original);

    file.seek(0, SEEK_SET);
    QByteArray read = io::readBsob(file);

    QCOMPARE(read, original);
}

QTEST_GUILESS_MAIN(tst_KadIO)
#include "tst_KadIO.moc"
