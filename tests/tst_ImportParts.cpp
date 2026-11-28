/// @file tst_ImportParts.cpp
/// @brief Tests for transfer/ImportParts.

#include "TestHelpers.h"
#include "transfer/ImportParts.h"
#include "files/PartFile.h"
#include "app/AppContext.h"
#include "transfer/DownloadQueue.h"
#include "transfer/LocalCopyScan.h"

#include <QSignalSpy>

#include <QCryptographicHash>
#include <QFile>
#include <QTest>

#include <array>
#include <cstring>
#include <vector>

using namespace eMule;

class tst_ImportParts : public QObject {
    Q_OBJECT

private slots:
    void importParts_nullPartFile();
    void importParts_missingSource();
    void importParts_emptySource();
    void importParts_sizeMismatch();
    void importParts_stub();
    void importParts_partlyDownloadedPart();
    void localCopy_hashFile();
    void localCopy_scanFindsByHash();
    void localCopy_importsWithoutHashSet();
};

namespace {

std::array<uint8, 16> md4Of(const QByteArray& data)
{
    const QByteArray digest = QCryptographicHash::hash(data, QCryptographicHash::Md4);
    std::array<uint8, 16> out{};
    std::memcpy(out.data(), digest.constData(), out.size());
    return out;
}

QByteArray pattern(qsizetype size, uint8 seed)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        out[i] = static_cast<char>((i * 31 + seed) & 0xFF);
    return out;
}

void writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(data), data.size());
}

/// eD2K hash of @p data, as eMule makes it: MD4 of the part hashes (+ empty hash on a boundary)
std::array<uint8, 16> ed2kHashOf(const QByteArray& data, std::vector<std::array<uint8, 16>>* parts = nullptr)
{
    std::vector<std::array<uint8, 16>> hashes;
    for (qsizetype off = 0; off < data.size(); off += qsizetype(PARTSIZE))
        hashes.push_back(md4Of(data.mid(off, qsizetype(PARTSIZE))));
    if (data.size() % qsizetype(PARTSIZE) == 0)
        hashes.push_back(md4Of({}));
    if (parts)
        *parts = hashes;
    if (hashes.size() == 1)
        return hashes.front();
    QByteArray joined;
    for (const auto& h : hashes)
        joined.append(reinterpret_cast<const char*>(h.data()), 16);
    return md4Of(joined);
}

} // namespace

void tst_ImportParts::importParts_nullPartFile()
{
    // Null partFile should return 0
    int result = importParts(nullptr, QStringLiteral("/some/path"));
    QCOMPARE(result, 0);
}

void tst_ImportParts::importParts_missingSource()
{
    PartFile pf;
    // Non-existent source file should return 0
    int result = importParts(&pf, QStringLiteral("/nonexistent/path/file.dat"));
    QCOMPARE(result, 0);
}

void tst_ImportParts::importParts_emptySource()
{
    PartFile pf;
    // Empty source path should return 0
    int result = importParts(&pf, QString());
    QCOMPARE(result, 0);
}

void tst_ImportParts::importParts_sizeMismatch()
{
    eMule::testing::TempDir tempDir;
    const QString filePath = tempDir.filePath(QStringLiteral("source.bin"));

    // Create a small file
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(1024, 'X'));
        f.close();
    }

    PartFile pf;
    pf.setFileSize(9999); // Different from file size (1024)

    int result = importParts(&pf, filePath);
    QCOMPARE(result, 0);
}

void tst_ImportParts::importParts_stub()
{
    eMule::testing::TempDir tempDir;
    const QString filePath = tempDir.filePath(QStringLiteral("source.bin"));

    // Create a file matching the PartFile size
    const uint64 fileSize = 4096;
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(static_cast<int>(fileSize), 'Z'));
        f.close();
    }

    PartFile pf;
    pf.setFileSize(fileSize);

    int progressCalls = 0;
    int lastPercent = -1;
    auto callback = [&progressCalls, &lastPercent](int percent) {
        ++progressCalls;
        lastPercent = percent;
    };

    // Stub returns 0 (no parts imported yet)
    int result = importParts(&pf, filePath, callback);
    QCOMPARE(result, 0);

    // Progress callback should have been called (at least 0% and 100%)
    QVERIFY(progressCalls >= 2);
    QCOMPARE(lastPercent, 100);
}

// A part with some blocks already downloaded is imported too (MFC), not skipped
// forever: the import used to stop short of 100 % and a rerun took 0 parts.
void tst_ImportParts::importParts_partlyDownloadedPart()
{
    eMule::testing::TempDir tempDir;
    const QByteArray part0 = pattern(static_cast<qsizetype>(PARTSIZE), 7);
    const QByteArray part1 = pattern(1000, 9);

    // The source has part 0 right and part 1 wrong
    const QString sourcePath = tempDir.filePath(QStringLiteral("source.bin"));
    {
        QFile f(sourcePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(part0);
        f.write(pattern(1000, 99));
    }

    const std::vector<std::array<uint8, 16>> partHashes{md4Of(part0), md4Of(part1)};
    QByteArray joined;
    for (const auto& h : partHashes)
        joined.append(reinterpret_cast<const char*>(h.data()), 16);

    PartFile pf;
    pf.setFileName(QStringLiteral("target.bin"));
    pf.setFileSize(PARTSIZE + 1000);
    pf.setFileHash(md4Of(joined).data());
    QVERIFY(pf.fileIdentifier().setMD4HashSet(partHashes));
    QVERIFY(pf.createPartFile(tempDir.path()));

    // Part 0 partly downloaded
    pf.writeToBuffer(300, reinterpret_cast<const uint8*>(part0.constData()), 0, 299, nullptr);
    pf.flushBuffer();
    QVERIFY(!pf.isComplete(0u));

    QCOMPARE(importParts(&pf, sourcePath), 1);
    pf.flushBuffer();
    QVERIFY(pf.isComplete(0u));
    QVERIFY(!pf.isCorruptedPart(0));
    QVERIFY(!pf.isComplete(1u));   // wrong data in the source is never taken

    // Nothing left that the source can give
    QCOMPARE(importParts(&pf, sourcePath), 0);
}

// #251 Locate Files: the hash of a copy is the eD2K file hash, small, multi-part and aligned
void tst_ImportParts::localCopy_hashFile()
{
    eMule::testing::TempDir tempDir;
    const std::atomic<bool> cancel{false};
    for (const qsizetype size : {qsizetype(5000), qsizetype(PARTSIZE), qsizetype(PARTSIZE) * 2 + 77}) {
        const QByteArray data = pattern(size, uint8(size));
        const QString path = tempDir.filePath(QStringLiteral("f%1.bin").arg(size));
        writeFile(path, data);
        std::vector<std::array<uint8, 16>> parts;
        const auto want = ed2kHashOf(data, &parts);
        const auto got = LocalCopyScan::hashFile(path, uint64(size), cancel);
        QVERIFY(got);
        QCOMPARE(got->fileHash, want);
        QCOMPARE(got->partHashes, parts);
    }
    QVERIFY(!LocalCopyScan::hashFile(tempDir.filePath(QStringLiteral("f5000.bin")), 5001, cancel));
}

// Only files of a wanted size are hashed; a same-size decoy is not taken, nor a .part
void tst_ImportParts::localCopy_scanFindsByHash()
{
    eMule::testing::TempDir tempDir;
    QDir(tempDir.path()).mkpath(QStringLiteral("a/b"));
    const QByteArray right = pattern(70000, 1);
    writeFile(tempDir.filePath(QStringLiteral("a/decoy.bin")), pattern(70000, 2));
    writeFile(tempDir.filePath(QStringLiteral("a/b/right.bin")), right);
    writeFile(tempDir.filePath(QStringLiteral("a/b/right.bin.part")), right);
    writeFile(tempDir.filePath(QStringLiteral("other.bin")), pattern(1234, 3));

    const std::vector<LocalCopyScan::Wanted> wanted{{ed2kHashOf(right), 70000}};
    const std::atomic<bool> cancel{false};
    QStringList found;
    const LocalScanResult r = LocalCopyScan::scan({tempDir.path()}, wanted, cancel,
                                                  [&](const QString& path, const LocalCopyScan::Hashed& h) {
        QCOMPARE(h.fileHash, ed2kHashOf(right));
        found << path;
    });
    QCOMPARE(r.files, 3);
    QCOMPARE(r.candidates, 2);
    QCOMPARE(r.matched, 1);
    QCOMPARE(found.size(), 1);
    QVERIFY(found.front().endsWith(QStringLiteral("a/b/right.bin")));
}

// A download still without its hash set takes the copy's and is imported in the background
void tst_ImportParts::localCopy_importsWithoutHashSet()
{
    eMule::testing::TempDir tempDir;
    const QByteArray data = pattern(qsizetype(PARTSIZE) * 2 + 1000, 5);
    QDir(tempDir.path()).mkpath(QStringLiteral("temp"));
    QDir(tempDir.path()).mkpath(QStringLiteral("found"));
    writeFile(tempDir.filePath(QStringLiteral("found/copy.bin")), data);

    DownloadQueue dq;
    theApp.downloadQueue = &dq;
    auto* pf = new PartFile;
    pf->setFileName(QStringLiteral("target.bin"));
    pf->setFileSize(uint64(data.size()));
    pf->setFileHash(ed2kHashOf(data).data());
    pf->setMD4HashsetNeeded(true);
    QVERIFY(pf->createPartFile(tempDir.filePath(QStringLiteral("temp"))));
    dq.addDownload(pf);
    QVERIFY(!pf->fileIdentifier().hasExpectedMD4HashCount());

    LocalCopyScan scan;
    QSignalSpy finished(&scan, &LocalCopyScan::finished);
    QVERIFY(scan.start({tempDir.filePath(QStringLiteral("found"))}));
    QVERIFY(finished.wait(20000));
    QCOMPARE(finished.at(0).at(0).value<LocalScanResult>().matched, 1);

    QTRY_VERIFY_WITH_TIMEOUT(pf->isComplete(0u) && pf->isComplete(1u) && pf->isComplete(2u), 20000);
    QVERIFY(pf->fileIdentifier().hasExpectedMD4HashCount());
    QVERIFY(!pf->isMD4HashsetNeeded());
    QTRY_VERIFY(!partImportRunning(pf));

    // nothing left to look for: finished() at once
    QSignalSpy again(&scan, &LocalCopyScan::finished);
    QVERIFY(!scan.start({tempDir.filePath(QStringLiteral("found"))}));
    QCOMPARE(again.size(), 1);

    dq.deleteAll();
    theApp.downloadQueue = nullptr;
}

QTEST_GUILESS_MAIN(tst_ImportParts)
#include "tst_ImportParts.moc"
