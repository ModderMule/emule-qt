/// @file tst_PartFileData.cpp
/// @brief Integration test — load real data/002.part.met into a PartFile
///        and add it to the DownloadQueue.
///
/// Copies the project-level data/002.part and data/002.part.met into a
/// temporary directory, loads the metadata via PartFile::loadPartFile(),
/// and verifies the file can be added to a DownloadQueue instance.

#include "TestHelpers.h"
#include "files/PartFile.h"
#include "transfer/DownloadQueue.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace eMule::testing;

class tst_PartFileData : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void loadPartFile_fromProjectData();
    void loadPartFile_hasValidMetadata();
    void loadPartFile_downloadProgress();
    void addLoadedPartFile_toDownloadQueue();

private:
    QTemporaryDir m_tempDir;

    /// Copy 002.part and 002.part.met from project data/ into m_tempDir.
    /// Returns true on success.
    bool copyPartFiles();
};

void tst_PartFileData::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    thePrefs.setIncomingDir(m_tempDir.path() + QStringLiteral("/incoming"));
    thePrefs.setTempDirs({m_tempDir.path() + QStringLiteral("/temp")});
    QDir().mkpath(thePrefs.incomingDir());
    QDir().mkpath(thePrefs.tempDirs().first());
}

bool tst_PartFileData::copyPartFiles()
{
    const QString srcDir = projectDataDir();
    const QString dstDir = m_tempDir.path() + QStringLiteral("/temp");

    const bool copiedMet = QFile::copy(
        srcDir + QStringLiteral("/002.part.met"),
        dstDir + QStringLiteral("/002.part.met"));

    const bool copiedPart = QFile::copy(
        srcDir + QStringLiteral("/002.part"),
        dstDir + QStringLiteral("/002.part"));

    return copiedMet && copiedPart;
}

// ---------------------------------------------------------------------------
// Test: load 002.part.met and verify it succeeds
// ---------------------------------------------------------------------------

void tst_PartFileData::loadPartFile_fromProjectData()
{
    const QString metPath = projectDataDir() + QStringLiteral("/002.part.met");
    QVERIFY2(QFile::exists(metPath),
             qPrintable(QStringLiteral("Missing test fixture: %1").arg(metPath)));

    const QString partPath = projectDataDir() + QStringLiteral("/002.part");
    QVERIFY2(QFile::exists(partPath),
             qPrintable(QStringLiteral("Missing test fixture: %1").arg(partPath)));

    QVERIFY(copyPartFiles());

    const QString tempDir = m_tempDir.path() + QStringLiteral("/temp");

    PartFile pf;
    auto result = pf.loadPartFile(tempDir, QStringLiteral("002.part.met"));
    QCOMPARE(result, PartFileLoadResult::LoadSuccess);

    logDebug(QStringLiteral("Loaded part file: %1 (size: %2)")
                 .arg(pf.fileName())
                 .arg(static_cast<uint64>(pf.fileSize())));
}

// ---------------------------------------------------------------------------
// Test: loaded part file has plausible metadata
// ---------------------------------------------------------------------------

void tst_PartFileData::loadPartFile_hasValidMetadata()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/temp");

    // Files were already copied in the previous test; re-copy to be safe
    if (!QFile::exists(tempDir + QStringLiteral("/002.part.met")))
        QVERIFY(copyPartFiles());

    PartFile pf;
    auto result = pf.loadPartFile(tempDir, QStringLiteral("002.part.met"));
    QCOMPARE(result, PartFileLoadResult::LoadSuccess);

    // File must have a name
    QVERIFY2(!pf.fileName().isEmpty(), "Part file should have a filename");

    // File size must be non-zero
    QVERIFY2(static_cast<uint64>(pf.fileSize()) > 0, "Part file should have non-zero size");

    // Must have a non-zero hash
    const uint8* hash = pf.fileHash();
    QVERIFY(hash != nullptr);
    bool allZero = true;
    for (int i = 0; i < 16; ++i) {
        if (hash[i] != 0) {
            allZero = false;
            break;
        }
    }
    QVERIFY2(!allZero, "File hash must not be all zeros");

    // Part count should be consistent with file size
    QVERIFY(pf.partCount() > 0);

    // Completed percentage should be between 0 and 100
    QVERIFY(pf.percentCompleted() >= 0.0f);
    QVERIFY(pf.percentCompleted() <= 100.0f);

    logDebug(QStringLiteral("  Name: %1").arg(pf.fileName()));
    logDebug(QStringLiteral("  Size: %1 bytes").arg(static_cast<uint64>(pf.fileSize())));
    logDebug(QStringLiteral("  Parts: %1").arg(pf.partCount()));
    logDebug(QStringLiteral("  Completed: %1%").arg(pf.percentCompleted(), 0, 'f', 2));
    logDebug(QStringLiteral("  Gaps: %1").arg(pf.gapList().size()));
}

// ---------------------------------------------------------------------------
// Test: verify download progress — how much has been downloaded
// ---------------------------------------------------------------------------

void tst_PartFileData::loadPartFile_downloadProgress()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/temp");

    if (!QFile::exists(tempDir + QStringLiteral("/002.part.met")))
        QVERIFY(copyPartFiles());

    PartFile pf;
    auto result = pf.loadPartFile(tempDir, QStringLiteral("002.part.met"));
    QCOMPARE(result, PartFileLoadResult::LoadSuccess);

    const auto totalSize = static_cast<uint64>(pf.fileSize());
    const auto completed = static_cast<uint64>(pf.completedSize());
    const auto gapCount  = pf.gapList().size();

    // Calculate total gap bytes
    uint64 totalGapBytes = 0;
    for (const auto& gap : pf.gapList())
        totalGapBytes += gap.end - gap.start + 1;

    const uint64 remaining = totalSize - completed;

    // completedSize + gap bytes must equal file size
    QCOMPARE(completed + totalGapBytes, totalSize);

    // Percentage must be consistent with sizes
    const float expectedPercent = static_cast<float>(
        static_cast<double>(completed) * 100.0 / static_cast<double>(totalSize));
    QVERIFY(qAbs(pf.percentCompleted() - expectedPercent) < 0.01f);

    // Count how many of the 76 parts are fully complete
    uint32 completeParts = 0;
    for (uint32 i = 0; i < pf.partCount(); ++i) {
        if (pf.isComplete(i))
            ++completeParts;
    }

    // The 002.part file has less than 5 fully downloaded parts
    QVERIFY2(completeParts < 5,
             qPrintable(QStringLiteral("Expected < 5 complete parts, got %1")
                            .arg(completeParts)));

    // Most of the file is still missing
    QVERIFY(completed < totalSize);
    QVERIFY(remaining > completed);

    logDebug(QStringLiteral("=== Download Progress for '%1' ===").arg(pf.fileName()));
    logDebug(QStringLiteral("  Total size:      %1 bytes (%2 MB)")
                 .arg(totalSize)
                 .arg(totalSize / (1024 * 1024)));
    logDebug(QStringLiteral("  Downloaded:      %1 bytes (%2 MB)")
                 .arg(completed)
                 .arg(completed / (1024 * 1024)));
    logDebug(QStringLiteral("  Remaining:       %1 bytes (%2 MB)")
                 .arg(remaining)
                 .arg(remaining / (1024 * 1024)));
    logDebug(QStringLiteral("  Percent:         %1%").arg(pf.percentCompleted(), 0, 'f', 2));
    logDebug(QStringLiteral("  Parts:           %1 total, %2 complete")
                 .arg(pf.partCount())
                 .arg(completeParts));
    logDebug(QStringLiteral("  Gaps:            %1 (total %2 bytes)")
                 .arg(gapCount)
                 .arg(totalGapBytes));
}

// ---------------------------------------------------------------------------
// Test: load part file and add it to the download queue
// ---------------------------------------------------------------------------

void tst_PartFileData::addLoadedPartFile_toDownloadQueue()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/temp");

    if (!QFile::exists(tempDir + QStringLiteral("/002.part.met")))
        QVERIFY(copyPartFiles());

    auto* pf = new PartFile;
    auto result = pf->loadPartFile(tempDir, QStringLiteral("002.part.met"));
    QCOMPARE(result, PartFileLoadResult::LoadSuccess);

    DownloadQueue dq;

    QSignalSpy addedSpy(&dq, &DownloadQueue::fileAdded);

    dq.addDownload(pf, /*paused=*/true);

    // Verify the file was added
    QCOMPARE(dq.fileCount(), 1);
    QCOMPARE(addedSpy.count(), 1);

    // Verify lookup by hash works
    PartFile* found = dq.fileByID(pf->fileHash());
    QVERIFY(found != nullptr);
    QCOMPARE(found, pf);

    // Verify lookup by index works
    PartFile* byIdx = dq.fileByIndex(0);
    QVERIFY(byIdx != nullptr);
    QCOMPARE(byIdx, pf);

    // Verify the file is paused (we added it paused)
    QVERIFY(pf->isPaused());

    // Verify metadata is accessible through the queue
    QVERIFY(!found->fileName().isEmpty());
    QVERIFY(static_cast<uint64>(found->fileSize()) > 0);

    // Adding the same file again should be rejected (duplicate hash)
    dq.addDownload(pf);
    QCOMPARE(dq.fileCount(), 1);

    logDebug(QStringLiteral("Added '%1' to download queue (paused, %2 bytes)")
                 .arg(pf->fileName())
                 .arg(static_cast<uint64>(pf->fileSize())));

    dq.deleteAll();
}

QTEST_GUILESS_MAIN(tst_PartFileData)
#include "tst_PartFileData.moc"
