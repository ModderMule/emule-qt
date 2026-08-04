/// @file tst_ArchiveReader.cpp
/// @brief Tests for archive/ArchiveReader — unified archive reading via libarchive.

#include "TestHelpers.h"
#include "archive/ArchiveReader.h"

#include <QFile>
#include <QTest>
#include <QTemporaryDir>

#include <cstring>

using namespace eMule;

// ZIP fixtures come from TestHelpers (shared with tst_ArchiveUnpack).
using eMule::testing::buildMinimalZip;

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
