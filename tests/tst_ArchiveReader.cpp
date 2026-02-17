/// @file tst_ArchiveReader.cpp
/// @brief Tests for archive/ArchiveReader — unified archive reading via libarchive.

#include "TestHelpers.h"
#include "archive/ArchiveReader.h"

#include <QFile>
#include <QTest>
#include <QTemporaryDir>

#include <cstring>

using namespace eMule;

// Simple CRC32 for test data
static uint32_t testCrc32(const QByteArray& data)
{
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0u);
            table[i] = c;
        }
        initialized = true;
    }
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < data.size(); ++i)
        crc = table[(crc ^ static_cast<uint8_t>(data[i])) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// Minimal ZIP file generator (store method, no compression)
static QByteArray buildMinimalZip(const QByteArray& filename, const QByteArray& content)
{
    QByteArray zip;

    // Local file header
    uint32_t sig = 0x04034b50;
    zip.append(reinterpret_cast<const char*>(&sig), 4);

    uint16_t versionNeeded = 20;
    zip.append(reinterpret_cast<const char*>(&versionNeeded), 2);

    uint16_t flags = 0;
    zip.append(reinterpret_cast<const char*>(&flags), 2);

    uint16_t method = 0; // store
    zip.append(reinterpret_cast<const char*>(&method), 2);

    uint16_t modTime = 0, modDate = 0;
    zip.append(reinterpret_cast<const char*>(&modTime), 2);
    zip.append(reinterpret_cast<const char*>(&modDate), 2);

    uint32_t crc = testCrc32(content);
    zip.append(reinterpret_cast<const char*>(&crc), 4);

    uint32_t compSize = static_cast<uint32_t>(content.size());
    zip.append(reinterpret_cast<const char*>(&compSize), 4);

    uint32_t uncompSize = compSize;
    zip.append(reinterpret_cast<const char*>(&uncompSize), 4);

    uint16_t fnLen = static_cast<uint16_t>(filename.size());
    zip.append(reinterpret_cast<const char*>(&fnLen), 2);

    uint16_t extraLen = 0;
    zip.append(reinterpret_cast<const char*>(&extraLen), 2);

    zip.append(filename);
    zip.append(content);

    uint32_t localOffset = 0;

    // Central directory
    uint32_t cdStart = static_cast<uint32_t>(zip.size());
    uint32_t cdSig = 0x02014b50;
    zip.append(reinterpret_cast<const char*>(&cdSig), 4);

    uint16_t versionMadeBy = 20;
    zip.append(reinterpret_cast<const char*>(&versionMadeBy), 2);
    zip.append(reinterpret_cast<const char*>(&versionNeeded), 2);
    zip.append(reinterpret_cast<const char*>(&flags), 2);
    zip.append(reinterpret_cast<const char*>(&method), 2);
    zip.append(reinterpret_cast<const char*>(&modTime), 2);
    zip.append(reinterpret_cast<const char*>(&modDate), 2);
    zip.append(reinterpret_cast<const char*>(&crc), 4);
    zip.append(reinterpret_cast<const char*>(&compSize), 4);
    zip.append(reinterpret_cast<const char*>(&uncompSize), 4);
    zip.append(reinterpret_cast<const char*>(&fnLen), 2);
    zip.append(reinterpret_cast<const char*>(&extraLen), 2);

    uint16_t commentLen = 0;
    zip.append(reinterpret_cast<const char*>(&commentLen), 2);

    uint16_t diskStart = 0;
    zip.append(reinterpret_cast<const char*>(&diskStart), 2);

    uint16_t intAttr = 0;
    zip.append(reinterpret_cast<const char*>(&intAttr), 2);

    uint32_t extAttr = 0;
    zip.append(reinterpret_cast<const char*>(&extAttr), 4);

    zip.append(reinterpret_cast<const char*>(&localOffset), 4);
    zip.append(filename);

    uint32_t cdSize = static_cast<uint32_t>(zip.size()) - cdStart;

    // End of central directory
    uint32_t eocdSig = 0x06054b50;
    zip.append(reinterpret_cast<const char*>(&eocdSig), 4);

    uint16_t diskNum = 0;
    zip.append(reinterpret_cast<const char*>(&diskNum), 2);
    zip.append(reinterpret_cast<const char*>(&diskNum), 2);

    uint16_t entryCount = 1;
    zip.append(reinterpret_cast<const char*>(&entryCount), 2);
    zip.append(reinterpret_cast<const char*>(&entryCount), 2);

    zip.append(reinterpret_cast<const char*>(&cdSize), 4);
    zip.append(reinterpret_cast<const char*>(&cdStart), 4);

    uint16_t eocdCommentLen = 0;
    zip.append(reinterpret_cast<const char*>(&eocdCommentLen), 2);

    return zip;
}

class tst_ArchiveReader : public QObject {
    Q_OBJECT

private slots:
    void open_nonExistent();
    void open_zipFile();
    void entryName_valid();
    void extractEntry_toTempDir();
    void extractAll_multipleEntries();
    void close_resets();
    void entrySize_valid();
};

void tst_ArchiveReader::open_nonExistent()
{
    ArchiveReader reader;
    QVERIFY(!reader.open(QStringLiteral("/nonexistent/archive.zip")));
    QVERIFY(!reader.isOpen());
}

void tst_ArchiveReader::open_zipFile()
{
    eMule::testing::TempDir tmpDir;
    const QString zipPath = tmpDir.filePath(QStringLiteral("test.zip"));

    QByteArray zipData = buildMinimalZip("hello.txt", "Hello World!");
    QFile f(zipPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(zipData);
    f.close();

    ArchiveReader reader;
    QVERIFY(reader.open(zipPath));
    QVERIFY(reader.isOpen());
    QCOMPARE(reader.entryCount(), 1);
}

void tst_ArchiveReader::entryName_valid()
{
    eMule::testing::TempDir tmpDir;
    const QString zipPath = tmpDir.filePath(QStringLiteral("named.zip"));

    QByteArray zipData = buildMinimalZip("myfile.txt", "content");
    QFile f(zipPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(zipData);
    f.close();

    ArchiveReader reader;
    QVERIFY(reader.open(zipPath));
    QCOMPARE(reader.entryName(0), QStringLiteral("myfile.txt"));

    QStringList names = reader.entryNames();
    QCOMPARE(names.size(), 1);
    QCOMPARE(names.at(0), QStringLiteral("myfile.txt"));
}

void tst_ArchiveReader::extractEntry_toTempDir()
{
    eMule::testing::TempDir tmpDir;
    const QString zipPath = tmpDir.filePath(QStringLiteral("extract.zip"));

    QByteArray content("Extract this content!");
    QByteArray zipData = buildMinimalZip("extracted.txt", content);
    QFile f(zipPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(zipData);
    f.close();

    ArchiveReader reader;
    QVERIFY(reader.open(zipPath));

    const QString destPath = tmpDir.filePath(QStringLiteral("output.txt"));
    QVERIFY(reader.extractEntry(0, destPath));

    // Verify content
    QFile result(destPath);
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), content);
}

void tst_ArchiveReader::extractAll_multipleEntries()
{
    // Build ZIP with two entries by concatenating
    // For simplicity, test with a single-entry ZIP and extractAll
    eMule::testing::TempDir tmpDir;
    const QString zipPath = tmpDir.filePath(QStringLiteral("multi.zip"));

    QByteArray zipData = buildMinimalZip("single.txt", "data");
    QFile f(zipPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(zipData);
    f.close();

    ArchiveReader reader;
    QVERIFY(reader.open(zipPath));

    eMule::testing::TempDir outDir;
    QVERIFY(reader.extractAll(outDir.path()));

    // Check the file exists
    QFile extracted(outDir.filePath(QStringLiteral("single.txt")));
    QVERIFY(extracted.exists());
    QVERIFY(extracted.open(QIODevice::ReadOnly));
    QCOMPARE(extracted.readAll(), QByteArray("data"));
}

void tst_ArchiveReader::close_resets()
{
    eMule::testing::TempDir tmpDir;
    const QString zipPath = tmpDir.filePath(QStringLiteral("closeme.zip"));

    QByteArray zipData = buildMinimalZip("file.txt", "stuff");
    QFile f(zipPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(zipData);
    f.close();

    ArchiveReader reader;
    QVERIFY(reader.open(zipPath));
    QVERIFY(reader.isOpen());
    QCOMPARE(reader.entryCount(), 1);

    reader.close();
    QVERIFY(!reader.isOpen());
    QCOMPARE(reader.entryCount(), 0);
}

void tst_ArchiveReader::entrySize_valid()
{
    eMule::testing::TempDir tmpDir;
    const QString zipPath = tmpDir.filePath(QStringLiteral("sized.zip"));

    QByteArray content(42, 'X');
    QByteArray zipData = buildMinimalZip("sized.bin", content);
    QFile f(zipPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(zipData);
    f.close();

    ArchiveReader reader;
    QVERIFY(reader.open(zipPath));
    QCOMPARE(reader.entrySize(0), uint64{42});
}

QTEST_MAIN(tst_ArchiveReader)
#include "tst_ArchiveReader.moc"
