/// @file tst_SharedFileLargeSet.cpp
/// @brief Regression tests for SharedFileList hashing — large sets, reload during hashing,
///        multiple directories.

#include "TestHelpers.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace eMule::testing;

class tst_SharedFileLargeSet : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void hashManySmallFiles();
    void hashExternalPath();
    void reloadDuringHashing_completes();
    void multipleDirectories_singleChain();

private:
    /// Create N small files in the given directory.
    static void createFiles(const QString& dir, int count, int sizeBytes = 4096);

    /// Wait until shared.getCount() reaches target or timeout expires.
    static bool waitForCount(SharedFileList& shared, int target, int timeoutMs = 60000);
};

void tst_SharedFileLargeSet::createFiles(const QString& dir, int count, int sizeBytes)
{
    QDir().mkpath(dir);
    const QByteArray data(sizeBytes, 'X');
    for (int i = 0; i < count; ++i) {
        // Vary content so each file gets a unique hash
        QByteArray fileData = data;
        fileData[0] = static_cast<char>(i & 0xFF);
        fileData[1] = static_cast<char>((i >> 8) & 0xFF);
        fileData[2] = static_cast<char>((i >> 16) & 0xFF);

        QFile f(dir + QStringLiteral("/file_%1.bin").arg(i, 5, 10, QLatin1Char('0')));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(fileData);
        f.close();
    }
}

bool tst_SharedFileLargeSet::waitForCount(SharedFileList& shared, int target, int timeoutMs)
{
    constexpr int pollMs = 100;
    int waited = 0;
    while (shared.getCount() < target && waited < timeoutMs) {
        QTest::qWait(pollMs);
        waited += pollMs;
    }
    return shared.getCount() >= target;
}

void tst_SharedFileLargeSet::initTestCase()
{
    loadProjectEnv();
}

// ---------------------------------------------------------------------------
// Test: hash 200 small files, verify all appear in shared list
// ---------------------------------------------------------------------------

void tst_SharedFileLargeSet::hashManySmallFiles()
{
    TempDir tmp;
    const QString dir = tmp.path() + QStringLiteral("/incoming");
    constexpr int fileCount = 200;
    createFiles(dir, fileCount);

    thePrefs.setIncomingDir(dir);
    thePrefs.setSharedDirs({});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    QSignalSpy addedSpy(&shared, &SharedFileList::fileAdded);
    shared.reload();

    QVERIFY2(waitForCount(shared, fileCount),
             qPrintable(QStringLiteral("Only %1/%2 files hashed")
                            .arg(shared.getCount())
                            .arg(fileCount)));

    QCOMPARE(shared.getCount(), fileCount);
    QCOMPARE(shared.getHashingCount(), 0);

    logDebug(QStringLiteral("hashManySmallFiles: %1 files hashed OK").arg(shared.getCount()));
}

// ---------------------------------------------------------------------------
// Test: hash files from SHARED_FILES_TESTPATH env (skip if not set)
// ---------------------------------------------------------------------------

void tst_SharedFileLargeSet::hashExternalPath()
{
    const QString extPath = qEnvironmentVariable("SHARED_FILES_TESTPATH");
    if (extPath.isEmpty())
        QSKIP("SHARED_FILES_TESTPATH not set — skipping external path test");

    QVERIFY2(QDir(extPath).exists(),
             qPrintable(QStringLiteral("SHARED_FILES_TESTPATH does not exist: %1").arg(extPath)));

    // Count eligible files (must match SharedFileList filtering exactly)
    int eligible = 0;
    QDirIterator it(extPath, QDir::Files | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.size() == 0)
            continue;
        if (!fi.isReadable())
            continue;
        if (fi.fileName().endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)
            || fi.fileName().endsWith(QStringLiteral(".part.met"), Qt::CaseInsensitive))
            continue;
        ++eligible;
    }

    QVERIFY2(eligible > 0, "No eligible files found in SHARED_FILES_TESTPATH");

    thePrefs.setIncomingDir(extPath);
    thePrefs.setSharedDirs({});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    shared.reload();

    // Wait for all hashing to finish (not for an exact count — duplicates reduce it)
    constexpr int timeoutMs = 240000;
    constexpr int pollMs = 100;
    int waited = 0;
    while (shared.getHashingCount() > 0 && waited < timeoutMs) {
        QTest::qWait(pollMs);
        waited += pollMs;
    }
    QCOMPARE(shared.getHashingCount(), 0);

    const int count = shared.getCount();
    QVERIFY2(count > 0, "No files were hashed at all");
    QVERIFY2(count <= eligible,
             qPrintable(QStringLiteral("More files shared (%1) than eligible (%2)")
                            .arg(count).arg(eligible)));

    // Log missing files (duplicate hashes or filtering differences)
    if (count < eligible) {
        QSet<QString> hashedNames;
        shared.forEachFile([&](KnownFile* kf) {
            hashedNames.insert(kf->fileName());
        });

        int duplicates = 0;
        QDirIterator missing(extPath, QDir::Files | QDir::NoDotAndDotDot);
        while (missing.hasNext()) {
            missing.next();
            const QFileInfo fi = missing.fileInfo();
            if (fi.size() == 0 || !fi.isReadable())
                continue;
            if (fi.fileName().endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)
                || fi.fileName().endsWith(QStringLiteral(".part.met"), Qt::CaseInsensitive))
                continue;
            if (!hashedNames.contains(fi.fileName())) {
                qWarning() << "NOT in shared list (likely duplicate hash):" << fi.filePath()
                           << "size:" << fi.size();
                ++duplicates;
            }
        }
        logDebug(QStringLiteral("hashExternalPath: %1/%2 files shared, %3 dropped (duplicate hashes)")
                     .arg(count).arg(eligible).arg(duplicates));
    }

    logDebug(QStringLiteral("hashExternalPath: %1 files hashed OK").arg(count));
}

// ---------------------------------------------------------------------------
// Test: reload() mid-hashing completes without infinite loop (regression)
// ---------------------------------------------------------------------------

void tst_SharedFileLargeSet::reloadDuringHashing_completes()
{
    TempDir tmp;
    const QString dir = tmp.path() + QStringLiteral("/incoming");
    constexpr int fileCount = 100;
    createFiles(dir, fileCount);

    thePrefs.setIncomingDir(dir);
    thePrefs.setSharedDirs({});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    shared.reload();

    // Let some hashing start, then trigger a second reload
    QTest::qWait(200);

    shared.reload();

    QVERIFY2(waitForCount(shared, fileCount, 120000),
             qPrintable(QStringLiteral("After mid-hashing reload: only %1/%2 files")
                            .arg(shared.getCount())
                            .arg(fileCount)));

    QCOMPARE(shared.getCount(), fileCount);
    QCOMPARE(shared.getHashingCount(), 0);

    // Verify count stabilizes (no infinite loop re-adding)
    const int countAfter = shared.getCount();
    QTest::qWait(500);
    QCOMPARE(shared.getCount(), countAfter);

    logDebug(QStringLiteral("reloadDuringHashing: %1 files, stable").arg(shared.getCount()));
}

// ---------------------------------------------------------------------------
// Test: 3 directories with 50 files each → 150 total, single chain
// ---------------------------------------------------------------------------

void tst_SharedFileLargeSet::multipleDirectories_singleChain()
{
    TempDir tmp;
    const QString dir1 = tmp.path() + QStringLiteral("/dir1");
    const QString dir2 = tmp.path() + QStringLiteral("/dir2");
    const QString dir3 = tmp.path() + QStringLiteral("/dir3");

    constexpr int filesPerDir = 50;
    // Use different data sizes to ensure unique hashes across dirs
    createFiles(dir1, filesPerDir, 4096);
    createFiles(dir2, filesPerDir, 4097);
    createFiles(dir3, filesPerDir, 4098);

    thePrefs.setIncomingDir(dir1);
    thePrefs.setSharedDirs({dir2, dir3});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    QSignalSpy addedSpy(&shared, &SharedFileList::fileAdded);
    shared.reload();

    constexpr int totalFiles = filesPerDir * 3;

    QVERIFY2(waitForCount(shared, totalFiles, 120000),
             qPrintable(QStringLiteral("Only %1/%2 files from 3 dirs")
                            .arg(shared.getCount())
                            .arg(totalFiles)));

    QCOMPARE(shared.getCount(), totalFiles);
    QCOMPARE(shared.getHashingCount(), 0);

    logDebug(QStringLiteral("multipleDirectories: %1 files OK").arg(shared.getCount()));
}

QTEST_GUILESS_MAIN(tst_SharedFileLargeSet)
#include "tst_SharedFileLargeSet.moc"
