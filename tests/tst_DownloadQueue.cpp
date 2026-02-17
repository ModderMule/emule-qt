/// @file tst_DownloadQueue.cpp
/// @brief Tests for transfer/DownloadQueue — file management, lookup,
///        priority sorting, source management.

#include "TestHelpers.h"
#include "files/PartFile.h"
#include "transfer/DownloadQueue.h"
#include "client/UpDownClient.h"
#include "prefs/Preferences.h"
#include "utils/OtherFunctions.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

using namespace eMule;

class tst_DownloadQueue : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void construction_empty();
    void addDownload_basic();
    void addDownload_paused();
    void removeFile_basic();
    void fileByID_found();
    void fileByID_notFound();
    void isFileExisting_basic();
    void sortByPriority_ordering();
    void startNextFile_resumesPaused();
    void init_scansDirectory();
    void checkAndAddSource_basic();

private:
    QTemporaryDir m_tempDir;

    PartFile* createTestPartFile(const uint8* hash, const QString& name,
                                  uint8 priority = kPrNormal);
};

void tst_DownloadQueue::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    thePrefs.setIncomingDir(m_tempDir.path() + QStringLiteral("/incoming"));
    thePrefs.setTempDirs({m_tempDir.path() + QStringLiteral("/temp")});
    QDir().mkpath(thePrefs.incomingDir());
    QDir().mkpath(thePrefs.tempDirs().first());
}

PartFile* tst_DownloadQueue::createTestPartFile(const uint8* hash,
                                                  const QString& name,
                                                  uint8 priority)
{
    auto* pf = new PartFile;
    pf->setFileName(name);
    pf->setFileSize(PARTSIZE);
    pf->setFileHash(hash);
    pf->setAutoDownPriority(false);
    pf->setDownPriority(priority);
    return pf;
}

void tst_DownloadQueue::construction_empty()
{
    DownloadQueue dq;
    QCOMPARE(dq.fileCount(), 0);
    QCOMPARE(dq.datarate(), 0U);
    QVERIFY(dq.files().empty());
}

void tst_DownloadQueue::addDownload_basic()
{
    DownloadQueue dq;

    uint8 hash[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("test1.bin"));

    QSignalSpy spy(&dq, &DownloadQueue::fileAdded);

    dq.addDownload(pf);
    QCOMPARE(dq.fileCount(), 1);
    QCOMPARE(spy.count(), 1);

    // Don't add duplicate
    dq.addDownload(pf);
    QCOMPARE(dq.fileCount(), 1);

    dq.deleteAll();
}

void tst_DownloadQueue::addDownload_paused()
{
    DownloadQueue dq;

    uint8 hash[16] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    auto* pf = createTestPartFile(hash, QStringLiteral("paused.bin"));

    dq.addDownload(pf, true);
    QCOMPARE(dq.fileCount(), 1);
    QVERIFY(pf->isPaused());

    dq.deleteAll();
}

void tst_DownloadQueue::removeFile_basic()
{
    DownloadQueue dq;

    uint8 hash[16] = {3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
    auto* pf = createTestPartFile(hash, QStringLiteral("remove.bin"));

    dq.addDownload(pf);
    QCOMPARE(dq.fileCount(), 1);

    QSignalSpy spy(&dq, &DownloadQueue::fileRemoved);

    dq.removeFile(pf);
    QCOMPARE(dq.fileCount(), 0);
    QCOMPARE(spy.count(), 1);

    delete pf;
}

void tst_DownloadQueue::fileByID_found()
{
    DownloadQueue dq;

    uint8 hash[16] = {4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4};
    auto* pf = createTestPartFile(hash, QStringLiteral("find.bin"));

    dq.addDownload(pf);

    PartFile* found = dq.fileByID(hash);
    QVERIFY(found != nullptr);
    QCOMPARE(found, pf);

    dq.deleteAll();
}

void tst_DownloadQueue::fileByID_notFound()
{
    DownloadQueue dq;

    uint8 hash1[16] = {5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5};
    uint8 hash2[16] = {6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6};
    auto* pf = createTestPartFile(hash1, QStringLiteral("find2.bin"));

    dq.addDownload(pf);

    PartFile* found = dq.fileByID(hash2);
    QVERIFY(found == nullptr);

    dq.deleteAll();
}

void tst_DownloadQueue::isFileExisting_basic()
{
    DownloadQueue dq;

    uint8 hash[16] = {7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7};
    auto* pf = createTestPartFile(hash, QStringLiteral("exists.bin"));

    dq.addDownload(pf);

    QVERIFY(dq.isFileExisting(hash));

    uint8 otherHash[16] = {8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8};
    QVERIFY(!dq.isFileExisting(otherHash));

    dq.deleteAll();
}

void tst_DownloadQueue::sortByPriority_ordering()
{
    DownloadQueue dq;

    uint8 hash1[16] = {10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hash2[16] = {10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    uint8 hash3[16] = {10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};

    auto* low = createTestPartFile(hash1, QStringLiteral("low.bin"), kPrLow);
    auto* high = createTestPartFile(hash2, QStringLiteral("high.bin"), kPrHigh);
    auto* normal = createTestPartFile(hash3, QStringLiteral("normal.bin"), kPrNormal);

    // Add in wrong order
    dq.addDownload(low);
    dq.addDownload(normal);
    dq.addDownload(high);

    // After sorting, files should be ordered by priority
    // rightFileHasHigherPrio returns true when left < right priority
    const auto& files = dq.files();
    QCOMPARE(files.size(), 3U);

    // Verify high-priority file comes first (or at least higher-prio before lower)
    bool highBeforeLow = false;
    int highIdx = -1, lowIdx = -1;
    for (int i = 0; i < static_cast<int>(files.size()); ++i) {
        if (files[static_cast<size_t>(i)] == high) highIdx = i;
        if (files[static_cast<size_t>(i)] == low) lowIdx = i;
    }
    highBeforeLow = (highIdx < lowIdx);
    QVERIFY(highBeforeLow);

    dq.deleteAll();
}

void tst_DownloadQueue::startNextFile_resumesPaused()
{
    DownloadQueue dq;

    uint8 hash1[16] = {20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hash2[16] = {20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};

    auto* pf1 = createTestPartFile(hash1, QStringLiteral("paused1.bin"), kPrLow);
    auto* pf2 = createTestPartFile(hash2, QStringLiteral("paused2.bin"), kPrHigh);

    dq.addDownload(pf1, true);
    dq.addDownload(pf2, true);

    QVERIFY(pf1->isPaused());
    QVERIFY(pf2->isPaused());

    // Start next should resume the highest priority paused file
    dq.startNextFile();

    // At least one should be resumed
    QVERIFY(!pf1->isPaused() || !pf2->isPaused());

    dq.deleteAll();
}

void tst_DownloadQueue::init_scansDirectory()
{
    // Create a temp dir with a .part.met file
    const QString tempDir = m_tempDir.path() + QStringLiteral("/scan_test");
    QDir().mkpath(tempDir);

    // Create a PartFile and save it
    PartFile pf;
    pf.setFileName(QStringLiteral("scan_test.bin"));
    pf.setFileSize(10000);

    uint8 hash[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    pf.setFileHash(hash);

    QVERIFY(pf.createPartFile(tempDir));

    const QString metFileName = pf.partMetFileName();
    QVERIFY(QFile::exists(tempDir + QDir::separator() + metFileName));

    // Now create a DownloadQueue and init from the directory
    DownloadQueue dq;
    dq.init({tempDir});

    QCOMPARE(dq.fileCount(), 1);
    QVERIFY(dq.fileByID(hash) != nullptr);

    dq.deleteAll();
}

void tst_DownloadQueue::checkAndAddSource_basic()
{
    DownloadQueue dq;

    uint8 hash[16] = {30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("source_test.bin"));

    dq.addDownload(pf);

    UpDownClient client;
    client.setIP(0x01020304);
    client.setUserPort(4662);

    bool added = dq.checkAndAddSource(pf, &client);
    QVERIFY(added);
    QCOMPARE(pf->sourceCount(), 1);

    // Adding same source again should fail
    bool duplicate = dq.checkAndAddSource(pf, &client);
    QVERIFY(!duplicate);
    QCOMPARE(pf->sourceCount(), 1);

    dq.deleteAll();
}

QTEST_GUILESS_MAIN(tst_DownloadQueue)
#include "tst_DownloadQueue.moc"
