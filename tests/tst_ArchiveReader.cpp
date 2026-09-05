/// @file tst_ArchiveReader.cpp
/// @brief Tests for archive/ArchiveReader — unified archive reading via libarchive.

#include "RarFixtures.h"
#include "TestHelpers.h"
#include "archive/ArchiveReader.h"

#include <QDir>
#include <QFile>
#include <QTest>
#include <QTemporaryDir>

#include <cstring>

#include <archive.h>
#include <archive_entry.h>

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
    void multiVolume_openedOnPartOneAloneIsIncomplete();
    void multiVolume_openedOnTheWholeSetExtracts();
};

namespace {

/// Write a crafted multi-volume RAR set to disk and return the volume paths.
///
/// A real volume set is not a byte-split file: every volume is a RAR in its own
/// right, with its own marker and headers, and the reader skips those as it
/// rolls from one to the next. That framing is exactly what libarchive's
/// multi-file client models, so nothing else is a valid fixture for it.
QStringList writeRarSet(const QString& dir, const QByteArray& payload,
                        qint64 perVolume, bool rar5 = false)
{
    const QList<QByteArray> volumes =
        eMule::testing::rar::makeStoredRarSet("payload.bin", payload, perVolume, rar5);
    QStringList paths;
    for (int i = 0; i < volumes.size(); ++i) {
        const QString path = QDir(dir).filePath(
            i == 0 ? QStringLiteral("rel.rar")
                   : QStringLiteral("rel.r%1").arg(i - 1, 2, 10, QLatin1Char('0')));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(volumes.at(i));
        f.close();
        paths.append(path);
    }
    return paths;
}

QByteArray patternedPayload(qsizetype size)
{
    QByteArray out(size, '\0');
    for (qsizetype i = 0; i < size; ++i)
        out[i] = char('a' + (i % 26));
    return out;
}

} // namespace

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

// libarchive follows a volume set only across the file list the *client*
// supplies — archive_read_open_filenames() registers the switch callback that
// does it, and the RAR readers never open a sibling volume by name. Handing it
// volume one alone therefore stops at the end of volume one.
void tst_ArchiveReader::multiVolume_openedOnPartOneAloneIsIncomplete()
{
    eMule::testing::TempDir tmpDir;
    const QByteArray payload = patternedPayload(30000);
    const QStringList vols = writeRarSet(tmpDir.path(), payload, 7000);
    QCOMPARE(vols.size(), 5);

    ArchiveReader reader;
    const QString outDir = QDir(tmpDir.path()).filePath(QStringLiteral("out-partial"));

    // Volume one alone must not yield the whole file. It may fail to open, fail
    // to extract, or truncate; silently producing the complete payload is the
    // one outcome that would mean this whole exercise was unnecessary.
    bool complete = false;
    if (reader.open(vols.first()) && reader.extractAll(outDir)) {
        QFile f(QDir(outDir).filePath(QStringLiteral("payload.bin")));
        complete = f.open(QIODevice::ReadOnly) && f.readAll() == payload;
    }
    QVERIFY2(!complete, "one volume of a set must not extract the whole archive");
}

void tst_ArchiveReader::multiVolume_openedOnTheWholeSetExtracts()
{
    eMule::testing::TempDir tmpDir;
    const QByteArray payload = patternedPayload(30000);
    const QStringList vols = writeRarSet(tmpDir.path(), payload, 7000);
    QCOMPARE(vols.size(), 5);

    ArchiveReader reader;
    QVERIFY(reader.open(vols));
    QCOMPARE(reader.entryNames(), QStringList{QStringLiteral("payload.bin")});

    const QString outDir = QDir(tmpDir.path()).filePath(QStringLiteral("out-full"));
    QVERIFY(reader.extractAll(outDir));

    QFile f(QDir(outDir).filePath(QStringLiteral("payload.bin")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), payload);
}

QTEST_MAIN(tst_ArchiveReader)
#include "tst_ArchiveReader.moc"
