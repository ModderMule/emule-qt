/// @file tst_PartFileConvert.cpp
/// @brief Tests for files/PartFileConvert — format detection, job management.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "files/PartFile.h"
#include "files/PartFileConvert.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "transfer/DownloadQueue.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QtEndian>
#include <QTest>
#include <QThread>
#include <QTemporaryDir>

#include <cstring>

using namespace eMule;

class tst_PartFileConvert : public QObject {
    Q_OBJECT

private slots:
    void scanFolderToAdd_findsPartMet();
    void scanFolderToAdd_recursive();
    void addJob_queuesJob();
    void removeAllJobs_clears();
    void removeJob_specific();
    void formatDetection();
    void import_emuleFormatGetsANewNumberAndIsQueued();
    void import_removesTheSourceOnlyWhenAsked();
    void import_splitChunksLandAtTheirParts();
    void import_shareazaDescription();
    void import_runsOnTheWorkerThread();
    void startStopThread();
    void processQueue_startsThread();
};

void tst_PartFileConvert::scanFolderToAdd_findsPartMet()
{
    eMule::testing::TempDir tmpDir;

    // Create a .part.met file with valid header
    const QString partMetFile = tmpDir.filePath(QStringLiteral("test.part.met"));
    QFile f(partMetFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    // Write PARTFILE_VERSION header (0xE0)
    char header = static_cast<char>(0xE0);
    f.write(&header, 1);
    f.write(QByteArray(100, '\0')); // padding
    f.close();

    PartFileConvert::removeAllJobs();
    PartFileConvert::scanFolderToAdd(tmpDir.path());

    QVERIFY(PartFileConvert::jobCount() >= 1);
    PartFileConvert::removeAllJobs();
}

void tst_PartFileConvert::scanFolderToAdd_recursive()
{
    eMule::testing::TempDir tmpDir;

    // Create subdirectory with a .part.met file
    QDir dir(tmpDir.path());
    dir.mkdir(QStringLiteral("subdir"));

    const QString partMetFile = tmpDir.filePath(QStringLiteral("subdir/deep.part.met"));
    QFile f(partMetFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    char header = static_cast<char>(0xE0);
    f.write(&header, 1);
    f.write(QByteArray(100, '\0'));
    f.close();

    PartFileConvert::removeAllJobs();
    PartFileConvert::scanFolderToAdd(tmpDir.path(), true);

    QVERIFY(PartFileConvert::jobCount() >= 1);
    PartFileConvert::removeAllJobs();
}

void tst_PartFileConvert::addJob_queuesJob()
{
    PartFileConvert::removeAllJobs();

    ConvertJob job;
    job.folder = QStringLiteral("/tmp");
    job.filename = QStringLiteral("test.part.met");
    job.state = ConvertStatus::Queued;
    PartFileConvert::addJob(std::move(job));

    QCOMPARE(PartFileConvert::jobCount(), 1);

    auto retrieved = PartFileConvert::jobAt(0);
    QCOMPARE(retrieved.folder, QStringLiteral("/tmp"));
    QCOMPARE(retrieved.filename, QStringLiteral("test.part.met"));

    PartFileConvert::removeAllJobs();
}

void tst_PartFileConvert::removeAllJobs_clears()
{
    PartFileConvert::removeAllJobs();

    ConvertJob job;
    job.folder = QStringLiteral("/tmp");
    PartFileConvert::addJob(std::move(job));
    PartFileConvert::addJob(ConvertJob{});

    QCOMPARE(PartFileConvert::jobCount(), 2);
    PartFileConvert::removeAllJobs();
    QCOMPARE(PartFileConvert::jobCount(), 0);
}

void tst_PartFileConvert::removeJob_specific()
{
    PartFileConvert::removeAllJobs();

    ConvertJob job1;
    job1.filename = QStringLiteral("first.part.met");
    PartFileConvert::addJob(std::move(job1));

    ConvertJob job2;
    job2.filename = QStringLiteral("second.part.met");
    PartFileConvert::addJob(std::move(job2));

    QCOMPARE(PartFileConvert::jobCount(), 2);
    PartFileConvert::removeJob(0);
    QCOMPARE(PartFileConvert::jobCount(), 1);
    QCOMPARE(PartFileConvert::jobAt(0).filename, QStringLiteral("second.part.met"));

    PartFileConvert::removeAllJobs();
}

namespace {

QByteArray bytesOf(const SafeMemFile& mem) { return mem.buffer(); }

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

QByteArray readFile(const QString& path, qint64 offset, qint64 len)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(offset))
        return {};
    return f.read(len);
}

std::array<uint8, 16> hashOf(uint8 seed)
{
    std::array<uint8, 16> h{};
    for (int i = 0; i < 16; ++i)
        h[static_cast<size_t>(i)] = static_cast<uint8>(seed + i * 11);
    return h;
}

QByteArray pattern(int len, char seed)
{
    QByteArray data(len, '\0');
    for (int i = 0; i < len; ++i)
        data[i] = static_cast<char>(seed + i * 7);
    return data;
}

/// A gap as eMule names it: tag byte + index, end exclusive.
void writeGap(SafeMemFile& mem, int index, uint32 start, uint32 endExclusive)
{
    const QByteArray n = QByteArray::number(index);
    Tag(QByteArray(1, static_cast<char>(FT_GAPSTART)) + n, start).writeNewEd2kTag(mem);
    Tag(QByteArray(1, static_cast<char>(FT_GAPEND)) + n, endExclusive).writeNewEd2kTag(mem);
}

/// An eDonkey "new style" met: version, a filler byte, date, hash, tags.
QByteArray newStyleMet(uint8 version, const std::array<uint8, 16>& hash, const QString& name,
                       uint32 size, const std::vector<std::pair<uint32, uint32>>& gaps)
{
    SafeMemFile mem;
    mem.writeUInt8(version);
    mem.writeUInt8(0x02);
    mem.writeUInt32(1'600'000'000);
    mem.writeHash16(hash.data());
    mem.writeUInt32(static_cast<uint32>(2 + gaps.size() * 2));
    Tag(FT_FILENAME, name).writeNewEd2kTag(mem, UTF8Mode::OptBOM);
    Tag(FT_FILESIZE, size).writeNewEd2kTag(mem);
    int index = 0;
    for (const auto& [start, end] : gaps)
        writeGap(mem, index++, start, end);
    return bytesOf(mem);
}

/// A real eMule download in @p dir: 1000 bytes of a 300 000 byte file.
QString makeEmuleDownload(const QString& dir, const std::array<uint8, 16>& hash,
                          const QString& name, const QByteArray& head)
{
    QDir().mkpath(dir);
    PartFile pf;
    pf.setFileName(name, true);
    pf.setFileSize(300'000);
    pf.setFileHash(hash.data());
    if (!pf.createPartFile(dir))
        return {};
    pf.writeToBuffer(static_cast<uint64>(head.size()),
                     reinterpret_cast<const uint8*>(head.constData()), 0,
                     static_cast<uint64>(head.size()) - 1, nullptr);
    pf.flushBuffer();
    pf.savePartFile();
    return pf.partMetFileName();
}

/// A download queue of our own and a temp directory to import into.
struct ImportEnv {
    eMule::testing::TempDir temp;
    DownloadQueue queue;
    DownloadQueue* const savedQueue = theApp.downloadQueue;
    const QStringList savedTempDirs = thePrefs.tempDirs();
    const bool savedPaused = thePrefs.addNewFilesPaused();

    ImportEnv()
    {
        theApp.downloadQueue = &queue;
        thePrefs.setTempDirs({temp.path()});
        thePrefs.setAddNewFilesPaused(true);    // nothing here should go looking for sources
    }
    ~ImportEnv()
    {
        theApp.downloadQueue = savedQueue;
        thePrefs.setTempDirs(savedTempDirs);
        thePrefs.setAddNewFilesPaused(savedPaused);
    }

    ConvertStatus import(const QString& folder, const QString& filename, bool removeSource,
                         ConvertJob* out = nullptr)
    {
        ConvertJob job;
        job.folder = folder;
        job.filename = filename;
        job.removeSource = removeSource;
        const ConvertStatus status = PartFileConvert::performConvertToeMule(job);
        if (out)
            *out = job;
        return status;
    }
};

} // namespace

// MFC PartFile.cpp:729-753. The port guessed from the first byte alone and took
// 0x0E for "NewOld".
void tst_PartFileConvert::formatDetection()
{
    eMule::testing::TempDir tmpDir;
    const auto hash = hashOf(0x10);

    // A real eMule .part.met
    const QString met = makeEmuleDownload(tmpDir.path() + QStringLiteral("/emule"), hash,
                                          QStringLiteral("plain.bin"), pattern(1000, 3));
    QVERIFY(!met.isEmpty());
    QCOMPARE(PartFileConvert::detectFormat(tmpDir.path() + QStringLiteral("/emule/") + met),
             static_cast<int>(PartFileFormat::DefaultOld));

    // Split: version 0xE1
    const QString split = tmpDir.filePath(QStringLiteral("002.part.met"));
    QVERIFY(writeFile(split, newStyleMet(0xE1, hash, QStringLiteral("split.bin"), 5000, {})));
    QCOMPARE(PartFileConvert::detectFormat(split), static_cast<int>(PartFileFormat::Splitted));

    // eDonkey's "old part style": 0xE0 in the new layout. What MFC tests for — the
    // uint32 0x01020000 at offset 24 — is the upper half of the tag count followed by
    // the head of a classic string tag with a one-byte name (the file name).
    {
        SafeMemFile mem;
        mem.writeUInt8(0xE0);
        mem.writeUInt8(0x02);
        mem.writeUInt32(1'600'000'000);
        mem.writeHash16(hash.data());
        mem.writeUInt32(2);
        Tag(FT_FILENAME, QStringLiteral("old.bin")).writeTagToFile(mem);
        Tag(FT_FILESIZE, uint32{5000}).writeTagToFile(mem);
        const QString path = tmpDir.filePath(QStringLiteral("003.part.met"));
        QVERIFY(writeFile(path, mem.buffer()));
        QCOMPARE(PartFileConvert::detectFormat(path), static_cast<int>(PartFileFormat::NewOld));

        PartFileFormat format = PartFileFormat::Unknown;
        PartFile probe;
        QCOMPARE(probe.loadPartFile(tmpDir.path(), QStringLiteral("003.part.met"), &format),
                 PartFileLoadResult::CheckSuccess);
        QCOMPARE(probe.fileName(), QStringLiteral("old.bin"));
        QCOMPARE(static_cast<uint64>(probe.fileSize()), uint64{5000});
        QVERIFY(md4equ(probe.fileHash(), hash.data()));
    }

    // 0x0E is no part.met at all
    const QString bogus = tmpDir.filePath(QStringLiteral("004.part.met"));
    QVERIFY(writeFile(bogus, QByteArray(1, char(0x0E)) + QByteArray(50, '\0')));
    QCOMPARE(PartFileConvert::detectFormat(bogus), 0);

    const QString unknown = tmpDir.filePath(QStringLiteral("unknown.dat"));
    QVERIFY(writeFile(unknown, QByteArray(50, '\xFF')));
    QCOMPARE(PartFileConvert::detectFormat(unknown), 0);
}

// C55: MFC PartFileConvert.cpp:192-194, :298-368
void tst_PartFileConvert::import_emuleFormatGetsANewNumberAndIsQueued()
{
    ImportEnv env;
    eMule::testing::TempDir src;
    const auto hash = hashOf(0x21);
    const QByteArray head = pattern(1000, 5);
    const QString met = makeEmuleDownload(src.path(), hash, QStringLiteral("movie.bin"), head);
    QVERIFY(!met.isEmpty());
    QString srcPart = met;
    srcPart.chop(4);

    // The temp directory has a download under the same number.
    QVERIFY(writeFile(env.temp.filePath(srcPart), "x"));
    QVERIFY(writeFile(env.temp.filePath(met), "x"));

    ConvertJob job;
    QCOMPARE(env.import(src.path(), met, /*removeSource*/ false, &job), ConvertStatus::OK);
    QCOMPARE(job.size, uint64{300'000});
    QCOMPARE(job.fileHash, md4str(hash.data()));
    QCOMPARE(job.format, static_cast<int>(PartFileFormat::DefaultOld));

    PartFile* imported = env.queue.fileByID(hash.data());
    QVERIFY(imported != nullptr);
    QVERIFY(imported->partMetFileName() != met);           // a number of its own
    QCOMPARE(QFileInfo(imported->fullName()).absolutePath(), QFileInfo(env.temp.path()).absoluteFilePath());
    QCOMPARE(imported->fileName(), QStringLiteral("movie.bin"));
    QVERIFY(imported->isComplete(0, 999));
    QVERIFY(!imported->isComplete(1000, 299'999));
    QCOMPARE(readFile(imported->dataFilePath(), 0, 1000), head);
    QCOMPARE(readFile(env.temp.filePath(srcPart), 0, 10), QByteArray("x"));
    QCOMPARE(readFile(env.temp.filePath(met), 0, 10), QByteArray("x"));

    // Kept: nobody asked for the source to go.
    QVERIFY(QFile::exists(src.filePath(srcPart)));
    QVERIFY(QFile::exists(src.filePath(met)));

    // The same download again
    const int before = QDir(env.temp.path()).entryList(QDir::Files).size();
    QCOMPARE(env.import(src.path(), met, false), ConvertStatus::AlreadyExists);
    QCOMPARE(QDir(env.temp.path()).entryList(QDir::Files).size(), before);
}

void tst_PartFileConvert::import_removesTheSourceOnlyWhenAsked()
{
    ImportEnv env;
    eMule::testing::TempDir src;
    const auto hash = hashOf(0x31);
    const QByteArray head = pattern(1000, 9);
    const QString met = makeEmuleDownload(src.path(), hash, QStringLiteral("gone.bin"), head);
    QString srcPart = met;
    srcPart.chop(4);
    const QString bystander = met.left(3) + QStringLiteral(".keep.txt");
    QVERIFY(writeFile(src.filePath(bystander), "mine"));

    QCOMPARE(env.import(src.path(), met, /*removeSource*/ true), ConvertStatus::OK);
    PartFile* imported = env.queue.fileByID(hash.data());
    QVERIFY(imported != nullptr);
    QVERIFY(!QFile::exists(src.filePath(srcPart)));
    QVERIFY(!QFile::exists(src.filePath(met)));
    // MFC unlinks every "<number>.*"; only what was imported goes here.
    QVERIFY(QFile::exists(src.filePath(bystander)));
    QCOMPARE(readFile(imported->dataFilePath(), 0, 1000), head);

    // Not a download at all: refused, and the file stays where it is.
    QVERIFY(writeFile(src.filePath(QStringLiteral("009.part.met")), QByteArray(60, '\x0E')));
    QCOMPARE(env.import(src.path(), QStringLiteral("009.part.met"), true), ConvertStatus::BadFormat);
    QVERIFY(QFile::exists(src.filePath(QStringLiteral("009.part.met"))));

    // A description without its data file (MFC PartFileConvert.cpp:314-326): imported
    // with an empty .part, all of it still to download.
    {
        const auto other = hashOf(0x39);
        SafeMemFile mem;
        mem.writeUInt8(0xE0);
        mem.writeUInt32(1'600'000'000);
        mem.writeHash16(other.data());
        mem.writeUInt16(0);
        mem.writeUInt32(2);
        Tag(FT_FILENAME, QStringLiteral("half.bin")).writeNewEd2kTag(mem, UTF8Mode::OptBOM);
        Tag(FT_FILESIZE, uint32{50'000}).writeNewEd2kTag(mem);
        QVERIFY(writeFile(src.filePath(QStringLiteral("010.part.met")), mem.buffer()));
        const int before = QDir(env.temp.path()).entryList(QDir::Files).size();
        QCOMPARE(env.import(src.path(), QStringLiteral("010.part.met"), false), ConvertStatus::OK);
        PartFile* empty = env.queue.fileByID(other.data());
        QVERIFY(empty != nullptr);
        QVERIFY(!empty->isComplete(0, 49'999));
        QVERIFY(QDir(env.temp.path()).entryList(QDir::Files).size() > before);
    }
}

// C54: MFC PartFileConvert.cpp:205-289 — "NNN.<n>.part" holds part n.
void tst_PartFileConvert::import_splitChunksLandAtTheirParts()
{
    ImportEnv env;
    eMule::testing::TempDir src;
    const auto hash = hashOf(0x41);
    const uint32 size = static_cast<uint32>(PARTSIZE) * 2 + 5000;
    const QByteArray one = pattern(1000, 1);
    const QByteArray three = pattern(700, 3);

    // Missing: the rest of part 1, all of part 2, the rest of part 3
    const QByteArray met = newStyleMet(0xE1, hash, QStringLiteral("split.bin"), size,
        {{1000, static_cast<uint32>(PARTSIZE) * 2},
         {static_cast<uint32>(PARTSIZE) * 2 + 700, size}});
    QVERIFY(writeFile(src.filePath(QStringLiteral("007.part.met")), met));
    QVERIFY(writeFile(src.filePath(QStringLiteral("007.1.part")), one));
    QVERIFY(writeFile(src.filePath(QStringLiteral("007.3.part")), three));

    ConvertJob job;
    QCOMPARE(env.import(src.path(), QStringLiteral("007.part.met"), false, &job), ConvertStatus::OK);
    QCOMPARE(job.format, static_cast<int>(PartFileFormat::Splitted));

    PartFile* imported = env.queue.fileByID(hash.data());
    QVERIFY(imported != nullptr);
    QCOMPARE(static_cast<uint64>(imported->fileSize()), uint64{size});
    QVERIFY(imported->isComplete(0, 999));
    QVERIFY(!imported->isComplete(1000, PARTSIZE * 2 - 1));
    QVERIFY(imported->isComplete(PARTSIZE * 2, PARTSIZE * 2 + 699));

    const QString part = imported->dataFilePath();
    QCOMPARE(readFile(part, 0, 1000), one);
    QCOMPARE(readFile(part, static_cast<qint64>(PARTSIZE) * 2, 700), three);
    QVERIFY(QFile::exists(src.filePath(QStringLiteral("007.1.part"))));
}

// C53: MFC PartFile.cpp:457-702. The .sd describes the download; the data is the
// file beside it.
void tst_PartFileConvert::import_shareazaDescription()
{
    ImportEnv env;
    eMule::testing::TempDir src;
    const auto hash = hashOf(0x51);
    const quint64 size = 200'000;
    const QString name = QStringLiteral("Shared Movie.avi");
    const QByteArray data = pattern(1500, 4);

    QByteArray sd("SDL");
    const auto put = [&sd](auto value) {
        const auto le = qToLittleEndian(value);
        sd.append(reinterpret_cast<const char*>(&le), sizeof le);
    };
    put(qint32{32});                                       // version
    sd.append("\xFF\xFE\xFF", 3).append(static_cast<char>(name.size()));
    sd.append(reinterpret_cast<const char*>(name.utf16()), name.size() * 2);
    put(size);
    put(qint32{0}); put(qint32{0});                        // SHA1: none, trusted
    put(qint32{0}); put(qint32{0});                        // Tiger
    put(qint32{0}); put(qint32{0});                        // MD5
    put(qint32{1});                                        // eD2K
    sd.append(reinterpret_cast<const char*>(hash.data()), 16);
    put(qint32{1});
    sd.append(QByteArray(37, '\x55'));                     // sources, unparsed
    put(size); put(quint64{size - 1500});                  // total, remaining
    put(quint32{2});                                       // two fragments missing
    put(quint64{1500});   put(quint64{98'500});
    put(quint64{100'000}); put(quint64{100'000});
    sd.append(QByteArray(21, '\x66'));                     // torrent info, tiger tree
    put(quint32{0});                                       // hash set: one part, no list
    sd.append(reinterpret_cast<const char*>(hash.data()), 16);
    sd.append(QByteArray(9, '\x77'));

    QVERIFY(writeFile(src.filePath(QStringLiteral("dl.partial.sd")), sd));
    QVERIFY(writeFile(src.filePath(QStringLiteral("dl.partial")), data + QByteArray(100, '\0')));

    QCOMPARE(PartFileConvert::detectFormat(src.filePath(QStringLiteral("dl.partial.sd"))),
             static_cast<int>(PartFileFormat::Shareaza));
    // Looking at a file must not change it (the object used to save "its" met when
    // it went away, which rewrote the .sd in our format).
    QCOMPARE(readFile(src.filePath(QStringLiteral("dl.partial.sd")), 0, sd.size() + 1), sd);

    ConvertJob job;
    QCOMPARE(env.import(src.path(), QStringLiteral("dl.partial.sd"), false, &job), ConvertStatus::OK);
    QCOMPARE(job.size, size);
    QCOMPARE(job.fileHash, md4str(hash.data()));

    PartFile* imported = env.queue.fileByID(hash.data());
    QVERIFY(imported != nullptr);
    QCOMPARE(imported->fileName(), name);
    QCOMPARE(static_cast<uint64>(imported->fileSize()), size);
    QVERIFY(imported->partMetFileName().endsWith(QStringLiteral(".part.met")));
    QVERIFY(imported->isComplete(0, 1499));
    QVERIFY(!imported->isComplete(1500, size - 1));
    QCOMPARE(readFile(imported->dataFilePath(), 0, 1500), data);

    // The description and its data are still there, and the new met is ours.
    QCOMPARE(readFile(src.filePath(QStringLiteral("dl.partial.sd")), 0, sd.size() + 1), sd);
    QVERIFY(QFile::exists(src.filePath(QStringLiteral("dl.partial"))));
    QCOMPARE(PartFileConvert::detectFormat(imported->fullName()),
             static_cast<int>(PartFileFormat::DefaultOld));
}

// The dialog's path: scan, queue, worker thread. The worker hands the queue work
// to this thread and waits for it.
void tst_PartFileConvert::import_runsOnTheWorkerThread()
{
    ImportEnv env;
    eMule::testing::TempDir src;
    const auto hash = hashOf(0x61);
    const QByteArray head = pattern(1000, 6);
    QVERIFY(!makeEmuleDownload(src.path(), hash, QStringLiteral("threaded.bin"), head).isEmpty());

    PartFileConvert::removeAllJobs();
    PartFileConvert::scanFolderToAdd(src.path(), /*recursive*/ true, /*removeSource*/ false);
    QCOMPARE(PartFileConvert::jobCount(), 1);
    QCOMPARE(PartFileConvert::jobAt(0).format, static_cast<int>(PartFileFormat::DefaultOld));
    PartFileConvert::processQueue();

    QTRY_COMPARE_WITH_TIMEOUT(PartFileConvert::jobAt(0).state, ConvertStatus::OK, 10000);
    PartFileConvert::stopThread();

    PartFile* imported = env.queue.fileByID(hash.data());
    QVERIFY(imported != nullptr);
    QCOMPARE(readFile(imported->dataFilePath(), 0, 1000), head);
    QCOMPARE(PartFileConvert::jobAt(0).size, uint64{300'000});
    PartFileConvert::removeAllJobs();
}

void tst_PartFileConvert::startStopThread()
{
    PartFileConvert::removeAllJobs();

    // Start and immediately stop — should not crash
    PartFileConvert::startThread();
    QTest::qWait(50); // give thread a moment to start
    PartFileConvert::stopThread();
}

void tst_PartFileConvert::processQueue_startsThread()
{
    PartFileConvert::removeAllJobs();

    // Add a queued job
    ConvertJob job;
    job.folder = QStringLiteral("/tmp");
    job.filename = QStringLiteral("test.part.met");
    job.format = 1;
    job.state = ConvertStatus::Queued;
    PartFileConvert::addJob(std::move(job));

    // processQueue should start the thread
    PartFileConvert::processQueue();
    QTest::qWait(100); // give thread time to process

    // Stop and clean up
    PartFileConvert::stopThread();
    PartFileConvert::removeAllJobs();
}

QTEST_MAIN(tst_PartFileConvert)
#include "tst_PartFileConvert.moc"
