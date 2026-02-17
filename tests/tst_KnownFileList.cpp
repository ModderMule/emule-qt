/// @file tst_KnownFileList.cpp
/// @brief Tests for files/KnownFileList — persistence, lookup, cancelled files.

#include "TestHelpers.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"

#include <QTest>
#include <QTemporaryDir>

#include <cstring>
#include <ctime>

using namespace eMule;

class tst_KnownFileList : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void safeAddKFile_basic();
    void safeAddKFile_duplicate();
    void findKnownFile_byMetadata();
    void findKnownFile_notFound();
    void findKnownFileByID();
    void findKnownFileByPath();
    void isKnownFile_check();
    void isFilePtrInList_ptrCheck();
    void addCancelledFileID_and_check();
    void isCancelledFileByID_notCancelled();
    void saveLoadRoundTrip();
    void process_autoSave();
    void clear_deletesAll();
};

void tst_KnownFileList::construct_default()
{
    KnownFileList list;
    QCOMPARE(list.count(), size_t{0});
    QCOMPARE(list.totalTransferred, uint64{0});
    QCOMPARE(list.totalRequested, uint32{0});
    QCOMPARE(list.totalAccepted, uint32{0});
}

void tst_KnownFileList::safeAddKFile_basic()
{
    KnownFileList list;

    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0x11, 16);
    file->setFileHash(hash);
    file->setFileName(QStringLiteral("test.txt"));
    file->statistic.setAllTimeTransferred(1000);
    file->statistic.setAllTimeRequests(5);
    file->statistic.setAllTimeAccepts(2);

    QVERIFY(list.safeAddKFile(file));
    QCOMPARE(list.count(), size_t{1});
    QCOMPARE(list.totalTransferred, uint64{1000});
    QCOMPARE(list.totalRequested, uint32{5});
    QCOMPARE(list.totalAccepted, uint32{2});

    // Should be findable by ID
    auto* found = list.findKnownFileByID(hash);
    QCOMPARE(found, file);
}

void tst_KnownFileList::safeAddKFile_duplicate()
{
    KnownFileList list;

    uint8 hash[16];
    std::memset(hash, 0x22, 16);

    auto* file1 = new KnownFile();
    file1->setFileHash(hash);
    file1->statistic.setAllTimeTransferred(500);
    QVERIFY(list.safeAddKFile(file1));
    QCOMPARE(list.totalTransferred, uint64{500});

    // Add duplicate with different stats — should replace
    auto* file2 = new KnownFile();
    file2->setFileHash(hash);
    file2->statistic.setAllTimeTransferred(800);
    QVERIFY(list.safeAddKFile(file2));

    QCOMPARE(list.count(), size_t{1});
    // Stats delta: 800 - 500 = 300 added to initial 500
    QCOMPARE(list.totalTransferred, uint64{800});

    // Should find file2, not file1
    auto* found = list.findKnownFileByID(hash);
    QCOMPARE(found, file2);
}

void tst_KnownFileList::findKnownFile_byMetadata()
{
    KnownFileList list;

    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0x33, 16);
    file->setFileHash(hash);
    file->setFileName(QStringLiteral("video.avi"));
    file->setFileSize(12345);
    file->setUtcFileDate(1700000000);
    list.safeAddKFile(file);

    auto* found = list.findKnownFile(QStringLiteral("video.avi"), 1700000000, 12345);
    QCOMPARE(found, file);
}

void tst_KnownFileList::findKnownFile_notFound()
{
    KnownFileList list;
    auto* result = list.findKnownFile(QStringLiteral("nofile.txt"), 0, 0);
    QVERIFY(result == nullptr);
}

void tst_KnownFileList::findKnownFileByID()
{
    KnownFileList list;

    uint8 hash[16];
    std::memset(hash, 0x44, 16);

    auto* file = new KnownFile();
    file->setFileHash(hash);
    list.safeAddKFile(file);

    QVERIFY(list.findKnownFileByID(hash) == file);

    uint8 otherHash[16];
    std::memset(otherHash, 0x55, 16);
    QVERIFY(list.findKnownFileByID(otherHash) == nullptr);
}

void tst_KnownFileList::findKnownFileByPath()
{
    KnownFileList list;

    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0x66, 16);
    file->setFileHash(hash);
    file->setFilePath(QStringLiteral("/tmp/test/myfile.dat"));
    list.safeAddKFile(file);

    QVERIFY(list.findKnownFileByPath(QStringLiteral("/tmp/test/myfile.dat")) == file);
    QVERIFY(list.findKnownFileByPath(QStringLiteral("/tmp/test/other.dat")) == nullptr);
}

void tst_KnownFileList::isKnownFile_check()
{
    KnownFileList list;

    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0x77, 16);
    file->setFileHash(hash);
    list.safeAddKFile(file);

    QVERIFY(list.isKnownFile(file));

    KnownFile other;
    uint8 otherHash[16];
    std::memset(otherHash, 0x88, 16);
    other.setFileHash(otherHash);
    QVERIFY(!list.isKnownFile(&other));
}

void tst_KnownFileList::isFilePtrInList_ptrCheck()
{
    KnownFileList list;

    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0x99, 16);
    file->setFileHash(hash);
    list.safeAddKFile(file);

    QVERIFY(list.isFilePtrInList(file));

    KnownFile other;
    QVERIFY(!list.isFilePtrInList(&other));
}

void tst_KnownFileList::addCancelledFileID_and_check()
{
    eMule::testing::TempDir tmpDir;
    KnownFileList list;
    list.init(tmpDir.path());

    uint8 hash[16];
    std::memset(hash, 0xAA, 16);

    list.addCancelledFileID(hash);
    QVERIFY(list.isCancelledFileByID(hash));
}

void tst_KnownFileList::isCancelledFileByID_notCancelled()
{
    eMule::testing::TempDir tmpDir;
    KnownFileList list;
    list.init(tmpDir.path());

    uint8 hash[16];
    std::memset(hash, 0xBB, 16);

    QVERIFY(!list.isCancelledFileByID(hash));
}

void tst_KnownFileList::saveLoadRoundTrip()
{
    eMule::testing::TempDir tmpDir;

    // Create and populate
    {
        KnownFileList list;
        list.init(tmpDir.path());

        auto* file = new KnownFile();
        uint8 hash[16];
        std::memset(hash, 0xCC, 16);
        file->setFileHash(hash);
        file->setFileName(QStringLiteral("roundtrip.bin"));
        file->setFileSize(9999);
        file->setUtcFileDate(1700000000);
        file->statistic.setAllTimeTransferred(5000);
        file->statistic.setAllTimeRequests(10);
        file->statistic.setAllTimeAccepts(3);
        list.safeAddKFile(file);

        list.save();
    }

    // Reload and verify
    {
        KnownFileList list;
        QVERIFY(list.init(tmpDir.path()));
        QCOMPARE(list.count(), size_t{1});

        uint8 hash[16];
        std::memset(hash, 0xCC, 16);
        auto* found = list.findKnownFileByID(hash);
        QVERIFY(found != nullptr);
        QCOMPARE(found->fileName(), QStringLiteral("roundtrip.bin"));
        QCOMPARE(static_cast<uint64>(found->fileSize()), uint64{9999});
        QCOMPARE(found->statistic.allTimeTransferred(), uint64{5000});
    }
}

void tst_KnownFileList::process_autoSave()
{
    eMule::testing::TempDir tmpDir;
    KnownFileList list;
    list.init(tmpDir.path());

    // process() should not crash, even with empty list
    list.process();
}

void tst_KnownFileList::clear_deletesAll()
{
    KnownFileList list;

    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0xDD, 16);
    file->setFileHash(hash);
    list.safeAddKFile(file);

    QCOMPARE(list.count(), size_t{1});
    list.clear();
    QCOMPARE(list.count(), size_t{0});
    QCOMPARE(list.totalTransferred, uint64{0});
}

QTEST_MAIN(tst_KnownFileList)
#include "tst_KnownFileList.moc"
