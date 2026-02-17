/// @file tst_CollectionFile.cpp
/// @brief Tests for files/CollectionFile — construction, serialization, ed2k link init.

#include "TestHelpers.h"
#include "files/CollectionFile.h"
#include "files/ShareableFile.h"
#include "utils/SafeFile.h"

#include <QTest>
#include <cstring>

using namespace eMule;

class tst_CollectionFile : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void construct_fromAbstractFile();
    void writeAndRead_roundTrip();
    void initFromLink();
};

void tst_CollectionFile::construct_default()
{
    CollectionFile f;
    QVERIFY(f.fileName().isEmpty());
    QCOMPARE(f.fileSize(), EMFileSize{0});
    QVERIFY(f.hasNullHash());
    QVERIFY(!f.hasCollectionExtraInfo());
}

void tst_CollectionFile::construct_fromAbstractFile()
{
    // Create a ShareableFile to use as source (it's concrete unlike AbstractFile)
    ShareableFile src;
    src.setFileName(QStringLiteral("test_song.mp3"));
    src.setFileSize(12345678);
    uint8 hash[16];
    std::memset(hash, 0xAB, 16);
    src.setFileHash(hash);

    CollectionFile cf(&src);
    QCOMPARE(cf.fileName(), QStringLiteral("test_song.mp3"));
    QCOMPARE(cf.fileSize(), EMFileSize{12345678});
    QVERIFY(md4equ(cf.fileHash(), hash));
}

void tst_CollectionFile::writeAndRead_roundTrip()
{
    // Build a source file
    ShareableFile src;
    src.setFileName(QStringLiteral("video.mkv"));
    src.setFileSize(9999999);
    uint8 hash[16];
    std::memset(hash, 0xCD, 16);
    src.setFileHash(hash);

    // Set an AICH hash
    AICHHash aichHash;
    std::memset(aichHash.getRawHash(), 0x42, kAICHHashSize);
    src.fileIdentifier().setAICHHash(aichHash);

    CollectionFile original(&src);
    QVERIFY(original.hasCollectionExtraInfo());

    // Write to mem file
    SafeMemFile memFile;
    QVERIFY(original.writeCollectionInfo(memFile));

    // Read back
    memFile.seek(0, 0);
    CollectionFile loaded(memFile);

    QCOMPARE(loaded.fileName(), QStringLiteral("video.mkv"));
    QCOMPARE(loaded.fileSize(), EMFileSize{9999999});
    QVERIFY(md4equ(loaded.fileHash(), hash));
    QVERIFY(loaded.fileIdentifier().hasAICHHash());
    QCOMPARE(loaded.fileIdentifier().getAICHHash(), aichHash);
    QVERIFY(loaded.hasCollectionExtraInfo());
}

void tst_CollectionFile::initFromLink()
{
    // Build a valid ed2k file link
    // ed2k://|file|test.txt|12345|ABABABABABABABABABABABABABABABAB|/
    uint8 hash[16];
    std::memset(hash, 0xAB, 16);
    QString hashStr = encodeBase16({hash, 16});
    QString link = QStringLiteral("ed2k://|file|test_file.txt|12345|%1|/").arg(hashStr);

    CollectionFile cf;
    QVERIFY(cf.initFromLink(link));
    QCOMPARE(cf.fileName(), QStringLiteral("test_file.txt"));
    QCOMPARE(cf.fileSize(), EMFileSize{12345});
    QVERIFY(md4equ(cf.fileHash(), hash));
}

QTEST_MAIN(tst_CollectionFile)
#include "tst_CollectionFile.moc"
