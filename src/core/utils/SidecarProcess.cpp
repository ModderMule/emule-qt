#include "utils/SidecarProcess.h"

#include "utils/Log.h"

#include <QTimer>

#include <array>
#include <atomic>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif

namespace eMule {

namespace {

/// Pids of the running sidecars, for signalAll() from any thread. 0 = free slot.
std::array<std::atomic<qint64>, 16> s_childPids{};

void signalPid(qint64 pid, bool hard)
{
#ifdef Q_OS_WIN
    Q_UNUSED(hard);
    if (HANDLE h = ::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid))) {
        ::TerminateProcess(h, 1);
        ::CloseHandle(h);
    }
#else
    ::kill(static_cast<pid_t>(pid), hard ? SIGKILL : SIGTERM);
#endif
}

#ifdef Q_OS_WIN
/// Job that kills its members when its last handle closes: when the daemon exits,
/// crashes or is killed (Windows counterpart of PR_SET_PDEATHSIG). Never closed.
HANDLE sidecarJob()
{
    static const HANDLE job = [] {
        HANDLE h = ::CreateJobObjectW(nullptr, nullptr);
        if (!h)
            return HANDLE{};
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!::SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof info)) {
            ::CloseHandle(h);
            return HANDLE{};
        }
        return h;
    }();
    return job;
}

/// Puts @p pid in sidecarJob(). Children it starts later join too.
void joinSidecarJob(qint64 pid)
{
    HANDLE job = sidecarJob();
    HANDLE h = job ? ::OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid))
                   : nullptr;
    if (!h || !::AssignProcessToJobObject(job, h))
        logWarning(QStringLiteral("Sidecar %1 not bound to the core's lifetime (error %2)")
                       .arg(pid).arg(::GetLastError()));
    if (h)
        ::CloseHandle(h);
}
#endif

} // namespace

SidecarProcess::SidecarProcess(QObject* parent)
    : QObject(parent)
{
}

SidecarProcess::~SidecarProcess()
{
    if (!m_proc)
        return;
    m_proc->disconnect(this);
    m_proc->closeWriteChannel();
    // closed stdin (java) or an earlier SIGTERM (i2pd) ends it; else SIGTERM, then kill
    if (!m_proc->waitForFinished(2000)) {
        m_proc->terminate();
        if (!m_proc->waitForFinished(1000)) {
            m_proc->kill();
            m_proc->waitForFinished(1000);
        }
    }
    delete m_proc;
    setTrackedPid(0);
}

void SidecarProcess::signalAll(bool hard)
{
    for (auto& slot : s_childPids) {
        if (const qint64 pid = slot.load(); pid > 0)
            signalPid(pid, hard);
    }
}

bool SidecarProcess::start(const QString& program, const QStringList& args, const QString& workDir)
{
    if (running())
        return false;
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->deleteLater();
    }
    auto* p = new QProcess(this);
    m_proc = p;
    m_buf.clear();
    m_program = program;
    m_stopping = false;
    if (!workDir.isEmpty())
        p->setWorkingDirectory(workDir);
    p->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_UNIX
    // else the child inherits the daemon's listen sockets (BT, eD2K) and holds the ports
    p->setUnixProcessParameters(QProcess::UnixProcessFlag::CloseFileDescriptors);
#endif
#ifdef Q_OS_LINUX
    // Ends with the daemon, also when that is killed or crashes (#129). Bound to the
    // starting thread: sidecars start on the main thread.
    p->setChildProcessModifier([parent = ::getpid()] {
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (::getppid() != parent)   // gone before prctl
            ::_exit(1);
    });
#endif
    connect(p, &QProcess::readyReadStandardOutput, this, &SidecarProcess::onRead);
    connect(p, &QProcess::finished, this, &SidecarProcess::onFinished);
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || p != m_proc)
            return;
        setTrackedPid(0);
        m_proc = nullptr;
        p->deleteLater();
        emit failedToStart();
    });
    connect(p, &QProcess::started, this, [this, p] {
#ifdef Q_OS_WIN
        // ToDo: start suspended to close the window before the job holds it
        joinSidecarJob(p->processId());
#endif
        if (p == m_proc)
            setTrackedPid(p->processId());
    });
    m_startedAt.start();
    p->start(program, args);
    if (p == m_proc && p->processId() > 0)
        setTrackedPid(p->processId());
    return true;
}

void SidecarProcess::stop(int graceMs, bool terminate)
{
    if (!m_proc || m_proc->state() == QProcess::NotRunning)
        return;
    m_stopping = true;
    QProcess* p = m_proc;
    p->closeWriteChannel();
    if (terminate)
        p->terminate();
    QTimer::singleShot(graceMs, p, [p] {
        if (p->state() != QProcess::NotRunning)
            p->kill();
    });
}

bool SidecarProcess::write(const QByteArray& data)
{
    if (!running())
        return false;
    return m_proc->write(data) == data.size();
}

void SidecarProcess::closeWriteChannel()
{
    if (m_proc)
        m_proc->closeWriteChannel();
}

bool SidecarProcess::running() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

qint64 SidecarProcess::uptimeMs() const
{
    return m_startedAt.isValid() ? m_startedAt.elapsed() : 0;
}

void SidecarProcess::onRead()
{
    if (!m_proc)
        return;
    m_buf += m_proc->readAllStandardOutput();
    qsizetype nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl).trimmed();
        m_buf.remove(0, nl + 1);
        emit lineRead(line);
        if (!m_proc)
            return;   // a slot stopped and dropped it
    }
    if (m_buf.size() > kMaxLogBuffer)
        m_buf.clear();
}

void SidecarProcess::onFinished()
{
    if (!m_proc)
        return;
    onRead();
    if (!m_proc)
        return;
    const int code = m_proc->exitCode();
    const bool crashed = m_proc->exitStatus() == QProcess::CrashExit;
    const bool expected = m_stopping;
    setTrackedPid(0);
    m_proc->deleteLater();
    m_proc = nullptr;
    m_stopping = false;
    emit finished(code, crashed, expected);
}

void SidecarProcess::setTrackedPid(qint64 pid)
{
    if (pid == m_trackedPid)
        return;
    if (m_trackedPid > 0) {
        for (auto& slot : s_childPids) {
            qint64 expected = m_trackedPid;
            if (slot.compare_exchange_strong(expected, 0))
                break;
        }
    }
    m_trackedPid = 0;
    if (pid <= 0)
        return;
    for (auto& slot : s_childPids) {
        qint64 expected = 0;
        if (slot.compare_exchange_strong(expected, pid)) {
            m_trackedPid = pid;
            break;
        }
    }
}

} // namespace eMule
