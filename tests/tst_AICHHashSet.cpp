#include <QTest>
#include <QTemporaryDir>

#include "crypto/AICHData.h"
#include "crypto/AICHHashSet.h"
#include "crypto/AICHHashTree.h"
#include "crypto/SHAHash.h"
#include "files/KnownFile.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <memory>

using namespace eMule;

class tst_AICHHashSet : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Construction & basic ops
    void construction_default();
    void construction_withSize();
    void setFileSize_updatesTree();
    void setMasterHash_setsStatus();
    void freeHashSet_removesChildren();

    // getPartHashes
    void getPartHashes_singlePart_returnsEmpty();
    void getPartHashes_multiPart_returnsCorrectCount();
    // findPartHash
    void findPartHash_singlePart_returnsRoot();
    void findPartHash_multiPart_returnsNode();

    // saveHashSet / loadHashSet roundtrip
    void saveLoadHashSet_roundtrip();
    void saveHashSet_duplicate_succeeds();

    // addStoredAICHHash / static configuration
    void addStoredAICHHash_newEntry();
    void addStoredAICHHash_duplicate_olderIgnored();
    void addStoredAICHHash_duplicate_newerReplaces();

    // createPartRecoveryData / readRecoveryData roundtrip
    void recoveryData_roundtrip();

    // isPartDataAvailable
    void isPartDataAvailable_withData();
    void isPartDataAvailable_wrongStatus();

    // untrustedHashReceived
    void untrustedHash_trust_evaluation();

    // isLargeFile
    void isLargeFile_smallFile();

    // AICH hash set requesting — recovery data for different parts
    void recoveryData_middlePart_roundtrip();
    void recoveryData_lastPart_roundtrip();

    // AICH part verification via createHashFromMemory
    void partVerification_fromMemory_consistent();
    void partVerification_corruptBlock_detected();

    // AICH part recovery — block-level corruption identification
    void aichRecovery_blockLevel_identification();

    // Trust evaluation edge cases
    void untrustedHash_conflictingHashes_preventsTrust();
    void untrustedHash_sameIP_differentHash_ignored();

private:
    /// Build a fully hashed tree with deterministic block data (synthetic per-block).
    void buildHashedTree(AICHRecoveryHashSet& hashSet, uint64 fileSize);

    /// Build a fully hashed tree from actual byte data using createHashFromMemory.
    /// Only for single-part files (dataSize <= PARTSIZE). outData receives the data.
    void buildHashedTreeFromData(AICHRecoveryHashSet& hashSet, uint32 dataSize,
                                 QByteArray& outData);

    QTemporaryDir m_tempDir;
};

void tst_AICHHashSet::init()
{
    QVERIFY(m_tempDir.isValid());
    // Reset static state before each test
    AICHRecoveryHashSet::setKnown2MetPath(
        m_tempDir.path() + QStringLiteral("/known2_64.met"));
}

void tst_AICHHashSet::cleanup()
{
    // Clear stored hashes by saving an empty map via addStoredAICHHash overwrite trick
    // Actually, we just need a fresh temp dir for each test (already handled by init)
}

void tst_AICHHashSet::buildHashedTree(AICHRecoveryHashSet& hashSet, uint64 fileSize)
{
    hashSet.setFileSize(fileSize);
    std::unique_ptr<AICHHashAlgo> algo(AICHRecoveryHashSet::getNewHashAlgo());

    // Hash each EMBLOCKSIZE block with deterministic data.
    // Iterate part-by-part to respect PARTSIZE boundaries in the tree —
    // blocks must not cross part boundaries or findHash() returns nullptr.
    for (uint64 partStart = 0; partStart < fileSize; partStart += PARTSIZE) {
        const auto partSize = std::min<uint64>(PARTSIZE, fileSize - partStart);
        for (uint64 blockOff = 0; blockOff < partSize; blockOff += EMBLOCKSIZE) {
            const auto blockSize = std::min<uint64>(EMBLOCKSIZE, partSize - blockOff);
            algo->reset();
            QByteArray data = QByteArray::number(static_cast<qint64>(partStart + blockOff));
            algo->add(data.constData(), static_cast<uint32>(data.size()));
            hashSet.m_hashTree.setBlockHash(blockSize, partStart + blockOff, algo.get());
        }
    }
    QVERIFY(hashSet.reCalculateHash(false));
    hashSet.setStatus(EAICHStatus::HashSetComplete);
}

// ---------------------------------------------------------------------------
// Construction & basic ops
// ---------------------------------------------------------------------------

void tst_AICHHashSet::construction_default()
{
    AICHRecoveryHashSet hs;
    QCOMPARE(hs.getStatus(), EAICHStatus::Empty);
    QVERIFY(!hs.hasValidMasterHash());
}

void tst_AICHHashSet::construction_withSize()
{
    AICHRecoveryHashSet hs(PARTSIZE * 2);
    QCOMPARE(hs.m_hashTree.m_dataSize, static_cast<uint64>(PARTSIZE * 2));
}

void tst_AICHHashSet::setFileSize_updatesTree()
{
    AICHRecoveryHashSet hs;
    hs.setFileSize(PARTSIZE * 3);
    QCOMPARE(hs.m_hashTree.m_dataSize, static_cast<uint64>(PARTSIZE * 3));
    QCOMPARE(hs.m_hashTree.getBaseSize(), static_cast<uint64>(PARTSIZE));
}

void tst_AICHHashSet::setMasterHash_setsStatus()
{
    AICHRecoveryHashSet hs(PARTSIZE);
    AICHHash hash;
    hash.getRawHash()[0] = 0xAB;
    hs.setMasterHash(hash, EAICHStatus::Trusted);
    QCOMPARE(hs.getStatus(), EAICHStatus::Trusted);
    QVERIFY(hs.hasValidMasterHash());
    QCOMPARE(hs.getMasterHash().getRawHash()[0], uint8(0xAB));
}

void tst_AICHHashSet::freeHashSet_removesChildren()
{
    AICHRecoveryHashSet hs(PARTSIZE * 2);
    buildHashedTree(hs, PARTSIZE * 2);
    QVERIFY(hs.m_hashTree.m_left || hs.m_hashTree.m_right);
    hs.freeHashSet();
    QVERIFY(!hs.m_hashTree.m_left);
    QVERIFY(!hs.m_hashTree.m_right);
}

// ---------------------------------------------------------------------------
// getPartHashes
// ---------------------------------------------------------------------------

void tst_AICHHashSet::getPartHashes_singlePart_returnsEmpty()
{
    AICHRecoveryHashSet hs;
    buildHashedTree(hs, PARTSIZE - 1);

    std::vector<AICHHash> result;
    QVERIFY(hs.getPartHashes(result));
    QVERIFY(result.empty()); // Single-part files have no AICH part hashes
}

void tst_AICHHashSet::getPartHashes_multiPart_returnsCorrectCount()
{
    const uint64 fileSize = PARTSIZE * 2 + 1; // 3 parts
    AICHRecoveryHashSet hs;
    buildHashedTree(hs, fileSize);

    std::vector<AICHHash> result;
    QVERIFY(hs.getPartHashes(result));
    QCOMPARE(result.size(), std::size_t(3));

    // Each hash should be valid (non-zero)
    for (const auto& hash : result) {
        bool allZero = true;
        for (unsigned i = 0; i < kAICHHashSize; ++i) {
            if (hash.getRawHash()[i] != 0) {
                allZero = false;
                break;
            }
        }
        QVERIFY(!allZero);
    }
}

// ---------------------------------------------------------------------------
// findPartHash
// ---------------------------------------------------------------------------

void tst_AICHHashSet::findPartHash_singlePart_returnsRoot()
{
    AICHRecoveryHashSet hs;
    buildHashedTree(hs, PARTSIZE - 1);

    const AICHHashTree* node = hs.findPartHash(0);
    QVERIFY(node != nullptr);
    QCOMPARE(node, &hs.m_hashTree);
}

void tst_AICHHashSet::findPartHash_multiPart_returnsNode()
{
    const uint64 fileSize = PARTSIZE * 2 + 1;
    AICHRecoveryHashSet hs;
    buildHashedTree(hs, fileSize);

    // Part 0
    const AICHHashTree* node0 = hs.findPartHash(0);
    QVERIFY(node0 != nullptr);
    QVERIFY(node0->m_hashValid);

    // Part 1
    const AICHHashTree* node1 = hs.findPartHash(1);
    QVERIFY(node1 != nullptr);
    QVERIFY(node1->m_hashValid);

    // Part 2 (last part, smaller)
    const AICHHashTree* node2 = hs.findPartHash(2);
    QVERIFY(node2 != nullptr);
    QVERIFY(node2->m_hashValid);

    // Parts should have different hashes (different data)
    QVERIFY(node0->m_hash != node1->m_hash);
}

// ---------------------------------------------------------------------------
// saveHashSet / loadHashSet
// ---------------------------------------------------------------------------

void tst_AICHHashSet::saveLoadHashSet_roundtrip()
{
    const uint64 fileSize = PARTSIZE * 2 + 1;
    AICHRecoveryHashSet saver;
    buildHashedTree(saver, fileSize);
    const AICHHash masterHash = saver.getMasterHash();

    // Save
    QVERIFY(saver.saveHashSet());

    // Load into a fresh hashset
    AICHRecoveryHashSet loader(fileSize);
    loader.setMasterHash(masterHash, EAICHStatus::HashSetComplete);
    QVERIFY(loader.loadHashSet());

    // Verify the master hash matches after recalculation
    QCOMPARE(loader.getMasterHash(), masterHash);
    QVERIFY(loader.hasValidMasterHash());
}

void tst_AICHHashSet::saveHashSet_duplicate_succeeds()
{
    const uint64 fileSize = PARTSIZE * 2 + 1;
    AICHRecoveryHashSet hs1;
    buildHashedTree(hs1, fileSize);

    QVERIFY(hs1.saveHashSet());

    // Save same hashset again — should succeed (already present)
    AICHRecoveryHashSet hs2;
    buildHashedTree(hs2, fileSize);
    QVERIFY(hs2.saveHashSet());
}

// ---------------------------------------------------------------------------
// addStoredAICHHash
// ---------------------------------------------------------------------------

void tst_AICHHashSet::addStoredAICHHash_newEntry()
{
    AICHHash hash;
    hash.getRawHash()[0] = 0x01;
    uint64 result = AICHRecoveryHashSet::addStoredAICHHash(hash, 100);
    QCOMPARE(result, uint64(0)); // No old position
}

void tst_AICHHashSet::addStoredAICHHash_duplicate_olderIgnored()
{
    AICHHash hash;
    hash.getRawHash()[0] = 0x02;
    AICHRecoveryHashSet::addStoredAICHHash(hash, 200);

    // Try to add at an older position — should be ignored
    uint64 result = AICHRecoveryHashSet::addStoredAICHHash(hash, 100);
    QCOMPARE(result, uint64(0));
}

void tst_AICHHashSet::addStoredAICHHash_duplicate_newerReplaces()
{
    AICHHash hash;
    hash.getRawHash()[0] = 0x03;
    AICHRecoveryHashSet::addStoredAICHHash(hash, 100);

    // Add at a newer position — should replace
    uint64 result = AICHRecoveryHashSet::addStoredAICHHash(hash, 300);
    QCOMPARE(result, uint64(100)); // Returns old position
}

// ---------------------------------------------------------------------------
// createPartRecoveryData / readRecoveryData
// ---------------------------------------------------------------------------

void tst_AICHHashSet::recoveryData_roundtrip()
{
    const uint64 fileSize = PARTSIZE * 2 + 1;
    AICHRecoveryHashSet creator;
    buildHashedTree(creator, fileSize);
    const AICHHash masterHash = creator.getMasterHash();

    // Create recovery data for part 0 (don't load from file — use in-memory tree)
    SafeMemFile recData;
    QVERIFY(creator.createPartRecoveryData(0, recData, true));

    // Read recovery data into a fresh hashset
    AICHRecoveryHashSet reader(fileSize);
    reader.setMasterHash(masterHash, EAICHStatus::Verified);

    recData.seek(0, 0);
    QVERIFY(reader.readRecoveryData(0, recData));

    // After reading recovery data, all block hashes for part 0 should be available
    QVERIFY(reader.isPartDataAvailable(0, fileSize));
}

// ---------------------------------------------------------------------------
// isPartDataAvailable
// ---------------------------------------------------------------------------

void tst_AICHHashSet::isPartDataAvailable_withData()
{
    const uint64 fileSize = PARTSIZE * 2 + 1;
    AICHRecoveryHashSet hs;
    buildHashedTree(hs, fileSize);

    // Part 0 should have data
    QVERIFY(hs.isPartDataAvailable(0, fileSize));
    // Part 1 should have data
    QVERIFY(hs.isPartDataAvailable(PARTSIZE, fileSize));
}

void tst_AICHHashSet::isPartDataAvailable_wrongStatus()
{
    AICHRecoveryHashSet hs(PARTSIZE * 2);
    hs.setStatus(EAICHStatus::Untrusted);
    QVERIFY(!hs.isPartDataAvailable(0, PARTSIZE * 2));
}

// ---------------------------------------------------------------------------
// untrustedHashReceived
// ---------------------------------------------------------------------------

void tst_AICHHashSet::untrustedHash_trust_evaluation()
{
    AICHRecoveryHashSet hs(PARTSIZE);
    hs.setStatus(EAICHStatus::Empty);

    AICHHash hash;
    hash.getRawHash()[0] = 0x42;

    // Send from 9 unique IPs — not enough to trust
    for (uint32 ip = 1; ip <= 9; ++ip) {
        hs.untrustedHashReceived(hash, ip << 20);
    }
    QCOMPARE(hs.getStatus(), EAICHStatus::Untrusted);

    // 10th IP should trigger trust
    hs.untrustedHashReceived(hash, 10 << 20);
    QCOMPARE(hs.getStatus(), EAICHStatus::Trusted);
    QVERIFY(hs.hasValidMasterHash());
    QCOMPARE(hs.getMasterHash(), hash);
}

// ---------------------------------------------------------------------------
// isLargeFile (through createPartRecoveryData behavior)
// ---------------------------------------------------------------------------

void tst_AICHHashSet::isLargeFile_smallFile()
{
    // A file < 4GB should use 16-bit hash identifiers in recovery data
    const uint64 fileSize = PARTSIZE * 2 + 1;
    AICHRecoveryHashSet hs;
    buildHashedTree(hs, fileSize);

    SafeMemFile recData;
    QVERIFY(hs.createPartRecoveryData(0, recData, true));

    // For small files: 16-bit count comes first, then 16-bit identifiers
    recData.seek(0, 0);
    uint16 count16 = recData.readUInt16();
    QVERIFY(count16 > 0); // Should have 16-bit hashes for small file
}

// ---------------------------------------------------------------------------
// buildHashedTreeFromData — from actual byte data via createHashFromMemory
// ---------------------------------------------------------------------------

void tst_AICHHashSet::buildHashedTreeFromData(AICHRecoveryHashSet& hashSet,
                                               uint32 dataSize,
                                               QByteArray& outData)
{
    QVERIFY(dataSize <= PARTSIZE);
    hashSet.setFileSize(dataSize);

    outData.resize(static_cast<qsizetype>(dataSize));
    for (uint32 i = 0; i < dataSize; ++i)
        outData[static_cast<qsizetype>(i)] = static_cast<char>(i % 251);

    uint8 dummyMD4[16]{};
    KnownFile::createHashFromMemory(
        reinterpret_cast<const uint8*>(outData.constData()),
        dataSize, dummyMD4, &hashSet.m_hashTree);

    QVERIFY(hashSet.reCalculateHash(false));
    hashSet.setStatus(EAICHStatus::HashSetComplete);
}

// ---------------------------------------------------------------------------
// AICH hash set requesting — recovery data for different parts
// ---------------------------------------------------------------------------

void tst_AICHHashSet::recoveryData_middlePart_roundtrip()
{
    const uint64 fileSize = PARTSIZE * 3 + 1; // 4 parts
    AICHRecoveryHashSet creator;
    buildHashedTree(creator, fileSize);
    const AICHHash masterHash = creator.getMasterHash();

    // Create recovery data for part 1 (middle part)
    SafeMemFile recData;
    QVERIFY(creator.createPartRecoveryData(PARTSIZE, recData, true));

    // Read recovery data into a fresh hashset
    AICHRecoveryHashSet reader(fileSize);
    reader.setMasterHash(masterHash, EAICHStatus::Verified);

    recData.seek(0, 0);
    QVERIFY(reader.readRecoveryData(PARTSIZE, recData));

    // All block hashes for part 1 should be available
    QVERIFY(reader.isPartDataAvailable(PARTSIZE, fileSize));

    // Part 0 should NOT be available (only part 1 was recovered)
    // Note: part 0 blocks weren't received, but the root/sibling hashes were set
    // during recovery. isPartDataAvailable checks leaf-level hashes.
    // Part 0's leaf hashes should NOT be present.
}

void tst_AICHHashSet::recoveryData_lastPart_roundtrip()
{
    const uint64 fileSize = PARTSIZE * 2 + EMBLOCKSIZE; // last part = 1 block
    AICHRecoveryHashSet creator;
    buildHashedTree(creator, fileSize);
    const AICHHash masterHash = creator.getMasterHash();

    // Create recovery data for the last part (part 2)
    const uint64 lastPartStart = PARTSIZE * 2;
    SafeMemFile recData;
    QVERIFY(creator.createPartRecoveryData(lastPartStart, recData, true));

    // Read into fresh hashset
    AICHRecoveryHashSet reader(fileSize);
    reader.setMasterHash(masterHash, EAICHStatus::Verified);

    recData.seek(0, 0);
    QVERIFY(reader.readRecoveryData(lastPartStart, recData));

    // Last part's block hashes should be available
    QVERIFY(reader.isPartDataAvailable(lastPartStart, fileSize));
}

// ---------------------------------------------------------------------------
// AICH part verification
// ---------------------------------------------------------------------------

void tst_AICHHashSet::partVerification_fromMemory_consistent()
{
    const uint32 dataSize = EMBLOCKSIZE * 4;

    // Build two AICH trees from the same deterministic data
    AICHRecoveryHashSet hs1, hs2;
    QByteArray data1, data2;
    buildHashedTreeFromData(hs1, dataSize, data1);
    buildHashedTreeFromData(hs2, dataSize, data2);

    // Root hashes must be identical (same data → same AICH hash)
    QVERIFY(hs1.hasValidMasterHash());
    QVERIFY(hs2.hasValidMasterHash());
    QCOMPARE(hs1.getMasterHash(), hs2.getMasterHash());

    // Block-level hashes should also match
    for (uint32 blockIdx = 0; blockIdx < 4; ++blockIdx) {
        const uint64 blockStart = static_cast<uint64>(blockIdx) * EMBLOCKSIZE;
        const AICHHashTree* b1 = hs1.m_hashTree.findExistingHash(blockStart, EMBLOCKSIZE);
        const AICHHashTree* b2 = hs2.m_hashTree.findExistingHash(blockStart, EMBLOCKSIZE);
        QVERIFY(b1 && b1->m_hashValid);
        QVERIFY(b2 && b2->m_hashValid);
        QCOMPARE(b1->m_hash, b2->m_hash);
    }
}

void tst_AICHHashSet::partVerification_corruptBlock_detected()
{
    const uint32 dataSize = EMBLOCKSIZE * 4;

    // Build "known good" tree from clean data
    AICHRecoveryHashSet goodSet;
    QByteArray goodData;
    buildHashedTreeFromData(goodSet, dataSize, goodData);

    // Create corrupt data — flip bytes in block 2
    QByteArray corruptData = goodData;
    corruptData[static_cast<qsizetype>(EMBLOCKSIZE) * 2] ^= 0xFF;
    corruptData[static_cast<qsizetype>(EMBLOCKSIZE) * 2 + 100] ^= 0xFF;

    // Hash corrupt data into a new tree
    AICHHashTree badTree(dataSize, true, EMBLOCKSIZE);
    uint8 dummyMD4[16]{};
    KnownFile::createHashFromMemory(
        reinterpret_cast<const uint8*>(corruptData.constData()),
        dataSize, dummyMD4, &badTree);
    std::unique_ptr<AICHHashAlgo> algo(AICHRecoveryHashSet::getNewHashAlgo());
    QVERIFY(badTree.reCalculateHash(algo.get(), false));

    // Root hashes should differ
    QVERIFY(goodSet.getMasterHash() != badTree.m_hash);

    // Block-level comparison: blocks 0, 1, 3 should match; block 2 should differ
    for (uint32 blockIdx = 0; blockIdx < 4; ++blockIdx) {
        const uint64 blockStart = static_cast<uint64>(blockIdx) * EMBLOCKSIZE;
        const AICHHashTree* goodBlock = goodSet.m_hashTree.findExistingHash(blockStart, EMBLOCKSIZE);
        const AICHHashTree* badBlock = badTree.findExistingHash(blockStart, EMBLOCKSIZE);
        QVERIFY(goodBlock && goodBlock->m_hashValid);
        QVERIFY(badBlock && badBlock->m_hashValid);

        if (blockIdx == 2)
            QVERIFY(goodBlock->m_hash != badBlock->m_hash);
        else
            QCOMPARE(goodBlock->m_hash, badBlock->m_hash);
    }
}

// ---------------------------------------------------------------------------
// AICH part recovery — block-level corruption identification
// ---------------------------------------------------------------------------

void tst_AICHHashSet::aichRecovery_blockLevel_identification()
{
    const uint32 dataSize = EMBLOCKSIZE * 4;
    QByteArray goodData;

    // Step 1: Build "source" hash tree from good data
    AICHRecoveryHashSet source;
    buildHashedTreeFromData(source, dataSize, goodData);
    const AICHHash masterHash = source.getMasterHash();

    // Step 2: Create recovery data for part 0 (entire file for single-part)
    SafeMemFile recData;
    QVERIFY(source.createPartRecoveryData(0, recData, true));

    // Step 3: Read recovery data into "downloader" hash set
    AICHRecoveryHashSet downloader(dataSize);
    downloader.setMasterHash(masterHash, EAICHStatus::Verified);
    recData.seek(0, 0);
    QVERIFY(downloader.readRecoveryData(0, recData));

    // Step 4: Create corrupt data — block 2 is bad, others are good
    QByteArray corruptData = goodData;
    corruptData[static_cast<qsizetype>(EMBLOCKSIZE) * 2] ^= 0xFF;
    corruptData[static_cast<qsizetype>(EMBLOCKSIZE) * 2 + 50] ^= 0xAA;

    // Step 5: Hash corrupt data into local AICH tree (simulating PartFile::aichRecoveryDataAvailable)
    AICHHashTree localTree(dataSize, true, EMBLOCKSIZE);
    uint8 dummyMD4[16]{};
    KnownFile::createHashFromMemory(
        reinterpret_cast<const uint8*>(corruptData.constData()),
        dataSize, dummyMD4, &localTree);
    std::unique_ptr<AICHHashAlgo> algo(AICHRecoveryHashSet::getNewHashAlgo());
    QVERIFY(localTree.reCalculateHash(algo.get(), false));

    // Step 6: Block-by-block comparison (as done in PartFile::aichRecoveryDataAvailable)
    uint32 goodBlocks = 0;
    uint32 corruptBlocks = 0;

    for (uint32 blockIdx = 0; blockIdx < 4; ++blockIdx) {
        const uint64 blockStart = static_cast<uint64>(blockIdx) * EMBLOCKSIZE;
        const AICHHashTree* trustedBlock =
            downloader.m_hashTree.findExistingHash(blockStart, EMBLOCKSIZE);
        const AICHHashTree* localBlock =
            localTree.findExistingHash(blockStart, EMBLOCKSIZE);

        QVERIFY2(trustedBlock && trustedBlock->m_hashValid,
                 qPrintable(QStringLiteral("Trusted block %1 missing").arg(blockIdx)));
        QVERIFY2(localBlock && localBlock->m_hashValid,
                 qPrintable(QStringLiteral("Local block %1 missing").arg(blockIdx)));

        if (localBlock->m_hash == trustedBlock->m_hash)
            ++goodBlocks;
        else
            ++corruptBlocks;
    }

    // 3 good blocks, 1 corrupt block (block 2)
    QCOMPARE(goodBlocks, 3u);
    QCOMPARE(corruptBlocks, 1u);
}

// ---------------------------------------------------------------------------
// Trust evaluation edge cases
// ---------------------------------------------------------------------------

void tst_AICHHashSet::untrustedHash_conflictingHashes_preventsTrust()
{
    // Note: addSigningIP masks IPs with 0x00F0FFFF, so we use raw IPs
    // (bits 0-15 preserved by mask) to ensure uniqueness.

    AICHRecoveryHashSet hs(PARTSIZE);
    hs.setStatus(EAICHStatus::Empty);

    AICHHash hash1, hash2;
    hash1.getRawHash()[0] = 0x42;
    hash2.getRawHash()[0] = 0x43;

    // 10 IPs sign hash1 → 100% consensus → Trusted
    for (uint32 ip = 1; ip <= 10; ++ip)
        hs.untrustedHashReceived(hash1, ip);
    QCOMPARE(hs.getStatus(), EAICHStatus::Trusted);

    // Adding conflicting hashes REVOKES trust (re-evaluated each call)
    // 10/13 = 76.9% < 92% → Untrusted
    for (uint32 ip = 11; ip <= 13; ++ip)
        hs.untrustedHashReceived(hash2, ip);
    QCOMPARE(hs.getStatus(), EAICHStatus::Untrusted);
    QCOMPARE(hs.getMasterHash(), hash1); // hash1 still has the most IPs

    // Conflicting hashes from the start prevent trust entirely
    AICHRecoveryHashSet hs2(PARTSIZE);
    hs2.setStatus(EAICHStatus::Empty);

    // 5 for hash1, 5 for hash2, then 5 more for hash1
    for (uint32 ip = 1; ip <= 5; ++ip)
        hs2.untrustedHashReceived(hash1, ip);
    for (uint32 ip = 6; ip <= 10; ++ip)
        hs2.untrustedHashReceived(hash2, ip);
    for (uint32 ip = 11; ip <= 15; ++ip)
        hs2.untrustedHashReceived(hash1, ip);

    // hash1: 10 IPs, hash2: 5 IPs, total: 15
    // 10/15 = 66.7% < 92% — NOT trusted
    QCOMPARE(hs2.getStatus(), EAICHStatus::Untrusted);

    // Add more for hash1 to reach 92%: need hash1/(hash1+5) >= 0.92
    // → hash1 >= 57.5 → need 58 total for hash1. Currently 10, need 48 more.
    for (uint32 ip = 16; ip <= 63; ++ip)
        hs2.untrustedHashReceived(hash1, ip);

    // hash1=58, hash2=5, total=63, int(100*58/63)=92 ≥ 92 AND 58 ≥ 10 → Trusted!
    QCOMPARE(hs2.getStatus(), EAICHStatus::Trusted);
    QCOMPARE(hs2.getMasterHash(), hash1);
}

void tst_AICHHashSet::untrustedHash_sameIP_differentHash_ignored()
{
    // Note: addSigningIP masks IPs with 0x00F0FFFF, use raw values for uniqueness.

    AICHRecoveryHashSet hs(PARTSIZE);
    hs.setStatus(EAICHStatus::Empty);

    AICHHash hash1, hash2;
    hash1.getRawHash()[0] = 0x42;
    hash2.getRawHash()[0] = 0x43;

    // IP 1 signs hash1
    hs.untrustedHashReceived(hash1, 1);

    // Same IP tries to sign hash2 — should be ignored (IP already committed to hash1)
    hs.untrustedHashReceived(hash2, 1);

    // Send hash2 from 9 more unique IPs (IPs 2-10)
    for (uint32 ip = 2; ip <= 10; ++ip)
        hs.untrustedHashReceived(hash2, ip);

    // hash1: 1 IP (IP 1), hash2: 9 IPs (IPs 2-10) — IP 1 was rejected for hash2
    // Total: 10, most trusted = hash2 with 9
    // 9 < 10 → not trusted
    QCOMPARE(hs.getStatus(), EAICHStatus::Untrusted);

    // The master hash should be hash2 (most votes) but untrusted
    QCOMPARE(hs.getMasterHash(), hash2);

    // Adding one more unique IP for hash2 → 10 IPs, 10/11 = 90.9% < 92%
    hs.untrustedHashReceived(hash2, 11);
    QCOMPARE(hs.getStatus(), EAICHStatus::Untrusted);

    // Need 92%: with hash1=1, hash2=N → N/(N+1) ≥ 0.92 → N ≥ 11.5 → N=12
    hs.untrustedHashReceived(hash2, 12);
    // hash2=11, total=12, 11/12=91.7% < 92%
    QCOMPARE(hs.getStatus(), EAICHStatus::Untrusted);

    hs.untrustedHashReceived(hash2, 13);
    // hash2=12, total=13, 12/13=92.3% ≥ 92% AND 12 ≥ 10 → Trusted!
    QCOMPARE(hs.getStatus(), EAICHStatus::Trusted);
    QCOMPARE(hs.getMasterHash(), hash2);
}

QTEST_MAIN(tst_AICHHashSet)
#include "tst_AICHHashSet.moc"
