/// @file tst_SharedFileList.cpp
/// @brief Tests for files/SharedFileList — shared file management, hashing thread.

#include "TestHelpers.h"
#include "TestFixtures.h"
#include "client/ClientList.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadSearchManager.h"
#include "files/Collection.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "app/AppContext.h"
#include "files/SharedDirWatcher.h"
#include "files/SharedFileList.h"
#include "utils/FileDate.h"
#include "client/UpDownClient.h"

#include "prefs/Preferences.h"
#include "server/Server.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

#include <atomic>
#include <cstring>
#include <thread>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

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
    void excludeFile_refusedForCategoryIncomingDir();
    void sharedFilesConfig_roundTrips();
    void scan_skipsAFileStillBeingDelivered();
    void scan_skipsThumbsDbAndOversizedFiles();
    void scan_winSkipsSystemAndTemporarySharesHidden();
    void scan_winShellLinkFollowsTheOption();
    void pathRules_matchOtherSpellingsByKey();
    void directoryIndex_followsAddRemoveAndMove();
    void rescan_addsKeywordsThroughTheFrontDoor();
    void reload_dropsKeywordsOfFilesNoLongerShared();
    void knownListReplace_unhooksTheSharedOldObject();

    // Incremental reload, hashing retries, the watcher
    void kadPublishTimes_surviveARestartUntilTheFilesChange();
    void kadPublishStore_roundTripAndDamage();
    void hashing_aFileChangedSinceTheScanIsNotAFailure();
    void hashFailure_givenUpSurvivesARestart();
    void hashOrder_coldStartTakesTheSmallFilesFirst();
    void hashing_volumesRunSideBySideOneVolumeInSequence();
    void moveBetweenSharedDirs_keepsTheFileAndHashesNothing_data();
    void moveBetweenSharedDirs_keepsTheFileAndHashesNothing();
    void singleSharedFile_itsDirectoryIsWatchedAndRescanned();
    void staleMediaStamp_isBroughtUpToDateInTheBackground();
    void reload_leavesUnchangedFilesAlone();
    void reload_aShiftedDateOnALocalTimeVolumeIsNotAChangedFile();
    void reload_followsDeleteChangeAndRename();
    void reload_doesNotHashAFileTwice();
    void hashFailure_isRetriedThenRememberedUntilTheFileChanges();
    void freshFile_isHeldBackWhileWatching();
    void watching_picksUpACopiedInFileWithoutAReload();
    void hashFinished_duplicateContentKeepsTheSharedFile();

    // OP_OFFERFILES selection (MFC SendListToServer)
    void offer_capIsTheServersSoftFilesLimit();
    void offer_skipsLargeFilesForServersThatCannotIndexThem();
    void offer_marksPublishedSoTheNextPassIsEmpty();
    void republishFile_offersACompletedFileAgain();
    void offer_usesThePublishedFileType();
    void offer_carriesTheMediaTagsServersCanSearch();

    // Fake-file verdicts (warmContainerChecks)
    void theSweepSettlesVerdictsOffThePollPath();
    void theSweepStopsAndANewFileRestartsIt();

    // Kad publish probe
    void nextDueFile_isOneWalkAndRoundRobin();
    void publishDueNotes_probesAreSpacedTwoSeconds();
    void canPublishToKad_firewalledNeedsABuddy();

    // Rebuild of the media tags on request
    void rebuildMetaData_rereadsSharedFilesOnly();

    // Locking (one mutex, no nesting)
    void concurrentIterationWhileMutating();
    void reloadDoesNotDeadlockAgainstTheScan();
    void attachCollection_parsesAFileSharedBeforeItWasComplete();
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

/// A shared file backed by real bytes on disk, so the container sweep has something
/// to read. The name lies about the contents on purpose.
KnownFile* makeFileOnDisk(KnownFileList& known, SharedFileList& shared, uint8 hashByte,
                          const QString& dir, const QString& name, const QByteArray& head)
{
    const QString path = writeFile(dir, name, head);
    auto* f = makeFile(known, hashByte, name, static_cast<uint64>(head.size()));
    f->setPath(dir);
    f->setFilePath(path);
    shared.safeAddKFile(f);
    return f;
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

void tst_SharedFileList::pathRules_matchOtherSpellingsByKey()
{
    eMule::testing::TempDir tmp;
    const QString shareDir = tmp.filePath(QStringLiteral("Share"));
    const QString path = writeFile(shareDir, QStringLiteral("One.bin"), QByteArray(64, 'o'));
    QVERIFY(!path.isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({shareDir + QLatin1Char('/')});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // trailing slash, case, and a ".." detour all name the same directory and file
    const QString detour = shareDir + QStringLiteral("/../Share");
    QVERIFY(shared.shouldBeShared(shareDir, path, false));
    QVERIFY(shared.shouldBeShared(shareDir.toUpper(), path.toUpper(), false));
    QVERIFY(shared.shouldBeShared(detour, detour + QStringLiteral("/One.bin"), false));
    QVERIFY(!shared.shouldBeShared(shareDir + QStringLiteral("2"), path, false));

    // an exclusion is found under any spelling too, among many others
    for (int i = 0; i < 5000; ++i)
        shared.m_singleExcludedFiles.insert(
            SharedFileList::pathKey(shareDir + QStringLiteral("/x%1.bin").arg(i)), QString());
    QVERIFY(shared.excludeFile(detour + QStringLiteral("/ONE.BIN")));
    QVERIFY(!shared.shouldBeShared(shareDir, path, false));
    QCOMPARE(shared.m_singleExcludedFiles.size(), 5001);

    // one snapshot answers many questions
    const SharedFileList::ShareRules rules = shared.shareRules();
    QVERIFY(rules.sharedDirs.contains(SharedFileList::pathKey(shareDir)));
    QVERIFY(!shared.shouldBeShared(rules, shareDir, path, false));
    QVERIFY(shared.shouldBeShared(rules, shareDir, shareDir + QStringLiteral("/other.bin"), false));

    // un-excluding by yet another spelling
    QVERIFY(shared.addSingleSharedFile(path.toLower()));
    QVERIFY(shared.shouldBeShared(shareDir, path, false));
}

void tst_SharedFileList::directoryIndex_followsAddRemoveAndMove()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    KnownFile* a = makeFile(knownFiles, 0xD1, QStringLiteral("a.bin"));
    KnownFile* b = makeFile(knownFiles, 0xD2, QStringLiteral("b.bin"));
    KnownFile* c = makeFile(knownFiles, 0xD3, QStringLiteral("c.bin"));
    a->setPath(QStringLiteral("/share/one"));
    b->setPath(QStringLiteral("/share/one"));
    c->setPath(QStringLiteral("/somewhere/else"));
    c->setSharedDirectory(QStringLiteral("/linked"));   // what a peer is shown
    for (KnownFile* f : {a, b, c})
        QVERIFY(shared.safeAddKFile(f));

    auto dirs = shared.sharedDirectories();
    std::ranges::sort(dirs);
    QCOMPARE(dirs, (std::vector<QString>{QStringLiteral("/linked"), QStringLiteral("/share/one")}));
    QCOMPARE(shared.filesInDirectory(QStringLiteral("/share/one")).size(), size_t{2});
    QCOMPARE(shared.filesInDirectory(QStringLiteral("/linked")), std::vector<KnownFile*>{c});
    QVERIFY(shared.filesInDirectory(QStringLiteral("/somewhere/else")).empty());
    QVERIFY(shared.filesInDirectory(QStringLiteral("/SHARE/ONE")).empty());   // exact, as before

    // a completed download moves; nothing else re-files it
    b->setPath(QStringLiteral("/incoming"));
    shared.refreshDirectoryOf(b);
    QCOMPARE(shared.filesInDirectory(QStringLiteral("/share/one")), std::vector<KnownFile*>{a});
    QCOMPARE(shared.filesInDirectory(QStringLiteral("/incoming")), std::vector<KnownFile*>{b});

    QVERIFY(shared.removeFile(a));
    QVERIFY(shared.filesInDirectory(QStringLiteral("/share/one")).empty());
    QCOMPARE(shared.sharedDirectories().size(), size_t{2});

    shared.reload();   // nothing on disk: everything goes
    QVERIFY(shared.sharedDirectories().empty());
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

void tst_SharedFileList::excludeFile_refusedForCategoryIncomingDir()
{
    eMule::testing::TempDir tmp;
    const QString incoming = tmp.filePath(QStringLiteral("incoming"));
    const QString movies = tmp.filePath(QStringLiteral("movies"));
    const QString path = writeFile(movies, QStringLiteral("film.bin"), QByteArray(256, 'm'));
    QVERIFY(!path.isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(incoming);
    thePrefs.setSharedDirs({});

    eMule::DownloadCategory all;
    all.title = QStringLiteral("All");
    eMule::DownloadCategory cat;
    cat.title = QStringLiteral("Movies");
    cat.incomingPath = movies;
    thePrefs.setCategories({all, cat});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // A category's incoming directory is where its completed downloads land, so
    // it carries the same rule as the global one: always shared, never
    // unshareable (MFC ShouldBeShared:1394).
    QVERIFY(shared.shouldBeShared(movies, path, false));
    QVERIFY2(shared.shouldBeShared(movies, path, /*mustBeShared=*/true),
             "a category incoming directory is shared unconditionally");
    QVERIFY2(!shared.excludeFile(path),
             "a file in a category incoming directory cannot be unshared");

    // Dropping the category's folder gives the directory back its ordinary
    // status — it is no longer somewhere downloads arrive.
    thePrefs.setCategories({all});
    QVERIFY(!shared.shouldBeShared(movies, path, false));
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

        // The folder tree's bold marks: one entry per folder, however many files,
        // and none for a folder that only holds an excluded file.
        const QString second = writeFile(outside, QStringLiteral("second.bin"),
                                         QByteArray(256, 't'));
        QVERIFY(shared.addSingleSharedFile(second));
        const QStringList singleDirs = shared.singleSharedDirs();
        QCOMPARE(singleDirs.size(), 1);
        QCOMPARE(SharedFileList::pathKey(singleDirs.first()), SharedFileList::pathKey(outside));
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

// R10: a download still being copied in from another volume is not shared.
void tst_SharedFileList::scan_skipsAFileStillBeingDelivered()
{
    eMule::testing::TempDir tmp;
    const QString shareDir = tmp.filePath(QStringLiteral("share"));
    QVERIFY(!writeFile(shareDir, QStringLiteral("movie.bin") + Preferences::kCompletingSuffix,
                       QByteArray(512, 'c')).isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({shareDir});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.reload();

    QCOMPARE(shared.getCount(), 0);
    QCOMPARE(shared.getHashingCount(), 0);
}

void tst_SharedFileList::scan_skipsThumbsDbAndOversizedFiles()
{
    // MFC CheckAndAddSingleFile: no file over MAX_EMULE_FILE_SIZE, no thumbs.db.
    QVERIFY(SharedFileList::isShareableFile(QStringLiteral("a.bin"), MAX_EMULE_FILE_SIZE));
    QVERIFY(!SharedFileList::isShareableFile(QStringLiteral("a.bin"), MAX_EMULE_FILE_SIZE + 1));
    QVERIFY(!SharedFileList::isShareableFile(QStringLiteral("a.bin"), 0));
    QVERIFY(!SharedFileList::isShareableFile(QStringLiteral("Thumbs.db"), 512));

    eMule::testing::TempDir tmp;
    const QString shareDir = tmp.filePath(QStringLiteral("share"));
    QVERIFY(!writeFile(shareDir, QStringLiteral("Thumbs.db"), QByteArray(512, 't')).isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({shareDir});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.reload();

    QCOMPARE(shared.getCount(), 0);
    QCOMPARE(shared.getHashingCount(), 0);
}

// MFC SharedFileList.cpp:1474: system and temporary files stay out, hidden ones are shared.
void tst_SharedFileList::scan_winSkipsSystemAndTemporarySharesHidden()
{
#ifndef Q_OS_WIN
    QSKIP("file attributes are a Windows thing");
#else
    eMule::testing::TempDir tmp;
    const QString shareDir = tmp.filePath(QStringLiteral("share"));
    const auto putWithAttr = [&](const QString& name, DWORD attr) {
        const QString path = writeFile(shareDir, name, QByteArray(512, 'w'));
        return !path.isEmpty()
            && SetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
                                  attr);
    };
    QVERIFY(putWithAttr(QStringLiteral("system.bin"), FILE_ATTRIBUTE_SYSTEM));
    QVERIFY(putWithAttr(QStringLiteral("temporary.bin"), FILE_ATTRIBUTE_TEMPORARY));
    QVERIFY(putWithAttr(QStringLiteral("hidden.bin"), FILE_ATTRIBUTE_HIDDEN));

    QVERIFY(!SharedFileList::isShareableFile(QFileInfo(QDir(shareDir).filePath(QStringLiteral("system.bin")))));
    QVERIFY(!SharedFileList::isShareableFile(QFileInfo(QDir(shareDir).filePath(QStringLiteral("temporary.bin")))));
    QVERIFY(SharedFileList::isShareableFile(QFileInfo(QDir(shareDir).filePath(QStringLiteral("hidden.bin")))));

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({shareDir});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.reload();
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 1, 10000);
    QCOMPARE(shared.getHashingCount(), 0);
    bool hiddenShared = false;
    shared.forEachFile([&](KnownFile* f) { hiddenShared |= f->fileName() == QStringLiteral("hidden.bin"); });
    QVERIFY(hiddenShared);
#endif
}

// MFC SharedFileList.cpp:1488-1524: a .lnk is never shared itself; its target only
// with the option on.
void tst_SharedFileList::scan_winShellLinkFollowsTheOption()
{
#ifndef Q_OS_WIN
    QSKIP("shell links are a Windows thing");
#else
    eMule::testing::TempDir tmp;
    const QString shareDir = tmp.filePath(QStringLiteral("share"));
    QVERIFY(QDir().mkpath(shareDir));
    const QString target = writeFile(tmp.filePath(QStringLiteral("elsewhere")),
                                     QStringLiteral("target.bin"), QByteArray(777, 'l'));
    QVERIFY(!target.isEmpty());
    const QString link = QDir(shareDir).filePath(QStringLiteral("to target.lnk"));
    QVERIFY(QFile::link(target, link));
    QVERIFY(QFileInfo(link).isShortcut());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({shareDir});
    const bool before = thePrefs.resolveShellLinks();

    {
        thePrefs.setResolveShellLinks(false);
        KnownFileList knownFiles;
        SharedFileList shared(&knownFiles);
        shared.reload();
        QTest::qWait(300);
        QCOMPARE(shared.getCount(), 0);
        QCOMPARE(shared.getHashingCount(), 0);
    }
    {
        thePrefs.setResolveShellLinks(true);
        KnownFileList knownFiles;
        SharedFileList shared(&knownFiles);
        shared.reload();
        QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 1, 10000);
        KnownFile* found = nullptr;
        shared.forEachFile([&](KnownFile* f) { found = f; });
        QVERIFY(found);
        QCOMPARE(found->fileName(), QStringLiteral("target.bin"));
        QCOMPARE(found->fileSize(), uint64{777});
        QCOMPARE(QFileInfo(found->filePath()), QFileInfo(target));
    }
    thePrefs.setResolveShellLinks(before);
#endif
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

namespace {

/// Share dir + config in a temp dir, nothing else shared.
struct ShareEnv {
    eMule::testing::TempDir tmp;
    QString shareDir = tmp.filePath(QStringLiteral("share"));
    ShareEnv()
    {
        QDir().mkpath(shareDir);
        thePrefs.setConfigDir(tmp.path());
        thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
        thePrefs.setSharedDirs({shareDir});
    }
    QString put(const QString& name, const QByteArray& content) const
    {
        return writeFile(shareDir, name, content);
    }
};

KnownFile* sharedAt(const SharedFileList& shared, const QString& path)
{
    KnownFile* found = nullptr;
    shared.forEachFile([&](KnownFile* f) {
        if (f->filePath().compare(path, Qt::CaseInsensitive) == 0)
            found = f;
    });
    return found;
}

} // namespace

namespace {

PublishKeyword* keywordNamed(PublishKeywordList& list, const QString& word)
{
    list.resetNextKeyword();
    PublishKeyword* found = nullptr;
    while (PublishKeyword* kw = list.getNextKeyword())
        if (kw->keyword().compare(word, Qt::CaseInsensitive) == 0)
            found = kw;
    list.resetNextKeyword();
    return found;
}

} // namespace

// Keyword publish times used to live in memory only: every start published the
// whole share again.
void tst_SharedFileList::kadPublishTimes_surviveARestartUntilTheFilesChange()
{
    ShareEnv env;
    env.put(QStringLiteral("holiday alpha.bin"), QByteArray(700, 'a'));
    env.put(QStringLiteral("winter beta.bin"), QByteArray(900, 'b'));
    const QString storePath = env.tmp.filePath(QStringLiteral("kadpublish.dat"));
    const time_t now = std::time(nullptr);

    KnownFileList knownFiles;
    {
        SharedFileList shared(&knownFiles);
        shared.setKadPublishStorePath(storePath);
        shared.reload();
        QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 2, 10000);

        PublishKeyword* holiday = keywordNamed(shared.m_keywords, QStringLiteral("holiday"));
        PublishKeyword* winter = keywordNamed(shared.m_keywords, QStringLiteral("winter"));
        QVERIFY(holiday && winter);
        QVERIFY(shared.keywordIsDue(*holiday, now));
        shared.noteKeywordPublished(*holiday, now, true);
        shared.noteKeywordPublished(*winter, now, true);
        QVERIFY(!shared.keywordIsDue(*holiday, now + 60));
    }   // saved on the way out
    QVERIFY(QFile::exists(storePath));

    // Same files after a restart: nothing is due.
    {
        SharedFileList shared(&knownFiles);
        shared.setKadPublishStorePath(storePath);
        shared.reload();
        QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 2, 10000);

        PublishKeyword* holiday = keywordNamed(shared.m_keywords, QStringLiteral("holiday"));
        QVERIFY(holiday);
        QCOMPARE(holiday->nextPublishTime(), time_t{0});
        QVERIFY(!shared.keywordIsDue(*holiday, now + 60));
        QVERIFY(shared.keywordIsDue(*holiday, now + KADEMLIAREPUBLISHTIMEK + 1));
    }

    // A file added while we were off shares one keyword: that one is due, the
    // other is not.
    env.put(QStringLiteral("holiday gamma.bin"), QByteArray(800, 'c'));
    {
        SharedFileList shared(&knownFiles);
        shared.setKadPublishStorePath(storePath);
        shared.reload();
        QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 3, 10000);

        PublishKeyword* holiday = keywordNamed(shared.m_keywords, QStringLiteral("holiday"));
        PublishKeyword* winter = keywordNamed(shared.m_keywords, QStringLiteral("winter"));
        QVERIFY(holiday && winter);
        QVERIFY(shared.keywordIsDue(*holiday, now + 60));
        QVERIFY(!shared.keywordIsDue(*winter, now + 60));
    }
}

void tst_SharedFileList::kadPublishStore_roundTripAndDamage()
{
    eMule::testing::TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("kadpublish.dat"));
    const time_t now = 1'000'000;

    uint8 rawA[16]{1}, rawB[16]{2}, hash1[16]{0x11}, hash2[16]{0x22};
    const kad::UInt128 a(rawA), b(rawB);
    KadPublishStore::Fingerprint one{}, both{};
    KadPublishStore::mix(one, hash1);
    KadPublishStore::mix(both, hash1);
    KadPublishStore::mix(both, hash2);

    KadPublishStore store;
    store.load(path);                       // no file: empty, no complaint
    QCOMPARE(store.count(), size_t{0});
    store.note(a, now + 500, one);
    store.note(b, now - 5, both);           // already due: not worth keeping
    QVERIFY(store.isDirty());
    QVERIFY(store.save(path, now));
    QVERIFY(!store.isDirty());

    KadPublishStore loaded;
    loaded.load(path);
    QCOMPARE(loaded.count(), size_t{1});
    QCOMPARE(loaded.dueTime(a, one), std::optional<time_t>(now + 500));
    QVERIFY(!loaded.dueTime(a, both));      // other files behind the keyword
    QVERIFY(!loaded.dueTime(b, both));

    // Order of the files does not matter.
    KadPublishStore::Fingerprint swapped{};
    KadPublishStore::mix(swapped, hash2);
    KadPublishStore::mix(swapped, hash1);
    QCOMPARE(swapped, both);

    // A cut-off file reads as nothing rather than as half a list.
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(file.size() - 7));
    file.close();
    loaded.load(path);
    QCOMPARE(loaded.count(), size_t{0});
}

void tst_SharedFileList::reload_leavesUnchangedFilesAlone()
{
    ShareEnv env;
    const QString a = env.put(QStringLiteral("alpha one.bin"), QByteArray(700, 'a'));
    const QString b = env.put(QStringLiteral("beta two.bin"), QByteArray(900, 'b'));

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.reload();
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 2, 10000);
    KnownFile* const fa = sharedAt(shared, a);
    KnownFile* const fb = sharedAt(shared, b);
    QVERIFY(fa && fb);
    fa->setPublishedED2K(true);

    QSignalSpy added(&shared, &SharedFileList::fileAdded);
    QSignalSpy removed(&shared, &SharedFileList::fileRemoved);
    const int keywords = shared.m_keywords.keywordCount();
    shared.reload();
    QTest::qWait(200);

    QCOMPARE(added.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(sharedAt(shared, a), fa);
    QCOMPARE(sharedAt(shared, b), fb);
    QVERIFY2(fa->publishedED2K(), "an untouched file keeps its publish state");
    QCOMPARE(shared.m_keywords.keywordCount(), keywords);
    QCOMPARE(shared.getHashingCount(), 0);
}

// Issue #9: on FAT a DST switch moves every file date by an hour, and the whole share
// was read again. The record keeps its hash and takes the date the disk reports now.
void tst_SharedFileList::reload_aShiftedDateOnALocalTimeVolumeIsNotAChangedFile()
{
    ShareEnv env;
    const QString a = env.put(QStringLiteral("gamma three.bin"), QByteArray(700, 'g'));

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.reload();
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 1, 10000);
    KnownFile* const file = sharedAt(shared, a);
    QVERIFY(file);
    const time_t hashedDate = file->utcFileDate();

    const auto shiftBy = [&a](qint64 secs) {
        QFile f(a);
        return f.open(QIODevice::ReadWrite)
            && f.setFileTime(QFileInfo(a).lastModified().addSecs(secs),
                             QFileDevice::FileModificationTime);
    };
    struct ProbeReset {
        ~ProbeReset() { setLocalTimeVolumeProbe({}); }
    } const probeReset;

    setLocalTimeVolumeProbe([](const QString&) { return true; });
    QVERIFY(shiftBy(3600));
    QSignalSpy added(&shared, &SharedFileList::fileAdded);
    QSignalSpy removed(&shared, &SharedFileList::fileRemoved);
    shared.reload();
    QTest::qWait(200);

    QCOMPARE(removed.count(), 0);
    QCOMPARE(added.count(), 0);
    QCOMPARE(shared.getHashingCount(), 0);
    QCOMPARE(sharedAt(shared, a), file);
    QCOMPARE(file->utcFileDate(), hashedDate + 3600);
    QVERIFY2(knownFiles.isDirty(), "the adopted date has to reach known.met");

    // Anywhere else an hour's difference is a changed file, as before.
    setLocalTimeVolumeProbe([](const QString&) { return false; });
    QVERIFY(shiftBy(3600));
    shared.reload();
    QTRY_VERIFY_WITH_TIMEOUT(removed.count() == 1, 10000);
}

void tst_SharedFileList::reload_followsDeleteChangeAndRename()
{
    ShareEnv env;
    const QString gone = env.put(QStringLiteral("gone.bin"), QByteArray(500, 'g'));
    const QString grown = env.put(QStringLiteral("grown.bin"), QByteArray(600, 'c'));
    const QString before = env.put(QStringLiteral("before name.bin"), QByteArray(800, 'r'));

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.reload();
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 3, 10000);
    KnownFile* const renamed = sharedAt(shared, before);
    QVERIFY(renamed);
    const QByteArray renamedHash(reinterpret_cast<const char*>(renamed->fileHash()), 16);
    const QByteArray grownHash(reinterpret_cast<const char*>(sharedAt(shared, grown)->fileHash()), 16);

    QVERIFY(QFile::remove(gone));
    QVERIFY(!env.put(QStringLiteral("grown.bin"), QByteArray(650, 'c')).isEmpty());
    const QString after = QDir(env.shareDir).filePath(QStringLiteral("after name.bin"));
    QVERIFY(QFile::rename(before, after));

    QSignalSpy removed(&shared, &SharedFileList::fileRemoved);
    QSignalSpy relocated(&shared, &SharedFileList::fileRelocated);
    shared.rescanDirectory(env.shareDir);

    // deleted: gone at once
    QVERIFY(!sharedAt(shared, gone));
    // renamed: the same object, no hashing, new name and keywords
    QCOMPARE(relocated.count(), 1);
    QCOMPARE(sharedAt(shared, after), renamed);
    QCOMPARE(renamed->fileName(), QStringLiteral("after name.bin"));
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(renamed->fileHash()), 16), renamedHash);
    bool hasAfter = false, hasBefore = false;
    shared.m_keywords.resetNextKeyword();
    while (PublishKeyword* kw = shared.m_keywords.getNextKeyword()) {
        hasAfter |= kw->keyword() == QStringLiteral("after");
        hasBefore |= kw->keyword() == QStringLiteral("before");
    }
    QVERIFY(hasAfter && !hasBefore);
    // changed: out now, back with a new hash once hashed
    QCOMPARE(removed.count(), 2);
    QTRY_VERIFY_WITH_TIMEOUT(sharedAt(shared, grown) != nullptr, 10000);
    QVERIFY(QByteArray(reinterpret_cast<const char*>(sharedAt(shared, grown)->fileHash()), 16) != grownHash);
    QCOMPARE(shared.getCount(), 2);
}

void tst_SharedFileList::reload_doesNotHashAFileTwice()
{
    ShareEnv env;
    for (int i = 0; i < 40; ++i)
        QVERIFY(!env.put(QStringLiteral("file %1.bin").arg(i), QByteArray(2000 + i, char('a' + i % 20))).isEmpty());

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    QSignalSpy added(&shared, &SharedFileList::fileAdded);
    shared.reload();
    // while the first ones are on the worker or done, and the rest still wait
    for (int i = 0; i < 5; ++i) {
        QTest::qWait(1);
        shared.reload();
    }
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 40, 20000);
    QTest::qWait(200);
    QCOMPARE(added.count(), 40);
    QCOMPARE(shared.getHashingCount(), 0);
}

void tst_SharedFileList::hashFailure_isRetriedThenRememberedUntilTheFileChanges()
{
#ifdef Q_OS_WIN
    QSKIP("needs POSIX permissions to make a file unreadable");
#endif
    ShareEnv env;
    const QString locked = env.put(QStringLiteral("locked.bin"), QByteArray(400, 'l'));
    QVERIFY(QFile::setPermissions(locked, QFileDevice::Permissions{}));
    if (QFile(locked).open(QIODevice::ReadOnly))
        QSKIP("running as a user who can read anything");

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.m_hashRetrySecs = {0, 0, 0};
    const QString key = SharedFileList::pathKey(locked);

    shared.reload();
    QTRY_VERIFY_WITH_TIMEOUT(shared.m_hashFailures.contains(key), 5000);
    // three more tries, each started by the tick
    for (int i = 0; i < 200 && !shared.m_hashFailures.value(key).givenUp; ++i) {
        shared.process();
        QTest::qWait(20);
    }
    QVERIFY(shared.m_hashFailures.value(key).givenUp);
    QCOMPARE(shared.m_hashFailures.value(key).attempts, 3);

    // remembered: neither the tick nor a reload queues it again
    shared.process();
    shared.reload();
    QCOMPARE(shared.getHashingCount(), 0);
    QCOMPARE(shared.getCount(), 0);

    // readable, and changed: hashed
    QVERIFY(QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY(!env.put(QStringLiteral("locked.bin"), QByteArray(450, 'l')).isEmpty());
    shared.reload();
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 1, 10000);
    QVERIFY(!shared.m_hashFailures.contains(key));
}

void tst_SharedFileList::hashing_aFileChangedSinceTheScanIsNotAFailure()
{
    ShareEnv env;
    const QString path = env.put(QStringLiteral("moving.bin"), QByteArray(500, 'm'));
    const QFileInfo fi(path);

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    const QString key = SharedFileList::pathKey(path);
    {
        // As if the scan had seen it 100 bytes shorter.
        QMutexLocker locker(&shared.m_hashMutex);
        shared.queueForHash({fi.absolutePath(), fi.fileName(), {}, key, 400,
                             static_cast<time_t>(fi.lastModified().toSecsSinceEpoch()),
                             shared.volumeKeyFor(fi.absolutePath())});
        shared.hashNextFile();
    }
    QCOMPARE(shared.getHashingCount(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(shared.getHashingCount(), 0, 5000);

    QCOMPARE(shared.getCount(), 0);
    QVERIFY2(!shared.m_hashFailures.contains(key), "a file that moved on is not a failed one");
    QVERIFY(shared.m_settleDirs.contains(fi.absolutePath()));

    // The next look at the directory takes it as it is now.
    shared.rescanDirectory(fi.absolutePath());
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 1, 10000);
}

void tst_SharedFileList::hashFailure_givenUpSurvivesARestart()
{
    ShareEnv env;
    const QString bad = env.put(QStringLiteral("bad.bin"), QByteArray(400, 'b'));
    const QString storePath = env.tmp.filePath(QStringLiteral("hashfailures.dat"));
    const QString key = SharedFileList::pathKey(bad);
    const QFileInfo fi(bad);

    KnownFileList knownFiles;
    {
        SharedFileList shared(&knownFiles);
        shared.setHashFailureStorePath(storePath);
        // Given up on, as after the retry ladder.
        auto& failure = shared.m_hashFailures[key];
        failure.entry = {fi.absolutePath(), fi.fileName(), {}, key};
        failure.size = static_cast<uint64>(fi.size());
        failure.mtime = static_cast<time_t>(fi.lastModified().toSecsSinceEpoch());
        failure.attempts = 3;
        failure.givenUp = true;
        // One still on the ladder is not worth keeping.
        auto& retrying = shared.m_hashFailures[QStringLiteral("/elsewhere/x.bin")];
        retrying.entry = {QStringLiteral("/elsewhere"), QStringLiteral("x.bin"), {},
                          QStringLiteral("/elsewhere/x.bin")};
        retrying.retryAt = std::time(nullptr) + 3600;
    }   // saved on the way out
    QCOMPARE(HashFailureFile::read(storePath).size(), size_t{1});

    {
        SharedFileList shared(&knownFiles);
        shared.setHashFailureStorePath(storePath);
        QVERIFY(shared.m_hashFailures.value(key).givenUp);
        shared.reload();
        QCOMPARE(shared.getHashingCount(), 0);     // left alone
        QTest::qWait(100);
        QCOMPARE(shared.getCount(), 0);

        // Changed: worth another look.
        QVERIFY(!env.put(QStringLiteral("bad.bin"), QByteArray(450, 'b')).isEmpty());
        shared.reload();
        QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 1, 10000);
        QVERIFY(!shared.m_hashFailures.contains(key));
    }
    QCOMPARE(HashFailureFile::read(storePath).size(), size_t{0});
}

void tst_SharedFileList::hashOrder_coldStartTakesTheSmallFilesFirst()
{
    const auto entry = [](const char* name, uint64 size, const char* volume = "/") {
        const QString file = QString::fromLatin1(name);
        return UnknownFileEntry{QStringLiteral("/s"), file, {}, QStringLiteral("/s/") + file,
                                size, 1, QString::fromLatin1(volume)};
    };
    const auto names = [](const std::vector<UnknownFileEntry>& entries) {
        QStringList out;
        for (const UnknownFileEntry& e : entries)
            out << e.filename;
        return out.join(u' ');
    };
    const uint64 mib = 1024 * 1024;

    // A share with a history: path order within a volume, nothing else.
    std::vector<UnknownFileEntry> entries{entry("c", 5), entry("a", 900), entry("b", 1, "/vol2"),
                                          entry("d", 3)};
    SharedFileList::orderForHashing(entries, false);
    QCOMPARE(names(entries), QStringLiteral("a c d b"));

    // Nothing hashed yet: smallest first, across volumes.
    SharedFileList::orderForHashing(entries, true);
    QCOMPARE(names(entries), QStringLiteral("b d c a"));

    // The early group stops at 256 MiB; what is left keeps path order.
    std::vector<UnknownFileEntry> big{entry("z-small", 1 * mib), entry("a-huge", 400 * mib),
                                      entry("m-mid", 200 * mib), entry("b-large", 100 * mib)};
    SharedFileList::orderForHashing(big, true);
    QCOMPARE(names(big), QStringLiteral("z-small b-large a-huge m-mid"));

    // ... and at 200 files.
    std::vector<UnknownFileEntry> many;
    for (int i = 0; i < 260; ++i)
        many.push_back(entry(qPrintable(QStringLiteral("f%1").arg(i, 3, 10, QLatin1Char('0'))),
                             static_cast<uint64>(1000 - i)));
    SharedFileList::orderForHashing(many, true);
    QCOMPARE(many.front().filename, QStringLiteral("f259"));      // the smallest
    QCOMPARE(many[199].filename, QStringLiteral("f060"));
    QCOMPARE(many[200].filename, QStringLiteral("f000"));         // then by path
}

// Two files on one disk only slow each other down; two disks can work at once.
void tst_SharedFileList::hashing_volumesRunSideBySideOneVolumeInSequence()
{
    eMule::testing::TempDir tmp;
    const QString dirA = tmp.filePath(QStringLiteral("diskA"));
    const QString dirB = tmp.filePath(QStringLiteral("diskB"));
    for (int i = 0; i < 3; ++i) {
        QVERIFY(!writeFile(dirA, QStringLiteral("a%1.bin").arg(i), QByteArray(600 + i, 'a')).isEmpty());
        QVERIFY(!writeFile(dirB, QStringLiteral("b%1.bin").arg(i), QByteArray(700 + i, 'b')).isEmpty());
    }
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({dirA, dirB});

    {
        KnownFileList knownFiles;
        SharedFileList shared(&knownFiles);
        shared.m_volumeKeyFn = [](const QString& dir) { return QFileInfo(dir).fileName(); };
        shared.reload();   // dispatches before any worker result is delivered

        QCOMPARE(shared.m_hashWorkers.size(), size_t{2});
        QCOMPARE(shared.m_hashing.size(), 2);
        QSet<QString> volumes;
        QSet<HashingThread*> workers;
        for (const auto& h : std::as_const(shared.m_hashing)) {
            volumes.insert(h.entry.volume);
            workers.insert(h.worker);
        }
        QCOMPARE(volumes.size(), 2);
        QCOMPARE(workers.size(), 2);
        QCOMPARE(shared.getHashingCount(), 6);
        QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 6, 10000);
        QCOMPARE(shared.getHashingCount(), 0);
    }

    // Everything on one volume: one file at a time.
    {
        KnownFileList knownFiles;
        SharedFileList shared(&knownFiles);
        shared.m_volumeKeyFn = [](const QString&) { return QStringLiteral("one"); };
        shared.reload();
        QCOMPARE(shared.m_hashWorkers.size(), size_t{1});
        QCOMPARE(shared.m_hashing.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 6, 10000);
    }
}

// The watcher reports both directories of a move in one batch; seen one at a time
// the file was unshared in the first rescan and re-added in the second.
void tst_SharedFileList::moveBetweenSharedDirs_keepsTheFileAndHashesNothing_data()
{
    // Which directories the watcher reports, in which order: 'a' is where the file
    // was, 'b' where it went. The two ends often arrive apart.
    QTest::addColumn<QStringList>("reports");
    QTest::newRow("both at once") << QStringList{QStringLiteral("a+b")};
    QTest::newRow("old home only") << QStringList{QStringLiteral("a")};
    QTest::newRow("old home first") << QStringList{QStringLiteral("a"), QStringLiteral("b")};
    QTest::newRow("new home first") << QStringList{QStringLiteral("b"), QStringLiteral("a")};
}

void tst_SharedFileList::moveBetweenSharedDirs_keepsTheFileAndHashesNothing()
{
    QFETCH(QStringList, reports);

    eMule::testing::TempDir tmp;
    const QString dirA = tmp.filePath(QStringLiteral("a"));
    const QString dirB = tmp.filePath(QStringLiteral("b"));
    QDir().mkpath(dirB);
    const QString before = writeFile(dirA, QStringLiteral("travels.bin"), QByteArray(900, 't'));
    QVERIFY(!writeFile(dirA, QStringLiteral("stays.bin"), QByteArray(700, 's')).isEmpty());
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({dirA, dirB});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.reload();
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 2, 10000);
    KnownFile* const file = sharedAt(shared, before);
    QVERIFY(file);

    const QString after = QDir(dirB).filePath(QStringLiteral("travels.bin"));
    QVERIFY(QFile::rename(before, after));

    QSignalSpy removed(&shared, &SharedFileList::fileRemoved);
    QSignalSpy added(&shared, &SharedFileList::fileAdded);
    QSignalSpy relocated(&shared, &SharedFileList::fileRelocated);
    for (const QString& report : reports) {
        if (report == QLatin1String("a+b"))
            shared.rescanDirectories({dirA, dirB});
        else
            shared.rescanDirectories({report == QLatin1String("a") ? dirA : dirB});
    }

    QCOMPARE(removed.count(), 0);
    QCOMPARE(added.count(), 0);
    QCOMPARE(relocated.count(), 1);
    QCOMPARE(sharedAt(shared, after), file);
    QCOMPARE(shared.getCount(), 2);
    QCOMPARE(shared.getHashingCount(), 0);
}

void tst_SharedFileList::singleSharedFile_itsDirectoryIsWatchedAndRescanned()
{
    ShareEnv env;
    const QString outside = env.tmp.filePath(QStringLiteral("elsewhere"));
    const QString single = writeFile(outside, QStringLiteral("lonely.bin"), QByteArray(600, 'l'));
    QVERIFY(!writeFile(outside, QStringLiteral("neighbour.bin"), QByteArray(650, 'n')).isEmpty());

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.setWatchingEnabled(true);
    shared.m_settleSecs = 0;
    QVERIFY(!shared.watcher()->roots().contains(outside));

    QVERIFY(shared.addSingleSharedFile(single));
    QVERIFY2(shared.watcher()->roots().contains(outside),
             "the directory of a file shared by itself is watched");
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 1, 10000);
    KnownFile* const first = sharedAt(shared, single);
    QVERIFY(first);
    const QByteArray firstHash(reinterpret_cast<const char*>(first->fileHash()), 16);

    // Rewritten in place: what the watcher's report leads to. Only that file is
    // looked at — its neighbour is not shared and stays out.
    QVERIFY(!writeFile(outside, QStringLiteral("lonely.bin"), QByteArray(640, 'L')).isEmpty());
    shared.rescanDirectories({outside});
    QTRY_VERIFY_WITH_TIMEOUT(sharedAt(shared, single) != nullptr
                                 && QByteArray(reinterpret_cast<const char*>(
                                        sharedAt(shared, single)->fileHash()), 16) != firstHash,
                             10000);
    QCOMPARE(shared.getCount(), 1);

    QVERIFY(shared.excludeFile(single));
    QVERIFY(!shared.watcher()->roots().contains(outside));
}

// A library hashed before the extractor stamped its work (or by an older one) is
// read again, a slice per tick — a freshly hashed file is not.
void tst_SharedFileList::staleMediaStamp_isBroughtUpToDateInTheBackground()
{
    ShareEnv env;
    env.put(QStringLiteral("one.bin"), QByteArray(600, '1'));
    env.put(QStringLiteral("two.bin"), QByteArray(700, '2'));

    KnownFileList knownFiles;
    {
        SharedFileList shared(&knownFiles);
        shared.reload();
        QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 2, 10000);
        QVERIFY2(shared.m_metaRebuildQueue.empty(), "just hashed: already up to date");

        // As loaded from an older known.met.
        int stale = 0;
        shared.forEachFile([&](KnownFile* f) {
            f->setMediaExtractVer(0);
            ++stale;
        });
        QCOMPARE(stale, 2);
    }

    SharedFileList shared(&knownFiles);
    shared.reload();
    QCOMPARE(shared.getCount(), 2);
    QCOMPARE(shared.m_metaRebuildQueue.size(), size_t{2});
    for (int i = 0; i < 100 && !shared.m_metaRebuildQueue.empty(); ++i)
        shared.process();
    QVERIFY(shared.m_metaRebuildQueue.empty());
    shared.forEachFile([](KnownFile* f) { QVERIFY(!f->mediaExtractIsStale()); });
}

void tst_SharedFileList::freshFile_isHeldBackWhileWatching()
{
    ShareEnv env;
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.m_settleSecs = 5;

    const QString fresh = env.put(QStringLiteral("fresh.bin"), QByteArray(300, 'f'));
    const QString old = env.put(QStringLiteral("old.bin"), QByteArray(310, 'o'));
    {
        QFile f(old);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
    }

    shared.reload();
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 1, 10000);
    QVERIFY(sharedAt(shared, old));
    QVERIFY2(shared.m_settleDirs.contains(env.shareDir), "the directory is looked at again later");

    // the later look, once the file has been quiet long enough
    {
        QFile f(fresh);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-60), QFileDevice::FileModificationTime));
    }
    shared.m_settleDirs[env.shareDir] = 0;
    shared.process();
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 2, 10000);
    QVERIFY(shared.m_settleDirs.isEmpty());
}

void tst_SharedFileList::watching_picksUpACopiedInFileWithoutAReload()
{
    ShareEnv env;
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.reload();
    shared.setWatchingEnabled(true);
    shared.m_settleSecs = 0;
    QVERIFY(shared.watcher());
    shared.watcher()->setTimings(100, 1000, 200);
    QCOMPARE(shared.watcher()->roots().size(), 2);   // incoming + share

    const QString path = env.put(QStringLiteral("dropped in.bin"), QByteArray(1234, 'd'));
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 1, 15000);
    QVERIFY(sharedAt(shared, path));

    QVERIFY(QFile::remove(path));
    QTRY_COMPARE_WITH_TIMEOUT(shared.getCount(), 0, 15000);

    // a directory that stops being shared stops being watched
    thePrefs.setSharedDirs({});
    shared.reload();
    QCOMPARE(shared.watcher()->roots().size(), 1);
}

// MFC Reload() (srchybrid/SharedFileList.cpp:784-796). The map is emptied wholesale, so
// without this the keyword refs of everything that was shared stay behind for good.
void tst_SharedFileList::reload_dropsKeywordsOfFilesNoLongerShared()
{
    eMule::testing::TempDir tmp;
    const QString shareDir = tmp.filePath(QStringLiteral("share"));
    QVERIFY(!writeFile(shareDir, QStringLiteral("staying.bin"), QByteArray(512, 's')).isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({shareDir});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    auto makeKnown = [&](const QString& name, uint8 pattern, bool onDisk) {
        auto* known = new KnownFile();
        uint8 hash[16];
        std::memset(hash, pattern, 16);
        known->setFileHash(hash);
        known->setFileName(name);
        if (onDisk) {
            const QFileInfo fi(QDir(shareDir).filePath(name));
            known->setFileSize(static_cast<uint64>(fi.size()));
            known->setUtcFileDate(static_cast<time_t>(fi.lastModified().toSecsSinceEpoch()));
            known->setFilePath(fi.absoluteFilePath());
            known->setPath(fi.absolutePath());
        } else {
            known->setFileSize(1000);
        }
        knownFiles.safeAddKFile(known);
        return known;
    };

    makeKnown(QStringLiteral("staying.bin"), 0xC1, true);
    KnownFile* gone = makeKnown(QStringLiteral("vanished.bin"), 0xC2, false);

    shared.reload();
    QVERIFY(shared.safeAddKFile(gone));   // shared by hand, in no shared directory
    QCOMPARE(shared.getCount(), 2);
    const int both = shared.m_keywords.keywordCount();
    QVERIFY(both >= 2);

    shared.reload();                      // the rescan does not find "vanished"
    QCOMPARE(shared.getCount(), 1);
    QVERIFY2(shared.m_keywords.keywordCount() < both,
             "keywords of a file that is no longer shared must be purged");

    PublishKeyword* kw = nullptr;
    shared.m_keywords.resetNextKeyword();
    while ((kw = shared.m_keywords.getNextKeyword()) != nullptr) {
        for (const KnownFile* f : kw->fileRefs())
            QVERIFY2(f != gone, "no keyword may still reference the dropped file");
    }
}

// MFC SafeAddKFile (srchybrid/KnownFileList.cpp:276-336). Replacing a known entry deletes
// the old object, so it has to leave the shared list, its keywords and its uploaders first.
void tst_SharedFileList::knownListReplace_unhooksTheSharedOldObject()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    theApp.sharedFileList = &shared;

    uint8 hash[16];
    std::memset(hash, 0xD1, 16);
    auto make = [&](const QString& name) {
        auto* f = new KnownFile();
        f->setFileHash(hash);
        f->setFileName(name);
        f->setFileSize(4096);
        return f;
    };

    KnownFile* older = make(QStringLiteral("older name.bin"));
    older->statistic.setAllTimeTransferred(700);
    QVERIFY(knownFiles.safeAddKFile(older));
    QVERIFY(shared.safeAddKFile(older));

    UpDownClient client;
    client.setUploadFileID(older);

    KnownFile* newer = make(QStringLiteral("newer name.bin"));
    newer->statistic.setAllTimeTransferred(50);
    QVERIFY(knownFiles.safeAddKFile(newer));   // deletes `older`

    QCOMPARE(shared.getFileByID(hash), newer);
    QCOMPARE(shared.getCount(), 1);
    QVERIFY2(client.uploadFile() == nullptr, "an uploader must not keep the deleted file");
    QCOMPARE(newer->statistic.allTimeTransferred(), uint64{750});
    QCOMPARE(knownFiles.totalTransferred, uint64{750});

    shared.m_keywords.resetNextKeyword();
    while (PublishKeyword* kw = shared.m_keywords.getNextKeyword()) {
        for (const KnownFile* f : kw->fileRefs())
            QCOMPARE(f, newer);
    }

    // Adding the same object again is a no-op, not a self-delete.
    QVERIFY(knownFiles.safeAddKFile(newer));
    QCOMPARE(shared.getFileByID(hash), newer);

    theApp.sharedFileList = nullptr;
}

// MFC FileHashingFinished (srchybrid/SharedFileList.cpp:732-765): the same content at a
// second path is dropped before the known list ever sees it.
void tst_SharedFileList::hashFinished_duplicateContentKeepsTheSharedFile()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    theApp.sharedFileList = &shared;

    uint8 hash[16];
    std::memset(hash, 0xD2, 16);
    auto* first = new KnownFile();
    first->setFileHash(hash);
    first->setFileName(QStringLiteral("first.bin"));
    first->setFileSize(4096);
    QVERIFY(knownFiles.safeAddKFile(first));
    QVERIFY(shared.safeAddKFile(first));

    auto* second = new KnownFile();
    second->setFileHash(hash);
    second->setFileName(QStringLiteral("second.bin"));
    second->setFileSize(4096);
    shared.onHashingFinished(second, shared.m_generation);

    QCOMPARE(shared.getFileByID(hash), first);
    QCOMPARE(knownFiles.findKnownFileByID(hash), first);
    QCOMPARE(first->fileName(), QStringLiteral("first.bin"));   // still alive and readable

    theApp.sharedFileList = nullptr;
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

// Archives and CD images are published as "Pro", and a server that takes integer types
// gets one. MFC SharedFileList.cpp:967-985.
void tst_SharedFileList::offer_usesThePublishedFileType()
{
    KnownFileList knownFiles;
    KnownFile* archive = makeFile(knownFiles, 0xD1, QStringLiteral("pack.zip"));
    KnownFile* video = makeFile(knownFiles, 0xD2, QStringLiteral("clip.avi"));
    KnownFile* coll = makeFile(knownFiles, 0xD3, QStringLiteral("set.emulecollection"));

    auto typeTag = [](const std::vector<Tag>& tags) -> const Tag* {
        for (const Tag& t : tags)
            if (t.nameId() == FT_FILETYPE)
                return &t;
        return nullptr;
    };

    Server plain(0x01020304u, 4661);
    QCOMPARE(typeTag(SharedFileList::offeredTags(*archive, &plain))->strValue(), QStringLiteral("Pro"));
    QCOMPARE(typeTag(SharedFileList::offeredTags(*video, &plain))->strValue(), QStringLiteral("Video"));

    Server typed(0x01020305u, 4661);
    typed.setTCPFlags(SrvTcpFlag::TypeTagInteger);
    const auto archiveTags = SharedFileList::offeredTags(*archive, &typed);
    QVERIFY(typeTag(archiveTags)->isInt());
    QCOMPARE(typeTag(archiveTags)->intValue(), uint32{4});   // Program
    QCOMPARE(typeTag(SharedFileList::offeredTags(*video, &typed))->intValue(), uint32{2});
    // No integer exists for a collection: it stays a string.
    QCOMPARE(typeTag(SharedFileList::offeredTags(*coll, &typed))->strValue(),
             QStringLiteral("EmuleCollection"));
}

// Length, bitrate and codec go to the server; without them no server can match the media
// constraints of a search. MFC SharedFileList.cpp:997-1056.
void tst_SharedFileList::offer_carriesTheMediaTagsServersCanSearch()
{
    KnownFileList knownFiles;
    KnownFile* file = makeFile(knownFiles, 0xC4, QStringLiteral("song.mp3"));
    file->addTagUnique(Tag(FT_MEDIA_LENGTH, uint32{3725}));
    file->addTagUnique(Tag(FT_MEDIA_BITRATE, uint32{192}));
    file->addTagUnique(Tag(FT_MEDIA_CODEC, QStringLiteral("mp3")));
    file->addTagUnique(Tag(FT_MEDIA_ARTIST, QStringLiteral("Somebody")));

    const auto find = [](const std::vector<Tag>& tags, uint8 id, const char* name) -> const Tag* {
        for (const Tag& t : tags) {
            if ((id != 0 && t.nameId() == id) || (name && t.name() == name))
                return &t;
        }
        return nullptr;
    };

    Server modern(0x01020304u, 4661);
    modern.setTCPFlags(SrvTcpFlag::NewTags | SrvTcpFlag::Compression);

    // No version, no media tags.
    QVERIFY(find(SharedFileList::offeredTags(*file, &modern), FT_MEDIA_LENGTH, nullptr) == nullptr);

    file->setMetaDataVer(1);
    const auto numeric = SharedFileList::offeredTags(*file, &modern);
    QVERIFY(find(numeric, FT_MEDIA_LENGTH, nullptr) != nullptr);
    QCOMPARE(find(numeric, FT_MEDIA_LENGTH, nullptr)->intValue(), uint32{3725});
    QCOMPARE(find(numeric, FT_MEDIA_BITRATE, nullptr)->intValue(), uint32{192});
    QCOMPARE(find(numeric, FT_MEDIA_CODEC, nullptr)->strValue(), QStringLiteral("mp3"));
    QVERIFY(find(numeric, FT_MEDIA_ARTIST, nullptr) == nullptr);   // clients only

    // An old server: names as text, the length as h:mm:ss.
    Server old(0x01020305u, 4661);
    const auto named = SharedFileList::offeredTags(*file, &old);
    QVERIFY(find(named, FT_MEDIA_LENGTH, nullptr) == nullptr);
    QCOMPARE(find(named, 0, FT_ED2K_MEDIA_LENGTH)->strValue(), QStringLiteral("1:02:05"));
    QCOMPARE(find(named, 0, FT_ED2K_MEDIA_BITRATE)->intValue(), uint32{192});
    QCOMPARE(find(named, 0, FT_ED2K_MEDIA_CODEC)->strValue(), QStringLiteral("mp3"));
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

void tst_SharedFileList::republishFile_offersACompletedFileAgain()
{
    // MFC RepublishFile: a file that completed is offered again so the server
    // swaps its partial marker for the complete one.
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    KnownFile* file = makeFile(knownFiles, 0xC4, QStringLiteral("done.bin"));
    QVERIFY(shared.safeAddKFile(file));

    Server srv(0x01020304u, 4661);
    srv.setTCPFlags(SrvTcpFlag::Compression);
    QCOMPARE(static_cast<int>(shared.takeFilesToOffer(&srv).size()), 1);
    QVERIFY(shared.takeFilesToOffer(&srv).empty());

    // an old server has no marker to update
    Server old(0x01020305u, 4661);
    shared.republishFile(file, &old);
    QVERIFY(file->publishedED2K());

    shared.republishFile(file, &srv);
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

// ---------------------------------------------------------------------------
// Fake-file verdicts — the sweep that keeps the IPC poll path off the disk
// ---------------------------------------------------------------------------

void tst_SharedFileList::theSweepSettlesVerdictsOffThePollPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // Random padding named .wmv: not the ASF the extension promises, and not
    // anything else we can name either.
    const QByteArray junk("\xFF\xFB\x10\xC0\x0B\x0A\x07\x05\x00\x07\x07\x0A", 12);
    const QByteArray asf("\x30\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA", 12);

    auto* fake = makeFileOnDisk(knownFiles, shared, 0x21, dir.path(),
                                QStringLiteral("fake.wmv"), junk);
    auto* real = makeFileOnDisk(knownFiles, shared, 0x22, dir.path(),
                                QStringLiteral("real.wmv"), asf);

    // Nothing has looked yet, and nothing may: this is what the list payload reads.
    QVERIFY(!fake->containerCheckResolved());
    QVERIFY(!fake->containerCheckIfResolved().isSuspect());

    shared.process();

    QVERIFY(fake->containerCheckResolved());
    QVERIFY(fake->containerCheckIfResolved().isSuspect());
    QCOMPARE(fake->containerCheckIfResolved().expected, QStringLiteral("ASF"));

    // And the honest one is settled too, as not suspect — the sweep is not a
    // fake-detector, it is what makes the verdict available at all.
    QVERIFY(real->containerCheckResolved());
    QVERIFY(!real->containerCheckIfResolved().isSuspect());
}

void tst_SharedFileList::theSweepStopsAndANewFileRestartsIt()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    const QByteArray asf("\x30\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA", 12);
    makeFileOnDisk(knownFiles, shared, 0x31, dir.path(), QStringLiteral("one.wmv"), asf);

    // First tick resolves it, the second finds nothing left and parks the sweep.
    shared.process();
    shared.process();

    // A file joining the share afterwards must re-arm it. This is the whole of what
    // the idle flag can get wrong: park once and a file added later never gets a
    // verdict, so its mark never appears no matter how long the daemon runs.
    auto* late = makeFileOnDisk(knownFiles, shared, 0x32, dir.path(),
                                QStringLiteral("late.wmv"),
                                QByteArray("\xFF\xFB\x10\xC0\x0B\x0A\x07\x05\x00\x07\x07\x0A", 12));
    QVERIFY(!late->containerCheckResolved());
    shared.process();
    QVERIFY(late->containerCheckResolved());
    QVERIFY(late->containerCheckIfResolved().isSuspect());
}

// The probe asks each file at most once per call and never a file past the first
// "yes" — due() marks a file as published, so asking and not publishing loses a turn.
void tst_SharedFileList::nextDueFile_isOneWalkAndRoundRobin()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    for (uint8 i = 1; i <= 5; ++i)
        shared.safeAddKFile(makeFile(knownFiles, i, QStringLiteral("f%1.bin").arg(i)));

    uint32 cursor = 0;
    int asked = 0;
    const auto always = [&asked](KnownFile*) { ++asked; return true; };

    // Everything due: five calls hand out five different files, one question each
    QSet<KnownFile*> seen;
    for (int i = 0; i < 5; ++i)
        seen.insert(shared.nextDueFile(cursor, always));
    QCOMPARE(seen.size(), 5);
    QCOMPARE(asked, 5);
    // ... and the sixth wraps around
    QVERIFY(seen.contains(shared.nextDueFile(cursor, always)));

    // Nothing due: every file is asked exactly once, wherever the cursor stands
    cursor = 3;
    asked = 0;
    QVERIFY(!shared.nextDueFile(cursor, [&asked](KnownFile*) { ++asked; return false; }));
    QCOMPARE(asked, 5);

    // Only one file due, behind the cursor: found on the wrap
    KnownFile* only = *seen.begin();
    cursor = 0;
    shared.nextDueFile(cursor, [only](KnownFile* f) { return f == only; });
    const uint32 after = cursor;
    QCOMPARE(shared.nextDueFile(cursor, [only](KnownFile* f) { return f == only; }), only);
    QCOMPARE(cursor, after);
}

// A record with a stale tag and one with none both end with what the file says.
// MFC RebuildMetaData, srchybrid/SharedFileList.cpp:1381-1386.
void tst_SharedFileList::rebuildMetaData_rereadsSharedFilesOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // 2 s of 8 kHz mono 8-bit PCM
    QByteArray wav;
    QDataStream out(&wav, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    const quint32 dataLen = 16000;
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataLen);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(8000)
        << quint16(1) << quint16(8);
    out.writeRawData("data", 4);
    out << dataLen;
    wav.append(QByteArray(int(dataLen), '\x80'));

    auto* stale = makeFileOnDisk(knownFiles, shared, 0x41, dir.path(), QStringLiteral("a.wav"), wav);
    stale->addTagUnique(Tag(FT_MEDIA_LENGTH, uint32{999}));
    stale->addTagUnique(Tag(FT_MEDIA_CODEC, QStringLiteral("PCM (Old Display Name)")));
    auto* bare = makeFileOnDisk(knownFiles, shared, 0x42, dir.path(), QStringLiteral("b.wav"), wav);
    QVERIFY(!bare->hasMetaDataTags());

    QCOMPARE(shared.rebuildMetaData(), 2);
    for (int i = 0; i < 20 && shared.isRebuildingMetaData(); ++i)
        shared.process();
    QVERIFY(!shared.isRebuildingMetaData());

    for (KnownFile* f : {stale, bare}) {
        QCOMPARE(f->getIntTagValue(FT_MEDIA_LENGTH), uint32{2});
        QCOMPARE(f->metaDataVer(), KnownFile::kMetaDataVer);
        QVERIFY(f->getStrTagValue(FT_MEDIA_CODEC) != QStringLiteral("PCM (Old Display Name)"));
    }
}

// One store search per KADEMLIAPUBLISHTIME, not one per tick for as long as files are due.
void tst_SharedFileList::publishDueNotes_probesAreSpacedTwoSeconds()
{
    eMule::testing::KadFixture kadFixture;
    const auto stop = qScopeGuard([] { kad::SearchManager::stopAllSearches(); });

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    std::vector<kad::UInt128> targets;
    for (uint8 i = 1; i <= 3; ++i) {
        KnownFile* f = makeFile(knownFiles, i, QStringLiteral("rated%1.bin").arg(i));
        f->setFileRating(4);
        shared.safeAddKFile(f);
        kad::UInt128 target;
        target.setValueBE(f->fileHash());
        targets.push_back(target);
    }
    const auto running = [&] {
        return std::count_if(targets.begin(), targets.end(), [](const kad::UInt128& t) {
            return kad::SearchManager::alreadySearchingFor(t);
        });
    };

    const time_t t0 = std::time(nullptr);
    shared.publishDueNotes(t0);
    QCOMPARE(running(), 1);
    shared.publishDueNotes(t0 + 1);
    QCOMPARE(running(), 1);
    shared.publishDueNotes(t0 + KADEMLIAPUBLISHTIME);
    QCOMPARE(running(), 2);
}

void tst_SharedFileList::canPublishToKad_firewalledNeedsABuddy()
{
    {
        eMule::testing::KadFixture kadFixture(eMule::testing::KadMode::Open);
        kadFixture.kadPrefs().setLastContact();
        QVERIFY(SharedFileList::canPublishToKad());
    }
    eMule::testing::KadFixture kadFixture(eMule::testing::KadMode::Firewalled);
    kadFixture.kadPrefs().setLastContact();
    QVERIFY(!SharedFileList::canPublishToKad());

    ClientList list;
    ClientList* const saved = theApp.clientList;
    theApp.clientList = &list;
    const auto restore = qScopeGuard([saved] { theApp.clientList = saved; });
    UpDownClient buddy;
    list.setBuddy(&buddy, BuddyStatus::Connecting);
    QVERIFY(!SharedFileList::canPublishToKad());
    list.setBuddy(&buddy, BuddyStatus::Connected);
    QVERIFY(SharedFileList::canPublishToKad());
    list.setBuddy(nullptr, BuddyStatus::None);
}

// C56: a downloading .emulecollection is in the shared list long before its bytes
// are there, so safeAddKFile() at completion returns as a duplicate and never looks.
void tst_SharedFileList::attachCollection_parsesAFileSharedBeforeItWasComplete()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    const QString name = QStringLiteral("set.emulecollection");
    auto* file = makeFileOnDisk(knownFiles, shared, 0x51, dir.path(), name, QByteArray(64, '\0'));
    QVERIFY(file->collection() == nullptr);

    writeFile(dir.path(), name,
              "ed2k://|file|one.bin|1000|0123456789ABCDEF0123456789ABCDEF|/\n"
              "ed2k://|file|two.bin|2000|FEDCBA9876543210FEDCBA9876543210|/\n");
    shared.safeAddKFile(file);              // the completion path's call: a duplicate
    QVERIFY(file->collection() == nullptr);

    shared.attachCollection(file);
    QVERIFY(file->collection() != nullptr);
    QCOMPARE(file->collection()->fileCount(), 2);

    // Not a collection by name: left alone.
    auto* other = makeFileOnDisk(knownFiles, shared, 0x52, dir.path(), QStringLiteral("x.txt"),
                                 "ed2k://|file|one.bin|1000|0123456789ABCDEF0123456789ABCDEF|/\n");
    shared.attachCollection(other);
    QVERIFY(other->collection() == nullptr);
}

QTEST_MAIN(tst_SharedFileList)
#include "tst_SharedFileList.moc"
