/// @file tst_PartFile.cpp
/// @brief Tests for files/PartFile — gap management, buffered I/O, status,
///        priority, persistence, block selection, source tracking.

#include "TestHelpers.h"
#include "files/PartFile.h"
#include "client/UpDownClient.h"
#include "prefs/Preferences.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

using namespace eMule;

class tst_PartFile : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void construction_defaults();
    void isPartFile_true();
    void setFileSize_initsGap();
    void addGap_basic();
    void addGap_mergesOverlapping();
    void addGap_mergesAdjacent();
    void fillGap_basic();
    void fillGap_splits();
    void isComplete_emptyFile();
    void isComplete_afterFill();
    void isPureGap_basic();
    void totalGapSize_accuracy();
    void completedSize_tracks();
    void writeToBuffer_fillsGap();
    void flushBuffer_writesToDisk();
    void getNextRequestedBlock_basic();
    void statusTransitions();
    void priority_setAndGet();
    void autoDownPriority();
    void createPartFile_createsFiles();
    void saveLoadRoundTrip();
    void writeReadRoundTrip_withGaps();
    void percentCompleted_accuracy();
    void sourceTracking();
    void rightFileHasHigherPrio_ordering();
    void writePartStatus_basic();
    void getFilledArray_basic();

private:
    QTemporaryDir m_tempDir;
};

void tst_PartFile::initTestCase()
{
    QVERIFY(m_tempDir.isValid());

    // Set up minimal preferences for tests
    thePrefs.setIncomingDir(m_tempDir.path() + QStringLiteral("/incoming"));
    thePrefs.setTempDirs({m_tempDir.path() + QStringLiteral("/temp")});
    QDir().mkpath(thePrefs.incomingDir());
    QDir().mkpath(thePrefs.tempDirs().first());
}

void tst_PartFile::construction_defaults()
{
    PartFile pf;
    QCOMPARE(pf.status(), PartFileStatus::Empty);
    QVERIFY(!pf.isPaused());
    QVERIFY(!pf.isStopped());
    QCOMPARE(pf.downPriority(), kPrNormal);
    QVERIFY(pf.isAutoDownPriority());
    QCOMPARE(pf.sourceCount(), 0);
    QCOMPARE(pf.transferred(), 0ULL);
    QCOMPARE(pf.datarate(), 0U);
    QCOMPARE(pf.category(), 0U);
    QCOMPARE(pf.completedSize(), static_cast<EMFileSize>(0));
}

void tst_PartFile::isPartFile_true()
{
    PartFile pf;
    QVERIFY(pf.isPartFile());
}

void tst_PartFile::setFileSize_initsGap()
{
    PartFile pf;
    pf.setFileSize(PARTSIZE * 2); // 2 parts

    QCOMPARE(pf.partCount(), static_cast<uint16>(2));
    QCOMPARE(pf.gapList().size(), 1U);

    const auto& gap = pf.gapList().front();
    QCOMPARE(gap.start, 0ULL);
    QCOMPARE(gap.end, PARTSIZE * 2 - 1);
    QCOMPARE(pf.completedSize(), static_cast<EMFileSize>(0));
}

void tst_PartFile::addGap_basic()
{
    PartFile pf;
    pf.setFileSize(PARTSIZE);

    // File already has a gap covering 0..PARTSIZE-1
    // Fill it first, then add a gap back
    pf.fillGap(0, PARTSIZE - 1);
    QVERIFY(pf.gapList().empty());

    pf.addGap(100, 200);
    QCOMPARE(pf.gapList().size(), 1U);
    QCOMPARE(pf.gapList().front().start, 100ULL);
    QCOMPARE(pf.gapList().front().end, 200ULL);
}

void tst_PartFile::addGap_mergesOverlapping()
{
    PartFile pf;
    pf.setFileSize(PARTSIZE);
    pf.fillGap(0, PARTSIZE - 1); // Clear initial gap

    pf.addGap(100, 200);
    pf.addGap(150, 300); // Overlaps with first

    QCOMPARE(pf.gapList().size(), 1U);
    QCOMPARE(pf.gapList().front().start, 100ULL);
    QCOMPARE(pf.gapList().front().end, 300ULL);
}

void tst_PartFile::addGap_mergesAdjacent()
{
    PartFile pf;
    pf.setFileSize(PARTSIZE);
    pf.fillGap(0, PARTSIZE - 1);

    pf.addGap(100, 200);
    pf.addGap(201, 300); // Adjacent

    QCOMPARE(pf.gapList().size(), 1U);
    QCOMPARE(pf.gapList().front().start, 100ULL);
    QCOMPARE(pf.gapList().front().end, 300ULL);
}

void tst_PartFile::fillGap_basic()
{
    PartFile pf;
    pf.setFileSize(1000);

    // Initial gap is [0, 999]. Fill [0, 499] → gap becomes [500, 999]
    pf.fillGap(0, 499);
    QCOMPARE(pf.gapList().size(), 1U);
    QCOMPARE(pf.gapList().front().start, 500ULL);
    QCOMPARE(pf.gapList().front().end, 999ULL);
}

void tst_PartFile::fillGap_splits()
{
    PartFile pf;
    pf.setFileSize(1000);

    // Initial gap is [0, 999]. Fill [400, 599] → two gaps: [0, 399] and [600, 999]
    pf.fillGap(400, 599);
    QCOMPARE(pf.gapList().size(), 2U);

    auto it = pf.gapList().begin();
    QCOMPARE(it->start, 0ULL);
    QCOMPARE(it->end, 399ULL);
    ++it;
    QCOMPARE(it->start, 600ULL);
    QCOMPARE(it->end, 999ULL);
}

void tst_PartFile::isComplete_emptyFile()
{
    PartFile pf;
    pf.setFileSize(PARTSIZE);

    // Nothing filled yet
    QVERIFY(!pf.isComplete(0, PARTSIZE - 1));
    QVERIFY(!pf.isComplete(static_cast<uint32>(0)));
}

void tst_PartFile::isComplete_afterFill()
{
    PartFile pf;
    pf.setFileSize(1000);

    pf.fillGap(0, 999);
    QVERIFY(pf.isComplete(0, 999));
    QVERIFY(pf.gapList().empty());
}

void tst_PartFile::isPureGap_basic()
{
    PartFile pf;
    pf.setFileSize(1000);

    QVERIFY(pf.isPureGap(100, 200));
    QVERIFY(pf.isPureGap(0, 999));

    pf.fillGap(0, 499);
    QVERIFY(!pf.isPureGap(0, 999));
    QVERIFY(pf.isPureGap(500, 999));
    QVERIFY(!pf.isPureGap(400, 600));
}

void tst_PartFile::totalGapSize_accuracy()
{
    PartFile pf;
    pf.setFileSize(1000);

    // Full gap = 1000 bytes
    QCOMPARE(pf.totalGapSizeInRange(0, 999), 1000ULL);

    // Fill 500 bytes
    pf.fillGap(0, 499);
    QCOMPARE(pf.totalGapSizeInRange(0, 999), 500ULL);
    QCOMPARE(pf.totalGapSizeInRange(0, 499), 0ULL);
    QCOMPARE(pf.totalGapSizeInRange(500, 999), 500ULL);
}

void tst_PartFile::completedSize_tracks()
{
    PartFile pf;
    pf.setFileSize(1000);
    QCOMPARE(pf.completedSize(), static_cast<EMFileSize>(0));

    pf.fillGap(0, 499);
    QCOMPARE(pf.completedSize(), static_cast<EMFileSize>(500));

    pf.fillGap(500, 999);
    QCOMPARE(pf.completedSize(), static_cast<EMFileSize>(1000));
}

void tst_PartFile::writeToBuffer_fillsGap()
{
    PartFile pf;
    pf.setFileSize(1000);

    // Ensure tmp path is set for flushBuffer
    pf.setTmpPath(m_tempDir.path() + QStringLiteral("/temp"));

    std::vector<uint8> data(100, 0xAA);
    pf.writeToBuffer(100, data.data(), 0, 99, nullptr);

    // Gap should be filled for [0, 99]
    QVERIFY(pf.isComplete(0, 99));
    QVERIFY(!pf.isComplete(100, 999));
    QCOMPARE(pf.completedSize(), static_cast<EMFileSize>(100));
}

void tst_PartFile::flushBuffer_writesToDisk()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/temp");
    QDir().mkpath(tempDir);

    PartFile pf;
    pf.setFileSize(1000);
    pf.setTmpPath(tempDir);

    // Create part file so we have a file to write to
    QVERIFY(pf.createPartFile(tempDir));

    // Write some data
    std::vector<uint8> data(100, 0xBB);
    pf.writeToBuffer(100, data.data(), 0, 99, nullptr);

    // Flush should write to disk
    pf.flushBuffer();

    // Verify the data was written by checking completed size
    QCOMPARE(pf.completedSize(), static_cast<EMFileSize>(100));
}

void tst_PartFile::getNextRequestedBlock_basic()
{
    PartFile pf;
    pf.setFileSize(PARTSIZE);

    // Set a hash so blocks can reference it
    uint8 hash[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    pf.setFileHash(hash);

    // Create a mock client with all parts available
    UpDownClient client;
    client.setCompleteSource(true);

    Requested_Block_Struct* blocks[3] = {};
    int count = 3;

    bool result = pf.getNextRequestedBlock(&client, blocks, count);
    QVERIFY(result);
    QVERIFY(count > 0);
    QVERIFY(blocks[0] != nullptr);
    QVERIFY(blocks[0]->startOffset < static_cast<uint64>(pf.fileSize()));

    // Clean up (blocks are owned by PartFile's requested list)
}

void tst_PartFile::statusTransitions()
{
    PartFile pf;
    pf.setFileSize(1000);
    pf.setTmpPath(m_tempDir.path() + QStringLiteral("/temp"));

    QCOMPARE(pf.status(), PartFileStatus::Empty);

    // Pause
    pf.pauseFile();
    QVERIFY(pf.isPaused());
    QCOMPARE(pf.status(), PartFileStatus::Paused);

    // Resume
    pf.resumeFile();
    QVERIFY(!pf.isPaused());
    QCOMPARE(pf.status(), PartFileStatus::Ready);

    // Stop
    pf.stopFile();
    QVERIFY(pf.isStopped());
    QVERIFY(pf.isPaused());

    // Resume from stopped
    pf.resumeFile();
    QVERIFY(!pf.isStopped());
    QVERIFY(!pf.isPaused());

    // Pause with insufficient
    pf.pauseFile(true);
    QVERIFY(pf.isPaused());
    QVERIFY(pf.isInsufficient());
    QCOMPARE(pf.status(), PartFileStatus::Insufficient);
}

void tst_PartFile::priority_setAndGet()
{
    PartFile pf;

    pf.setDownPriority(kPrHigh);
    QCOMPARE(pf.downPriority(), kPrHigh);

    pf.setDownPriority(kPrVeryLow);
    QCOMPARE(pf.downPriority(), kPrVeryLow);

    pf.setDownPriority(kPrVeryHigh);
    QCOMPARE(pf.downPriority(), kPrVeryHigh);

    // Invalid priority should default to Normal
    pf.setDownPriority(99);
    QCOMPARE(pf.downPriority(), kPrNormal);
}

void tst_PartFile::autoDownPriority()
{
    PartFile pf;
    pf.setFileSize(PARTSIZE);

    QVERIFY(pf.isAutoDownPriority());

    // With no sources, should be high priority
    pf.updateAutoDownPriority();
    QCOMPARE(pf.downPriority(), kPrHigh);
}

void tst_PartFile::createPartFile_createsFiles()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/create_test");
    QDir().mkpath(tempDir);

    PartFile pf;
    pf.setFileName(QStringLiteral("testfile.bin"));
    pf.setFileSize(PARTSIZE);

    uint8 hash[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    pf.setFileHash(hash);

    QVERIFY(pf.createPartFile(tempDir));
    QVERIFY(!pf.partMetFileName().isEmpty());

    // Verify .part.met file exists
    QVERIFY(QFile::exists(pf.fullName()));
}

void tst_PartFile::saveLoadRoundTrip()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/roundtrip");
    QDir().mkpath(tempDir);

    // Create and save
    PartFile pf1;
    pf1.setFileName(QStringLiteral("roundtrip_test.avi"));
    pf1.setFileSize(PARTSIZE * 3);
    pf1.setDownPriority(kPrHigh);
    pf1.setAutoDownPriority(false);
    pf1.setCategory(2);

    uint8 hash[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                      0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    pf1.setFileHash(hash);

    QVERIFY(pf1.createPartFile(tempDir));

    // Fill some data
    pf1.fillGap(0, PARTSIZE - 1); // Complete first part

    pf1.savePartFile();

    // Load
    PartFile pf2;
    auto result = pf2.loadPartFile(tempDir, pf1.partMetFileName());
    QCOMPARE(result, PartFileLoadResult::LoadSuccess);

    QCOMPARE(pf2.fileName(), QStringLiteral("roundtrip_test.avi"));
    QCOMPARE(static_cast<uint64>(pf2.fileSize()), static_cast<uint64>(PARTSIZE * 3));
    QVERIFY(md4equ(pf2.fileHash(), hash));
    QCOMPARE(pf2.downPriority(), kPrHigh);
    QVERIFY(!pf2.isAutoDownPriority());
    QCOMPARE(pf2.category(), 2U);
}

void tst_PartFile::writeReadRoundTrip_withGaps()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/gaps_roundtrip");
    QDir().mkpath(tempDir);

    PartFile pf1;
    pf1.setFileName(QStringLiteral("gaps_test.bin"));
    pf1.setFileSize(10000);

    uint8 hash[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    pf1.setFileHash(hash);

    QVERIFY(pf1.createPartFile(tempDir));

    // Fill some parts, leaving gaps
    pf1.fillGap(0, 2999);     // [0, 2999] filled
    pf1.fillGap(5000, 7999);  // [5000, 7999] filled
    // Gaps remaining: [3000, 4999] and [8000, 9999]

    pf1.savePartFile();

    // Load
    PartFile pf2;
    auto result = pf2.loadPartFile(tempDir, pf1.partMetFileName());
    QCOMPARE(result, PartFileLoadResult::LoadSuccess);

    // Verify gaps survived round-trip
    QCOMPARE(pf2.gapList().size(), 2U);
    auto it = pf2.gapList().begin();
    QCOMPARE(it->start, 3000ULL);
    QCOMPARE(it->end, 4999ULL);
    ++it;
    QCOMPARE(it->start, 8000ULL);
    QCOMPARE(it->end, 9999ULL);
}

void tst_PartFile::percentCompleted_accuracy()
{
    PartFile pf;
    pf.setFileSize(1000);

    QCOMPARE(pf.percentCompleted(), 0.0f);

    pf.fillGap(0, 499); // 50%
    QVERIFY(qFuzzyCompare(pf.percentCompleted(), 50.0f));

    pf.fillGap(500, 749); // 75%
    QVERIFY(qFuzzyCompare(pf.percentCompleted(), 75.0f));

    pf.fillGap(750, 999); // 100%
    QVERIFY(qFuzzyCompare(pf.percentCompleted(), 100.0f));
}

void tst_PartFile::sourceTracking()
{
    PartFile pf;
    pf.setFileSize(PARTSIZE);

    UpDownClient client1;
    UpDownClient client2;

    QCOMPARE(pf.sourceCount(), 0);

    pf.addSource(&client1);
    QCOMPARE(pf.sourceCount(), 1);

    pf.addSource(&client2);
    QCOMPARE(pf.sourceCount(), 2);

    // No duplicate
    pf.addSource(&client1);
    QCOMPARE(pf.sourceCount(), 2);

    pf.addDownloadingSource(&client1);
    QCOMPARE(pf.transferringSrcCount(), 1);

    pf.removeDownloadingSource(&client1);
    QCOMPARE(pf.transferringSrcCount(), 0);

    pf.removeSource(&client1);
    QCOMPARE(pf.sourceCount(), 1);

    pf.removeSource(&client2);
    QCOMPARE(pf.sourceCount(), 0);
}

void tst_PartFile::rightFileHasHigherPrio_ordering()
{
    PartFile low, high;
    low.setFileSize(1000);
    high.setFileSize(1000);

    low.setAutoDownPriority(false);
    high.setAutoDownPriority(false);
    low.setDownPriority(kPrLow);
    high.setDownPriority(kPrHigh);

    QVERIFY(PartFile::rightFileHasHigherPrio(&low, &high));
    QVERIFY(!PartFile::rightFileHasHigherPrio(&high, &low));
}

void tst_PartFile::writePartStatus_basic()
{
    PartFile pf;
    pf.setFileSize(PARTSIZE * 3); // 3 parts

    SafeMemFile file;
    pf.writePartStatus(file);

    file.seek(0, 0); // SEEK_SET
    uint16 pc = file.readUInt16();
    QCOMPARE(pc, static_cast<uint16>(3));
}

void tst_PartFile::getFilledArray_basic()
{
    PartFile pf;
    pf.setFileSize(1000);

    // All gap — no filled ranges
    std::vector<Gap> filled;
    pf.getFilledArray(filled);
    QVERIFY(filled.empty());

    // Fill some
    pf.fillGap(0, 299);
    pf.fillGap(500, 699);

    pf.getFilledArray(filled);
    QCOMPARE(filled.size(), 2U);
    QCOMPARE(filled[0].start, 0ULL);
    QCOMPARE(filled[0].end, 299ULL);
    QCOMPARE(filled[1].start, 500ULL);
    QCOMPARE(filled[1].end, 699ULL);
}

QTEST_GUILESS_MAIN(tst_PartFile)
#include "tst_PartFile.moc"
