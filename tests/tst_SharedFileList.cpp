/// @file tst_SharedFileList.cpp
/// @brief Tests for files/SharedFileList — shared file management, hashing thread.

#include "TestHelpers.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"

#include "prefs/Preferences.h"
#include "server/Server.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

#include <atomic>
#include <cstring>
#include <thread>

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

    // Share membership (MFC ShouldBeShared)
    void unsharedMark_isNotAReAddGate();
    void excludeFile_survivesReload();
    void excludeFile_refusedForIncomingDir();
    void sharedFilesConfig_roundTrips();
    void rescan_addsKeywordsThroughTheFrontDoor();

    // OP_OFFERFILES selection (MFC SendListToServer)
    void offer_capIsTheServersSoftFilesLimit();
    void offer_skipsLargeFilesForServersThatCannotIndexThem();
    void offer_marksPublishedSoTheNextPassIsEmpty();

    // Locking (one mutex, no nesting)
    void concurrentIterationWhileMutating();
    void reloadDoesNotDeadlockAgainstTheScan();
};

namespace {

/// A KnownFile that looks real enough for the shared list: a distinct hash, a size,
/// and a name so keyword extraction has something to chew on.
KnownFile* makeFile(KnownFileList& known, uint8 hashByte, const QString& name,
                    uint64 size = 1000)
{
    auto* f = new KnownFile();
    uint8 hash[16];
    std::memset(hash, hashByte, 16);
    f->setFileHash(hash);
    f->setFileName(name);
    f->setFileSize(size);
    known.safeAddKFile(f);
    return f;
}

/// Write a file with real content, so a directory scan will pick it up.
QString writeFile(const QString& dir, const QString& name, const QByteArray& content)
{
    QDir().mkpath(dir);
    QFile f(QDir(dir).filePath(name));
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(content);
    f.close();
    return QDir(dir).filePath(name);
}

} // namespace

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

// ---------------------------------------------------------------------------
// Share membership
// ---------------------------------------------------------------------------

void tst_SharedFileList::unsharedMark_isNotAReAddGate()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    KnownFile* file = makeFile(knownFiles, 0xA1, QStringLiteral("readd.bin"));
    QVERIFY(shared.safeAddKFile(file));

    QVERIFY(shared.removeFile(file));
    QVERIFY2(shared.isUnsharedFile(file->fileHash()),
             "removeFile must record the hash so isUnsharedFile() can answer a peer");

    // The old code refused this forever, because isDuplicate() consulted the same set.
    QVERIFY2(shared.safeAddKFile(file),
             "a previously removed file must be addable again (MFC AddFile:695)");
    QCOMPARE(shared.getCount(), 1);
    QVERIFY2(!shared.isUnsharedFile(file->fileHash()),
             "a successful add must clear the mark");
}

void tst_SharedFileList::excludeFile_survivesReload()
{
    eMule::testing::TempDir tmp;
    const QString shareDir = tmp.filePath(QStringLiteral("share"));
    const QString keepPath = writeFile(shareDir, QStringLiteral("keep.bin"), QByteArray(512, 'k'));
    const QString dropPath = writeFile(shareDir, QStringLiteral("drop.bin"), QByteArray(512, 'd'));
    QVERIFY(!keepPath.isEmpty() && !dropPath.isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({shareDir});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    QVERIFY(shared.shouldBeShared(shareDir, dropPath, false));
    QVERIFY(shared.excludeFile(dropPath));
    QVERIFY(!shared.shouldBeShared(shareDir, dropPath, false));
    QVERIFY2(shared.shouldBeShared(shareDir, keepPath, false),
             "excluding one file must not affect its neighbours");

    // The point of the whole exercise: a rescan must not put it back. The in-memory
    // hash set never survived this, which is why unsharing looked permanent and wasn't.
    shared.reload();
    bool sawDropped = false;
    shared.forEachFile([&](KnownFile* f) {
        if (f->filePath().compare(dropPath, Qt::CaseInsensitive) == 0)
            sawDropped = true;
    });
    QVERIFY2(!sawDropped, "an excluded file must stay out across a reload");
}

void tst_SharedFileList::excludeFile_refusedForIncomingDir()
{
    eMule::testing::TempDir tmp;
    const QString incoming = tmp.filePath(QStringLiteral("incoming"));
    const QString path = writeFile(incoming, QStringLiteral("got.bin"), QByteArray(256, 'i'));
    QVERIFY(!path.isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(incoming);
    thePrefs.setSharedDirs({});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    QVERIFY(shared.shouldBeShared(incoming, path, false));
    QVERIFY2(shared.shouldBeShared(incoming, path, /*mustBeShared=*/true),
             "the incoming directory is shared unconditionally");
    QVERIFY2(!shared.excludeFile(path),
             "a file in the incoming directory cannot be unshared (MFC ExcludeFile:1448)");
    QVERIFY(shared.shouldBeShared(incoming, path, false));
}

void tst_SharedFileList::sharedFilesConfig_roundTrips()
{
    eMule::testing::TempDir tmp;
    const QString outside = tmp.filePath(QStringLiteral("elsewhere"));
    const QString shareDir = tmp.filePath(QStringLiteral("share"));
    // A non-ASCII name, because the file format is UTF-16 for exactly this reason.
    const QString singlePath = writeFile(outside, QStringLiteral("björk — tröst.bin"),
                                         QByteArray(256, 's'));
    const QString excludedPath = writeFile(shareDir, QStringLiteral("nope.bin"),
                                           QByteArray(256, 'n'));
    QVERIFY(!singlePath.isEmpty() && !excludedPath.isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({shareDir});

    {
        KnownFileList knownFiles;
        SharedFileList shared(&knownFiles);
        QVERIFY(shared.addSingleSharedFile(singlePath));
        QVERIFY(shared.excludeFile(excludedPath));
        QVERIFY(shared.containsSingleSharedFiles(outside));
    }

    QVERIFY2(QFile::exists(QDir(tmp.path()).filePath(QStringLiteral("sharedfiles.dat"))),
             "the lists must be persisted, or unsharing lasts only until restart");

    // A fresh instance, as after a restart.
    KnownFileList knownFiles;
    SharedFileList reloaded(&knownFiles);
    QVERIFY2(reloaded.shouldBeShared(outside, singlePath, false),
             "a single-shared file must come back after a restart");
    QVERIFY2(!reloaded.shouldBeShared(shareDir, excludedPath, false),
             "an excluded file must stay excluded after a restart");
    QVERIFY(reloaded.containsSingleSharedFiles(outside));
}

void tst_SharedFileList::rescan_addsKeywordsThroughTheFrontDoor()
{
    eMule::testing::TempDir tmp;
    const QString shareDir = tmp.filePath(QStringLiteral("share"));
    QVERIFY(!writeFile(shareDir, QStringLiteral("rescanned.bin"), QByteArray(512, 'r')).isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({shareDir});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // Pretend the file is already known, which is the branch that used to write m_map
    // directly: it skipped isDuplicate(), the collection check and addKeywords(), so a
    // rescanned file was invisible to Kad keyword publishing.
    const QFileInfo fi(QDir(shareDir).filePath(QStringLiteral("rescanned.bin")));
    auto* known = new KnownFile();
    uint8 hash[16];
    std::memset(hash, 0xB2, 16);
    known->setFileHash(hash);
    known->setFileName(fi.fileName());
    known->setFileSize(static_cast<uint64>(fi.size()));
    known->setUtcFileDate(static_cast<time_t>(fi.lastModified().toSecsSinceEpoch()));
    known->setFilePath(fi.absoluteFilePath());
    known->setPath(fi.absolutePath());
    knownFiles.safeAddKFile(known);

    QSignalSpy addedSpy(&shared, &SharedFileList::fileAdded);
    shared.reload();

    QVERIFY2(shared.getFileByID(hash) != nullptr, "the rescan must find the known file");
    QCOMPARE(addedSpy.count(), 1);

    // Adding it a second time must be refused, rather than silently overwriting the
    // map entry as the direct write did.
    QVERIFY2(!shared.safeAddKFile(known), "a duplicate hash must be rejected");
    QCOMPARE(shared.getCount(), 1);
}

// ---------------------------------------------------------------------------
// OP_OFFERFILES selection
// ---------------------------------------------------------------------------

void tst_SharedFileList::offer_capIsTheServersSoftFilesLimit()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // More than any cap under test, so the cap is always what limits the result.
    for (int i = 0; i < 210; ++i) {
        auto* f = new KnownFile();
        uint8 hash[16];
        std::memset(hash, 0, 16);
        hash[0] = static_cast<uint8>(i & 0xFF);
        hash[1] = static_cast<uint8>(i >> 8);
        f->setFileHash(hash);
        f->setFileName(QStringLiteral("f%1.bin").arg(i));
        f->setFileSize(1000);
        knownFiles.safeAddKFile(f);
        QVERIFY(shared.safeAddKFile(f));
    }
    QCOMPARE(shared.getCount(), 210);

    const auto unpublishAll = [&] {
        shared.forEachFile([](KnownFile* f) { f->setPublishedED2K(false); });
    };

    Server srv(0x01020304u, 4661);

    // A server that says nothing gets our own ceiling.
    srv.setSoftFiles(0);
    QCOMPARE(static_cast<int>(shared.takeFilesToOffer(&srv).size()), 200);

    // A lower limit is honoured...
    unpublishAll();
    srv.setSoftFiles(50);
    QCOMPARE(static_cast<int>(shared.takeFilesToOffer(&srv).size()), 50);

    // ...but it may not raise ours (srchybrid/SharedFileList.cpp:832-834).
    unpublishAll();
    srv.setSoftFiles(5000);
    QCOMPARE(static_cast<int>(shared.takeFilesToOffer(&srv).size()), 200);

    // No server at all behaves like "unknown".
    unpublishAll();
    QCOMPARE(static_cast<int>(shared.takeFilesToOffer(nullptr).size()), 200);
}

void tst_SharedFileList::offer_skipsLargeFilesForServersThatCannotIndexThem()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // OLD_MAX_EMULE_FILE_SIZE is 4290048000; anything above it is a "large" file.
    KnownFile* small = makeFile(knownFiles, 0xC1, QStringLiteral("small.bin"), 1000);
    KnownFile* large = makeFile(knownFiles, 0xC2, QStringLiteral("large.bin"), 5000000000ULL);
    QVERIFY(shared.safeAddKFile(small));
    QVERIFY(shared.safeAddKFile(large));
    QVERIFY2(large->isLargeFile(), "the fixture must actually be a large file");

    Server plain(0x01020304u, 4661);
    auto offered = shared.takeFilesToOffer(&plain);
    QCOMPARE(static_cast<int>(offered.size()), 1);
    QCOMPARE(offered.front(), small);
    QVERIFY2(!large->publishedED2K(),
             "a skipped file must not be marked published, or it never gets offered");

    // Same list, a server that advertises large-file support: both go.
    shared.forEachFile([](KnownFile* f) { f->setPublishedED2K(false); });
    Server big(0x01020305u, 4661);
    big.setTCPFlags(SrvTcpFlag::LargeFiles);
    QVERIFY(big.supportsLargeFilesTCP());
    QCOMPARE(static_cast<int>(shared.takeFilesToOffer(&big).size()), 2);
}

void tst_SharedFileList::offer_marksPublishedSoTheNextPassIsEmpty()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    KnownFile* file = makeFile(knownFiles, 0xC3, QStringLiteral("once.bin"));
    QVERIFY(shared.safeAddKFile(file));

    Server srv(0x01020304u, 4661);
    QCOMPARE(static_cast<int>(shared.takeFilesToOffer(&srv).size()), 1);
    QVERIFY(file->publishedED2K());

    // The offer is incremental: nothing changed, so there is nothing to send again.
    QVERIFY(shared.takeFilesToOffer(&srv).empty());

    // Reconnecting to a server clears the flags — and must re-arm the republish, or
    // the whole share is marked unpublished and then never offered.
    shared.clearED2KPublishFlags();
    QVERIFY(!file->publishedED2K());
    QCOMPARE(static_cast<int>(shared.takeFilesToOffer(&srv).size()), 1);
}

// ---------------------------------------------------------------------------
// Locking
// ---------------------------------------------------------------------------

void tst_SharedFileList::concurrentIterationWhileMutating()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    std::vector<KnownFile*> files;
    for (int i = 0; i < 64; ++i) {
        auto* f = new KnownFile();
        uint8 hash[16];
        std::memset(hash, 0, 16);
        hash[0] = static_cast<uint8>(i);
        hash[1] = 0xD1;
        f->setFileHash(hash);
        f->setFileName(QStringLiteral("c%1.bin").arg(i));
        f->setFileSize(1000);
        knownFiles.safeAddKFile(f);
        files.push_back(f);
    }

    // A reader on another thread, as AICHSyncThread is: it walks the map through
    // forEachFile() and also asks for the hashing count, which used to be guarded by a
    // *different* mutex than the map — so the two could run against each other.
    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            int seen = 0;
            shared.forEachFile([&](KnownFile* f) {
                if (f && !f->fileName().isEmpty())
                    ++seen;
            });
            (void)shared.getHashingCount();
            (void)shared.getCount();
            reads.fetch_add(seen, std::memory_order_relaxed);
        }
    });

    for (int pass = 0; pass < 20; ++pass) {
        for (KnownFile* f : files)
            shared.safeAddKFile(f);
        for (KnownFile* f : files)
            shared.removeFile(f);
    }

    stop.store(true, std::memory_order_relaxed);
    reader.join();

    QCOMPARE(shared.getCount(), 0);
    QVERIFY2(reads.load() >= 0, "the reader must have run to completion without deadlocking");
}

void tst_SharedFileList::reloadDoesNotDeadlockAgainstTheScan()
{
    eMule::testing::TempDir tmp;
    const QString shareDir = tmp.filePath(QStringLiteral("share"));
    for (int i = 0; i < 8; ++i)
        QVERIFY(!writeFile(shareDir, QStringLiteral("r%1.bin").arg(i), QByteArray(64, 'x')).isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({shareDir});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // reload() used to hold a lock across the whole scan. Now the scan feeds files back
    // in through safeAddKFile() -> addEntity(), which takes the map lock per file: with
    // one mutex that is a self-deadlock unless reload() releases first. Two passes,
    // because the second one also exercises the clear-then-refill path.
    shared.reload();
    shared.reload();

    // Getting here at all is the assertion; the count just confirms the scan ran.
    QVERIFY(shared.getHashingCount() >= 0);
}

QTEST_MAIN(tst_SharedFileList)
#include "tst_SharedFileList.moc"
