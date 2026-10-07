/// @file tst_PartFileWriteThread.cpp
/// @brief The disk worker: download writes and part hashing off the main thread.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/PartFileWriteThread.h"
#include "prefs/Preferences.h"

#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

#include <array>
#include <vector>

using namespace eMule;
using namespace eMule::testing;

class tst_PartFileWriteThread : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // the worker alone
    void job_writesItsChunksAndHashesTheParts();
    void job_thatCannotWriteHandsItsBytesBack();
    void stop_finishesWhatIsQueued();

    // PartFile through the worker
    void asyncFlush_dataIsNotServableUntilItLanded();
    void asyncFlush_lastBlockOfABadPartReopensIt();
    void asyncFlush_lastBlockOfAGoodFileCompletesIt();
    void syncFlush_waitsForTheOneInFlight();
    void deletedFile_stillGetsItsWrite();
    void writeFailure_keepsTheBuffer();

private:
    [[nodiscard]] QString makeFile(const QString& name, qint64 size);
    static std::vector<uint8> pattern(std::size_t size, uint8 seed);
    static std::array<uint8, 16> md4Of(const std::vector<uint8>& data);

    TempDir m_tmp;
};

namespace {

/// Installs a worker as the global one for a test.
struct WriterScope {
    WriterScope()
    {
        saved = theApp.partFileWriter;
        theApp.partFileWriter = &writer;
    }
    ~WriterScope() { theApp.partFileWriter = saved; }
    PartFileWriteThread writer;
    PartFileWriteThread* saved;
};

} // namespace

void tst_PartFileWriteThread::initTestCase()
{
    thePrefs.setIncomingDir(m_tmp.filePath(QStringLiteral("incoming")));
    QDir().mkpath(thePrefs.incomingDir());
}

QString tst_PartFileWriteThread::makeFile(const QString& name, qint64 size)
{
    const QString path = m_tmp.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || !f.resize(size))
        return {};
    return path;
}

std::vector<uint8> tst_PartFileWriteThread::pattern(std::size_t size, uint8 seed)
{
    std::vector<uint8> data(size);
    for (std::size_t i = 0; i < size; ++i)
        data[i] = static_cast<uint8>(seed + i * 7);
    return data;
}

std::array<uint8, 16> tst_PartFileWriteThread::md4Of(const std::vector<uint8>& data)
{
    std::array<uint8, 16> hash{};
    KnownFile::createHashFromMemory(data.data(), static_cast<uint32>(data.size()), hash.data(), nullptr);
    return hash;
}

// ---------------------------------------------------------------------------
// The worker alone
// ---------------------------------------------------------------------------

void tst_PartFileWriteThread::job_writesItsChunksAndHashesTheParts()
{
    const QString path = makeFile(QStringLiteral("plain.part"), 2000);
    QVERIFY(!path.isEmpty());
    const std::vector<uint8> content = pattern(2000, 3);

    PartFileWriteThread writer;

    PartFileWriteJob job;
    job.token = PartFileWriteThread::nextToken();
    job.partPath = path;
    // Out of order, as blocks arrive.
    job.chunks.push_back({1000, {content.begin() + 1000, content.end()}});
    job.chunks.push_back({0, {content.begin(), content.begin() + 1000}});
    PartDigestRequest request;
    request.part = 0;
    request.start = 0;
    request.length = 2000;
    job.digests.push_back(request);
    const quint64 token = job.token;
    writer.enqueue(std::move(job));

    auto result = writer.waitFor(token);
    QVERIFY(result.has_value());
    QVERIFY2(result->written, qPrintable(result->error));
    QVERIFY(result->chunks.empty());
    QCOMPARE(result->digests.size(), size_t{1});
    QVERIFY(result->digests.at(0).read);
    QVERIFY(result->digests.at(0).md4 == md4Of(content));
    QVERIFY(!result->digests.at(0).aichValid);        // not asked for

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray(reinterpret_cast<const char*>(content.data()), 2000));

    // A result is handed out once.
    QVERIFY(!writer.takeResult(token).has_value());
    QVERIFY(!writer.waitFor(token).has_value());
}

void tst_PartFileWriteThread::job_thatCannotWriteHandsItsBytesBack()
{
    PartFileWriteThread writer;

    PartFileWriteJob job;
    job.token = PartFileWriteThread::nextToken();
    job.partPath = m_tmp.filePath(QStringLiteral("no/such/dir/x.part"));
    job.chunks.push_back({0, pattern(64, 1)});
    const quint64 token = job.token;
    writer.enqueue(std::move(job));

    auto result = writer.waitFor(token);
    QVERIFY(result.has_value());
    QVERIFY(!result->written);
    QVERIFY(!result->error.isEmpty());
    QCOMPARE(result->chunks.size(), size_t{1});
    QVERIFY(result->chunks.front().data == pattern(64, 1));
}

// A queued write is downloaded data. The thread used to clear its queue on stop.
void tst_PartFileWriteThread::stop_finishesWhatIsQueued()
{
    const QString path = makeFile(QStringLiteral("drain.part"), 64 * 100);
    {
        PartFileWriteThread writer;
            for (uint64 i = 0; i < 100; ++i) {
            PartFileWriteJob job;
            job.token = PartFileWriteThread::nextToken();
            job.partPath = path;
            job.chunks.push_back({i * 64, std::vector<uint8>(64, static_cast<uint8>(i + 1))});
            writer.enqueue(std::move(job));
        }
    }   // destroyed with most of them still queued

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray all = f.readAll();
    QCOMPARE(all.size(), qsizetype{6400});
    for (int i = 0; i < 100; ++i)
        QCOMPARE(static_cast<uint8>(all[i * 64 + 63]), static_cast<uint8>(i + 1));
}

// ---------------------------------------------------------------------------
// PartFile through the worker
// ---------------------------------------------------------------------------

void tst_PartFileWriteThread::asyncFlush_dataIsNotServableUntilItLanded()
{
    const QString dir = m_tmp.filePath(QStringLiteral("t1"));
    QDir().mkpath(dir);
    const std::vector<uint8> content = pattern(1000, 9);

    WriterScope scope;
    PartFile pf;
    pf.setFileSize(1000);
    pf.setFileHash(md4Of(content).data());
    QVERIFY(pf.createPartFile(dir));

    pf.writeToBuffer(500, content.data(), 0, 499, nullptr);
    pf.flushBufferAsync();
    QVERIFY(pf.isFlushPending());
    QVERIFY(pf.isComplete(0, 499));                        // the gap is filled...
    QVERIFY2(!pf.isCompleteBDSafe(0, 499), "data still with the worker was offered for upload");

    QTRY_VERIFY_WITH_TIMEOUT(!pf.isFlushPending(), 5000);
    QVERIFY(pf.isCompleteBDSafe(0, 499));
    QVERIFY(!pf.isCorruptedPart(0));
}

// The last block of a part: the worker hashes it, the verdict is the main thread's.
void tst_PartFileWriteThread::asyncFlush_lastBlockOfABadPartReopensIt()
{
    const QString dir = m_tmp.filePath(QStringLiteral("t2"));
    QDir().mkpath(dir);
    const std::vector<uint8> content = pattern(1000, 9);
    const std::vector<uint8> wrong = pattern(1000, 77);

    WriterScope scope;
    PartFile pf;
    pf.setFileSize(1000);
    pf.setFileHash(md4Of(content).data());                 // expects `content`
    QVERIFY(pf.createPartFile(dir));

    pf.writeToBuffer(500, wrong.data(), 0, 499, nullptr);
    pf.writeToBuffer(500, wrong.data() + 500, 500, 999, nullptr);   // no gap left: flushes
    QVERIFY(pf.isFlushPending());
    QVERIFY2(!pf.isCorruptedPart(0), "the part was hashed on the calling thread");

    QTRY_VERIFY_WITH_TIMEOUT(!pf.isFlushPending(), 5000);
    QVERIFY(pf.isCorruptedPart(0));
    QVERIFY(!pf.isComplete(0, 999));
    QVERIFY(pf.status() != PartFileStatus::Complete && pf.status() != PartFileStatus::Completing);
}

void tst_PartFileWriteThread::asyncFlush_lastBlockOfAGoodFileCompletesIt()
{
    const QString dir = m_tmp.filePath(QStringLiteral("t2b"));
    QDir().mkpath(dir);
    const std::vector<uint8> content = pattern(1000, 9);

    WriterScope scope;
    PartFile pf;
    pf.setFileName(QStringLiteral("good.bin"));
    pf.setFileSize(1000);
    pf.setFileHash(md4Of(content).data());
    QVERIFY(pf.createPartFile(dir));

    pf.writeToBuffer(500, content.data(), 0, 499, nullptr);
    pf.flushBufferAsync();
    // The rest arrives while the first half is still out: it must not be left behind.
    pf.writeToBuffer(500, content.data() + 500, 500, 999, nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(pf.status() == PartFileStatus::Complete, 10000);
    QFile f(thePrefs.incomingDir() + QStringLiteral("/good.bin"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray(reinterpret_cast<const char*>(content.data()), 1000));
}

void tst_PartFileWriteThread::syncFlush_waitsForTheOneInFlight()
{
    const QString dir = m_tmp.filePath(QStringLiteral("t3"));
    QDir().mkpath(dir);
    const std::vector<uint8> content = pattern(1000, 5);

    WriterScope scope;
    PartFile pf;
    pf.setFileSize(1000);
    pf.setFileHash(md4Of(content).data());
    QVERIFY(pf.createPartFile(dir));

    pf.writeToBuffer(100, content.data(), 0, 99, nullptr);
    pf.flushBufferAsync();
    pf.writeToBuffer(100, content.data() + 100, 100, 199, nullptr);
    pf.flushBufferAsync();                                  // one job per file: stays buffered
    QVERIFY(pf.isFlushPending());

    pf.flushBuffer();                                       // stop, shutdown, completion
    QVERIFY(!pf.isFlushPending());
    QVERIFY(pf.isCompleteBDSafe(0, 199));

    // The queued notification for the job finds nothing left to do.
    QCoreApplication::processEvents();
    QVERIFY(pf.isCompleteBDSafe(0, 199));

    QFile f(pf.dataFilePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.read(200), QByteArray(reinterpret_cast<const char*>(content.data()), 200));
}

void tst_PartFileWriteThread::deletedFile_stillGetsItsWrite()
{
    const QString dir = m_tmp.filePath(QStringLiteral("t4"));
    QDir().mkpath(dir);
    const std::vector<uint8> content = pattern(1000, 11);

    WriterScope scope;
    auto* pf = new PartFile;
    pf->setFileSize(1000);
    pf->setFileHash(md4Of(content).data());
    QVERIFY(pf->createPartFile(dir));

    const QString partPath = pf->dataFilePath();
    pf->writeToBuffer(300, content.data(), 0, 299, nullptr);
    pf->flushBufferAsync();
    QVERIFY(pf->isFlushPending());
    delete pf;                                              // collects the write itself

    QCoreApplication::processEvents();                      // the late notification: no file, no crash

    QFile f(partPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.read(300), QByteArray(reinterpret_cast<const char*>(content.data()), 300));
}

void tst_PartFileWriteThread::writeFailure_keepsTheBuffer()
{
#ifdef Q_OS_WIN
    // An open .part cannot be deleted from under its handle here
    QSKIP("Windows does not let an open file vanish");
#endif
    const QString dir = m_tmp.filePath(QStringLiteral("t5"));
    QDir().mkpath(dir);
    const std::vector<uint8> content = pattern(1000, 13);

    WriterScope scope;
    PartFile pf;
    pf.setFileSize(1000);
    pf.setFileHash(md4Of(content).data());
    QVERIFY(pf.createPartFile(dir));

    // The .part vanishes under it (a pulled disk, a deleted temp directory).
    const QString partPath = pf.dataFilePath();
    QVERIFY(QFile::remove(partPath));
    QVERIFY(QDir(dir).removeRecursively());

    pf.writeToBuffer(100, content.data(), 0, 99, nullptr);
    pf.flushBufferAsync();
    QTRY_VERIFY_WITH_TIMEOUT(!pf.isFlushPending(), 5000);
    QVERIFY2(!pf.isCompleteBDSafe(0, 99), "a failed write was taken for written");

    // With the directory back the same bytes go out.
    QDir().mkpath(dir);
    {
        QFile recreated(partPath);
        QVERIFY(recreated.open(QIODevice::WriteOnly) && recreated.resize(1000));
    }
    pf.flushBuffer();
    QVERIFY(pf.isCompleteBDSafe(0, 99));
    QFile f(partPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.read(100), QByteArray(reinterpret_cast<const char*>(content.data()), 100));
}

QTEST_GUILESS_MAIN(tst_PartFileWriteThread)
#include "tst_PartFileWriteThread.moc"
