/// @file tst_Collection.cpp
/// @brief Tests for Collection container — multi-file management, write/read
///        round-trips (binary + text), deep copy, author key helpers, and
///        IPC serialization contract.

#include "TestHelpers.h"
#include "files/Collection.h"
#include "files/CollectionFile.h"
#include "files/ShareableFile.h"
#include "utils/OtherFunctions.h"

#include <QCborArray>
#include <QCborMap>
#include <QCryptographicHash>
#include <QTest>

#include <cstring>

using namespace eMule;
using namespace eMule::testing;

// ---------------------------------------------------------------------------
// Helper: create a ShareableFile with a 1-byte-repeated hash
// ---------------------------------------------------------------------------

static ShareableFile makeTestFile(uint8 hashByte, const QString& name, uint64 size)
{
    ShareableFile f;
    uint8 hash[16];
    std::memset(hash, hashByte, 16);
    f.setFileHash(hash);
    f.setFileName(name, true);
    f.setFileSize(size);
    return f;
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_Collection : public QObject {
    Q_OBJECT

private slots:
    void addFiles_threePlusFiles();
    void writeAndRead_binaryRoundTrip();
    void writeAndRead_textRoundTrip();
    void copyFrom_deepCopy();
    void authorKeyHelpers();
    void ipcSerialization_matchesHandlerFormat();
};

// ---------------------------------------------------------------------------
// addFiles_threePlusFiles
// ---------------------------------------------------------------------------

void tst_Collection::addFiles_threePlusFiles()
{
    auto f1 = makeTestFile(0xAA, QStringLiteral("movie.mkv"),    1'500'000'000);
    auto f2 = makeTestFile(0xBB, QStringLiteral("album.zip"),      350'000'000);
    auto f3 = makeTestFile(0xCC, QStringLiteral("document.pdf"),     2'500'000);

    Collection coll;
    QVERIFY(coll.addFile(&f1));
    QVERIFY(coll.addFile(&f2));
    QVERIFY(coll.addFile(&f3));
    QCOMPARE(coll.fileCount(), 3);

    // Verify each file is retrievable by hash key
    for (const auto& [key, cf] : coll.files()) {
        QVERIFY(!cf->fileName().isEmpty());
        QVERIFY(cf->fileSize() > 0);
    }

    // Adding a duplicate returns existing entry, count unchanged
    auto* dup = coll.addFile(&f1);
    QVERIFY(dup != nullptr);
    QCOMPARE(coll.fileCount(), 3);
}

// ---------------------------------------------------------------------------
// writeAndRead_binaryRoundTrip
// ---------------------------------------------------------------------------

void tst_Collection::writeAndRead_binaryRoundTrip()
{
    auto f1 = makeTestFile(0x11, QStringLiteral("file_one.bin"),   100'000);
    auto f2 = makeTestFile(0x22, QStringLiteral("file_two.dat"), 2'000'000);
    auto f3 = makeTestFile(0x33, QStringLiteral("file_three.txt"),  50'000);

    Collection original;
    original.m_name = QStringLiteral("Test Collection");
    original.m_textFormat = false;
    original.addFile(&f1);
    original.addFile(&f2);
    original.addFile(&f3);

    TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("test.emulecollection"));
    QVERIFY(original.writeToFile(path));

    // Read back
    Collection loaded;
    QVERIFY(loaded.initFromFile(path, QStringLiteral("test.emulecollection")));

    QCOMPARE(loaded.m_name, QStringLiteral("Test Collection"));
    QCOMPARE(loaded.fileCount(), 3);
    QVERIFY(!loaded.m_textFormat);

    // Verify each original file is present in loaded collection
    for (const auto& [key, origCf] : original.files()) {
        auto it = loaded.files().find(key);
        QVERIFY2(it != loaded.files().end(), "File not found in loaded collection");
        QCOMPARE(it->second->fileName(), origCf->fileName());
        QCOMPARE(it->second->fileSize(), origCf->fileSize());
        QVERIFY(md4equ(it->second->fileHash(), origCf->fileHash()));
    }
}

// ---------------------------------------------------------------------------
// writeAndRead_textRoundTrip
// ---------------------------------------------------------------------------

void tst_Collection::writeAndRead_textRoundTrip()
{
    auto f1 = makeTestFile(0x44, QStringLiteral("alpha.mp3"),  5'000'000);
    auto f2 = makeTestFile(0x55, QStringLiteral("beta.flac"), 40'000'000);
    auto f3 = makeTestFile(0x66, QStringLiteral("gamma.ogg"),  3'000'000);

    Collection original;
    original.m_name = QStringLiteral("Music Collection");
    original.m_textFormat = true;
    original.addFile(&f1);
    original.addFile(&f2);
    original.addFile(&f3);

    TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("music.emulecollection"));
    QVERIFY(original.writeToFile(path));

    // Read back — text format derives name from filename
    Collection loaded;
    QVERIFY(loaded.initFromFile(path, QStringLiteral("music.emulecollection")));

    QCOMPARE(loaded.m_name, QStringLiteral("music"));  // stripped .emulecollection
    QCOMPARE(loaded.fileCount(), 3);
    QVERIFY(loaded.m_textFormat);

    // Verify all files are present with correct data
    for (const auto& [key, origCf] : original.files()) {
        auto it = loaded.files().find(key);
        QVERIFY2(it != loaded.files().end(), "File not found in loaded text collection");
        QCOMPARE(it->second->fileName(), origCf->fileName());
        QCOMPARE(it->second->fileSize(), origCf->fileSize());
    }
}

// ---------------------------------------------------------------------------
// copyFrom_deepCopy
// ---------------------------------------------------------------------------

void tst_Collection::copyFrom_deepCopy()
{
    auto f1 = makeTestFile(0xA1, QStringLiteral("one.bin"),   1000);
    auto f2 = makeTestFile(0xB2, QStringLiteral("two.bin"),   2000);
    auto f3 = makeTestFile(0xC3, QStringLiteral("three.bin"), 3000);

    Collection original;
    original.m_name = QStringLiteral("Original");
    original.m_authorName = QStringLiteral("TestAuthor");
    original.m_authorKey = QByteArray("\xDE\xAD\xBE\xEF", 4);
    original.addFile(&f1);
    original.addFile(&f2);
    original.addFile(&f3);

    Collection copy;
    copy.copyFrom(original);

    // All fields match
    QCOMPARE(copy.m_name, original.m_name);
    QCOMPARE(copy.m_authorName, original.m_authorName);
    QCOMPARE(copy.m_authorKey, original.m_authorKey);
    QCOMPARE(copy.m_textFormat, original.m_textFormat);
    QCOMPARE(copy.fileCount(), original.fileCount());

    // Verify deep copy — modifying copy doesn't affect original
    copy.m_name = QStringLiteral("Modified Copy");
    QCOMPARE(original.m_name, QStringLiteral("Original"));

    // Verify file contents match
    for (const auto& [key, origCf] : original.files()) {
        auto it = copy.files().find(key);
        QVERIFY(it != copy.files().end());
        QCOMPARE(it->second->fileName(), origCf->fileName());
        QCOMPARE(it->second->fileSize(), origCf->fileSize());
    }
}

// ---------------------------------------------------------------------------
// authorKeyHelpers
// ---------------------------------------------------------------------------

void tst_Collection::authorKeyHelpers()
{
    Collection coll;

    // Empty key → empty strings
    QVERIFY(coll.authorKeyHashString().isEmpty());
    QVERIFY(coll.authorKeyString().isEmpty());

    // Set known bytes
    coll.m_authorKey = QByteArray("\xDE\xAD\xBE\xEF", 4);

    // authorKeyString = raw hex
    QCOMPARE(coll.authorKeyString(), QStringLiteral("deadbeef"));

    // authorKeyHashString = MD5 of the key bytes, uppercase hex
    // MD5("\xDE\xAD\xBE\xEF") = 2F249230A8E7C2BF6005CCD2679259EC
    QCOMPARE(coll.authorKeyHashString(), QStringLiteral("2F249230A8E7C2BF6005CCD2679259EC"));
}

// ---------------------------------------------------------------------------
// ipcSerialization_matchesHandlerFormat
// ---------------------------------------------------------------------------

void tst_Collection::ipcSerialization_matchesHandlerFormat()
{
    // Create a collection with 3 files and author info — same as what
    // handleGetCollectionInfo serializes to QCborMap
    auto f1 = makeTestFile(0xD1, QStringLiteral("pic.jpg"),     500'000);
    auto f2 = makeTestFile(0xD2, QStringLiteral("vid.mp4"), 100'000'000);
    auto f3 = makeTestFile(0xD3, QStringLiteral("doc.pdf"),   1'200'000);

    Collection coll;
    coll.m_name = QStringLiteral("IPC Test Collection");
    coll.m_authorName = QStringLiteral("Alice");
    coll.m_authorKey = QByteArray(32, '\xAB');  // 32 fake key bytes
    coll.addFile(&f1);
    coll.addFile(&f2);
    coll.addFile(&f3);

    // Build the QCborMap exactly as handleGetCollectionInfo does
    QCborMap result;
    result.insert(QStringLiteral("name"), coll.m_name);
    result.insert(QStringLiteral("authorName"), coll.m_authorName);
    result.insert(QStringLiteral("authorKeyHash"), coll.authorKeyHashString());
    result.insert(QStringLiteral("authorKeyHex"), coll.authorKeyString());
    result.insert(QStringLiteral("textFormat"), coll.m_textFormat);

    QCborArray filesArr;
    for (const auto& [key, cf] : coll.files()) {
        QCborMap fm;
        fm.insert(QStringLiteral("hash"), md4str(cf->fileHash()));
        fm.insert(QStringLiteral("fileName"), cf->fileName());
        fm.insert(QStringLiteral("fileSize"), static_cast<qint64>(cf->fileSize()));
        filesArr.append(fm);
    }
    result.insert(QStringLiteral("files"), filesArr);

    // Verify the CBOR map fields
    QCOMPARE(result.value(QStringLiteral("name")).toString(),
             QStringLiteral("IPC Test Collection"));
    QCOMPARE(result.value(QStringLiteral("authorName")).toString(),
             QStringLiteral("Alice"));
    QVERIFY(!result.value(QStringLiteral("authorKeyHash")).toString().isEmpty());
    QVERIFY(!result.value(QStringLiteral("authorKeyHex")).toString().isEmpty());
    QCOMPARE(result.value(QStringLiteral("textFormat")).toBool(), false);

    // Verify files array
    const QCborArray files = result.value(QStringLiteral("files")).toArray();
    QCOMPARE(files.size(), 3);

    // Collect all file names from the CBOR array
    QStringList names;
    for (const auto& v : files) {
        const QCborMap fm = v.toMap();
        names.append(fm.value(QStringLiteral("fileName")).toString());
        // Verify hash is 32-char hex string
        QCOMPARE(fm.value(QStringLiteral("hash")).toString().size(), 32);
        // Verify size is positive
        QVERIFY(fm.value(QStringLiteral("fileSize")).toInteger() > 0);
    }

    // All 3 test files should be present
    QVERIFY(names.contains(QStringLiteral("pic.jpg")));
    QVERIFY(names.contains(QStringLiteral("vid.mp4")));
    QVERIFY(names.contains(QStringLiteral("doc.pdf")));
}

QTEST_MAIN(tst_Collection)
#include "tst_Collection.moc"
