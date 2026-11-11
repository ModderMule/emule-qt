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
#include <csignal>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <semaphore>
#include <thread>

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
    void kernelBlocked_stackAddedOnceCallReturns();
    void linuxSyscall_namesTheFile();
    void backgroundWriter_newestWinsAndReadsBack();
    void backgroundWriter_discardDropsQueued();
    void backgroundWriter_producedOnWorker();
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

void tst_StallWatchdog::kernelBlocked_stackAddedOnceCallReturns()
{
#if defined(Q_OS_WIN) || !__has_include(<execinfo.h>)
    QSKIP("signal stack capture only");
#else
    // A held signal is what a thread asleep in the kernel does with it (#169): it is
    // taken once the call returns, after the report was written
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    StallWatchdog watchdog(dir.path(), 400ms);
    runLoopFor(300ms);
    sigset_t held;
    sigemptyset(&held);
    sigaddset(&held, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &held, nullptr);
    blockFor(3000ms);   // report at 400 ms, its 2 s wait for the stack runs out
    pthread_sigmask(SIG_UNBLOCK, &held, nullptr);
    runLoopFor(1500ms);
    QCOMPARE(watchdog.stallCount(), 1);

    QFile f(watchdog.lastReportPath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray text = f.readAll();
    QVERIFY2(text.contains("no answer in 2 s"), text.constData());
    const qsizetype late = text.indexOf("Main thread stack once the kernel call returned:");
    QVERIFY2(late > 0, text.constData());
    QVERIFY2(text.indexOf("#2 ", late) > late, text.constData());
#endif
}

void tst_StallWatchdog::linuxSyscall_namesTheFile()
{
#ifndef Q_OS_LINUX
    QSKIP("Linux /proc only");
#else
    // Main thread asleep in read() on a FIFO; a helper writes after the report
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray fifo = QFile::encodeName(dir.filePath(QStringLiteral("stall.fifo")));
    QCOMPARE(::mkfifo(fifo.constData(), 0600), 0);
    const int fd = ::open(fifo.constData(), O_RDWR);   // RDWR: open does not wait for a writer
    QVERIFY(fd >= 0);

    StallWatchdog watchdog(dir.path(), 400ms);
    runLoopFor(300ms);
    sigset_t held;   // the stack signal would cut the read short
    sigemptyset(&held);
    sigaddset(&held, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &held, nullptr);
    std::thread writer([fd] {
        std::this_thread::sleep_for(3000ms);
        static_cast<void>(::write(fd, "x", 1));
    });
    char c = 0;
    QCOMPARE(::read(fd, &c, 1), 1);
    pthread_sigmask(SIG_UNBLOCK, &held, nullptr);
    writer.join();
    ::close(fd);
    runLoopFor(1500ms);
    QCOMPARE(watchdog.stallCount(), 1);

    QFile f(watchdog.lastReportPath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray text = f.readAll();
    const QByteArray want = QByteArray("Syscall: read fd ") + QByteArray::number(fd) + " → " + fifo;
    QVERIFY2(text.contains(want), text.constData());
    QVERIFY2(text.contains("Main thread stack once the kernel call returned:"), text.constData());
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

void tst_StallWatchdog::backgroundWriter_producedOnWorker()
{
    // report 28: the bytes are made on the worker, not by the caller
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString held = dir.filePath(QStringLiteral("held"));
    const QString path = dir.filePath(QStringLiteral("resume"));
    std::binary_semaphore release(0);
    std::atomic<int> made{0};
    std::atomic<bool> madeOnCaller{false};
    const auto caller = std::this_thread::get_id();
    BackgroundFileWriter writer;
    writer.write(held, "h", [&release](const QString& p, const QByteArray& d) {
        release.acquire();
        return BackgroundFileWriter::saveFile(p, d);
    });
    writer.writeProduced(path, [&] {
        ++made;
        if (std::this_thread::get_id() == caller)
            madeOnCaller = true;
        return QByteArray("encoded");
    }, &BackgroundFileWriter::saveFile);
    QCOMPARE(made.load(), 0);   // queued, not made yet
    // a read-back makes it on the caller's thread
    QCOMPARE(writer.pending(path).value_or(QByteArray()), QByteArray("encoded"));
    QVERIFY(madeOnCaller.exchange(false));
    release.release();
    writer.flush();
    QCOMPARE(fileBytes(path), QByteArray("encoded"));
    QCOMPARE(made.load(), 2);
    QVERIFY(!madeOnCaller.load());   // the write made it on the worker
    QVERIFY(!writer.pending(path));
}

QTEST_GUILESS_MAIN(tst_StallWatchdog)
#include "tst_StallWatchdog.moc"
