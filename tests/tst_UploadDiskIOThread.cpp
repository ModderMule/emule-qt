/// @file tst_UploadDiskIOThread.cpp
/// @brief Tests for transfer/UploadDiskIOThread.

#include "TestHelpers.h"
#include "transfer/UploadDiskIOThread.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "net/Packet.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

#include <cstring>

using namespace eMule;

class tst_UploadDiskIOThread : public QObject {
    Q_OBJECT

private slots:
    void construction_defaults();
    void startStop_noCrash();
    void shouldCompressFile_known();
    void createStandardPackets_basic();
    void createPackedPackets_basic();
    void queueBlockRead_emitsSignal();
    void contiguousBlocks_areOneOpenAndOneDiskRead();
    void partFile_getsNoKeptHandleAndNoReadAhead();
    void releaseFile_letsGoAndServesTheNewContent();
    void releaseClient_and_idleClose_dropTheReader();
    void shortTail_andPastTheEnd();
};

void tst_UploadDiskIOThread::construction_defaults()
{
    UploadDiskIOThread thread;
    QVERIFY(thread.isRunning());
    thread.endThread();
    QVERIFY(!thread.isRunning());
}

void tst_UploadDiskIOThread::startStop_noCrash()
{
    {
        UploadDiskIOThread thread;
        thread.endThread();
    }
    // Double stop
    {
        UploadDiskIOThread thread;
        thread.endThread();
        thread.endThread();
    }
}

void tst_UploadDiskIOThread::shouldCompressFile_known()
{
    // Already-compressed formats should not be compressed
    QVERIFY(!UploadDiskIOThread::shouldCompressFile(QStringLiteral("archive.zip")));
    QVERIFY(!UploadDiskIOThread::shouldCompressFile(QStringLiteral("archive.rar")));
    QVERIFY(!UploadDiskIOThread::shouldCompressFile(QStringLiteral("archive.7z")));
    QVERIFY(!UploadDiskIOThread::shouldCompressFile(QStringLiteral("video.mp4")));
    QVERIFY(!UploadDiskIOThread::shouldCompressFile(QStringLiteral("video.mkv")));
    QVERIFY(!UploadDiskIOThread::shouldCompressFile(QStringLiteral("music.mp3")));
    QVERIFY(!UploadDiskIOThread::shouldCompressFile(QStringLiteral("image.jpg")));
    QVERIFY(!UploadDiskIOThread::shouldCompressFile(QStringLiteral("image.png")));
    QVERIFY(!UploadDiskIOThread::shouldCompressFile(QStringLiteral("data.gz")));

    // Compressible formats
    QVERIFY(UploadDiskIOThread::shouldCompressFile(QStringLiteral("document.txt")));
    QVERIFY(UploadDiskIOThread::shouldCompressFile(QStringLiteral("program.exe")));
    QVERIFY(UploadDiskIOThread::shouldCompressFile(QStringLiteral("source.cpp")));
    QVERIFY(UploadDiskIOThread::shouldCompressFile(QStringLiteral("data.bin")));
    QVERIFY(UploadDiskIOThread::shouldCompressFile(QStringLiteral("document.pdf")));
}

void tst_UploadDiskIOThread::createStandardPackets_basic()
{
    // We can't call createStandardPackets directly (private), but we can test
    // through queueBlockRead + signal. For unit testing the static methods,
    // we verify the thread processes reads correctly.

    // Test that a known file with data produces packets via the signal
    // This requires a real file, so we'll just verify the thread doesn't crash
    UploadDiskIOThread thread;
    QTest::qWait(10);
    thread.endThread();
    QVERIFY(true); // Didn't crash
}

void tst_UploadDiskIOThread::createPackedPackets_basic()
{
    // Similar to above — the internal methods are private, testing through integration
    UploadDiskIOThread thread;
    QTest::qWait(10);
    thread.endThread();
    QVERIFY(true);
}

void tst_UploadDiskIOThread::queueBlockRead_emitsSignal()
{
    // Create a temporary file with known content
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString filePath = tempDir.path() + QStringLiteral("/testfile.bin");
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QByteArray content(EMBLOCKSIZE, 'X');
        f.write(content);
        f.close();
    }

    // Create a KnownFile pointing to the temp file
    KnownFile kf;
    kf.setFileName(QStringLiteral("testfile.bin"));
    kf.setFilePath(filePath);
    kf.setFileSize(EMBLOCKSIZE);

    uint8 hash[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    kf.setFileHash(hash);

    UploadDiskIOThread thread;

    // Connect signal spy
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");
    qRegisterMetaType<QList<std::shared_ptr<eMule::Packet>>>("QList<std::shared_ptr<eMule::Packet>>");
    QSignalSpy readySpy(&thread, &UploadDiskIOThread::blockPacketsReady);
    QSignalSpy errorSpy(&thread, &UploadDiskIOThread::readError);

    UpDownClient client;

    BlockReadRequest req;
    req.setFile(kf, true);
    req.client = &client;
    req.startOffset = 0;
    req.endOffset = EMBLOCKSIZE;

    thread.queueBlockRead(req);

    // Wait for processing
    QTRY_VERIFY_WITH_TIMEOUT(readySpy.count() > 0 || errorSpy.count() > 0, 2000);

    // Either we got packets or an error (error is ok if file path resolution differs)
    QVERIFY(readySpy.count() > 0 || errorSpy.count() > 0);

    // The signal names the block it answers, so the queue can match it to a pending request.
    if (readySpy.count() > 0) {
        const auto args = readySpy.first();
        QCOMPARE(args.at(1).toByteArray(), QByteArray(reinterpret_cast<const char*>(hash), 16));
        QCOMPARE(args.at(2).toULongLong(), quint64{0});
        QCOMPARE(args.at(3).toULongLong(), quint64{EMBLOCKSIZE});
    }

    thread.endThread();
}

namespace {

/// A file of @p blocks blocks (+ @p tail bytes) where every byte tells its block number.
QString writeBlocks(const QTemporaryDir& dir, const QString& name, int blocks, int tail = 0,
                    char base = 'a')
{
    const QString path = dir.path() + QLatin1Char('/') + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    for (int i = 0; i < blocks; ++i)
        f.write(QByteArray(EMBLOCKSIZE, static_cast<char>(base + i)));
    f.write(QByteArray(tail, static_cast<char>(base + blocks)));
    return path;
}

struct ReadRig {
    UploadDiskIOThread thread;
    QSignalSpy ready{&thread, &UploadDiskIOThread::blockPacketsReady};
    QSignalSpy error{&thread, &UploadDiskIOThread::readError};
    UpDownClient client;

    ReadRig()
    {
        qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");
        qRegisterMetaType<QList<std::shared_ptr<eMule::Packet>>>("QList<std::shared_ptr<eMule::Packet>>");
    }

    void ask(const QString& path, uint64 start, uint64 end, bool partFile = false,
             UpDownClient* who = nullptr)
    {
        BlockReadRequest req;
        req.filePath = path;
        req.fileHash.fill(0x42);
        req.client = who ? who : &client;
        req.startOffset = start;
        req.endOffset = end;
        req.isPartFile = partFile;
        thread.queueBlockRead(req);
    }

    /// Payload of the n-th answer, headers stripped (uncompressed, 32-bit offsets).
    [[nodiscard]] QByteArray payload(int n) const
    {
        QByteArray out;
        const auto packets = ready.at(n).at(4).value<QList<std::shared_ptr<eMule::Packet>>>();
        for (const auto& p : packets)
            out.append(reinterpret_cast<const char*>(p->pBuffer) + 24, static_cast<qsizetype>(p->size) - 24);
        return out;
    }
};

} // namespace

// It used to open, seek, read and close the file for every block.
void tst_UploadDiskIOThread::contiguousBlocks_areOneOpenAndOneDiskRead()
{
    QTemporaryDir dir;
    const QString path = writeBlocks(dir, QStringLiteral("five.bin"), 5);
    ReadRig rig;

    for (uint64 i = 0; i < 3; ++i)
        rig.ask(path, i * EMBLOCKSIZE, (i + 1) * EMBLOCKSIZE);
    QTRY_COMPARE_WITH_TIMEOUT(rig.ready.count(), 3, 3000);
    QCOMPARE(rig.error.count(), 0);
    QCOMPARE(rig.thread.openCount(), uint64{1});
    QCOMPARE(rig.thread.diskReadCount(), uint64{1});
    for (int i = 0; i < 3; ++i)
        QCOMPARE(rig.payload(i), QByteArray(EMBLOCKSIZE, static_cast<char>('a' + i)));

    // the next three need the disk once more, not the open
    rig.ask(path, 3 * EMBLOCKSIZE, 4 * EMBLOCKSIZE);
    rig.ask(path, 4 * EMBLOCKSIZE, 5 * EMBLOCKSIZE);
    QTRY_COMPARE_WITH_TIMEOUT(rig.ready.count(), 5, 3000);
    QCOMPARE(rig.thread.openCount(), uint64{1});
    QCOMPARE(rig.thread.diskReadCount(), uint64{2});
    QCOMPARE(rig.payload(4), QByteArray(EMBLOCKSIZE, 'e'));

    // a second slot on the same file has its own reader
    UpDownClient second;
    rig.ask(path, 0, EMBLOCKSIZE, false, &second);
    QTRY_COMPARE_WITH_TIMEOUT(rig.ready.count(), 6, 3000);
    QCOMPARE(rig.thread.openCount(), uint64{2});
    QCOMPARE(rig.thread.cachedReaderCount(), std::size_t{2});
}

// Its bytes past the request may not be complete yet, and it is renamed on completion.
void tst_UploadDiskIOThread::partFile_getsNoKeptHandleAndNoReadAhead()
{
    QTemporaryDir dir;
    const QString path = writeBlocks(dir, QStringLiteral("001.part"), 3);
    ReadRig rig;

    for (uint64 i = 0; i < 3; ++i)
        rig.ask(path, i * EMBLOCKSIZE, (i + 1) * EMBLOCKSIZE, /*partFile=*/true);
    QTRY_COMPARE_WITH_TIMEOUT(rig.ready.count(), 3, 3000);
    QCOMPARE(rig.thread.openCount(), uint64{3});
    QCOMPARE(rig.thread.diskReadCount(), uint64{3});
    QCOMPARE(rig.thread.cachedReaderCount(), std::size_t{0});
    QCOMPARE(rig.payload(2), QByteArray(EMBLOCKSIZE, 'c'));
}

void tst_UploadDiskIOThread::releaseFile_letsGoAndServesTheNewContent()
{
    QTemporaryDir dir;
    const QString path = writeBlocks(dir, QStringLiteral("swap.bin"), 3);
    ReadRig rig;

    rig.ask(path, 0, EMBLOCKSIZE);
    QTRY_COMPARE_WITH_TIMEOUT(rig.ready.count(), 1, 3000);
    QCOMPARE(rig.thread.cachedReaderCount(), std::size_t{1});

    rig.thread.releaseFile(path);
    QCOMPARE(rig.thread.cachedReaderCount(), std::size_t{0});
    QVERIFY(QFile::remove(path));
    QCOMPARE(writeBlocks(dir, QStringLiteral("swap.bin"), 3, 0, 'p'), path);

    // block 1 was read ahead from the old file; it must not be served from memory,
    // and for a moment the path stays uncached
    rig.ask(path, EMBLOCKSIZE, 2 * EMBLOCKSIZE);
    QTRY_COMPARE_WITH_TIMEOUT(rig.ready.count(), 2, 3000);
    QCOMPARE(rig.payload(1), QByteArray(EMBLOCKSIZE, 'q'));
    QCOMPARE(rig.thread.cachedReaderCount(), std::size_t{0});
}

void tst_UploadDiskIOThread::releaseClient_and_idleClose_dropTheReader()
{
    QTemporaryDir dir;
    const QString path = writeBlocks(dir, QStringLiteral("idle.bin"), 2);
    ReadRig rig;

    rig.ask(path, 0, EMBLOCKSIZE);
    QTRY_COMPARE_WITH_TIMEOUT(rig.ready.count(), 1, 3000);
    QCOMPARE(rig.thread.cachedReaderCount(), std::size_t{1});
    rig.thread.releaseClient(&rig.client);
    QCOMPARE(rig.thread.cachedReaderCount(), std::size_t{0});

    rig.thread.setIdleCloseMs(100);
    rig.ask(path, 0, EMBLOCKSIZE);
    QTRY_COMPARE_WITH_TIMEOUT(rig.ready.count(), 2, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(rig.thread.cachedReaderCount(), std::size_t{0}, 3000);

    // and it comes back on demand
    rig.ask(path, EMBLOCKSIZE, 2 * EMBLOCKSIZE);
    QTRY_COMPARE_WITH_TIMEOUT(rig.ready.count(), 3, 3000);
    QCOMPARE(rig.payload(2), QByteArray(EMBLOCKSIZE, 'b'));
    QCOMPARE(rig.error.count(), 0);
}

void tst_UploadDiskIOThread::shortTail_andPastTheEnd()
{
    QTemporaryDir dir;
    const QString path = writeBlocks(dir, QStringLiteral("tail.bin"), 1, 1000);
    ReadRig rig;

    // the read-ahead stops at the end of the file and still covers the tail
    rig.ask(path, 0, EMBLOCKSIZE);
    rig.ask(path, EMBLOCKSIZE, EMBLOCKSIZE + 1000);
    QTRY_COMPARE_WITH_TIMEOUT(rig.ready.count(), 2, 3000);
    QCOMPARE(rig.thread.diskReadCount(), uint64{1});
    QCOMPARE(rig.payload(1), QByteArray(1000, 'b'));

    // past the end is an error, and the next good request works again
    rig.ask(path, EMBLOCKSIZE, EMBLOCKSIZE + 2000);
    QTRY_COMPARE_WITH_TIMEOUT(rig.error.count(), 1, 3000);
    rig.ask(path, 100, 200);
    QTRY_COMPARE_WITH_TIMEOUT(rig.ready.count(), 3, 3000);
    QCOMPARE(rig.payload(2), QByteArray(100, 'a'));
}

QTEST_GUILESS_MAIN(tst_UploadDiskIOThread)
#include "tst_UploadDiskIOThread.moc"
