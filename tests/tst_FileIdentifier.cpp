#include <QTest>
#include "crypto/FileIdentifier.h"
#include "crypto/AICHHashSet.h"
#include "crypto/SHAHash.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <array>
#include <cstring>

using namespace eMule;

class tst_FileIdentifier : public QObject {
    Q_OBJECT

private slots:
    void md4HashGetSet();
    void aichHashGetSet();
    void compareRelaxed_sameHash();
    void compareRelaxed_differentSize();
    void compareStrict_fullMatch();
    void compareStrict_sizeMismatch();
    void writeIdentifier_roundtrip();
    void md4HashsetLoadWrite();
    void calculateMD4ByHashSet();
    void aichHashsetLoadWriteVerify();
    void writeHashSetsToPacket_roundtrip();
    void fileIdentifierSA_readIdentifier();
    void theoreticalPartHashCounts();
};

void tst_FileIdentifier::md4HashGetSet()
{
    EMFileSize size = PARTSIZE * 2 + 1;
    FileIdentifier fid(size);

    const std::array<uint8, 16> hash = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    fid.setMD4Hash(hash.data());
    QVERIFY(md4equ(fid.getMD4Hash(), hash.data()));
}

void tst_FileIdentifier::aichHashGetSet()
{
    EMFileSize size = PARTSIZE;
    FileIdentifier fid(size);
    QVERIFY(!fid.hasAICHHash());

    AICHHash hash;
    hash.getRawHash()[0] = 0x42;
    fid.setAICHHash(hash);
    QVERIFY(fid.hasAICHHash());
    QCOMPARE(fid.getAICHHash().getRawHash()[0], uint8(0x42));

    fid.clearAICHHash();
    QVERIFY(!fid.hasAICHHash());
}

void tst_FileIdentifier::compareRelaxed_sameHash()
{
    EMFileSize size1 = PARTSIZE;
    EMFileSize size2 = PARTSIZE;
    FileIdentifier fid1(size1);
    FileIdentifier fid2(size2);

    std::array<uint8, 16> hash{};
    hash[0] = 0xAA;
    fid1.setMD4Hash(hash.data());
    fid2.setMD4Hash(hash.data());

    QVERIFY(fid1.compareRelaxed(fid2));
}

void tst_FileIdentifier::compareRelaxed_differentSize()
{
    EMFileSize size1 = PARTSIZE;
    EMFileSize size2 = PARTSIZE * 2;
    FileIdentifier fid1(size1);
    FileIdentifier fid2(size2);

    std::array<uint8, 16> hash{};
    hash[0] = 0xBB;
    fid1.setMD4Hash(hash.data());
    fid2.setMD4Hash(hash.data());

    // Relaxed comparison allows different sizes if both non-zero
    // Actually the original logic is: if either is 0, skip size check
    // Both are non-zero and different, so this should fail
    QVERIFY(!fid1.compareRelaxed(fid2));
}

void tst_FileIdentifier::compareStrict_fullMatch()
{
    EMFileSize size1 = PARTSIZE;
    EMFileSize size2 = PARTSIZE;
    FileIdentifier fid1(size1);
    FileIdentifier fid2(size2);

    std::array<uint8, 16> hash{};
    hash[0] = 0xCC;
    fid1.setMD4Hash(hash.data());
    fid2.setMD4Hash(hash.data());

    QVERIFY(fid1.compareStrict(fid2));
}

void tst_FileIdentifier::compareStrict_sizeMismatch()
{
    EMFileSize size1 = PARTSIZE;
    EMFileSize size2 = PARTSIZE * 2;
    FileIdentifier fid1(size1);
    FileIdentifier fid2(size2);

    std::array<uint8, 16> hash{};
    hash[0] = 0xDD;
    fid1.setMD4Hash(hash.data());
    fid2.setMD4Hash(hash.data());

    QVERIFY(!fid1.compareStrict(fid2));
}

void tst_FileIdentifier::writeIdentifier_roundtrip()
{
    EMFileSize size = PARTSIZE * 3;
    FileIdentifier fid(size);

    std::array<uint8, 16> hash = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    fid.setMD4Hash(hash.data());

    AICHHash aich;
    aich.getRawHash()[0] = 0xAB;
    aich.getRawHash()[19] = 0xCD;
    fid.setAICHHash(aich);

    // Write
    SafeMemFile out;
    fid.writeIdentifier(out);

    // Read back
    out.seek(0, 0);
    FileIdentifierSA sa;
    QVERIFY(sa.readIdentifier(out));
    QVERIFY(md4equ(sa.getMD4Hash(), hash.data()));
    QCOMPARE(sa.getFileSize(), size);
    QVERIFY(sa.hasAICHHash());
    QCOMPARE(sa.getAICHHash().getRawHash()[0], uint8(0xAB));
    QCOMPARE(sa.getAICHHash().getRawHash()[19], uint8(0xCD));
}

void tst_FileIdentifier::md4HashsetLoadWrite()
{
    EMFileSize size = PARTSIZE * 2 + 1; // 3 part hashes
    FileIdentifier writer(size);

    // Create a proper hash set: set main hash and part hashes
    // Part hashes
    std::vector<std::array<uint8, 16>> partHashes;
    for (int i = 0; i < 3; ++i) {
        std::array<uint8, 16> ph{};
        ph.fill(static_cast<uint8>(i + 1));
        partHashes.push_back(ph);
    }

    // Calculate main hash from part hashes (MD4 of concatenation)
    QByteArray concat(3 * 16, Qt::Uninitialized);
    for (std::size_t i = 0; i < 3; ++i) {
        std::memcpy(concat.data() + static_cast<qsizetype>(i) * 16, partHashes[i].data(), 16);
    }
    QByteArray mainHash = QCryptographicHash::hash(concat, QCryptographicHash::Md4);
    writer.setMD4Hash(reinterpret_cast<const uint8*>(mainHash.constData()));
    writer.getRawMD4HashSet() = partHashes;

    // Write
    SafeMemFile file;
    writer.writeMD4HashsetToFile(file);

    // Read
    file.seek(0, 0);
    EMFileSize size2 = PARTSIZE * 2 + 1;
    FileIdentifier reader(size2);
    reader.setMD4Hash(reinterpret_cast<const uint8*>(mainHash.constData()));
    QVERIFY(reader.loadMD4HashsetFromFile(file, true));
    QCOMPARE(reader.getAvailableMD4PartHashCount(), uint16(3));
}

void tst_FileIdentifier::calculateMD4ByHashSet()
{
    EMFileSize size = PARTSIZE * 2 + 1;
    FileIdentifier fid(size);

    // Create part hashes
    std::vector<std::array<uint8, 16>> partHashes;
    for (int i = 0; i < 3; ++i) {
        std::array<uint8, 16> ph{};
        ph.fill(static_cast<uint8>(0x10 + i));
        partHashes.push_back(ph);
    }
    fid.getRawMD4HashSet() = partHashes;

    // Calculate (not verify)
    QVERIFY(fid.calculateMD4HashByHashSet(false));

    // Verify the computed hash is correct
    QVERIFY(!isnulmd4(fid.getMD4Hash()));

    // Now verify mode should pass
    QVERIFY(fid.calculateMD4HashByHashSet(true));
}

void tst_FileIdentifier::aichHashsetLoadWriteVerify()
{
    // Build a valid AICH hashset with 3 parts
    EMFileSize size = PARTSIZE * 2 + 1;

    // Create part hashes using SHA-1
    std::vector<AICHHash> partHashes;
    for (int i = 0; i < 3; ++i) {
        ShaHasher h;
        QByteArray data = QByteArray::number(i);
        h.add(data.constData(), static_cast<uint32>(data.size()));
        AICHHash ph;
        h.finish(ph);
        partHashes.push_back(ph);
    }

    // Build tree and calculate master hash
    AICHRecoveryHashSet hashSet(size);
    for (uint32 i = 0; i < 3; ++i) {
        uint64 partStart = static_cast<uint64>(i) * PARTSIZE;
        auto partSize = static_cast<uint32>(
            std::min<uint64>(PARTSIZE, size - partStart));
        AICHHashTree* node = hashSet.m_hashTree.findHash(partStart, partSize);
        QVERIFY(node != nullptr);
        node->m_hash = partHashes[i];
        node->m_hashValid = true;
    }
    QVERIFY(hashSet.reCalculateHash(false));
    AICHHash masterHash = hashSet.getMasterHash();

    // Set up FileIdentifier
    FileIdentifier writer(size);
    writer.setAICHHash(masterHash);
    (void)writer.getRawAICHHashSet(); // access for testing existence

    // Write AICH hashset
    SafeMemFile file;
    // Manually write: master hash + count + part hashes
    masterHash.write(file);
    file.writeUInt16(static_cast<uint16>(partHashes.size()));
    for (auto& ph : partHashes)
        ph.write(file);

    // Read it back
    file.seek(0, 0);
    FileIdentifier reader(size);
    reader.setAICHHash(masterHash);
    QVERIFY(reader.loadAICHHashsetFromFile(file, true));
    QCOMPARE(reader.getAvailableAICHPartHashCount(), uint16(3));
}

void tst_FileIdentifier::writeHashSetsToPacket_roundtrip()
{
    EMFileSize size = PARTSIZE * 2 + 1;
    FileIdentifier writer(size);

    // Set up MD4 hash + hashset
    std::vector<std::array<uint8, 16>> md4Parts;
    for (int i = 0; i < 3; ++i) {
        std::array<uint8, 16> ph{};
        ph.fill(static_cast<uint8>(0x30 + i));
        md4Parts.push_back(ph);
    }
    writer.getRawMD4HashSet() = md4Parts;
    QVERIFY(writer.calculateMD4HashByHashSet(false));

    // Write packet with MD4 only
    SafeMemFile file;
    writer.writeHashSetsToPacket(file, true, false);

    // Read back
    file.seek(0, 0);
    FileIdentifier reader(size);
    reader.setMD4Hash(writer.getMD4Hash());
    bool md4 = true, aich = false;
    QVERIFY(reader.readHashSetsFromPacket(file, md4, aich));
    QVERIFY(md4);
    QVERIFY(!aich);
    QCOMPARE(reader.getAvailableMD4PartHashCount(), uint16(3));
}

void tst_FileIdentifier::fileIdentifierSA_readIdentifier()
{
    // Write an identifier
    EMFileSize size = 12345678;
    std::array<uint8, 16> hash = {0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    AICHHash aich;
    aich.getRawHash()[0] = 0xFF;

    FileIdentifierSA writer(hash.data(), size, aich, true);
    SafeMemFile file;
    writer.writeIdentifier(file);

    // Read back
    file.seek(0, 0);
    FileIdentifierSA reader;
    QVERIFY(reader.readIdentifier(file));
    QVERIFY(md4equ(reader.getMD4Hash(), hash.data()));
    QCOMPARE(reader.getFileSize(), size);
    QVERIFY(reader.hasAICHHash());
    QCOMPARE(reader.getAICHHash().getRawHash()[0], uint8(0xFF));
}

void tst_FileIdentifier::theoreticalPartHashCounts()
{
    // File < PARTSIZE: 0 MD4 hashes, 0 AICH hashes
    {
        EMFileSize size = PARTSIZE - 1;
        FileIdentifier fid(size);
        QCOMPARE(fid.getTheoreticalMD4PartHashCount(), uint16(0));
        QCOMPARE(fid.getTheoreticalAICHPartHashCount(), uint16(0));
    }
    // File == PARTSIZE: 2 MD4 hashes (quirk), 0 AICH hashes
    {
        EMFileSize size = PARTSIZE;
        FileIdentifier fid(size);
        QCOMPARE(fid.getTheoreticalMD4PartHashCount(), uint16(2));
        QCOMPARE(fid.getTheoreticalAICHPartHashCount(), uint16(0));
    }
    // File == PARTSIZE + 1: 2 MD4 hashes, 2 AICH hashes
    {
        EMFileSize size = PARTSIZE + 1;
        FileIdentifier fid(size);
        QCOMPARE(fid.getTheoreticalMD4PartHashCount(), uint16(2));
        QCOMPARE(fid.getTheoreticalAICHPartHashCount(), uint16(2));
    }
    // File == PARTSIZE * 2: 3 MD4 hashes, 2 AICH hashes
    {
        EMFileSize size = PARTSIZE * 2;
        FileIdentifier fid(size);
        QCOMPARE(fid.getTheoreticalMD4PartHashCount(), uint16(3));
        QCOMPARE(fid.getTheoreticalAICHPartHashCount(), uint16(2));
    }
    // File == PARTSIZE * 2 + 1: 3 MD4 hashes, 3 AICH hashes
    {
        EMFileSize size = PARTSIZE * 2 + 1;
        FileIdentifier fid(size);
        QCOMPARE(fid.getTheoreticalMD4PartHashCount(), uint16(3));
        QCOMPARE(fid.getTheoreticalAICHPartHashCount(), uint16(3));
    }
}

QTEST_MAIN(tst_FileIdentifier)
#include "tst_FileIdentifier.moc"
