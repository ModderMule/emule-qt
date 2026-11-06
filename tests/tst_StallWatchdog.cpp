/// @file tst_StallWatchdog.cpp
/// @brief Tests for StallWatchdog — a blocked main thread is reported once, with a file.

#include "TestHelpers.h"

#include "utils/StallWatchdog.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>

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

QTEST_GUILESS_MAIN(tst_StallWatchdog)
#include "tst_StallWatchdog.moc"
