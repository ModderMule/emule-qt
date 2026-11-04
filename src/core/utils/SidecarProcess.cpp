#include "utils/SidecarProcess.h"

#include <QTimer>

namespace eMule {

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
    if (!m_proc->waitForFinished(2000)) {
        m_proc->kill();
        m_proc->waitForFinished(1000);
    }
    delete m_proc;
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
    connect(p, &QProcess::readyReadStandardOutput, this, &SidecarProcess::onRead);
    connect(p, &QProcess::finished, this, &SidecarProcess::onFinished);
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || p != m_proc)
            return;
        m_proc = nullptr;
        p->deleteLater();
        emit failedToStart();
    });
    m_startedAt.start();
    p->start(program, args);
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
    m_proc->deleteLater();
    m_proc = nullptr;
    m_stopping = false;
    emit finished(code, crashed, expected);
}

} // namespace eMule
