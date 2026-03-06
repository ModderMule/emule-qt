/// @file tst_KnownFile.cpp
/// @brief Tests for files/KnownFile — part counts, priority, serialization,
///        purge, upload client tracking, auto-priority, metadata, signals.

#include "TestHelpers.h"
#include "crypto/MD4Hash.h"
#include "files/KnownFile.h"
#include "protocol/Tag.h"
#include "utils/SafeFile.h"

#include <QBuffer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

#include <cstring>
#include <ctime>

using namespace eMule;

// ---------------------------------------------------------------------------
// Helpers for synthetic AVI construction (shared with tst_MediaInfo)
// ---------------------------------------------------------------------------

static void putLE32(QByteArray& buf, uint32 val)
{
    buf.append(static_cast<char>(val & 0xFF));
    buf.append(static_cast<char>((val >> 8) & 0xFF));
    buf.append(static_cast<char>((val >> 16) & 0xFF));
    buf.append(static_cast<char>((val >> 24) & 0xFF));
}

static void putLE16(QByteArray& buf, uint16 val)
{
    buf.append(static_cast<char>(val & 0xFF));
    buf.append(static_cast<char>((val >> 8) & 0xFF));
}

static constexpr uint32 fourCC(char a, char b, char c, char d)
{
    return static_cast<uint32>(static_cast<uint8>(a))
         | (static_cast<uint32>(static_cast<uint8>(b)) << 8)
         | (static_cast<uint32>(static_cast<uint8>(c)) << 16)
         | (static_cast<uint32>(static_cast<uint8>(d)) << 24);
}

static QByteArray buildMinimalAVI()
{
    QByteArray strhData;
    putLE32(strhData, fourCC('v','i','d','s'));
    putLE32(strhData, 0);
    putLE32(strhData, 0);
    putLE16(strhData, 0);
    putLE16(strhData, 0);
    putLE32(strhData, 0);
    putLE32(strhData, 1);       // dwScale
    putLE32(strhData, 25);      // dwRate (25 fps)
    putLE32(strhData, 0);
    putLE32(strhData, 100);     // dwLength (100 frames = 4 sec)
    putLE32(strhData, 0);
    putLE32(strhData, 0);
    putLE32(strhData, 0);
    putLE16(strhData, 0); putLE16(strhData, 0);
    putLE16(strhData, 320); putLE16(strhData, 240);

    QByteArray strhChunk;
    putLE32(strhChunk, fourCC('s','t','r','h'));
    putLE32(strhChunk, static_cast<uint32>(strhData.size()));
    strhChunk.append(strhData);

    QByteArray bmi;
    putLE32(bmi, 40);
    putLE32(bmi, 320);
    putLE32(bmi, 240);
    putLE16(bmi, 1);
    putLE16(bmi, 24);
    putLE32(bmi, fourCC('D','I','V','X'));
    putLE32(bmi, 320*240*3);
    putLE32(bmi, 0); putLE32(bmi, 0);
    putLE32(bmi, 0); putLE32(bmi, 0);

    QByteArray strfChunk;
    putLE32(strfChunk, fourCC('s','t','r','f'));
    putLE32(strfChunk, static_cast<uint32>(bmi.size()));
    strfChunk.append(bmi);

    QByteArray strlPayload;
    strlPayload.append(strhChunk);
    strlPayload.append(strfChunk);

    QByteArray strlList;
    putLE32(strlList, fourCC('L','I','S','T'));
    putLE32(strlList, static_cast<uint32>(4 + strlPayload.size()));
    putLE32(strlList, fourCC('s','t','r','l'));
    strlList.append(strlPayload);

    QByteArray hdrlList;
    putLE32(hdrlList, fourCC('L','I','S','T'));
    putLE32(hdrlList, static_cast<uint32>(4 + strlList.size()));
    putLE32(hdrlList, fourCC('h','d','r','l'));
    hdrlList.append(strlList);

    QByteArray moviList;
    putLE32(moviList, fourCC('L','I','S','T'));
    putLE32(moviList, 4);
    putLE32(moviList, fourCC('m','o','v','i'));

    QByteArray payload;
    putLE32(payload, fourCC('A','V','I',' '));
    payload.append(hdrlList);
    payload.append(moviList);

    QByteArray riff;
    putLE32(riff, fourCC('R','I','F','F'));
    putLE32(riff, static_cast<uint32>(payload.size()));
    riff.append(payload);

    return riff;
}

static QString writeTempFile(const QByteArray& data, const QString& suffix)
{
    auto* tmp = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/tst_knownfile_XXXXXX") + suffix);
    tmp->setAutoRemove(true);
    if (!tmp->open()) {
        delete tmp;
        return {};
    }
    tmp->write(data);
    tmp->flush();
    static QList<QTemporaryFile*> s_files;
    s_files.append(tmp);
    return tmp->fileName();
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_KnownFile : public QObject {
    Q_OBJECT

private slots:
    // --- Existing tests ---
    void construct_default();
    void setFileSize_partCounts();
    void priority_setAndGet();
    void priority_invalid();
    void loadWriteRoundTrip();
    void shouldPartiallyPurgeFile();
    void publishTimes();
    void statisticFile_embedded();
    void setFileName_basic();

    // --- Upload client tracking ---
    void addRemoveUploadingClient();

    // --- Auto-priority ---
    void updateAutoUpPriority_low();
    void updateAutoUpPriority_normal();
    void updateAutoUpPriority_high();
    void updateAutoUpPriority_notAuto();

    // --- Signal emissions ---
    void priorityChanged_signal();
    void publishedED2K_signal();

    // --- Metadata ---
    void removeMetaDataTags();
    void updateMetaDataTags_aviFile();

    // --- Frame grabbing ---
    void requestGrabFrames_signal();
    void requestGrabFrames_nonVideo();

    // --- Hashing (Phase 3) ---
    void createHash_emptyData();
    void createHash_smallData();
    void createHash_exactPartSize();
    void createFromFile_smallFile();
    void createFromFile_zeroFile();
    void createFromFile_nonExistent();
    void createFromFile_setsDate();
    void createFromFile_progressCallback();
    void updatePartsInfo_noClients();
    void publishSrc_timing();
    void publishNotes_timing();
    void createHashFromMemory_basic();
    void createHashFromFile_basic();
};

// ---------------------------------------------------------------------------
// Existing tests (unchanged)
// ---------------------------------------------------------------------------

void tst_KnownFile::construct_default()
{
    KnownFile f;
    QCOMPARE(f.partCount(), uint16{0});
    QCOMPARE(f.ed2kPartCount(), uint16{0});
    QCOMPARE(f.upPriority(), kPrNormal);
    QVERIFY(f.isAutoUpPriority());
    QVERIFY(!f.publishedED2K());
    QCOMPARE(f.kadFileSearchID(), uint32{0});
    QCOMPARE(f.lastPublishTimeKadSrc(), time_t{0});
    QCOMPARE(f.lastPublishTimeKadNotes(), time_t{0});
    QVERIFY(!f.isAICHRecoverHashSetAvailable());
    QCOMPARE(f.completeSourcesCount(), uint16{1});
    QCOMPARE(f.uploadingClientCount(), 0);
    QVERIFY(!f.hasUploadingClients());
    QVERIFY(f.notifier() != nullptr);
}

void tst_KnownFile::setFileSize_partCounts()
{
    KnownFile f;

    f.setFileSize(0);
    QCOMPARE(f.partCount(), uint16{0});
    QCOMPARE(f.ed2kPartCount(), uint16{0});

    f.setFileSize(1);
    QCOMPARE(f.partCount(), uint16{1});
    QCOMPARE(f.ed2kPartCount(), uint16{1});

    f.setFileSize(PARTSIZE - 1);
    QCOMPARE(f.partCount(), uint16{1});
    QCOMPARE(f.ed2kPartCount(), uint16{1});

    f.setFileSize(PARTSIZE);
    QCOMPARE(f.partCount(), uint16{1});
    QCOMPARE(f.ed2kPartCount(), uint16{2});

    f.setFileSize(PARTSIZE + 1);
    QCOMPARE(f.partCount(), uint16{2});
    QCOMPARE(f.ed2kPartCount(), uint16{2});

    f.setFileSize(PARTSIZE * 2);
    QCOMPARE(f.partCount(), uint16{2});
    QCOMPARE(f.ed2kPartCount(), uint16{3});

    f.setFileSize(PARTSIZE * 3 + 1);
    QCOMPARE(f.partCount(), uint16{4});
    QCOMPARE(f.ed2kPartCount(), uint16{4});
}

void tst_KnownFile::priority_setAndGet()
{
    KnownFile f;

    f.setUpPriority(kPrVeryLow);
    QCOMPARE(f.upPriority(), kPrVeryLow);

    f.setUpPriority(kPrLow);
    QCOMPARE(f.upPriority(), kPrLow);

    f.setUpPriority(kPrNormal);
    QCOMPARE(f.upPriority(), kPrNormal);

    f.setUpPriority(kPrHigh);
    QCOMPARE(f.upPriority(), kPrHigh);

    f.setUpPriority(kPrVeryHigh);
    QCOMPARE(f.upPriority(), kPrVeryHigh);

    f.setAutoUpPriority(true);
    QVERIFY(f.isAutoUpPriority());
    f.setAutoUpPriority(false);
    QVERIFY(!f.isAutoUpPriority());
}

void tst_KnownFile::priority_invalid()
{
    KnownFile f;
    f.setUpPriority(255);
    QCOMPARE(f.upPriority(), kPrNormal);

    f.setUpPriority(99);
    QCOMPARE(f.upPriority(), kPrNormal);
}

void tst_KnownFile::loadWriteRoundTrip()
{
    KnownFile original;

    uint8 hash[16];
    std::memset(hash, 0xAB, 16);
    original.setFileHash(hash);
    original.setFileSize(PARTSIZE * 2 + 500);
    original.setFileName(QStringLiteral("test_video.mkv"));
    original.setUtcFileDate(1700000000);
    original.setLastSeen(1700100000);
    original.setUpPriority(kPrHigh);
    original.setAutoUpPriority(false);
    original.setPublishedED2K(true);
    original.setLastPublishTimeKadSrc(1700050000);
    original.setLastPublishTimeKadNotes(1700060000);

    original.statistic.setAllTimeRequests(42);
    original.statistic.setAllTimeAccepts(10);
    original.statistic.setAllTimeTransferred(999888777);

    SafeMemFile memFile;
    QVERIFY(original.writeToFile(memFile));

    memFile.seek(0, 0);
    KnownFile loaded;
    QVERIFY(loaded.loadFromFile(memFile));

    QCOMPARE(loaded.fileName(), QStringLiteral("test_video.mkv"));
    QCOMPARE(loaded.fileSize(), original.fileSize());
    QVERIFY(md4equ(loaded.fileHash(), hash));
    QCOMPARE(loaded.utcFileDate(), time_t{1700000000});
    QCOMPARE(loaded.partCount(), original.partCount());
    QCOMPARE(loaded.ed2kPartCount(), original.ed2kPartCount());

    QCOMPARE(loaded.upPriority(), kPrHigh);
    QVERIFY(!loaded.isAutoUpPriority());

    QCOMPARE(loaded.lastSeen(), time_t{1700100000});
    QCOMPARE(loaded.lastPublishTimeKadSrc(), time_t{1700050000});
    QCOMPARE(loaded.lastPublishTimeKadNotes(), time_t{1700060000});

    QCOMPARE(loaded.statistic.allTimeRequests(), uint32{42});
    QCOMPARE(loaded.statistic.allTimeAccepts(), uint32{10});
    QCOMPARE(loaded.statistic.allTimeTransferred(), uint64{999888777});
}

void tst_KnownFile::shouldPartiallyPurgeFile()
{
    KnownFile f;

    f.setLastSeen(std::time(nullptr));
    QVERIFY(!f.shouldPartiallyPurgeFile());

    f.setLastSeen(std::time(nullptr) - DAY2S(32));
    QVERIFY(f.shouldPartiallyPurgeFile());

    f.setLastSeen(std::time(nullptr) - DAY2S(31));
    QVERIFY(!f.shouldPartiallyPurgeFile());
}

void tst_KnownFile::publishTimes()
{
    KnownFile f;
    QCOMPARE(f.lastPublishTimeKadSrc(), time_t{0});
    QCOMPARE(f.lastPublishTimeKadNotes(), time_t{0});

    f.setLastPublishTimeKadSrc(1234567890);
    QCOMPARE(f.lastPublishTimeKadSrc(), time_t{1234567890});

    f.setLastPublishTimeKadNotes(9876543210);
    QCOMPARE(f.lastPublishTimeKadNotes(), time_t{9876543210});
}

void tst_KnownFile::statisticFile_embedded()
{
    KnownFile f;
    f.statistic.addRequest();
    f.statistic.addRequest();
    f.statistic.addAccepted();
    f.statistic.addTransferred(5000);

    QCOMPARE(f.statistic.requests(), uint32{2});
    QCOMPARE(f.statistic.accepts(), uint32{1});
    QCOMPARE(f.statistic.transferred(), uint64{5000});
    QCOMPARE(f.statistic.allTimeRequests(), uint32{2});
    QCOMPARE(f.statistic.allTimeAccepts(), uint32{1});
    QCOMPARE(f.statistic.allTimeTransferred(), uint64{5000});
}

void tst_KnownFile::setFileName_basic()
{
    KnownFile f;
    f.setFileName(QStringLiteral("movie.avi"));
    QCOMPARE(f.fileName(), QStringLiteral("movie.avi"));
    QCOMPARE(f.fileType(), QStringLiteral(ED2KFTSTR_VIDEO));
}

// ---------------------------------------------------------------------------
// Upload client tracking
// ---------------------------------------------------------------------------

void tst_KnownFile::addRemoveUploadingClient()
{
    KnownFile f;
    f.setAutoUpPriority(false); // prevent auto-priority from changing things

    QSignalSpy updatedSpy(f.notifier(), &FileNotifier::fileUpdated);

    // Use dummy non-null pointers (we only store pointers, no dereferencing)
    auto* client1 = reinterpret_cast<UpDownClient*>(0x1000);
    auto* client2 = reinterpret_cast<UpDownClient*>(0x2000);

    // Add first client
    f.addUploadingClient(client1);
    QCOMPARE(f.uploadingClientCount(), 1);
    QVERIFY(f.hasUploadingClients());
    QCOMPARE(updatedSpy.count(), 1);

    // Duplicate add — ignored
    f.addUploadingClient(client1);
    QCOMPARE(f.uploadingClientCount(), 1);
    QCOMPARE(updatedSpy.count(), 1); // no extra signal

    // Add second client
    f.addUploadingClient(client2);
    QCOMPARE(f.uploadingClientCount(), 2);
    QCOMPARE(updatedSpy.count(), 2);

    // Null pointer — ignored
    f.addUploadingClient(nullptr);
    QCOMPARE(f.uploadingClientCount(), 2);
    QCOMPARE(updatedSpy.count(), 2);

    // Remove first client
    f.removeUploadingClient(client1);
    QCOMPARE(f.uploadingClientCount(), 1);
    QCOMPARE(updatedSpy.count(), 3);
    QCOMPARE(f.uploadingClients()[0], client2);

    // Remove non-existent — no signal
    f.removeUploadingClient(client1);
    QCOMPARE(f.uploadingClientCount(), 1);
    QCOMPARE(updatedSpy.count(), 3);

    // Remove last
    f.removeUploadingClient(client2);
    QCOMPARE(f.uploadingClientCount(), 0);
    QVERIFY(!f.hasUploadingClients());
}

// ---------------------------------------------------------------------------
// Auto-priority tests
// ---------------------------------------------------------------------------

void tst_KnownFile::updateAutoUpPriority_low()
{
    KnownFile f;
    f.setAutoUpPriority(true);

    // Add 21 clients → should become Low priority
    for (int i = 1; i <= 21; ++i)
        f.addUploadingClient(reinterpret_cast<UpDownClient*>(static_cast<uintptr_t>(i)));

    QCOMPARE(f.upPriority(), kPrLow);
}

void tst_KnownFile::updateAutoUpPriority_normal()
{
    KnownFile f;
    f.setAutoUpPriority(true);

    // Add 5 clients → should be Normal
    for (int i = 1; i <= 5; ++i)
        f.addUploadingClient(reinterpret_cast<UpDownClient*>(static_cast<uintptr_t>(i)));

    QCOMPARE(f.upPriority(), kPrNormal);
}

void tst_KnownFile::updateAutoUpPriority_high()
{
    KnownFile f;
    f.setAutoUpPriority(true);

    // 0 clients → High (default for auto-priority)
    f.updateAutoUpPriority();
    QCOMPARE(f.upPriority(), kPrHigh);

    // 1 client → still High (count > 1 needed for Normal)
    f.addUploadingClient(reinterpret_cast<UpDownClient*>(uintptr_t{1}));
    QCOMPARE(f.upPriority(), kPrHigh);
}

void tst_KnownFile::updateAutoUpPriority_notAuto()
{
    KnownFile f;
    f.setAutoUpPriority(false);
    f.setUpPriority(kPrVeryHigh);

    QSignalSpy prioritySpy(f.notifier(), &FileNotifier::priorityChanged);
    prioritySpy.clear(); // clear the signal from setUpPriority

    // Manually call updateAutoUpPriority — should not change anything
    f.updateAutoUpPriority();
    QCOMPARE(f.upPriority(), kPrVeryHigh);
    QCOMPARE(prioritySpy.count(), 0);
}

// ---------------------------------------------------------------------------
// Signal emission tests
// ---------------------------------------------------------------------------

void tst_KnownFile::priorityChanged_signal()
{
    KnownFile f;
    QSignalSpy spy(f.notifier(), &FileNotifier::priorityChanged);

    f.setUpPriority(kPrHigh);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<uint8>(), kPrHigh);

    f.setUpPriority(kPrLow);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).value<uint8>(), kPrLow);
}

void tst_KnownFile::publishedED2K_signal()
{
    KnownFile f;
    QSignalSpy spy(f.notifier(), &FileNotifier::fileUpdated);

    f.setPublishedED2K(true);
    QVERIFY(f.publishedED2K());
    QCOMPARE(spy.count(), 1);

    f.setPublishedED2K(false);
    QVERIFY(!f.publishedED2K());
    QCOMPARE(spy.count(), 2);
}

// ---------------------------------------------------------------------------
// Metadata tests
// ---------------------------------------------------------------------------

void tst_KnownFile::removeMetaDataTags()
{
    KnownFile f;

    // Add some media tags manually
    f.addTagUnique(Tag(FT_MEDIA_LENGTH, uint32{120}));
    f.addTagUnique(Tag(FT_MEDIA_CODEC, QStringLiteral("DIVX")));
    f.addTagUnique(Tag(FT_MEDIA_ARTIST, QStringLiteral("Test Artist")));

    QVERIFY(f.getTag(FT_MEDIA_LENGTH) != nullptr);
    QVERIFY(f.getTag(FT_MEDIA_CODEC) != nullptr);
    QVERIFY(f.getTag(FT_MEDIA_ARTIST) != nullptr);

    f.removeMetaDataTags();

    QVERIFY(f.getTag(FT_MEDIA_LENGTH) == nullptr);
    QVERIFY(f.getTag(FT_MEDIA_CODEC) == nullptr);
    QVERIFY(f.getTag(FT_MEDIA_ARTIST) == nullptr);
    QCOMPARE(f.metaDataVer(), uint32{0});
}

void tst_KnownFile::updateMetaDataTags_aviFile()
{
    // Build a synthetic AVI and write it to a temp file
    QByteArray avi = buildMinimalAVI();
    QString path = writeTempFile(avi, QStringLiteral(".avi"));
    QVERIFY(!path.isEmpty());

    KnownFile f;
    f.setFileName(QStringLiteral("test.avi"));
    f.setFilePath(path);

    QSignalSpy spy(f.notifier(), &FileNotifier::metadataUpdated);

    f.updateMetaDataTags();

    // Should have emitted metadataUpdated
    QCOMPARE(spy.count(), 1);

    // Should have set metaDataVer
    QCOMPARE(f.metaDataVer(), uint32{1});

    // Should have created FT_MEDIA_CODEC tag (DIVX video)
    const Tag* codecTag = f.getTag(FT_MEDIA_CODEC);
    QVERIFY(codecTag != nullptr);
    QVERIFY(codecTag->isStr());
    QVERIFY(codecTag->strValue().contains(u"DivX", Qt::CaseInsensitive));
}

// ---------------------------------------------------------------------------
// Frame grabbing tests
// ---------------------------------------------------------------------------

void tst_KnownFile::requestGrabFrames_signal()
{
    KnownFile f;
    f.setFileName(QStringLiteral("movie.avi")); // video type
    f.setFilePath(QStringLiteral("/tmp/movie.avi"));

    QSignalSpy spy(f.notifier(), &FileNotifier::grabFramesRequested);

    f.requestGrabFrames(5, 1.5, true, 640);

    QCOMPARE(spy.count(), 1);
    auto args = spy.at(0);
    QCOMPARE(args.at(0).toString(), QStringLiteral("/tmp/movie.avi"));
    QCOMPARE(args.at(1).value<uint8>(), uint8{5});
    QCOMPARE(args.at(2).toDouble(), 1.5);
    QCOMPARE(args.at(3).toBool(), true);
    QCOMPARE(args.at(4).value<uint16>(), uint16{640});
}

void tst_KnownFile::requestGrabFrames_nonVideo()
{
    KnownFile f;
    f.setFileName(QStringLiteral("document.pdf")); // not video
    f.setFilePath(QStringLiteral("/tmp/document.pdf"));

    QSignalSpy spy(f.notifier(), &FileNotifier::grabFramesRequested);

    f.requestGrabFrames(5, 1.5, true, 640);

    // No signal emitted for non-video files
    QCOMPARE(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// Hashing tests (Phase 3)
// ---------------------------------------------------------------------------

void tst_KnownFile::createHash_emptyData()
{
    // Hash of empty input via QIODevice
    QByteArray empty;
    QBuffer buf(&empty);
    buf.open(QIODevice::ReadOnly);

    uint8 hash[16]{};
    KnownFile::createHash(buf, 0, hash, nullptr);

    // Verify it matches MD4 of empty data
    MD4Hasher hasher;
    hasher.add(static_cast<const void*>(nullptr), 0);
    hasher.finish();
    QVERIFY(md4equ(hash, hasher.getHash()));
}

void tst_KnownFile::createHash_smallData()
{
    // Create small test data (1KB)
    QByteArray testData(1024, 'A');
    QBuffer buf(&testData);
    buf.open(QIODevice::ReadOnly);

    uint8 hash[16]{};
    KnownFile::createHash(buf, static_cast<uint64>(testData.size()), hash, nullptr);

    // Verify via direct MD4
    MD4Hasher hasher;
    hasher.add(testData.constData(), static_cast<std::size_t>(testData.size()));
    hasher.finish();
    QVERIFY(md4equ(hash, hasher.getHash()));
}

void tst_KnownFile::createHash_exactPartSize()
{
    // Create exactly PARTSIZE bytes
    QByteArray testData(static_cast<int>(PARTSIZE), '\x42');
    QBuffer buf(&testData);
    buf.open(QIODevice::ReadOnly);

    uint8 hash[16]{};
    KnownFile::createHash(buf, PARTSIZE, hash, nullptr);

    // Should produce valid non-null hash
    QVERIFY(!isnulmd4(hash));
}

void tst_KnownFile::createFromFile_smallFile()
{
    eMule::testing::TempDir tmpDir;
    const QString filename = QStringLiteral("testfile.bin");
    const QString filePath = tmpDir.filePath(filename);

    // Write 5000 bytes of test data
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QByteArray data(5000, 'X');
    f.write(data);
    f.close();

    KnownFile kf;
    bool ok = kf.createFromFile(tmpDir.path(), filename);
    QVERIFY(ok);

    // Should have 1 part
    QCOMPARE(kf.partCount(), uint16{1});
    QCOMPARE(static_cast<uint64>(kf.fileSize()), uint64{5000});

    // Hash should be set and non-null
    QVERIFY(!kf.hasNullHash());

    // Filename set
    QCOMPARE(kf.fileName(), filename);
}

void tst_KnownFile::createFromFile_zeroFile()
{
    eMule::testing::TempDir tmpDir;
    const QString filename = QStringLiteral("empty.bin");
    const QString filePath = tmpDir.filePath(filename);

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    KnownFile kf;
    bool ok = kf.createFromFile(tmpDir.path(), filename);
    QVERIFY(ok);
    QCOMPARE(kf.partCount(), uint16{0});
    QCOMPARE(static_cast<uint64>(kf.fileSize()), uint64{0});
}

void tst_KnownFile::createFromFile_nonExistent()
{
    KnownFile kf;
    bool ok = kf.createFromFile(QStringLiteral("/nonexistent/dir"),
                                QStringLiteral("nofile.bin"));
    QVERIFY(!ok);
}

void tst_KnownFile::createFromFile_setsDate()
{
    eMule::testing::TempDir tmpDir;
    const QString filename = QStringLiteral("dated.bin");
    const QString filePath = tmpDir.filePath(filename);

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello", 5);
    f.close();

    KnownFile kf;
    QVERIFY(kf.createFromFile(tmpDir.path(), filename));

    // File date should be set to something recent
    time_t now = std::time(nullptr);
    QVERIFY(kf.utcFileDate() > now - 60);
    QVERIFY(kf.utcFileDate() <= now + 1);
}

void tst_KnownFile::createFromFile_progressCallback()
{
    eMule::testing::TempDir tmpDir;
    const QString filename = QStringLiteral("progress.bin");
    const QString filePath = tmpDir.filePath(filename);

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QByteArray data(10000, 'P');
    f.write(data);
    f.close();

    int lastProgress = -1;
    int callCount = 0;

    KnownFile kf;
    QVERIFY(kf.createFromFile(tmpDir.path(), filename,
                              [&](int percent) {
                                  QVERIFY(percent >= lastProgress);
                                  lastProgress = percent;
                                  ++callCount;
                              }));

    QVERIFY(callCount >= 1);
    QCOMPARE(lastProgress, 100);
}

void tst_KnownFile::updatePartsInfo_noClients()
{
    KnownFile kf;
    kf.setFileSize(PARTSIZE * 2 + 100);
    kf.updatePartsInfo();

    QCOMPARE(kf.availPartFrequency().size(), static_cast<size_t>(3));
    for (auto freq : kf.availPartFrequency())
        QCOMPARE(freq, uint16{0});
}

void tst_KnownFile::publishSrc_timing()
{
    KnownFile kf;

    // First call should succeed (lastPublishTimeKadSrc starts at 0, which is < now)
    QVERIFY(kf.publishSrc());

    // Immediate second call should fail (just set the time into the future)
    QVERIFY(!kf.publishSrc());

    // Simulate time passing beyond the republish interval
    kf.setLastPublishTimeKadSrc(std::time(nullptr) - 1);
    QVERIFY(kf.publishSrc());
}

void tst_KnownFile::publishNotes_timing()
{
    KnownFile kf;

    // No comment and no rating — should always return false
    QVERIFY(!kf.publishNotes());

    // Set a rating — now it should be publishable
    kf.addTagUnique(Tag(FT_FILERATING, uint32{3}));
    QVERIFY(kf.publishNotes());

    // Immediate second call should fail (time set into the future)
    QVERIFY(!kf.publishNotes());

    // Simulate time passage
    kf.setLastPublishTimeKadNotes(std::time(nullptr) - 1);
    QVERIFY(kf.publishNotes());
}

void tst_KnownFile::createHashFromMemory_basic()
{
    const uint8 data[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8 hash[16]{};
    QVERIFY(KnownFile::createHashFromMemory(data, 8, hash, nullptr));
    QVERIFY(!isnulmd4(hash));

    // Verify consistency
    uint8 hash2[16]{};
    QVERIFY(KnownFile::createHashFromMemory(data, 8, hash2, nullptr));
    QVERIFY(md4equ(hash, hash2));
}

void tst_KnownFile::createHashFromFile_basic()
{
    eMule::testing::TempDir tmpDir;
    const QString filePath = tmpDir.filePath(QStringLiteral("hashtest.bin"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QByteArray data(256, '\xAA');
    f.write(data);
    f.close();

    uint8 hash[16]{};
    QVERIFY(KnownFile::createHashFromFile(filePath, 256, hash, nullptr));
    QVERIFY(!isnulmd4(hash));
}

QTEST_MAIN(tst_KnownFile)
#include "tst_KnownFile.moc"
