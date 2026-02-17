/// @file tst_ImportParts.cpp
/// @brief Tests for transfer/ImportParts.

#include "TestHelpers.h"
#include "transfer/ImportParts.h"
#include "files/PartFile.h"

#include <QFile>
#include <QTest>

using namespace eMule;

class tst_ImportParts : public QObject {
    Q_OBJECT

private slots:
    void importParts_nullPartFile();
    void importParts_missingSource();
    void importParts_emptySource();
    void importParts_sizeMismatch();
    void importParts_stub();
};

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

QTEST_GUILESS_MAIN(tst_ImportParts)
#include "tst_ImportParts.moc"
