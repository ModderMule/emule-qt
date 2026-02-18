/// @file tst_SharedFileData.cpp
/// @brief Integration test — scan data/incoming and share the files.
///
/// Points the incoming directory at the project-level data/incoming,
/// triggers SharedFileList::reload() to discover and hash the files,
/// then verifies they appear in the shared list with valid metadata.

#include "TestHelpers.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace eMule::testing;

class tst_SharedFileData : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void reload_discoversIncomingFiles();
    void sharedFiles_haveValidMetadata();

private:
    QTemporaryDir m_tempDir;
    int m_expectedFileCount = 0;
};

void tst_SharedFileData::initTestCase()
{
    QVERIFY(m_tempDir.isValid());

    // Copy real incoming files to a temp directory so we don't modify originals
    const QString srcDir = projectDataDir() + QStringLiteral("/incoming");
    QVERIFY2(QDir(srcDir).exists(),
             qPrintable(QStringLiteral("Missing test fixture: %1").arg(srcDir)));

    const QString dstDir = m_tempDir.path() + QStringLiteral("/incoming");
    QDir().mkpath(dstDir);

    QDir src(srcDir);
    const auto entries = src.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto& fi : entries) {
        if (fi.size() == 0)
            continue;
        // Skip .part/.part.met files (SharedFileList filters these out)
        if (fi.fileName().endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)
            || fi.fileName().endsWith(QStringLiteral(".part.met"), Qt::CaseInsensitive))
            continue;

        QVERIFY2(QFile::copy(fi.absoluteFilePath(), dstDir + QDir::separator() + fi.fileName()),
                 qPrintable(QStringLiteral("Failed to copy %1").arg(fi.fileName())));
        ++m_expectedFileCount;
    }

    QVERIFY2(m_expectedFileCount > 0, "No shareable files found in data/incoming");

    thePrefs.setIncomingDir(dstDir);
    thePrefs.setTempDirs({m_tempDir.path() + QStringLiteral("/temp")});
    QDir().mkpath(thePrefs.tempDirs().first());

    logDebug(QStringLiteral("Copied %1 files from %2 to %3")
                 .arg(m_expectedFileCount)
                 .arg(srcDir, dstDir));
}

// ---------------------------------------------------------------------------
// Test: reload() discovers and hashes files from the incoming directory
// ---------------------------------------------------------------------------

void tst_SharedFileData::reload_discoversIncomingFiles()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    QSignalSpy addedSpy(&shared, &SharedFileList::fileAdded);

    shared.reload();

    // Wait for all files to be hashed (up to 30s for larger files)
    const int timeout = 30000;
    const int pollInterval = 100;
    int waited = 0;
    while (addedSpy.count() < m_expectedFileCount && waited < timeout) {
        QTest::qWait(pollInterval);
        waited += pollInterval;
    }

    QCOMPARE(addedSpy.count(), m_expectedFileCount);
    QCOMPARE(shared.getCount(), m_expectedFileCount);

    logDebug(QStringLiteral("SharedFileList contains %1 files after reload")
                 .arg(shared.getCount()));
}

// ---------------------------------------------------------------------------
// Test: shared files have valid hashes, names, sizes
// ---------------------------------------------------------------------------

void tst_SharedFileData::sharedFiles_haveValidMetadata()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    QSignalSpy addedSpy(&shared, &SharedFileList::fileAdded);
    shared.reload();

    const int timeout = 30000;
    const int pollInterval = 100;
    int waited = 0;
    while (addedSpy.count() < m_expectedFileCount && waited < timeout) {
        QTest::qWait(pollInterval);
        waited += pollInterval;
    }

    QCOMPARE(shared.getCount(), m_expectedFileCount);

    // Verify each shared file
    uint64 largest = 0;
    const uint64 totalSize = shared.getDataSize(largest);
    QVERIFY(totalSize > 0);
    QVERIFY(largest > 0);

    shared.forEachFile([](KnownFile* file) {
        // Must have a non-empty filename
        QVERIFY2(!file->fileName().isEmpty(),
                 "Shared file has empty filename");

        // Must have a non-zero file size
        QVERIFY2(static_cast<uint64>(file->fileSize()) > 0,
                 qPrintable(QStringLiteral("File '%1' has zero size").arg(file->fileName())));

        // Must have a valid (non-null) MD4 hash
        QVERIFY2(!file->hasNullHash(),
                 qPrintable(QStringLiteral("File '%1' has null hash").arg(file->fileName())));

        // Should not be a part file
        QVERIFY2(!file->isPartFile(),
                 qPrintable(QStringLiteral("File '%1' is a part file").arg(file->fileName())));

        // Part count must be consistent with file size
        QVERIFY(file->partCount() > 0);

        logDebug(QStringLiteral("  Shared: '%1'  size: %2 bytes  parts: %3")
                     .arg(file->fileName())
                     .arg(static_cast<uint64>(file->fileSize()))
                     .arg(file->partCount()));
    });

    logDebug(QStringLiteral("Total shared: %1 bytes, largest: %2 bytes")
                 .arg(totalSize)
                 .arg(largest));
}

QTEST_GUILESS_MAIN(tst_SharedFileData)
#include "tst_SharedFileData.moc"
