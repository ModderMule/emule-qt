/// @file tst_ImportParts.cpp
/// @brief Tests for transfer/ImportParts.

#include "TestHelpers.h"
#include "transfer/ImportParts.h"
#include "files/PartFile.h"

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

QTEST_GUILESS_MAIN(tst_ImportParts)
#include "tst_ImportParts.moc"
