/// @file tst_SidecarProcess.cpp
/// @brief SidecarProcess: start/stop, and the child ends with its parent when that is
/// killed (Linux PR_SET_PDEATHSIG, Windows kill-on-close job).
/// The test exe runs itself with --sidecar-parent as the parent to kill.

#include "utils/SidecarProcess.h"

#include <QCoreApplication>
#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#endif

using namespace eMule;

namespace {

const char* const kParentArg = "--sidecar-parent";

/// A child that idles for a minute.
std::pair<QString, QStringList> longChild()
{
#ifdef Q_OS_WIN
    return {QStringLiteral("ping"), {QStringLiteral("-n"), QStringLiteral("60"), QStringLiteral("127.0.0.1")}};
#else
    return {QStringLiteral("sleep"), {QStringLiteral("60")}};
#endif
}

bool alive(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE h = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h)
        return false;
    const bool running = ::WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    ::CloseHandle(h);
    return running;
#elif defined(Q_OS_LINUX)
    // a zombie counts as gone
    QFile f(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray stat = f.readAll();
    const qsizetype close = stat.lastIndexOf(')');
    return close < 0 || stat.mid(close + 2, 1) != "Z";
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

void killPid(qint64 pid)
{
#ifdef Q_OS_WIN
    if (HANDLE h = ::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid))) {
        ::TerminateProcess(h, 1);
        ::CloseHandle(h);
    }
#else
    ::kill(static_cast<pid_t>(pid), SIGKILL);
#endif
}

/// --sidecar-parent: starts the child, prints its pid, idles until killed.
int runParent(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    SidecarProcess sidecar;
    const auto [program, args] = longChild();
    sidecar.start(program, args);
    // the pid is not exposed: take it from the QProcess child
    const auto procs = sidecar.findChildren<QProcess*>();
    if (procs.isEmpty() || !procs.first()->waitForStarted(5000))
        return 2;
    QFile out;
    if (!out.open(stdout, QIODevice::WriteOnly))
        return 2;
    out.write(QByteArray::number(procs.first()->processId()) + '\n');
    out.flush();
    return app.exec();
}

} // namespace

class tst_SidecarProcess : public QObject {
    Q_OBJECT

private slots:
    void stop_emitsExpectedFinish();
    void childEndsWhenParentKilled();
};

void tst_SidecarProcess::stop_emitsExpectedFinish()
{
    SidecarProcess sidecar;
    QSignalSpy finished(&sidecar, &SidecarProcess::finished);
    const auto [program, args] = longChild();
    QVERIFY(sidecar.start(program, args));
    QTRY_VERIFY(sidecar.running());
    sidecar.stop(500);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
    QCOMPARE(finished.first().at(2).toBool(), true);
    QVERIFY(!sidecar.running());
}

void tst_SidecarProcess::childEndsWhenParentKilled()
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_LINUX)
    QSKIP("no parent-death binding on this platform");
#endif
    QProcess parent;
    parent.start(QCoreApplication::applicationFilePath(), {QString::fromLatin1(kParentArg)});
    QVERIFY(parent.waitForStarted(5000));
    QVERIFY(parent.waitForReadyRead(10000));
    const qint64 child = parent.readLine().trimmed().toLongLong();
    QVERIFY(child > 0);
    QVERIFY(alive(child));

    parent.kill();
    QVERIFY(parent.waitForFinished(5000));
    const auto cleanup = qScopeGuard([child] {
        if (alive(child))
            killPid(child);
    });
    QTRY_VERIFY_WITH_TIMEOUT(!alive(child), 5000);
}

int main(int argc, char** argv)
{
    if (argc > 1 && qstrcmp(argv[1], kParentArg) == 0)
        return runParent(argc, argv);
    QCoreApplication app(argc, argv);
    tst_SidecarProcess t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_SidecarProcess.moc"
