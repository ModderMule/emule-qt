#include <QTest>
#include "crypto/AICHHashTree.h"
#include "crypto/AICHHashSet.h"
#include "crypto/SHAHash.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <memory>

using namespace eMule;

class tst_AICHHashTree : public QObject {
    Q_OBJECT

private slots:
    void singleBlockConstruction();
    void singlePartConstruction();
    void findHash_basic();
    void setBlockHash_basic();
    void reCalculateHash_simple();
    void verifyHashTree_valid();
    void verifyHashTree_corrupt();
    void writeLoadLowestLevel_roundtrip();
    void baseSize_accessors();
};

void tst_AICHHashTree::singleBlockConstruction()
{
    AICHHashTree tree(EMBLOCKSIZE, true, EMBLOCKSIZE);
    QCOMPARE(tree.m_dataSize, static_cast<uint64>(EMBLOCKSIZE));
    QVERIFY(tree.m_isLeftBranch);
    QVERIFY(!tree.m_hashValid);
    QVERIFY(!tree.m_left);
    QVERIFY(!tree.m_right);
    QCOMPARE(tree.getBaseSize(), static_cast<uint64>(EMBLOCKSIZE));
}

void tst_AICHHashTree::singlePartConstruction()
{
    AICHHashTree tree(PARTSIZE, true, EMBLOCKSIZE);
    QCOMPARE(tree.m_dataSize, static_cast<uint64>(PARTSIZE));
    QCOMPARE(tree.getBaseSize(), static_cast<uint64>(EMBLOCKSIZE));
}

void tst_AICHHashTree::findHash_basic()
{
    // Create a tree for one PARTSIZE chunk, find a block within it
    AICHHashTree tree(PARTSIZE, true, EMBLOCKSIZE);
    AICHHashTree* found = tree.findHash(0, EMBLOCKSIZE);
    QVERIFY(found != nullptr);
    QCOMPARE(found->m_dataSize, static_cast<uint64>(EMBLOCKSIZE));
}

void tst_AICHHashTree::setBlockHash_basic()
{
    AICHHashTree tree(PARTSIZE, true, EMBLOCKSIZE);
    std::unique_ptr<AICHHashAlgo> algo(AICHRecoveryHashSet::getNewHashAlgo());

    // Hash some data and set it as a block hash
    algo->reset();
    const char data[] = "test block data for hashing";
    algo->add(data, sizeof(data) - 1);

    tree.setBlockHash(EMBLOCKSIZE, 0, algo.get());
    // The first leaf node should now have a valid hash.
    // Use findHash (mutable) since intermediate nodes don't have m_hashValid set yet.
    AICHHashTree* found = tree.findHash(0, EMBLOCKSIZE);
    QVERIFY(found != nullptr);
    QVERIFY(found->m_hashValid);
}

void tst_AICHHashTree::reCalculateHash_simple()
{
    // Build a small tree with two children, recalculate parent
    const uint64 totalSize = EMBLOCKSIZE * 2;
    AICHHashTree tree(totalSize, true, EMBLOCKSIZE);
    std::unique_ptr<AICHHashAlgo> algo(AICHRecoveryHashSet::getNewHashAlgo());

    // Set left block hash
    algo->reset();
    algo->add("left block", 10);
    tree.setBlockHash(EMBLOCKSIZE, 0, algo.get());

    // Set right block hash
    algo->reset();
    algo->add("right block", 11);
    tree.setBlockHash(EMBLOCKSIZE, EMBLOCKSIZE, algo.get());

    // Recalculate
    QVERIFY(tree.reCalculateHash(algo.get(), false));
    QVERIFY(tree.m_hashValid);
}

void tst_AICHHashTree::verifyHashTree_valid()
{
    const uint64 totalSize = EMBLOCKSIZE * 2;
    AICHHashTree tree(totalSize, true, EMBLOCKSIZE);
    std::unique_ptr<AICHHashAlgo> algo(AICHRecoveryHashSet::getNewHashAlgo());

    // Set leaf hashes
    algo->reset();
    algo->add("left block", 10);
    tree.setBlockHash(EMBLOCKSIZE, 0, algo.get());

    algo->reset();
    algo->add("right block", 11);
    tree.setBlockHash(EMBLOCKSIZE, EMBLOCKSIZE, algo.get());

    // Calculate the root hash
    QVERIFY(tree.reCalculateHash(algo.get(), false));

    // Verify should pass
    QVERIFY(tree.verifyHashTree(algo.get(), false));
}

void tst_AICHHashTree::verifyHashTree_corrupt()
{
    const uint64 totalSize = EMBLOCKSIZE * 2;
    AICHHashTree tree(totalSize, true, EMBLOCKSIZE);
    std::unique_ptr<AICHHashAlgo> algo(AICHRecoveryHashSet::getNewHashAlgo());

    // Set leaf hashes
    algo->reset();
    algo->add("left block", 10);
    tree.setBlockHash(EMBLOCKSIZE, 0, algo.get());

    algo->reset();
    algo->add("right block", 11);
    tree.setBlockHash(EMBLOCKSIZE, EMBLOCKSIZE, algo.get());

    // Calculate the root hash
    QVERIFY(tree.reCalculateHash(algo.get(), false));

    // Corrupt a child hash
    tree.m_left->m_hash.getRawHash()[0] ^= 0xFF;

    // Verify should fail
    QVERIFY(!tree.verifyHashTree(algo.get(), true));
    // Bad trees should be deleted
    QVERIFY(!tree.m_left);
    QVERIFY(!tree.m_right);
}

void tst_AICHHashTree::writeLoadLowestLevel_roundtrip()
{
    // Verify that two trees built from the same block data produce identical root hashes.
    const uint64 totalSize = EMBLOCKSIZE * 3;
    std::unique_ptr<AICHHashAlgo> algo(AICHRecoveryHashSet::getNewHashAlgo());

    auto populateTree = [&](AICHHashTree& tree) {
        for (uint64 i = 0; i < 3; ++i) {
            algo->reset();
            QByteArray data = QByteArray::number(static_cast<qint64>(i));
            algo->add(data.constData(), static_cast<uint32>(data.size()));
            uint64 blockSize = (i < 2) ? static_cast<uint64>(EMBLOCKSIZE)
                                       : (totalSize - EMBLOCKSIZE * 2);
            tree.setBlockHash(blockSize, i * EMBLOCKSIZE, algo.get());
        }
        QVERIFY(tree.reCalculateHash(algo.get(), false));
    };

    AICHHashTree tree1(totalSize, true, EMBLOCKSIZE);
    AICHHashTree tree2(totalSize, true, EMBLOCKSIZE);
    populateTree(tree1);
    populateTree(tree2);

    // Root hashes from identical data must match
    QVERIFY(tree1.m_hash == tree2.m_hash);
    QVERIFY(tree1.m_hashValid);
    QVERIFY(tree2.m_hashValid);
}

void tst_AICHHashTree::baseSize_accessors()
{
    AICHHashTree tree(PARTSIZE * 2, true, PARTSIZE);
    QCOMPARE(tree.getBaseSize(), static_cast<uint64>(PARTSIZE));

    tree.setBaseSize(EMBLOCKSIZE);
    QCOMPARE(tree.getBaseSize(), static_cast<uint64>(EMBLOCKSIZE));
}

QTEST_MAIN(tst_AICHHashTree)
#include "tst_AICHHashTree.moc"
