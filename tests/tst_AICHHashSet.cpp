#include <QTest>
#include <QTemporaryDir>

#include "crypto/AICHData.h"
#include "crypto/AICHHashSet.h"
#include "crypto/AICHHashTree.h"
#include "crypto/SHAHash.h"
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

private:
    /// Build a fully hashed tree with deterministic block data.
    void buildHashedTree(AICHRecoveryHashSet& hashSet, uint64 fileSize);

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

QTEST_MAIN(tst_AICHHashSet)
#include "tst_AICHHashSet.moc"
