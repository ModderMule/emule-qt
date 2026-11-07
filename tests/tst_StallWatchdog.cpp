/// @file tst_StallWatchdog.cpp
/// @brief Tests for StallWatchdog — a blocked main thread is reported once, with a file —
/// and BackgroundFileWriter, which keeps file syncs off the main thread (report 23).

#include "TestHelpers.h"

#include "utils/BackgroundFileWriter.h"
#include "utils/StallWatchdog.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <semaphore>

using namespace eMule;
using namespace std::chrono_literals;

namespace {

/// Busy, not asleep: the stack signal would cut a sleep short
void blockFor(std::chrono::milliseconds ms)
{
    const auto until = std::chrono::steady_clock::now() + ms;
    while (std::chrono::steady_clock::now() < until) {
    }
}

void runLoopFor(std::chrono::milliseconds ms)
{
    const auto until = std::chrono::steady_clock::now() + ms;
    while (std::chrono::steady_clock::now() < until)
        QTest::qWait(20);
}

} // namespace

class tst_StallWatchdog : public QObject {
    Q_OBJECT

private slots:
    void runningLoop_noReport();
    void blockedLoop_reportedOnceWithStack();
    void backgroundWriter_newestWinsAndReadsBack();
    void backgroundWriter_discardDropsQueued();
};

void tst_StallWatchdog::runningLoop_noReport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StallWatchdog watchdog(dir.path(), 400ms);
    runLoopFor(1500ms);
    QCOMPARE(watchdog.stallCount(), 0);
    QVERIFY(watchdog.lastReportPath().isEmpty());
}

void tst_StallWatchdog::blockedLoop_reportedOnceWithStack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StallWatchdog watchdog(dir.path(), 400ms);
    runLoopFor(300ms);
    blockFor(2000ms);   // one stall, however long
    runLoopFor(500ms);  // the loop runs again: no second report
    QCOMPARE(watchdog.stallCount(), 1);

    const QString path = watchdog.lastReportPath();
    QVERIFY(path.endsWith(QStringLiteral(".stall")));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray text = f.readAll();
    QVERIFY(text.contains("main thread stalled for"));
    QVERIFY(text.contains("Main thread stack:"));
#if (!defined(Q_OS_WIN) && __has_include(<execinfo.h>)) || (defined(Q_OS_WIN) && (defined(_M_X64) || defined(_M_ARM64)))
    QVERIFY2(text.contains("#2 "), text.constData());   // frames past the top one
#endif
}

namespace {

QByteArray fileBytes(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

void tst_StallWatchdog::backgroundWriter_newestWinsAndReadsBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString held = dir.filePath(QStringLiteral("held"));
    const QString path = dir.filePath(QStringLiteral("sub/state"));
    std::binary_semaphore release(0);
    std::atomic<int> writes{0};
    BackgroundFileWriter writer;
    // the worker is busy with the first file until released
    writer.write(held, "h", [&release](const QString& p, const QByteArray& d) {
        release.acquire();
        return BackgroundFileWriter::saveFile(p, d);
    });
    const auto counted = [&writes](const QString& p, const QByteArray& d) {
        ++writes;
        return BackgroundFileWriter::saveFile(p, d);
    };
    writer.write(path, "one", counted);
    writer.write(path, "two", counted);
    // not on disk yet, read back from the queue
    QVERIFY(!QFile::exists(path));
    QCOMPARE(writer.pending(path).value_or(QByteArray()), QByteArray("two"));
    QCOMPARE(writer.pending(held).value_or(QByteArray()), QByteArray("h"));
    release.release();
    writer.flush();
    QCOMPARE(fileBytes(path), QByteArray("two"));   // folder made, newest only
    QCOMPARE(writes.load(), 1);
    QCOMPARE(fileBytes(held), QByteArray("h"));
    QVERIFY(!writer.pending(path));
}

void tst_StallWatchdog::backgroundWriter_discardDropsQueued()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString held = dir.filePath(QStringLiteral("held"));
    const QString one = dir.filePath(QStringLiteral("one"));
    const QString nested = dir.filePath(QStringLiteral("kept/a/b"));
    const QString kept = dir.filePath(QStringLiteral("other"));
    std::binary_semaphore release(0);
    BackgroundFileWriter writer;
    writer.write(held, "h", [&release](const QString& p, const QByteArray& d) {
        release.acquire();
        return BackgroundFileWriter::saveFile(p, d);
    });
    writer.write(one, "1", &BackgroundFileWriter::saveFile);
    writer.write(nested, "n", &BackgroundFileWriter::saveFile);
    writer.write(kept, "k", &BackgroundFileWriter::saveFile);
    writer.discard(one);
    writer.discardUnder(dir.filePath(QStringLiteral("kept")));
    QVERIFY(!writer.pending(one));
    QVERIFY(!writer.pending(nested));
    release.release();
    writer.flush();
    QVERIFY(!QFile::exists(one));
    QVERIFY(!QFile::exists(nested));
    QCOMPARE(fileBytes(kept), QByteArray("k"));
}

QTEST_GUILESS_MAIN(tst_StallWatchdog)
#include "tst_StallWatchdog.moc"
