/// @file tst_KnownFileList.cpp
/// @brief Tests for files/KnownFileList — persistence, lookup, cancelled files.

#include "TestHelpers.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "utils/FileDate.h"
#include "prefs/Preferences.h"

#include <QFile>
#include <QFileInfo>
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
    void findKnownFile_followsAddReplaceRemove();
    void findKnownFile_skipsARecordShortOfTheBoundaryHash();
    void dropSupersededRecord_replacesTheShortBoundaryRecord();
    void findKnownFileByID();
    void findKnownFileByPath();
    void isKnownFile_check();
    void isFilePtrInList_ptrCheck();
    void addCancelledFileID_and_check();
    void isCancelledFileByID_notCancelled();
    void saveLoadRoundTrip();
    void load_fallsBackToTheBackup_data();
    void load_fallsBackToTheBackup();
    void aCancelledHashIsStillCancelledAfterARestart();
    void aCancelledHashIsForgottenWhenTheUserAsksUsNotToRemember();
    void knownFilesAreNotWrittenWhenTheUserAsksUsNotToRemember();
    void process_autoSave();
    void clear_deletesAll();
    void sameFileDate_rules();
    void findKnownFile_toleratesAShiftedDateOnlyOnALocalTimeVolume();
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
    // The replacement inherits the old entry's history (MFC MergeFileStats,
    // srchybrid/KnownFileList.cpp:302), so both the file and the total hold the sum.
    QCOMPARE(list.totalTransferred, uint64{1300});
    QCOMPARE(file2->statistic.allTimeTransferred(), uint64{1300});

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

void tst_KnownFileList::findKnownFile_skipsARecordShortOfTheBoundaryHash()
{
    // Records hashed before the boundary-part fix: the scan must not take them for the
    // file on disk, or the wrong hash would be shared for good.
    KnownFileList list;
    const auto make = [](uint8 hashByte, const QString& name, uint64 size, int partHashes) {
        auto* f = new KnownFile();
        uint8 hash[16];
        std::memset(hash, hashByte, 16);
        f->setFileHash(hash);
        f->setFileName(name);
        f->setUtcFileDate(100);
        f->setFileSize(size);
        auto& set = f->fileIdentifier().getRawMD4HashSet();
        for (int i = 0; i < partHashes; ++i)
            set.push_back({});
        return f;
    };

    auto* stale = make(1, QStringLiteral("stale.bin"), 2 * PARTSIZE, 2);
    auto* good  = make(2, QStringLiteral("good.bin"),  2 * PARTSIZE, 3);
    auto* tiny  = make(3, QStringLiteral("tiny.bin"),  PARTSIZE, 0);
    auto* odd   = make(4, QStringLiteral("odd.bin"),   2 * PARTSIZE + 1, 0);
    for (auto* f : {stale, good, tiny, odd})
        QVERIFY(list.safeAddKFile(f));

    QVERIFY(!list.findKnownFile(QStringLiteral("stale.bin"), 100, 2 * PARTSIZE));
    QCOMPARE(list.findKnownFile(QStringLiteral("good.bin"), 100, 2 * PARTSIZE), good);
    QVERIFY(!list.findKnownFile(QStringLiteral("tiny.bin"), 100, PARTSIZE));
    // Off the boundary nothing changes, whatever the record holds.
    QCOMPARE(list.findKnownFile(QStringLiteral("odd.bin"), 100, 2 * PARTSIZE + 1), odd);
}

// The short record used to stay in known.met for good, under a hash nobody computes.
void tst_KnownFileList::dropSupersededRecord_replacesTheShortBoundaryRecord()
{
    KnownFileList list;
    const auto make = [](uint8 hashByte, const QString& name, uint64 size, int partHashes) {
        auto* f = new KnownFile();
        uint8 hash[16];
        std::memset(hash, hashByte, 16);
        f->setFileHash(hash);
        f->setFileName(name);
        f->setUtcFileDate(100);
        f->setFileSize(size);
        auto& set = f->fileIdentifier().getRawMD4HashSet();
        for (int i = 0; i < partHashes; ++i)
            set.push_back({});
        return f;
    };

    auto* stale = make(1, QStringLiteral("movie.bin"), 2 * PARTSIZE, 2);
    stale->statistic.setAllTimeTransferred(5000);
    stale->statistic.setAllTimeRequests(7);
    auto* other = make(2, QStringLiteral("other.bin"), 2 * PARTSIZE, 2);   // another name
    auto* whole = make(3, QStringLiteral("MOVIE.bin"), 2 * PARTSIZE, 3);   // already right
    for (auto* f : {stale, other, whole})
        QVERIFY(list.safeAddKFile(f));
    QCOMPARE(list.totalTransferred, uint64{5000});

    auto* fresh = make(9, QStringLiteral("Movie.bin"), 2 * PARTSIZE, 3);
    QVERIFY(list.dropSupersededRecord(fresh));
    QVERIFY(list.safeAddKFile(fresh));

    uint8 hash[16];
    std::memset(hash, 1, 16);
    QVERIFY(!list.findKnownFileByID(hash));
    QVERIFY(list.isKnownFile(other));
    QVERIFY(list.isKnownFile(whole));
    QCOMPARE(fresh->statistic.allTimeTransferred(), uint64{5000});
    QCOMPARE(fresh->statistic.allTimeRequests(), uint32{7});
    QCOMPARE(list.totalTransferred, uint64{5000});
    QCOMPARE(list.totalRequested, uint32{7});

    // Off a part boundary there is no such record to look for.
    auto* odd = make(10, QStringLiteral("other.bin"), 2 * PARTSIZE + 1, 3);
    QVERIFY(!list.dropSupersededRecord(odd));
    delete odd;
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

// A known.met that is gone, empty or cut short used to read as a first run or as a
// shorter library, and the next save then rotated the good .bak away.
void tst_KnownFileList::load_fallsBackToTheBackup_data()
{
    QTest::addColumn<QString>("damage");
    QTest::newRow("missing") << QStringLiteral("missing");
    QTest::newRow("empty") << QStringLiteral("empty");
    QTest::newRow("truncated") << QStringLiteral("truncated");
    QTest::newRow("bad header") << QStringLiteral("header");
}

void tst_KnownFileList::load_fallsBackToTheBackup()
{
    QFETCH(QString, damage);
    eMule::testing::TempDir tmpDir;
    const QString met = tmpDir.path() + QStringLiteral("/known.met");

    const auto addFile = [](KnownFileList& list, uint8 fill, const QString& name) {
        auto* file = new KnownFile();
        uint8 hash[16];
        std::memset(hash, fill, 16);
        file->setFileHash(hash);
        file->setFileName(name);
        file->setFileSize(4321);
        file->setUtcFileDate(1700000000);
        list.safeAddKFile(file);
    };
    {
        KnownFileList list;
        list.init(tmpDir.path());
        addFile(list, 0xA1, QStringLiteral("one.bin"));
        addFile(list, 0xA2, QStringLiteral("two.bin"));
        list.save();
        list.save();   // the second save leaves the first as known.met.bak
    }
    QVERIFY(QFileInfo(met + QStringLiteral(".bak")).size() > 0);

    if (damage == QLatin1String("missing")) {
        QVERIFY(QFile::remove(met));
    } else {
        QFile f(met);
        QVERIFY(f.open(QIODevice::ReadWrite));
        if (damage == QLatin1String("empty"))
            QVERIFY(f.resize(0));
        else if (damage == QLatin1String("truncated"))
            QVERIFY(f.resize(f.size() - 20));
        else
            QCOMPARE(f.write("\x01", 1), qint64{1});
    }

    {
        KnownFileList list;
        list.init(tmpDir.path());
        QCOMPARE(list.count(), size_t{2});
        list.save();
    }
    // The save after the fallback wrote the full list back.
    KnownFileList again;
    again.init(tmpDir.path());
    QCOMPARE(again.count(), size_t{2});
}

void tst_KnownFileList::aCancelledHashIsStillCancelledAfterARestart()
{
    eMule::testing::TempDir tmpDir;
    thePrefs.setRememberCancelledFiles(true);

    uint8 hash[16];
    std::memset(hash, 0xAA, 16);

    {
        KnownFileList list;
        list.init(tmpDir.path());
        list.addCancelledFileID(hash);
        QVERIFY(list.isCancelledFileByID(hash));
        list.save();
    }

    // The bug this pins: the seed used to be minted at *save* time, after the key
    // had already been derived from the zero sentinel — so the header claimed one
    // seed and every record beneath it came from another, and nothing ever matched
    // again. In-memory coverage alone cannot see it; only the round trip can.
    KnownFileList list;
    QVERIFY(list.init(tmpDir.path()));
    QVERIFY(list.isCancelledFileByID(hash));

    uint8 other[16];
    std::memset(other, 0xBB, 16);
    QVERIFY(!list.isCancelledFileByID(other));
}

void tst_KnownFileList::aCancelledHashIsForgottenWhenTheUserAsksUsNotToRemember()
{
    eMule::testing::TempDir tmpDir;
    thePrefs.setRememberCancelledFiles(true);

    uint8 hash[16];
    std::memset(hash, 0xA5, 16);

    {
        KnownFileList list;
        list.init(tmpDir.path());
        list.addCancelledFileID(hash);
        list.save();
    }

    // Off means forget, the way cancelled.met has always behaved: not read, not
    // written, and not matched — so turning it back on does not resurrect it.
    thePrefs.setRememberCancelledFiles(false);
    {
        KnownFileList list;
        QVERIFY(list.init(tmpDir.path()));
        QVERIFY(!list.isCancelledFileByID(hash));
        list.addCancelledFileID(hash);
        QVERIFY(!list.isCancelledFileByID(hash));
        list.save();
    }

    thePrefs.setRememberCancelledFiles(true);
    KnownFileList list;
    QVERIFY(list.init(tmpDir.path()));
    QVERIFY(!list.isCancelledFileByID(hash));
}

void tst_KnownFileList::knownFilesAreNotWrittenWhenTheUserAsksUsNotToRemember()
{
    eMule::testing::TempDir tmpDir;
    thePrefs.setRememberDownloadedFiles(false);

    uint8 hash[16];
    std::memset(hash, 0xDD, 16);

    {
        KnownFileList list;
        list.init(tmpDir.path());

        auto* file = new KnownFile();
        file->setFileHash(hash);
        file->setFileName(QStringLiteral("forgettable.bin"));
        file->setFileSize(1234);
        file->setUtcFileDate(1700000000);
        list.safeAddKFile(file);
        QCOMPARE(list.count(), size_t{1});

        // Nothing is sharing it, so with the preference off it must not survive
        // the write — which is the whole of what the checkbox does, and what it
        // did not do at all until now.
        list.save();
    }

    {
        KnownFileList list;
        QVERIFY(list.init(tmpDir.path()));
        QCOMPARE(list.count(), size_t{0});
    }

    thePrefs.setRememberDownloadedFiles(true);
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

// The size index has to stay in step with the hash map through every change.
void tst_KnownFileList::findKnownFile_followsAddReplaceRemove()
{
    KnownFileList list;
    const auto make = [](uint8 hashByte, const QString& name, time_t date, uint64 size) {
        auto* f = new KnownFile();
        uint8 hash[16];
        std::memset(hash, hashByte, 16);
        f->setFileHash(hash);
        f->setFileName(name);
        f->setUtcFileDate(date);
        f->setFileSize(size);
        return f;
    };

    // Two records of the same size: told apart by name and date
    auto* a = make(1, QStringLiteral("One.bin"), 100, 5000);
    auto* b = make(2, QStringLiteral("two.bin"), 200, 5000);
    QVERIFY(list.safeAddKFile(a));
    QVERIFY(list.safeAddKFile(b));
    QCOMPARE(list.findKnownFile(QStringLiteral("one.BIN"), 100, 5000), a);   // case-insensitive
    QCOMPARE(list.findKnownFile(QStringLiteral("two.bin"), 200, 5000), b);
    QVERIFY(!list.findKnownFile(QStringLiteral("one.bin"), 101, 5000));
    QVERIFY(!list.findKnownFile(QStringLiteral("one.bin"), 100, 5001));

    // A date that changes after the add is still found
    a->setUtcFileDate(150);
    QCOMPARE(list.findKnownFile(QStringLiteral("one.bin"), 150, 5000), a);

    // Same hash again replaces the record: the old object must not be handed out
    auto* a2 = make(1, QStringLiteral("renamed.bin"), 300, 5000);
    QVERIFY(list.safeAddKFile(a2));
    QVERIFY(!list.findKnownFile(QStringLiteral("one.bin"), 150, 5000));
    QCOMPARE(list.findKnownFile(QStringLiteral("renamed.bin"), 300, 5000), a2);

    list.remove(b);
    QVERIFY(!list.findKnownFile(QStringLiteral("two.bin"), 200, 5000));
    delete b;

    list.clear();
    QVERIFY(!list.findKnownFile(QStringLiteral("renamed.bin"), 300, 5000));
}

QTEST_MAIN(tst_KnownFileList)
// FAT keeps local time in 2 s steps: a DST switch moves every date by an hour and a
// remount can round it. Neither is a changed file (issue #9: the whole share was rehashed).
void tst_KnownFileList::sameFileDate_rules()
{
    const time_t t = 1700000000;
    QVERIFY(sameFileDate(t, t, false));
    QVERIFY(!sameFileDate(t, t + 1, false));
    QVERIFY(!sameFileDate(t, t + 3600, false));

    QVERIFY(sameFileDate(t, t + 2, true));
    QVERIFY(sameFileDate(t, t - 3600, true));
    QVERIFY(sameFileDate(t, t + 3601, true));
    QVERIFY(!sameFileDate(t, t + 3, true));
    QVERIFY(!sameFileDate(t, t + 1800, true));
    QVERIFY(!sameFileDate(t, t + 7200, true));
}

void tst_KnownFileList::findKnownFile_toleratesAShiftedDateOnlyOnALocalTimeVolume()
{
    KnownFileList list;
    const auto add = [&list](uint8 hashByte, time_t date) {
        auto* file = new KnownFile();
        uint8 hash[16];
        std::memset(hash, hashByte, 16);
        file->setFileHash(hash);
        file->setFileName(QStringLiteral("video.avi"));
        file->setFileSize(12345);
        file->setUtcFileDate(date);
        list.safeAddKFile(file);
        return file;
    };
    KnownFile* const summer = add(0x41, 1700000000);

    QCOMPARE(list.findKnownFile(QStringLiteral("video.avi"), 1700003600, 12345), nullptr);
    QCOMPARE(list.findKnownFile(QStringLiteral("video.avi"), 1700003600, 12345, true), summer);
    QCOMPARE(list.findKnownFile(QStringLiteral("video.avi"), 1700001800, 12345, true), nullptr);

    // An exact record wins over a shifted one, whatever the order they are met in.
    KnownFile* const exact = add(0x42, 1700003600);
    QCOMPARE(list.findKnownFile(QStringLiteral("video.avi"), 1700003600, 12345, true), exact);
    QCOMPARE(list.findKnownFile(QStringLiteral("video.avi"), 1700000000, 12345, true), summer);
}

#include "tst_KnownFileList.moc"
