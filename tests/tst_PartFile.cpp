/// @file tst_PartFile.cpp
/// @brief Tests for files/PartFile — gap management, buffered I/O, status,
///        priority, persistence, block selection, source tracking.

#include "TestHelpers.h"
#include "TestFixtures.h"
#include "app/AppContext.h"
#include "files/PartFile.h"
#include "client/UpDownClient.h"
#include "crypto/AICHHashSet.h"
#include "crypto/FileIdentifier.h"
#include "crypto/SHAHash.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "stats/Statistics.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <functional>
#include <set>
#include <span>
#include <vector>

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
    void flushBuffer_isRefusedAtTheDiskFloorAndRetried();
    void getNextRequestedBlock_basic();
    void chunkSelection_spreadsTies();
    void chunkSelection_rarestFirst();
    void chunkSelection_shortLastPartNotFavoured();
    void emptyBlock_alignsToEmBlockSize();
    void endPhase_followsWhatIsLeft();
    void lateDownload_healthyHolderKeepsItsBlock();
    void lateDownload_stalledBlockGoesToASecondSourceOnly();
    void endgame_fastSourceDoublesUpOnASlowHolder();
    void lateDownload_slowSourceReservesLittle();
    void removeBlockFromList_dropsTheCallersOwnEntry();
    void requestedBlock_shrinksAroundTaken();
    void statusTransitions();
    void priority_setAndGet();
    void autoDownPriority();
    void createPartFile_createsFiles();
    void saveLoadRoundTrip();
    void writeReadRoundTrip_withGaps();
    void percentCompleted_accuracy();
    void sourceTracking();
    void ratingAveragesSourcesAndKadNotesTogether();
    void aDepartedSourceStopsCounting();
    void containerCheckWaitsForTheFirstBytes();
    void containerCheckReadsTheDataFile();
    void fakeVerdictFollowsTheFirstBytesAndNames();
    void rightFileHasHigherPrio_ordering();
    void writePartStatus_basic();
    void validSourcesCount_countsMfcStates();
    void updatePartsInfo_rebuildsFrequencyAndCompleteCount();
    void getFilledArray_basic();
    void writeToBuffer_countsCompressionGain();
    void createPartFile_cleansTheNameWhenAsked();
    void writeToBuffer_refusesDuplicatesAndCompleteParts();
    void ich_recoversCorruptedPartOnRehash();
    void seedAICHRecoveryMasterHash_adoptsTheIdentifierHash();

    // hashSinglePart — MFC srchybrid/PartFile.cpp:3122-3198
    void hashSinglePart_singlePartFileComparesTheFileHash();
    void hashSinglePart_exactlyOnePartSizeUsesThePartHash();
    void hashSinglePart_missingHashsetsBailAndFlagBothNeeded();
    void hashSinglePart_aichDisagreementCondemnsThePart();
    void unreadablePart_isNotVerifiedAndBlocksCompletion();
    void unreadablePart_ichDoesNotFillItsGaps();
    void completion_damagedPartIsNotDelivered();
    void copyThenRename_deliversAndLeavesNoStagedFile();
    void copyThenRename_interruptedLeavesTheSourceOnly();
    void copyThenRename_missingSourceFails();
    void completion_setsTheKnownFileDate();
    void completion_storesTheAICHRecoverySet();
    void completion_loadedCompleteFileIsVerifiedAndDelivered_data();
    void completion_loadedCompleteFileIsVerifiedAndDelivered();
    void verifyPartData_reportsUnreadParts();

    // Changed-part tracking — MFC m_aChangedPart, srchybrid/PartFile.cpp:4154-4158
    void flushBuffer_verifiesOnlyChangedParts();
    void hashsetReceived_condemnsBadPartCompletedWithoutHashset();
    void hashsetReceived_checksEachWaitingPartOnce();

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

// At the floor the data stays in memory and the file parks itself; with room again the
// same buffer goes to disk.
void tst_PartFile::flushBuffer_isRefusedAtTheDiskFloorAndRetried()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/floor");
    QDir().mkpath(tempDir);

    const bool savedCheck = thePrefs.checkDiskspace();
    const uint64 savedFloor = thePrefs.minFreeDiskSpace();
    DownloadQueue dq;
    auto* savedQueue = theApp.downloadQueue;
    theApp.downloadQueue = &dq;
    const auto restore = qScopeGuard([&] {
        theApp.downloadQueue = savedQueue;
        thePrefs.setCheckDiskspace(savedCheck);
        thePrefs.setMinFreeDiskSpace(savedFloor);
    });
    thePrefs.setCheckDiskspace(true);
    thePrefs.setMinFreeDiskSpace(1000);
    uint64 freeBytes = 1050;
    dq.setFreeSpaceProbe([&freeBytes](const QString&) { return std::optional<uint64>(freeBytes); });

    PartFile pf;
    pf.setFileSize(1000);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    std::vector<uint8> data(100, 0xBB);
    pf.writeToBuffer(100, data.data(), 0, 99, nullptr);
    pf.flushBuffer();
    QVERIFY(pf.isInsufficient());
    QVERIFY2(!pf.isCompleteBDSafe(0, 99), "the block was reported written at the floor");

    freeBytes = 5000;
    dq.setFreeSpaceProbe([&freeBytes](const QString&) { return std::optional<uint64>(freeBytes); });
    pf.resumeFile();
    pf.flushBuffer();
    QVERIFY(!pf.isInsufficient());
    QVERIFY(pf.isCompleteBDSafe(0, 99));
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

namespace {

/// First part a fresh complete source is given, on a fresh file of @p parts parts.
uint32 firstPickedPart(uint16 parts, const std::function<void(PartFile&)>& setup = {})
{
    PartFile pf;
    pf.setFileName(QStringLiteral("pick.bin"));
    pf.setFileSize(PARTSIZE * parts);
    uint8 hash[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    pf.setFileHash(hash);
    if (setup)
        setup(pf);

    UpDownClient client;
    client.setCompleteSource(true);
    Requested_Block_Struct* blocks[1] = {};
    int count = 1;
    if (!pf.getNextRequestedBlock(&client, blocks, count))
        return UINT32_MAX;
    return static_cast<uint32>(blocks[0]->startOffset / PARTSIZE);
}

} // namespace

void tst_PartFile::chunkSelection_spreadsTies()
{
    // All parts equally available: MFC picks at random among the best, so the
    // file doesn't fill left to right
    std::set<uint32> picked;
    for (int i = 0; i < 60; ++i)
        picked.insert(firstPickedPart(20));
    QVERIFY(!picked.contains(UINT32_MAX));
    QVERIFY2(picked.size() > 5, "tied chunks must be picked at random");
}

void tst_PartFile::chunkSelection_rarestFirst()
{
    for (int i = 0; i < 20; ++i) {
        const uint32 part = firstPickedPart(20, [](PartFile& pf) {
            auto& freq = pf.srcPartFrequency();
            std::ranges::fill(freq, uint16{50});
            freq[7] = 1;
        });
        QCOMPARE(part, 7u);
    }
}

void tst_PartFile::chunkSelection_shortLastPartNotFavoured()
{
    // Last part is 2 KB with 1.5 KB done (75 % of itself), part 0 is half done.
    // Completion counts against PARTSIZE, so the short last part gets no head start.
    for (int i = 0; i < 20; ++i) {
        PartFile pf;
        pf.setFileName(QStringLiteral("last.bin"));
        pf.setFileSize(PARTSIZE * 2 + 2048);
        uint8 hash[16] = {1};
        pf.setFileHash(hash);
        pf.fillGap(0, PARTSIZE * 2 + 2048 - 1);
        pf.addGap(PARTSIZE / 2, PARTSIZE - 1);                 // part 0: half left
        pf.addGap(PARTSIZE * 2 + 1536, PARTSIZE * 2 + 2047);   // part 2: 512 B left

        UpDownClient client;
        client.setCompleteSource(true);
        Requested_Block_Struct* blocks[1] = {};
        int count = 1;
        QVERIFY(pf.getNextRequestedBlock(&client, blocks, count));
        QCOMPARE(blocks[0]->startOffset / PARTSIZE, uint64{0});
    }
}

void tst_PartFile::emptyBlock_alignsToEmBlockSize()
{
    PartFile pf;
    pf.setFileSize(PARTSIZE * 2);
    pf.fillGap(0, PARTSIZE * 2 - 1);
    const uint64 partStart = PARTSIZE;
    pf.addGap(partStart + 1000, partStart + 3 * EMBLOCKSIZE);

    Requested_Block_Struct block;
    QVERIFY(pf.getNextEmptyBlockInPart(1, &block));
    QCOMPARE(block.startOffset, partStart + 1000);
    // ends on the part-relative block boundary, not start + EMBLOCKSIZE
    QCOMPARE(block.endOffset, partStart + EMBLOCKSIZE - 1);
}

void tst_PartFile::requestedBlock_shrinksAroundTaken()
{
    // Another source holds the middle of the first block: we get the free
    // prefix, then the free suffix, instead of skipping the whole block
    PartFile pf;
    pf.setFileName(QStringLiteral("shrink.bin"));
    pf.setFileSize(PARTSIZE * 4);   // no endgame
    uint8 hash[16] = {1};
    pf.setFileHash(hash);

    UpDownClient other;
    other.setCompleteSource(true);
    other.setClientVersion(makeClientVersion(0, 50, 0));
    UpDownClient client;
    client.setCompleteSource(true);

    // pin both sources to part 0: everything else is already there
    pf.fillGap(PARTSIZE, PARTSIZE * 4 - 1);
    Requested_Block_Struct* taken[1] = {};
    int one = 1;
    QVERIFY(pf.getNextRequestedBlock(&other, taken, one));
    QCOMPARE(taken[0]->startOffset, uint64{0});
    // narrow the other request to the middle of block 0
    taken[0]->startOffset = 50000;
    taken[0]->endOffset = 60000;

    Requested_Block_Struct* blocks[2] = {};
    int count = 2;
    QVERIFY(pf.getNextRequestedBlock(&client, blocks, count));
    QCOMPARE(count, 2);
    QCOMPARE(blocks[0]->startOffset, uint64{0});
    QCOMPARE(blocks[0]->endOffset, uint64{49999});
    QCOMPARE(blocks[1]->startOffset, uint64{60001});
    QCOMPARE(blocks[1]->endOffset, uint64{EMBLOCKSIZE - 1});
}

namespace {

/// A file with only the first @p blocksLeft blocks of part 0 missing.
struct NearlyDone {
    PartFile pf;
    explicit NearlyDone(uint64 blocksLeft, uint16 parts = 12)
    {
        pf.setFileName(QStringLiteral("nearly.bin"));
        pf.setFileSize(PARTSIZE * parts);
        uint8 hash[16] = {7};
        pf.setFileHash(hash);
        pf.fillGap(blocksLeft * EMBLOCKSIZE, PARTSIZE * parts - 1);
    }
};

/// A complete source that is transferring from @p pf at @p rate.
void join(PartFile& pf, UpDownClient& client, uint32 rate)
{
    client.setCompleteSource(true);
    client.setClientVersion(makeClientVersion(0, 50, 0));
    client.setDownDatarate(rate);
    pf.addDownloadingSource(&client);
}

std::vector<Requested_Block_Struct*> take(PartFile& pf, UpDownClient& client, int want)
{
    std::vector<Requested_Block_Struct*> blocks(static_cast<size_t>(want), nullptr);
    int count = want;
    if (!pf.getNextRequestedBlock(&client, blocks.data(), count))
        count = 0;
    blocks.resize(static_cast<size_t>(count));
    return blocks;
}

void freeAll(PartFile& pf)
{
    const std::vector<Requested_Block_Struct*> blocks(pf.requestedBlockList().begin(),
                                                      pf.requestedBlockList().end());
    pf.removeAllRequestedBlocks();
    for (auto* b : blocks)
        delete b;
}

} // namespace

void tst_PartFile::endPhase_followsWhatIsLeft()
{
    {
        PartFile pf;
        pf.setFileSize(PARTSIZE * 12);
        QCOMPARE(pf.endPhase(), PartFile::EndPhase::Normal);
        pf.fillGap(PARTSIZE * 2, PARTSIZE * 12 - 1);     // 2 of 12 parts left
        QCOMPARE(pf.endPhase(), PartFile::EndPhase::Normal);
        pf.fillGap(PARTSIZE, PARTSIZE * 2 - 1);          // 1 of 12: under 10 %
        QCOMPARE(pf.endPhase(), PartFile::EndPhase::Late);
        pf.fillGap(4 * EMBLOCKSIZE, PARTSIZE - 1);       // four blocks, still late
        QCOMPARE(pf.endPhase(), PartFile::EndPhase::Late);
        pf.fillGap(3 * EMBLOCKSIZE, 4 * EMBLOCKSIZE - 1);
        QCOMPARE(pf.endPhase(), PartFile::EndPhase::Endgame);
        pf.fillGap(0, 3 * EMBLOCKSIZE - 1);
        QCOMPARE(pf.endPhase(), PartFile::EndPhase::Normal);   // nothing left to race for
    }
    {
        // 99.9 % of a large file is more than three blocks
        PartFile pf;
        pf.setFileSize(PARTSIZE * 200);
        pf.fillGap(8 * EMBLOCKSIZE, PARTSIZE * 200 - 1);
        QCOMPARE(pf.endPhase(), PartFile::EndPhase::Endgame);
    }
}

void tst_PartFile::lateDownload_healthyHolderKeepsItsBlock()
{
    // Two blocks left, two sources. It used to hand every source every block.
    NearlyDone f(2);
    QCOMPARE(f.pf.endPhase(), PartFile::EndPhase::Endgame);
    UpDownClient first, second;
    join(f.pf, first, 100'000);
    join(f.pf, second, 100'000);

    const auto held = take(f.pf, first, 3);
    QCOMPARE(held.size(), size_t{2});
    for (auto* b : held)
        b->requested = true;   // the request went out
    QVERIFY2(take(f.pf, second, 3).empty(), "both blocks are in good hands");
    QCOMPARE(f.pf.requestedBlockList().size(), size_t{2});
    freeAll(f.pf);
}

void tst_PartFile::lateDownload_stalledBlockGoesToASecondSourceOnly()
{
    NearlyDone f(1);
    UpDownClient holder, second, third;
    join(f.pf, holder, 50'000);
    join(f.pf, second, 50'000);
    join(f.pf, third, 50'000);

    const auto held = take(f.pf, holder, 1);
    QCOMPARE(held.size(), size_t{1});
    held[0]->requested = true;
    QVERIFY(take(f.pf, second, 1).empty());

    // nothing written for long enough
    held[0]->lastProgressTick -= PartFile::kStalledBlockMs + 1;
    const auto doubled = take(f.pf, second, 1);
    QCOMPARE(doubled.size(), size_t{1});
    QCOMPARE(doubled[0]->startOffset, held[0]->startOffset);
    QCOMPARE(doubled[0]->endOffset, held[0]->endOffset);
    QCOMPARE(doubled[0]->holder, &second);

    QVERIFY2(take(f.pf, third, 1).empty(), "never more than two on one block");
    QVERIFY2(take(f.pf, holder, 1).empty(), "and not the holder a second time");
    freeAll(f.pf);
}

void tst_PartFile::endgame_fastSourceDoublesUpOnASlowHolder()
{
    {
        NearlyDone f(1);
        UpDownClient slow, fast, alsoSlow;
        join(f.pf, slow, 10'000);
        const auto held = take(f.pf, slow, 1);   // alone so far: the whole block
        QCOMPARE(held.size(), size_t{1});
        QCOMPARE(held[0]->endOffset, uint64{EMBLOCKSIZE - 1});
        held[0]->requested = true;
        join(f.pf, alsoSlow, 40'000);
        join(f.pf, fast, 50'000);

        QVERIFY2(take(f.pf, alsoSlow, 1).empty(), "four times as fast is not enough");
        QCOMPARE(take(f.pf, fast, 1).size(), size_t{1});
        freeAll(f.pf);
    }
    {
        // Merely late, not in the last stretch: speed alone is no reason
        NearlyDone f(5);
        QCOMPARE(f.pf.endPhase(), PartFile::EndPhase::Late);
        UpDownClient slow, fast;
        join(f.pf, slow, 10'000);
        for (auto* b : take(f.pf, slow, 5))
            b->requested = true;
        QCOMPARE(f.pf.requestedBlockList().size(), size_t{5});
        join(f.pf, fast, 500'000);
        QVERIFY(take(f.pf, fast, 1).empty());
        freeAll(f.pf);
    }
    {
        // Reserved but never asked for, by a source that has delivered nothing
        NearlyDone f(5);
        UpDownClient idle, other;
        join(f.pf, idle, 0);
        join(f.pf, other, 0);
        QCOMPARE(take(f.pf, idle, 5).size(), size_t{5});
        QCOMPARE(take(f.pf, other, 2).size(), size_t{2});
        freeAll(f.pf);
    }
}

void tst_PartFile::lateDownload_slowSourceReservesLittle()
{
    NearlyDone f(6);
    QCOMPARE(f.pf.endPhase(), PartFile::EndPhase::Late);
    UpDownClient fast, slow, crawling;
    join(f.pf, fast, 200'000);
    join(f.pf, slow, 5'000);
    join(f.pf, crawling, 100);

    // one piece, ten seconds of its own rate
    const auto small = take(f.pf, slow, 3);
    QCOMPARE(small.size(), size_t{1});
    QCOMPARE(small[0]->endOffset - small[0]->startOffset + 1,
             uint64{5'000} * PartFile::kSlowReservationSecs);

    // with a floor, so the request is still worth a packet
    const auto tiny = take(f.pf, crawling, 3);
    QCOMPARE(tiny.size(), size_t{1});
    QCOMPARE(tiny[0]->endOffset - tiny[0]->startOffset + 1, PartFile::kMinSlowReservation);

    // the fast one is not limited, and gets the rest of the first block too
    const auto big = take(f.pf, fast, 3);
    QCOMPARE(big.size(), size_t{3});
    QCOMPARE(big[0]->startOffset, tiny[0]->endOffset + 1);
    QCOMPARE(big[0]->endOffset, uint64{EMBLOCKSIZE - 1});
    freeAll(f.pf);

    // Early in the download nobody is limited
    PartFile early;
    early.setFileSize(PARTSIZE * 12);
    uint8 hash[16] = {9};
    early.setFileHash(hash);
    UpDownClient a, b;
    join(early, a, 200'000);
    join(early, b, 5'000);
    const auto full = take(early, b, 3);
    QCOMPARE(full.size(), size_t{3});
    QCOMPARE(full[0]->endOffset - full[0]->startOffset + 1, uint64{EMBLOCKSIZE});
    freeAll(early);
}

void tst_PartFile::removeBlockFromList_dropsTheCallersOwnEntry()
{
    NearlyDone f(1);
    UpDownClient holder, second;
    join(f.pf, holder, 50'000);
    join(f.pf, second, 50'000);
    const auto held = take(f.pf, holder, 1);
    held[0]->requested = true;
    held[0]->lastProgressTick -= PartFile::kStalledBlockMs + 1;
    const auto doubled = take(f.pf, second, 1);
    QCOMPARE(f.pf.requestedBlockList().size(), size_t{2});

    // same range twice: by range the first entry went, whoever asked
    QVERIFY(f.pf.removeBlockFromList(doubled[0]));
    QCOMPARE(f.pf.requestedBlockList().size(), size_t{1});
    QCOMPARE(f.pf.requestedBlockList().front(), held[0]);
    QVERIFY(!f.pf.removeBlockFromList(doubled[0]));
    delete doubled[0];
    freeAll(f.pf);
}

void tst_PartFile::statusTransitions()
{
    PartFile pf;
    pf.setFileSize(1000);
    pf.setTmpPath(m_tempDir.path() + QStringLiteral("/temp"));

    // Nothing verified complete yet, so there is nothing to offer: Empty, and it stays
    // Empty until a part actually verifies. Ready is the shareability latch, not
    // "currently running" (srchybrid/PartFile.cpp:1093-1105).
    QCOMPARE(pf.status(), PartFileStatus::Empty);

    // Pause is an overlay: status() reports it, the stored state is untouched, and
    // ignorePause sees straight through to what the file really is.
    pf.pauseFile();
    QVERIFY(pf.isPaused());
    QCOMPARE(pf.status(), PartFileStatus::Paused);
    QCOMPARE(pf.status(/*ignorePause=*/true), PartFileStatus::Empty);

    // Resume just drops the flag — there is nothing to re-derive.
    pf.resumeFile();
    QVERIFY(!pf.isPaused());
    QCOMPARE(pf.status(), PartFileStatus::Empty);

    // Stop
    pf.stopFile();
    QVERIFY(pf.isStopped());
    QVERIFY(pf.isPaused());
    QCOMPARE(pf.status(), PartFileStatus::Paused);
    QCOMPARE(pf.status(/*ignorePause=*/true), PartFileStatus::Empty);

    // Resume from stopped
    pf.resumeFile();
    QVERIFY(!pf.isStopped());
    QVERIFY(!pf.isPaused());

    // Out of disk space is its own condition, not a user pause: m_paused stays false
    // so status() can report Insufficient rather than Paused, which wins the overlay.
    // MFC srchybrid/PartFile.cpp:3351-3355.
    pf.pauseFile(true);
    QVERIFY(!pf.isPaused());
    QVERIFY(pf.isInsufficient());
    QCOMPARE(pf.status(), PartFileStatus::Insufficient);
    QCOMPARE(pf.status(/*ignorePause=*/true), PartFileStatus::Empty);

    // And it must still be resumable, even though m_paused was never set.
    pf.resumeFile();
    QVERIFY(!pf.isInsufficient());
    QCOMPARE(pf.status(), PartFileStatus::Empty);

    // The overlay states can never be stored: setStatus() drops them on the floor
    // rather than overwriting the latch (MFC asserts the same in _SetStatus).
    pf.setStatus(PartFileStatus::Ready);
    pf.setStatus(PartFileStatus::Paused);
    QCOMPARE(pf.status(), PartFileStatus::Ready);
    pf.setStatus(PartFileStatus::Insufficient);
    QCOMPARE(pf.status(), PartFileStatus::Ready);

    // And with the latch set, pausing still leaves the file shareable — which is the
    // whole reason the sharing paths pass ignorePause.
    pf.pauseFile();
    QCOMPARE(pf.status(), PartFileStatus::Paused);
    QCOMPARE(pf.status(/*ignorePause=*/true), PartFileStatus::Ready);
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
    pf1.setAutoUpPriority(false);
    pf1.setUpPriority(kPrVeryHigh, false);

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
    QVERIFY(!pf2.isAutoUpPriority());
    QCOMPARE(pf2.upPriority(), kPrVeryHigh);

    // Automatic survives as well
    pf1.setAutoUpPriority(true);
    pf1.savePartFile();
    PartFile pf3;
    QCOMPARE(pf3.loadPartFile(tempDir, pf1.partMetFileName()), PartFileLoadResult::LoadSuccess);
    QVERIFY(pf3.isAutoUpPriority());
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

void tst_PartFile::ratingAveragesSourcesAndKadNotesTogether()
{
    // Source ratings used to be received and dropped: the peer's value was stored
    // on the client and nothing ever folded it into the file, so the indicator sat
    // at 0 for practically every download and looked broken rather than empty.
    PartFile pf;
    pf.setFileSize(PARTSIZE);

    UpDownClient poor;
    poor.setFileRating(2);
    pf.addSource(&poor);
    pf.updateFileRatingCommentAvail();
    QCOMPARE(pf.userRating(), 2u);
    QVERIFY(pf.hasRating());

    // A second opinion, and the average is rounded rather than truncated --
    // MFC's ROUND(). Over a 1-5 scale truncation drags every average down a
    // notch, so 2 and 5 must come out 4, not 3.
    UpDownClient excellent;
    excellent.setFileRating(5);
    pf.addSource(&excellent);
    pf.updateFileRatingCommentAvail();
    QCOMPARE(pf.userRating(), 4u);

    // A comment with no rating says "there is something to read" without
    // pretending to be a score, so it must not move the average.
    UpDownClient commenter;
    commenter.setFileComment(QStringLiteral("works fine"));
    pf.addSource(&commenter);
    pf.updateFileRatingCommentAvail();
    QCOMPARE(pf.userRating(), 4u);
    QVERIFY(pf.hasComment());

    // Kad notes land in the same average as the sources, not in a separate one.
    // 16 bytes: addKadNote requires a stable publisher key of exactly that size.
    pf.addKadNote(QByteArrayLiteral("publisher-0123456"), QStringLiteral("Some Movie.wmv"),
                  QString{}, 5, time(nullptr));
    QCOMPARE(pf.userRating(), 4u);   // (2 + 5 + 5) / 3 = 4
}

void tst_PartFile::aDepartedSourceStopsCounting()
{
    PartFile pf;
    pf.setFileSize(PARTSIZE);

    UpDownClient fake;
    fake.setFileRating(1);
    UpDownClient good;
    good.setFileRating(4);
    pf.addSource(&fake);
    pf.addSource(&good);
    pf.updateFileRatingCommentAvail();
    QCOMPARE(pf.userRating(), 3u);   // (1 + 4) / 2 = 2.5, rounded up

    // Once a peer is gone its opinion goes with it. removeSource() re-aggregates
    // on its own, so nothing has to remember to ask.
    pf.removeSource(&fake);
    QCOMPARE(pf.userRating(), 4u);
}

void tst_PartFile::containerCheckWaitsForTheFirstBytes()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/container");
    QDir().mkpath(tempDir);

    PartFile pf;
    pf.setFileName(QStringLiteral("Some Movie.wmv"));
    pf.setFileSize(PARTSIZE);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    // Nothing downloaded yet. Opening the file here would read a hole full of
    // zeroes and report a perfectly good download as a fake, so the only honest
    // answer is "not yet" -- and it must not be cached, or the verdict would
    // never be revisited once the bytes actually arrive.
    QCOMPARE(pf.containerCheck().verdict, ContainerVerdict::Unchecked);
    QVERIFY(!pf.containerCheck().isSuspect());

    // The real fake's first bytes: random padding that happens to open on an
    // MPEG frame sync. Not the ASF a .wmv has to be.
    static const char kJunk[] = "\xFF\xFB\x10\xC0\x0B\x0A\x07\x05"
                                "\x00\x07\x07\x0A";
    std::vector<uint8> head(kJunk, kJunk + 12);
    pf.writeToBuffer(12, head.data(), 0, 11, nullptr);
    pf.flushBuffer();
    QVERIFY(pf.isComplete(0, 11));

    // Now there is something to judge, and the earlier Unchecked did not stick.
    QCOMPARE(pf.containerCheck().verdict, ContainerVerdict::NoKnownContainer);
    QVERIFY(pf.containerCheck().isSuspect());
    QCOMPARE(pf.containerCheck().expected, QStringLiteral("ASF"));
    QVERIFY(pf.containerCheck().actual.isEmpty());
}

void tst_PartFile::containerCheckReadsTheDataFile()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/containerok");
    QDir().mkpath(tempDir);

    PartFile pf;
    pf.setFileName(QStringLiteral("Some Movie.avi"));
    pf.setFileSize(PARTSIZE);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    // A real AVI head. Read from the .part.met instead, this came back as a fake.
    static const char kAvi[] = "RIFF\x00\x10\x00\x00" "AVI ";
    std::vector<uint8> head(kAvi, kAvi + 12);
    pf.writeToBuffer(12, head.data(), 0, 11, nullptr);
    pf.flushBuffer();

    QCOMPARE(pf.containerCheck().verdict, ContainerVerdict::Matches);
    QVERIFY(!pf.containerCheck().isSuspect());
    QCOMPARE(pf.fakeVerdict().score, 0);
}

void tst_PartFile::fakeVerdictFollowsTheFirstBytesAndNames()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/verdict");
    QDir().mkpath(tempDir);

    PartFile pf;
    pf.setFileName(QStringLiteral("Some Movie 2024.avi"));
    pf.setFileSize(PARTSIZE);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    // Nothing to go on yet
    QCOMPARE(pf.fakeVerdict().score, 0);
    QCOMPARE(pf.fakeVerdict().band, Confidence::LooksGood);

    // The names the search result went by arrive with the download
    pf.addObservedNames({QStringLiteral("Microsoft Office 2010 Pro.zip"),
                         QStringLiteral("Iron Maiden Discography.rar")});
    QVERIFY(pf.fakeVerdict().has(FakeReason::NamesSpanKinds));
    QCOMPARE(pf.fakeVerdict().band, Confidence::Suspect);

    // The first bytes land and are a Windows program: no waiting for the cache to age
    static const char kExe[] = "MZ\x90\x00\x03\x00\x00\x00\x04\x00\x00\x00";
    std::vector<uint8> head(kExe, kExe + 12);
    pf.writeToBuffer(12, head.data(), 0, 11, nullptr);
    pf.flushBuffer();
    const FakeFileVerdict& verdict = pf.fakeVerdict();
    QVERIFY(verdict.has(FakeReason::HeaderExtensionMismatch));
    QVERIFY(verdict.has(FakeReason::ExecutableMasquerade));
    QCOMPARE(verdict.score, 100);
    QCOMPARE(verdict.band, Confidence::LikelyFake);
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

void tst_PartFile::validSourcesCount_countsMfcStates()
{
    // The sources that actually answered us, which is what isSourceRequestAllowed()
    // weighs against the raw source count. MFC CPartFile::GetValidSourcesCount().
    PartFile pf;
    pf.setFileSize(PARTSIZE);

    const std::array<DownloadState, 6> states{
        DownloadState::OnQueue,        DownloadState::Downloading,
        DownloadState::Connected,      DownloadState::RemoteQueueFull,
        DownloadState::NoNeededParts,  DownloadState::None};

    std::vector<std::unique_ptr<UpDownClient>> clients;
    for (const auto state : states) {
        auto c = std::make_unique<UpDownClient>();
        c->setDownloadState(state);
        pf.addSource(c.get());
        clients.push_back(std::move(c));
    }

    QCOMPARE(pf.sourceCount(), 6);
    QCOMPARE(pf.validSourcesCount(), 4);

    for (auto& c : clients)
        pf.removeSource(c.get());
}

void tst_PartFile::updatePartsInfo_rebuildsFrequencyAndCompleteCount()
{
    // addSource()/removeSource() maintain the frequencies incrementally, but a source
    // that re-reports its part status was only ever added, never subtracted — so
    // availability drifted upward with every OP_FILESTATUS and every re-ask. The rebuild
    // is what keeps it honest (MFC CPartFile::UpdatePartsInfo).
    PartFile pf;
    pf.setFileName(QStringLiteral("freq.bin"));
    pf.setFileSize(PARTSIZE * 2 + 100);   // 3 parts
    QCOMPARE(pf.partCount(), uint16{3});

    UpDownClient src;
    pf.addSource(&src);

    auto report = [&pf, &src](uint8 bitmap) {
        SafeMemFile data;
        data.writeUInt16(pf.ed2kPartCount());
        data.writeUInt8(bitmap);
        data.seek(0, SEEK_SET);
        src.processFileStatus(false, data, &pf);
    };

    report(0x07);   // holds all three parts
    QCOMPARE(pf.srcPartFrequency().at(0), uint16{1});
    QCOMPARE(pf.srcPartFrequency().at(2), uint16{1});

    report(0x07);   // says the same thing again — this used to count it twice
    QCOMPARE(pf.srcPartFrequency().at(0), uint16{1});
    QCOMPARE(pf.srcPartFrequency().at(2), uint16{1});

    report(0x01);   // now it only has part 0
    QCOMPARE(pf.srcPartFrequency().at(0), uint16{1});
    QCOMPARE(pf.srcPartFrequency().at(1), uint16{0});
    QCOMPARE(pf.srcPartFrequency().at(2), uint16{0});

    pf.removeSource(&src);
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
    // An exact multiple of PARTSIZE is the case where the two part counts differ: 3 data
    // parts, but the ED2K count that goes on the wire is 4. Sending our own count made
    // MFC peers answer "wrong part number" for every such file.
    PartFile pf;
    pf.setFileSize(PARTSIZE * 3);
    QCOMPARE(pf.partCount(), static_cast<uint16>(3));

    SafeMemFile file;
    pf.writePartStatus(file);

    file.seek(0, 0); // SEEK_SET
    QCOMPARE(pf.ed2kPartCount(), static_cast<uint16>(4));
    QCOMPARE(file.readUInt16(), pf.ed2kPartCount());

    // The trailing zero-length part always reads as complete, which is what MFC's
    // IsCompleteBDSafe() yields over an empty range.
    const uint8 bits = file.readUInt8();
    QVERIFY2((bits & 0x08) != 0, "the extra ED2K part must be marked complete");

    // A size that is not a multiple has both counts equal.
    PartFile odd;
    odd.setFileSize(PARTSIZE * 3 + 1);
    SafeMemFile oddFile;
    odd.writePartStatus(oddFile);
    oddFile.seek(0, 0);
    QCOMPARE(oddFile.readUInt16(), odd.partCount());
    QCOMPARE(odd.partCount(), odd.ed2kPartCount());
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

namespace {

using testing::ScopedStatistics;

/// Deterministic filler so a re-written block is byte-identical to the first one.
std::vector<uint8> makePattern(size_t size)
{
    std::vector<uint8> data(size);
    for (size_t i = 0; i < size; ++i)
        data[i] = static_cast<uint8>((i * 31 + 7) & 0xFF);
    return data;
}

} // namespace

// A compressed block covers more of the file than it took on the wire; that
// difference is the gain. Before this, writeToBuffer() dropped transize on the
// floor and both compression rows sat at whatever the .part.met last held.
// MFC CreatePartFile, srchybrid/PartFile.cpp:449. The option existed and did nothing.
void tst_PartFile::createPartFile_cleansTheNameWhenAsked()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/cleanup-temp");
    QDir().mkpath(tempDir);
    const bool before = thePrefs.autoCleanupFilenames();

    auto nameAfterCreate = [&](bool cleanup) {
        thePrefs.setAutoCleanupFilenames(cleanup);
        PartFile pf;
        pf.setFileName(QStringLiteral("www.site.com_My.Movie.avi"), true);
        pf.setFileSize(1000);
        pf.setTmpPath(tempDir);
        return pf.createPartFile(tempDir) ? pf.fileName() : QStringLiteral("<create failed>");
    };

    QCOMPARE(nameAfterCreate(false), QStringLiteral("www.site.com_My.Movie.avi"));
    QCOMPARE(nameAfterCreate(true), QStringLiteral("Site My Movie.avi"));

    thePrefs.setAutoCleanupFilenames(before);
}

namespace { std::array<uint8, 16> md4Of(const std::vector<uint8>& data); }

// MFC srchybrid/PartFile.cpp:3966-3985. The endgame hands one block to several sources,
// so a second copy is routine: it must not be written again, and a late block must never
// land on a part that already verified.
void tst_PartFile::writeToBuffer_refusesDuplicatesAndCompleteParts()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/dup-temp");
    QDir().mkpath(tempDir);

    PartFile pf;
    pf.setFileName(QStringLiteral("dup.bin"));
    pf.setFileSize(PARTSIZE * 2 + 1000);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    const auto data = makePattern(PARTSIZE);
    auto& hashSet = pf.fileIdentifier().getRawMD4HashSet();
    hashSet.assign(3, md4Of(data));
    hashSet[2].fill(0xEE);

    // First copy is taken, the second is not.
    QCOMPARE(pf.writeToBuffer(1000, data.data(), PARTSIZE, PARTSIZE + 999, nullptr), 1000u);
    QCOMPARE(pf.writeToBuffer(1000, data.data(), PARTSIZE, PARTSIZE + 999, nullptr), 0u);

    // Past the end of the file.
    const uint64 size = PARTSIZE * 2 + 1000;
    QCOMPARE(pf.writeToBuffer(10, data.data(), size - 5, size + 4, nullptr), 0u);

    // Part 0 completes and verifies; nothing may be written into it afterwards, not
    // even a range that straddles into the still-open part 1.
    QCOMPARE(pf.writeToBuffer(PARTSIZE, data.data(), 0, PARTSIZE - 1, nullptr),
             static_cast<uint32>(PARTSIZE));
    pf.flushBuffer();
    QVERIFY(pf.isComplete(0u));
    const uint64 before = pf.totalGapSizeInPart(1);
    QCOMPARE(pf.writeToBuffer(2000, data.data(), PARTSIZE - 1000, PARTSIZE + 999 + 0, nullptr), 0u);
    QCOMPARE(pf.totalGapSizeInPart(1), before);
}

void tst_PartFile::writeToBuffer_countsCompressionGain()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/temp");
    QDir().mkpath(tempDir);

    ScopedStatistics stats;

    PartFile pf;
    pf.setFileSize(10000);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    const auto data = makePattern(1000);

    // 1000 bytes of file arrived as 400 bytes on the wire.
    pf.writeToBuffer(400, data.data(), 0, 999, nullptr);
    QCOMPARE(pf.compressionGain(), uint64{600});
    QCOMPARE(stats->sesCompressionGain(), uint64{600});

    // An uncompressed block costs exactly what it covers and counts nothing.
    pf.writeToBuffer(1000, data.data(), 1000, 1999, nullptr);
    QCOMPARE(pf.compressionGain(), uint64{600});
    QCOMPARE(stats->sesCompressionGain(), uint64{600});
}

// ICH: a part that failed its hash keeps its bytes on disk — they are only
// distrusted. When the part hashes clean on a later flush, the gaps were never
// really missing, so they are filled instead of re-downloaded.
void tst_PartFile::ich_recoversCorruptedPartOnRehash()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/temp");
    QDir().mkpath(tempDir);

    ScopedStatistics stats;
    QVERIFY(thePrefs.useICH());

    // Two parts, so recovering the first one does not complete the file and send
    // us down the file-move path.
    const uint64 fileSize = PARTSIZE + 1000;
    PartFile pf;
    pf.setFileSize(fileSize);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    const auto part0 = makePattern(PARTSIZE);

    uint8 goodHash[16]{};
    QVERIFY(KnownFile::createHashFromMemory(part0.data(), PARTSIZE, goodHash, nullptr));

    // Claim a hash the data does not have, so the first check condemns the part.
    auto& hashSet = pf.fileIdentifier().getRawMD4HashSet();
    hashSet.resize(2);
    hashSet[0].fill(0xEE);
    hashSet[1].fill(0xEE);

    pf.writeToBuffer(PARTSIZE, part0.data(), 0, PARTSIZE - 1, nullptr);
    pf.flushBuffer();

    QVERIFY(pf.isCorruptedPart(0));
    QCOMPARE(pf.totalGapSizeInPart(0), uint64{PARTSIZE});
    QCOMPARE(pf.corruptionLoss(), uint64{PARTSIZE});
    QCOMPARE(stats->sesCorruptionLoss(), uint64{PARTSIZE});
    QCOMPARE(stats->sesIchPartsSaved(), uint32{0});

    // The hashset turned out to be the liar, not the data: now one block of the
    // part is re-received and the part is asked to prove itself again.
    std::ranges::copy(std::span(goodHash, 16), hashSet[0].begin());
    pf.writeToBuffer(1000, part0.data(), 0, 999, nullptr);
    pf.flushBuffer();

    QVERIFY(!pf.isCorruptedPart(0));
    QCOMPARE(pf.totalGapSizeInPart(0), uint64{0});
    QCOMPARE(stats->sesIchPartsSaved(), uint32{1});

    // Everything but the re-received block was credited back.
    QCOMPARE(pf.corruptionLoss(), uint64{1000});
    QCOMPARE(stats->sesCorruptionLoss(), uint64{1000});

    // The second part is untouched, so the file is not complete.
    QVERIFY(pf.totalGapSize() > 0);
}

// An AICH root hash that arrives with an ed2k link or a search result is as trustworthy
// as the file hash beside it, and MFC seeds the recovery set from it at creation
// (srchybrid/PartFile.cpp:97, :188). Without this the set stays Empty and
// requestAICHRecovery() bails, so recovery was off for every download until a restart
// reloaded the .part.met.
void tst_PartFile::seedAICHRecoveryMasterHash_adoptsTheIdentifierHash()
{
    PartFile pf;
    pf.setFileName(QStringLiteral("aich-seed.bin"));
    pf.setFileSize(PARTSIZE * 3);

    // Nothing to adopt yet.
    pf.seedAICHRecoveryMasterHash();
    QCOMPARE(pf.aichRecoveryHashSet().getStatus(), EAICHStatus::Empty);

    uint8 raw[kAICHHashSize];
    std::memset(raw, 0x6E, sizeof(raw));
    const AICHHash root(raw);
    pf.fileIdentifier().setAICHHash(root);

    pf.seedAICHRecoveryMasterHash();
    QCOMPARE(pf.aichRecoveryHashSet().getStatus(), EAICHStatus::Verified);
    QCOMPARE(pf.aichRecoveryHashSet().getMasterHash(), root);

    // A set peers have already voted into Trusted is not downgraded by a second seeding.
    pf.aichRecoveryHashSet().setStatus(EAICHStatus::Trusted);
    pf.seedAICHRecoveryMasterHash();
    QCOMPARE(pf.aichRecoveryHashSet().getStatus(), EAICHStatus::Trusted);
}

// ---------------------------------------------------------------------------
// hashSinglePart — MFC demands that MD4 and AICH agree (PartFile.cpp:3124-3129)
// ---------------------------------------------------------------------------

// A file below one PARTSIZE has no MD4 part hashes — its file hash *is* the part hash
// (MFC PartFile.cpp:3168-3170). Without that arm nothing was ever compared for such a
// file, so a corrupt one verified clean and went straight to the incoming folder.
void tst_PartFile::hashSinglePart_singlePartFileComparesTheFileHash()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/single-temp");
    const QString incoming = m_tempDir.path() + QStringLiteral("/single-in");
    QDir().mkpath(tempDir);
    QDir().mkpath(incoming);
    thePrefs.setIncomingDir(incoming);

    ScopedStatistics stats;

    constexpr uint64 fileSize = 5000;
    PartFile pf;
    pf.setFileName(QStringLiteral("single.bin"));
    pf.setFileSize(fileSize);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    // No part hashes exist, and none are expected — that is exactly the case the
    // missing arm used to wave through.
    QCOMPARE(pf.fileIdentifier().getAvailableMD4PartHashCount(), uint16(0));
    QVERIFY(pf.fileIdentifier().hasExpectedMD4HashCount());

    const auto data = makePattern(fileSize);
    uint8 realHash[16]{};
    QVERIFY(KnownFile::createHashFromMemory(data.data(), fileSize, realHash, nullptr));

    uint8 wrongHash[16];
    std::memset(wrongHash, 0xEE, sizeof(wrongHash));
    pf.setFileHash(wrongHash);

    pf.writeToBuffer(fileSize, data.data(), 0, fileSize - 1, nullptr);
    pf.flushBuffer();

    QVERIFY2(pf.isCorruptedPart(0), "a single-part file whose hash disagrees must be condemned");
    QCOMPARE(pf.totalGapSizeInPart(0), fileSize);

    // The hash was the liar: with the right one the same bytes verify, and the file
    // finishes. (ICH re-hashes the part on the next flush.)
    pf.setFileHash(realHash);
    pf.writeToBuffer(1000, data.data(), 0, 999, nullptr);
    pf.flushBuffer();

    QVERIFY(!pf.isCorruptedPart(0));
    QCOMPARE(pf.totalGapSizeInPart(0), uint64{0});

    // The move runs on its own thread; let it land before the temp dir goes away.
    QTRY_COMPARE_WITH_TIMEOUT(pf.status(), PartFileStatus::Complete, 5000);
}

// Exactly one PARTSIZE is the awkward case MFC spells out: partCount() is 1, but the
// theoretical hashset has two entries, so the *part* hash decides, not the file hash.
void tst_PartFile::hashSinglePart_exactlyOnePartSizeUsesThePartHash()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/exact-temp");
    const QString incoming = m_tempDir.path() + QStringLiteral("/exact-in");
    QDir().mkpath(tempDir);
    QDir().mkpath(incoming);
    thePrefs.setIncomingDir(incoming);

    ScopedStatistics stats;

    PartFile pf;
    pf.setFileName(QStringLiteral("exact.bin"));
    pf.setFileSize(PARTSIZE);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    const auto part0 = makePattern(PARTSIZE);
    uint8 goodHash[16]{};
    QVERIFY(KnownFile::createHashFromMemory(part0.data(), PARTSIZE, goodHash, nullptr));

    // Two entries, as GetTheoreticalMD4PartHashCount() demands for an exact multiple.
    auto& hashSet = pf.fileIdentifier().getRawMD4HashSet();
    hashSet.resize(2);
    std::ranges::copy(std::span(goodHash, 16), hashSet[0].begin());
    hashSet[1].fill(0x00);
    QVERIFY(pf.fileIdentifier().hasExpectedMD4HashCount());

    // Deliberately nonsense: if the file hash were consulted here the part would be
    // condemned, which is how this pins the `|| fileSize == PARTSIZE` arm.
    uint8 wrongFileHash[16];
    std::memset(wrongFileHash, 0xEE, sizeof(wrongFileHash));
    pf.setFileHash(wrongFileHash);

    pf.writeToBuffer(PARTSIZE, part0.data(), 0, PARTSIZE - 1, nullptr);
    pf.flushBuffer();

    QVERIFY(!pf.isCorruptedPart(0));
    QCOMPARE(pf.corruptionLoss(), uint64{0});
    QTRY_COMPARE_WITH_TIMEOUT(pf.status(), PartFileStatus::Complete, 5000);
}

// With neither hashset there is nothing to compare against. MFC says so, asks for both,
// and does not condemn the data (PartFile.cpp:3132-3137).
void tst_PartFile::hashSinglePart_missingHashsetsBailAndFlagBothNeeded()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/nohash-temp");
    QDir().mkpath(tempDir);

    ScopedStatistics stats;

    PartFile pf;
    pf.setFileName(QStringLiteral("nohash.bin"));
    pf.setFileSize(PARTSIZE * 2);        // two parts, so part 0 does not finish the file
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    // Pretend both were satisfied, so the flags prove the bail actually ran.
    pf.setMD4HashsetNeeded(false);
    pf.setAICHPartHashsetNeeded(false);
    QVERIFY(!pf.fileIdentifier().hasExpectedMD4HashCount());

    const auto part0 = makePattern(PARTSIZE);
    pf.writeToBuffer(PARTSIZE, part0.data(), 0, PARTSIZE - 1, nullptr);
    pf.flushBuffer();

    QVERIFY2(!pf.isCorruptedPart(0), "unverifiable is not the same as corrupt");
    QCOMPARE(pf.corruptionLoss(), uint64{0});
    QVERIFY(pf.isMD4HashsetNeeded());
    QVERIFY(pf.isAICHPartHashsetNeeded());
}

namespace {

/// Two-part file (one full part + 1000 bytes) with real part hashes, on disk.
struct TwoPartFixture {
    static constexpr uint64 kTail = 1000;
    std::vector<uint8> data = makePattern(PARTSIZE + kTail);
    PartFile pf;

    explicit TwoPartFixture(const QString& tempDir, const QString& name)
    {
        pf.setFileName(name);
        pf.setFileSize(PARTSIZE + kTail);
        pf.setTmpPath(tempDir);
        pf.createPartFile(tempDir);

        auto& hashSet = pf.fileIdentifier().getRawMD4HashSet();
        hashSet.resize(2);
        KnownFile::createHashFromMemory(data.data(), PARTSIZE, hashSet[0].data(), nullptr);
        KnownFile::createHashFromMemory(data.data() + PARTSIZE, kTail, hashSet[1].data(), nullptr);
    }

    void write(uint64 start, uint64 end, const uint8* from = nullptr)
    {
        pf.writeToBuffer(end - start + 1, from ? from : data.data() + start, start, end, nullptr);
        pf.flushBuffer();
    }
};

} // namespace

// A part that cannot be read back has no verdict. It used to count as verified: shared,
// and the file completed (MFC throws and puts the file in error, PartFile.cpp:4276-4313).
void tst_PartFile::unreadablePart_isNotVerifiedAndBlocksCompletion()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/unread-temp");
    const QString incoming = m_tempDir.path() + QStringLiteral("/unread-in");
    QDir().mkpath(tempDir);
    QDir().mkpath(incoming);
    thePrefs.setIncomingDir(incoming);

    ScopedStatistics stats;
    TwoPartFixture f(tempDir, QStringLiteral("unread.bin"));
    QVERIFY(f.pf.fileIdentifier().hasExpectedMD4HashCount());

    f.write(0, PARTSIZE - 1);
    QVERIFY(f.pf.isComplete(0u));
    f.write(PARTSIZE + 100, PARTSIZE + TwoPartFixture::kTail - 1);

    // The tail goes missing under us; the last write then leaves the file 900 bytes short.
    const QString partPath = f.pf.partDataPath();
    QVERIFY(QFile::resize(partPath, static_cast<qint64>(PARTSIZE + 50)));
    f.write(PARTSIZE, PARTSIZE + 99);

    QCOMPARE(f.pf.status(), PartFileStatus::Error);
    QVERIFY(f.pf.completionError());
    QVERIFY2(!f.pf.isCorruptedPart(1), "unreadable is not the same as corrupt");
    QCOMPARE(f.pf.totalGapSizeInPart(1), uint64{0});
    QTest::qWait(200);
    QVERIFY(QDir(incoming).isEmpty());

    // Bytes back in place: resume checks the part for real and completes.
    {
        QFile part(partPath);
        QVERIFY(part.open(QIODevice::ReadWrite));
        QVERIFY(part.seek(static_cast<qint64>(PARTSIZE)));
        part.write(reinterpret_cast<const char*>(f.data.data() + PARTSIZE), TwoPartFixture::kTail);
    }
    f.pf.resumeFile();
    QTRY_COMPARE_WITH_TIMEOUT(f.pf.status(), PartFileStatus::Complete, 5000);
}

// ICH re-hashes a corrupted part that still has gaps and, on a match, fills them all.
// An unreadable part must not count as that match.
void tst_PartFile::unreadablePart_ichDoesNotFillItsGaps()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/unread-ich-temp");
    QDir().mkpath(tempDir);

    const bool hadICH = thePrefs.useICH();
    thePrefs.setUseICH(true);

    ScopedStatistics stats;
    TwoPartFixture f(tempDir, QStringLiteral("unread-ich.bin"));

    // Part 1 arrives corrupt: gapped again and remembered as corrupted.
    const std::vector<uint8> junk(TwoPartFixture::kTail, 0xAB);
    f.write(PARTSIZE, PARTSIZE + TwoPartFixture::kTail - 1, junk.data());
    QVERIFY(f.pf.isCorruptedPart(1));
    QCOMPARE(f.pf.totalGapSizeInPart(1), TwoPartFixture::kTail);

    QVERIFY(QFile::resize(f.pf.partDataPath(), static_cast<qint64>(PARTSIZE + 50)));
    f.write(PARTSIZE, PARTSIZE + 99);

    QCOMPARE(f.pf.totalGapSizeInPart(1), TwoPartFixture::kTail - 100);
    QVERIFY(f.pf.isCorruptedPart(1));

    thePrefs.setUseICH(hadICH);
}

// MFC re-reads the whole file before it delivers it (CompleteFile(false),
// PartFile.cpp:2685-2694). A part that verified once and was damaged on disk since must
// not reach the incoming folder, where it would be shared under a hash it no longer has.
void tst_PartFile::completion_damagedPartIsNotDelivered()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/final-temp");
    const QString incoming = m_tempDir.path() + QStringLiteral("/final-in");
    QDir().mkpath(tempDir);
    QDir().mkpath(incoming);
    thePrefs.setIncomingDir(incoming);

    ScopedStatistics stats;
    TwoPartFixture f(tempDir, QStringLiteral("final.bin"));
    QSignalSpy moved(f.pf.partNotifier(), &PartFileNotifier::fileMoveFinished);

    f.write(0, PARTSIZE - 1);
    QVERIFY(f.pf.isComplete(0u));

    // Part 0 rots on disk after its check.
    {
        QFile part(f.pf.partDataPath());
        QVERIFY(part.open(QIODevice::ReadWrite));
        QVERIFY(part.seek(4096));
        part.write(QByteArray(512, '\xEE'));
    }

    f.write(PARTSIZE, PARTSIZE + TwoPartFixture::kTail - 1);
    QCOMPARE(f.pf.status(), PartFileStatus::Completing);

    QTRY_COMPARE_WITH_TIMEOUT(moved.count(), 1, 10000);
    QCOMPARE(moved.takeFirst().at(0).toBool(), false);
    QCOMPARE(f.pf.status(), PartFileStatus::Ready);
    QVERIFY(!f.pf.isComplete(0u));
    QCOMPARE(f.pf.totalGapSizeInPart(0), uint64{PARTSIZE});
    QVERIFY(f.pf.isComplete(1u));
    QVERIFY(QDir(incoming).isEmpty());
    QVERIFY(QFile::exists(f.pf.partDataPath()));

    // Downloaded again, it is delivered — with the right bytes.
    f.write(0, PARTSIZE - 1);
    QTRY_COMPARE_WITH_TIMEOUT(f.pf.status(), PartFileStatus::Complete, 10000);
    QFile delivered(incoming + QStringLiteral("/final.bin"));
    QVERIFY(delivered.open(QIODevice::ReadOnly));
    const QByteArray bytes = delivered.readAll();
    QCOMPARE(bytes.size(), qsizetype(f.data.size()));
    QVERIFY(std::memcmp(bytes.constData(), f.data.data(), f.data.size()) == 0);
}

// R10: across volumes the file is copied to a staged sibling and renamed in place, so
// the final name never holds a half-written file.
void tst_PartFile::copyThenRename_deliversAndLeavesNoStagedFile()
{
    const QString dir = m_tempDir.path() + QStringLiteral("/copy-ok");
    QDir().mkpath(dir);
    const QString src = dir + QStringLiteral("/001.part");
    const QString dest = dir + QStringLiteral("/movie.bin");
    const QString staged = dest + Preferences::kCompletingSuffix;

    const std::vector<uint8> data = makePattern(700 * 1024 + 17);
    QFile f(src);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<qint64>(data.size()));
    f.close();
    // What a crash during an earlier attempt left behind
    QFile stale(staged);
    QVERIFY(stale.open(QIODevice::WriteOnly));
    stale.write(QByteArray(4 * 1024 * 1024, 'x'));
    stale.close();

    QVERIFY(FileMoveThread::copyThenRename(src, dest, [] { return true; }));

    QVERIFY(!QFile::exists(staged));
    QVERIFY(!QFile::exists(src));
    QFile out(dest);
    QVERIFY(out.open(QIODevice::ReadOnly));
    const QByteArray bytes = out.readAll();
    QCOMPARE(bytes.size(), qsizetype(data.size()));
    QVERIFY(std::memcmp(bytes.constData(), data.data(), data.size()) == 0);
}

void tst_PartFile::copyThenRename_interruptedLeavesTheSourceOnly()
{
    const QString dir = m_tempDir.path() + QStringLiteral("/copy-stop");
    QDir().mkpath(dir);
    const QString src = dir + QStringLiteral("/001.part");
    const QString dest = dir + QStringLiteral("/movie.bin");

    QFile f(src);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(900 * 1024, 'p'));
    f.close();

    int blocks = 0;
    QVERIFY(!FileMoveThread::copyThenRename(src, dest, [&blocks] { return ++blocks < 3; }));

    QVERIFY(!QFile::exists(dest));
    QVERIFY(!QFile::exists(dest + Preferences::kCompletingSuffix));
    QCOMPARE(QFileInfo(src).size(), qint64(900 * 1024));
}

void tst_PartFile::copyThenRename_missingSourceFails()
{
    const QString dir = m_tempDir.path() + QStringLiteral("/copy-none");
    QDir().mkpath(dir);
    const QString dest = dir + QStringLiteral("/movie.bin");

    QVERIFY(!FileMoveThread::copyThenRename(dir + QStringLiteral("/gone.part"), dest,
                                            [] { return true; }));
    QVERIFY(QDir(dir).isEmpty());
}

// R11: the delivered file's date is what known.met stores and the next scan matches by
// (KnownFileList::findKnownFile). Left unset, every completed download was rehashed.
void tst_PartFile::completion_setsTheKnownFileDate()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/date-temp");
    const QString incoming = m_tempDir.path() + QStringLiteral("/date-in");
    QDir().mkpath(tempDir);
    QDir().mkpath(incoming);
    thePrefs.setIncomingDir(incoming);

    ScopedStatistics stats;
    TwoPartFixture f(tempDir, QStringLiteral("dated.bin"));
    QCOMPARE(f.pf.utcFileDate(), static_cast<time_t>(-1));

    f.write(0, PARTSIZE + TwoPartFixture::kTail - 1);
    QTRY_COMPARE_WITH_TIMEOUT(f.pf.status(), PartFileStatus::Complete, 10000);

    const QFileInfo delivered(incoming + QStringLiteral("/dated.bin"));
    QVERIFY(delivered.exists());
    QCOMPARE(f.pf.utcFileDate(),
             static_cast<time_t>(delivered.lastModified().toSecsSinceEpoch()));
}

// MFC srchybrid/PartFile.cpp:1543-1550: the completion hash also yields the AICH recovery
// set. Without it the next start read every completed file once more to build it.
void tst_PartFile::completion_storesTheAICHRecoverySet()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/aich-temp");
    const QString incoming = m_tempDir.path() + QStringLiteral("/aich-in");
    QDir().mkpath(tempDir);
    QDir().mkpath(incoming);
    thePrefs.setIncomingDir(incoming);
    AICHRecoveryHashSet::setKnown2MetPath(m_tempDir.path() + QStringLiteral("/aich-known2_64.met"));
    const auto restore = qScopeGuard([] { AICHRecoveryHashSet::setKnown2MetPath(QString()); });

    ScopedStatistics stats;
    TwoPartFixture f(tempDir, QStringLiteral("recoverable.bin"));
    QVERIFY(!f.pf.isAICHRecoverHashSetAvailable());

    f.write(0, PARTSIZE + TwoPartFixture::kTail - 1);
    QTRY_COMPARE_WITH_TIMEOUT(f.pf.status(), PartFileStatus::Complete, 10000);

    const QString delivered = incoming + QStringLiteral("/recoverable.bin");
    AICHRecoveryHashSet expected(f.pf.fileSize());
    QVERIFY(KnownFile::buildAICHHashSet(delivered, static_cast<uint64>(f.pf.fileSize()), expected));

    QVERIFY(f.pf.fileIdentifier().hasAICHHash());
    QCOMPARE(f.pf.fileIdentifier().getAICHHash(), expected.getMasterHash());
    QVERIFY(AICHRecoveryHashSet::isStored(expected.getMasterHash()));
    QVERIFY(f.pf.isAICHRecoverHashSetAvailable());
    QVERIFY(f.pf.fileIdentifier().hasExpectedAICHHashCount());
}

void tst_PartFile::completion_loadedCompleteFileIsVerifiedAndDelivered_data()
{
    QTest::addColumn<bool>("truncated");
    QTest::newRow("intact") << false;
    QTest::newRow("tail missing") << true;
}

// A run that ended between "all downloaded" and "delivered" leaves a .part.met with no
// gaps. Nothing used to pick that up again: the file sat at Completing for good.
void tst_PartFile::completion_loadedCompleteFileIsVerifiedAndDelivered()
{
    QFETCH(bool, truncated);

    const QString tag = truncated ? QStringLiteral("cut") : QStringLiteral("ok");
    const QString tempDir = m_tempDir.path() + QStringLiteral("/loaded-temp-") + tag;
    const QString incoming = m_tempDir.path() + QStringLiteral("/loaded-in-") + tag;
    QDir().mkpath(tempDir);
    QDir().mkpath(incoming);
    thePrefs.setIncomingDir(incoming);

    ScopedStatistics stats;
    QString metName;
    QString partPath;
    std::vector<uint8> data;
    {
        // Destroyed the instant its completion starts: the worker is interrupted before
        // it can move anything, and the destructor saves the gapless .part.met.
        TwoPartFixture f(tempDir, QStringLiteral("loaded.bin"));
        metName = f.pf.partMetFileName();
        partPath = f.pf.partDataPath();
        data = f.data;
        f.write(0, PARTSIZE - 1);
        f.write(PARTSIZE, PARTSIZE + TwoPartFixture::kTail - 1);
    }
    QVERIFY2(QFile::exists(partPath), "the interrupted completion moved the file anyway");
    QVERIFY(QDir(incoming).isEmpty());

    PartFile loaded;
    QCOMPARE(loaded.loadPartFile(tempDir, metName), PartFileLoadResult::LoadSuccess);
    QCOMPARE(loaded.status(), PartFileStatus::Completing);
    QSignalSpy moved(loaded.partNotifier(), &PartFileNotifier::fileMoveFinished);

    // After the load, which gaps a short file by itself: the final check is on its own.
    if (truncated)
        QVERIFY(QFile::resize(partPath, static_cast<qint64>(PARTSIZE + 50)));

    loaded.finishLoadedDownload();
    QTRY_COMPARE_WITH_TIMEOUT(moved.count(), 1, 10000);

    if (!truncated) {
        QCOMPARE(moved.takeFirst().at(0).toBool(), true);
        QCOMPARE(loaded.status(), PartFileStatus::Complete);
        QVERIFY(QFile::exists(incoming + QStringLiteral("/loaded.bin")));
        return;
    }

    QCOMPARE(moved.takeFirst().at(0).toBool(), false);
    QCOMPARE(loaded.status(), PartFileStatus::Error);
    QVERIFY(loaded.completionError());
    QVERIFY(QDir(incoming).isEmpty());
    QCOMPARE(loaded.totalGapSizeInPart(1), uint64{0});   // unread is not corrupt

    // Readable again: resume delivers it.
    {
        QFile part(partPath);
        QVERIFY(part.open(QIODevice::ReadWrite));
        QVERIFY(part.seek(static_cast<qint64>(PARTSIZE)));
        part.write(reinterpret_cast<const char*>(data.data() + PARTSIZE), TwoPartFixture::kTail);
    }
    loaded.resumeFile();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.status(), PartFileStatus::Complete, 10000);
}

void tst_PartFile::verifyPartData_reportsUnreadParts()
{
    const QString dir = m_tempDir.path() + QStringLiteral("/verify-data");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/data.part");

    const auto data = makePattern(PARTSIZE + 1000);
    std::vector<std::array<uint8, 16>> hashes(2);
    KnownFile::createHashFromMemory(data.data(), PARTSIZE, hashes[0].data(), nullptr);
    KnownFile::createHashFromMemory(data.data() + PARTSIZE, 1000, hashes[1].data(), nullptr);

    const auto verdicts = [&] {
        return PartFile::verifyPartData(path, data.size(), QByteArray(16, '\0'), hashes);
    };
    const auto v = [](PartFile::PartVerdict x) { return static_cast<char>(x); };
    using V = PartFile::PartVerdict;

    QCOMPARE(verdicts(), QByteArray(2, v(V::Unread)));          // no file at all

    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<qint64>(data.size()));
    }
    QCOMPARE(verdicts(), QByteArray(2, v(V::Ok)));

    QVERIFY(QFile::resize(path, static_cast<qint64>(PARTSIZE + 10)));
    QCOMPARE(verdicts(), QByteArray().append(v(V::Ok)).append(v(V::Unread)));

    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.write("damage", 6);
    }
    QCOMPARE(verdicts(), QByteArray().append(v(V::Bad)).append(v(V::Unread)));
}

// MD4 alone is not enough: MFC condemns a part whose AICH part hash disagrees, however
// happy MD4 is (PartFile.cpp:3173-3198). Ours compared against the recovery tree, which
// for an ordinary download holds nothing but a root hash, so the check never ran.
void tst_PartFile::hashSinglePart_aichDisagreementCondemnsThePart()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/aich-temp");
    QDir().mkpath(tempDir);

    ScopedStatistics stats;

    constexpr uint64 fileSize = PARTSIZE + 1000;
    PartFile pf;
    pf.setFileName(QStringLiteral("aich.bin"));
    pf.setFileSize(fileSize);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    const auto part0 = makePattern(PARTSIZE);

    // MD4 is made to agree, so the verdict can only come from AICH.
    uint8 goodHash[16]{};
    QVERIFY(KnownFile::createHashFromMemory(part0.data(), PARTSIZE, goodHash, nullptr));
    auto& md4Set = pf.fileIdentifier().getRawMD4HashSet();
    md4Set.resize(2);
    std::ranges::copy(std::span(goodHash, 16), md4Set[0].begin());
    md4Set[1].fill(0x00);
    QVERIFY(pf.fileIdentifier().hasExpectedMD4HashCount());

    // An internally consistent AICH hashset that describes some other file. Part 1 is
    // never written, so only part 0's hash matters.
    std::vector<AICHHash> partHashes;
    for (int i = 0; i < 2; ++i) {
        ShaHasher h;
        const QByteArray seed = QByteArray::number(i) + "not-the-data";
        h.add(seed.constData(), static_cast<uint32>(seed.size()));
        AICHHash ph;
        h.finish(ph);
        partHashes.push_back(ph);
    }

    EMFileSize aichSize(fileSize);
    AICHRecoveryHashSet source(aichSize);
    for (uint32 i = 0; i < 2; ++i) {
        const uint64 partStart = static_cast<uint64>(i) * PARTSIZE;
        const auto partSize = static_cast<uint32>(std::min<uint64>(PARTSIZE, fileSize - partStart));
        AICHHashTree* node = source.m_hashTree.findHash(partStart, partSize);
        QVERIFY(node != nullptr);
        node->m_hash = partHashes[i];
        node->m_hashValid = true;
    }
    QVERIFY(source.reCalculateHash(false));

    pf.fileIdentifier().setAICHHash(source.getMasterHash());

    SafeMemFile blob;
    AICHHash master = source.getMasterHash();
    master.write(blob);
    blob.writeUInt16(static_cast<uint16>(partHashes.size()));
    for (auto& ph : partHashes)
        ph.write(blob);
    blob.seek(0, 0);
    QVERIFY(pf.fileIdentifier().loadAICHHashsetFromFile(blob, true));
    QVERIFY(pf.fileIdentifier().hasExpectedAICHHashCount());

    pf.writeToBuffer(PARTSIZE, part0.data(), 0, PARTSIZE - 1, nullptr);
    pf.flushBuffer();

    QVERIFY2(pf.isCorruptedPart(0),
             "MD4 agreed but AICH did not — eMule condemns the part either way");
    QCOMPARE(pf.totalGapSizeInPart(0), uint64{PARTSIZE});
}

namespace {

// Overwrite the head of part 0 behind PartFile's back
void clobberPartZeroOnDisk(const QString& dir)
{
    const auto parts = QDir(dir).entryList({QStringLiteral("*.part")}, QDir::Files);
    QCOMPARE(parts.size(), 1);
    QFile f(dir + u'/' + parts.first());
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.seek(0));
    QCOMPARE(f.write(QByteArray(4096, '\0')), qint64{4096});
}

std::array<uint8, 16> md4Of(const std::vector<uint8>& data)
{
    std::array<uint8, 16> h{};
    KnownFile::createHashFromMemory(data.data(), static_cast<uint32>(data.size()), h.data(), nullptr);
    return h;
}

} // namespace

// A flush re-checks only the parts it wrote. Re-hashing every finished part on every
// flush cost GBs of disk reads per flush near the end of a large download.
void tst_PartFile::flushBuffer_verifiesOnlyChangedParts()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/changed-temp");
    QDir().mkpath(tempDir);
    ScopedStatistics stats;

    PartFile pf;
    pf.setFileName(QStringLiteral("changed.bin"));
    pf.setFileSize(PARTSIZE * 2 + 1000);     // three parts, never completes here
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    const auto data = makePattern(PARTSIZE);
    auto& hashSet = pf.fileIdentifier().getRawMD4HashSet();
    hashSet.assign(3, md4Of(data));
    hashSet[2].fill(0xEE);
    QVERIFY(pf.fileIdentifier().hasExpectedMD4HashCount());

    pf.writeToBuffer(PARTSIZE, data.data(), 0, PARTSIZE - 1, nullptr);
    pf.flushBuffer();
    QCOMPARE(pf.totalGapSizeInPart(0), uint64{0});

    // Part 0 goes bad on disk; the next flush only touches part 1
    clobberPartZeroOnDisk(tempDir);
    pf.writeToBuffer(PARTSIZE, data.data(), PARTSIZE, 2 * PARTSIZE - 1, nullptr);
    pf.flushBuffer();

    QCOMPARE(pf.totalGapSizeInPart(1), uint64{0});
    QVERIFY2(pf.totalGapSizeInPart(0) == 0 && !pf.isCorruptedPart(0),
             "part 0 was not written, so it must not be re-hashed");
}

// A part finished without a hashset passes unverified; once the hashset arrives it is
// checked, and a bad one is sent back for re-download.
void tst_PartFile::hashsetReceived_condemnsBadPartCompletedWithoutHashset()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/late-bad-temp");
    QDir().mkpath(tempDir);
    ScopedStatistics stats;

    PartFile pf;
    pf.setFileName(QStringLiteral("late-bad.bin"));
    pf.setFileSize(PARTSIZE * 2 + 1000);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    const auto data = makePattern(PARTSIZE);
    pf.writeToBuffer(PARTSIZE, data.data(), 0, PARTSIZE - 1, nullptr);
    pf.flushBuffer();
    QCOMPARE(pf.totalGapSizeInPart(0), uint64{0});
    QVERIFY(!pf.isCorruptedPart(0));

    auto& hashSet = pf.fileIdentifier().getRawMD4HashSet();
    hashSet.assign(3, std::array<uint8, 16>{});
    for (auto& h : hashSet)
        h.fill(0xEE);
    pf.hashsetReceived();

    QVERIFY(pf.isCorruptedPart(0));
    QCOMPARE(pf.totalGapSizeInPart(0), uint64{PARTSIZE});
}

// A waiting part is checked once when the hashset lands, then left alone like any
// other verified part.
void tst_PartFile::hashsetReceived_checksEachWaitingPartOnce()
{
    const QString tempDir = m_tempDir.path() + QStringLiteral("/late-good-temp");
    QDir().mkpath(tempDir);
    ScopedStatistics stats;

    PartFile pf;
    pf.setFileName(QStringLiteral("late-good.bin"));
    pf.setFileSize(PARTSIZE * 2 + 1000);
    pf.setTmpPath(tempDir);
    QVERIFY(pf.createPartFile(tempDir));

    const auto data = makePattern(PARTSIZE);
    pf.writeToBuffer(PARTSIZE, data.data(), 0, PARTSIZE - 1, nullptr);
    pf.flushBuffer();

    auto& hashSet = pf.fileIdentifier().getRawMD4HashSet();
    hashSet.assign(3, md4Of(data));
    hashSet[2].fill(0xEE);
    pf.hashsetReceived();

    QCOMPARE(pf.totalGapSizeInPart(0), uint64{0});
    QVERIFY(!pf.isCorruptedPart(0));

    clobberPartZeroOnDisk(tempDir);
    pf.writeToBuffer(PARTSIZE, data.data(), PARTSIZE, 2 * PARTSIZE - 1, nullptr);
    pf.flushBuffer();
    pf.hashsetReceived();   // a second hashset answer re-checks nothing

    QCOMPARE(pf.totalGapSizeInPart(1), uint64{0});
    QVERIFY(pf.totalGapSizeInPart(0) == 0 && !pf.isCorruptedPart(0));
}

QTEST_GUILESS_MAIN(tst_PartFile)
#include "tst_PartFile.moc"
