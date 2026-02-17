/// @file tst_SharedFileList.cpp
/// @brief Tests for files/SharedFileList — shared file management, hashing thread.

#include "TestHelpers.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"

#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

#include <cstring>

using namespace eMule;

class tst_SharedFileList : public QObject {
    Q_OBJECT

private slots:
    void construct_empty();
    void safeAddKFile_addsToMap();
    void safeAddKFile_emitsSignal();
    void removeFile_removes();
    void removeFile_addsToUnshared();
    void isUnsharedFile();
    void getDataSize();
    void hashingThread_completesFile();
    void hashingThread_failsGracefully();
    void getCount();
    void sendListToServer_noServerConnect_noop();
    void sendListToServer_notConnected_noop();
};

void tst_SharedFileList::construct_empty()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    QCOMPARE(shared.getCount(), 0);
}

void tst_SharedFileList::safeAddKFile_addsToMap()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0x11, 16);
    file->setFileHash(hash);
    file->setFileSize(1000);
    knownFiles.safeAddKFile(file);

    QVERIFY(shared.safeAddKFile(file));
    QCOMPARE(shared.getCount(), 1);

    auto* found = shared.getFileByID(hash);
    QCOMPARE(found, file);
}

void tst_SharedFileList::safeAddKFile_emitsSignal()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    QSignalSpy spy(&shared, &SharedFileList::fileAdded);

    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0x22, 16);
    file->setFileHash(hash);
    knownFiles.safeAddKFile(file);

    shared.safeAddKFile(file);
    QCOMPARE(spy.count(), 1);
}

void tst_SharedFileList::removeFile_removes()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0x33, 16);
    file->setFileHash(hash);
    knownFiles.safeAddKFile(file);

    shared.safeAddKFile(file);
    QCOMPARE(shared.getCount(), 1);

    QVERIFY(shared.removeFile(file));
    QCOMPARE(shared.getCount(), 0);
}

void tst_SharedFileList::removeFile_addsToUnshared()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0x44, 16);
    file->setFileHash(hash);
    knownFiles.safeAddKFile(file);

    shared.safeAddKFile(file);
    shared.removeFile(file);

    // Hash should now be in unshared set
    QVERIFY(shared.isUnsharedFile(hash));
}

void tst_SharedFileList::isUnsharedFile()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    uint8 hash[16];
    std::memset(hash, 0x55, 16);

    // Not unshared initially
    QVERIFY(!shared.isUnsharedFile(hash));
}

void tst_SharedFileList::getDataSize()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    auto* file1 = new KnownFile();
    uint8 hash1[16];
    std::memset(hash1, 0x66, 16);
    file1->setFileHash(hash1);
    file1->setFileSize(1000);
    knownFiles.safeAddKFile(file1);
    shared.safeAddKFile(file1);

    auto* file2 = new KnownFile();
    uint8 hash2[16];
    std::memset(hash2, 0x77, 16);
    file2->setFileHash(hash2);
    file2->setFileSize(2000);
    knownFiles.safeAddKFile(file2);
    shared.safeAddKFile(file2);

    uint64 largest = 0;
    uint64 total = shared.getDataSize(largest);
    QCOMPARE(total, uint64{3000});
    QCOMPARE(largest, uint64{2000});
}

void tst_SharedFileList::hashingThread_completesFile()
{
    eMule::testing::TempDir tmpDir;

    // Create a test file to hash
    const QString filename = QStringLiteral("hashme.bin");
    QFile f(tmpDir.filePath(filename));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(1024, 'H'));
    f.close();

    HashingThread thread;
    QSignalSpy finishedSpy(&thread, &HashingThread::hashingFinished);

    thread.start();
    thread.enqueue({tmpDir.path(), filename, {}});

    // Wait for signal (up to 5 seconds)
    QVERIFY(finishedSpy.wait(5000));
    QCOMPARE(finishedSpy.count(), 1);

    // Clean up the created KnownFile
    auto* kf = finishedSpy.at(0).at(0).value<KnownFile*>();
    QVERIFY(kf != nullptr);
    QVERIFY(!kf->hasNullHash());
    delete kf;

    thread.requestStop();
    thread.wait();
}

void tst_SharedFileList::hashingThread_failsGracefully()
{
    HashingThread thread;
    QSignalSpy failedSpy(&thread, &HashingThread::hashingFailed);

    thread.start();
    thread.enqueue({QStringLiteral("/nonexistent"), QStringLiteral("nofile.bin"), {}});

    QVERIFY(failedSpy.wait(5000));
    QCOMPARE(failedSpy.count(), 1);

    thread.requestStop();
    thread.wait();
}

void tst_SharedFileList::getCount()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    QCOMPARE(shared.getCount(), 0);

    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0x88, 16);
    file->setFileHash(hash);
    knownFiles.safeAddKFile(file);

    shared.safeAddKFile(file);
    QCOMPARE(shared.getCount(), 1);
}

void tst_SharedFileList::sendListToServer_noServerConnect_noop()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // No server connect set — should not crash
    shared.sendListToServer();
    QCOMPARE(shared.getCount(), 0);
}

void tst_SharedFileList::sendListToServer_notConnected_noop()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // Add a file so the list isn't empty
    auto* file = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0x99, 16);
    file->setFileHash(hash);
    file->setFileSize(1000);
    knownFiles.safeAddKFile(file);
    shared.safeAddKFile(file);

    // No server connect — should not crash even with files present
    shared.sendListToServer();
    QVERIFY(!file->publishedED2K()); // should not have been published
}

QTEST_MAIN(tst_SharedFileList)
#include "tst_SharedFileList.moc"
