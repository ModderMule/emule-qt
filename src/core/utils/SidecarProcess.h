#pragma once

/// @file SidecarProcess.h
/// @brief A helper program the daemon runs and supervises: the hoster resolver (java),
/// the bundled i2pd. One child at a time; its stdout+stderr come back as lines.
/// Linux: the child gets SIGTERM when the daemon dies (PR_SET_PDEATHSIG).

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace eMule {

class SidecarProcess : public QObject {
    Q_OBJECT

public:
    /// A log line longer than this is dropped with its buffer.
    static constexpr qsizetype kMaxLogBuffer = 64 * 1024;

    explicit SidecarProcess(QObject* parent = nullptr);
    /// Waits up to 2 s for the child, then SIGTERM, 1 s later kills it.
    ~SidecarProcess() override;

    /// Starts @p program; false when one runs already. Merged channels; on Unix the
    /// daemon's descriptors are closed in the child (else it holds the BT/eD2K ports).
    bool start(const QString& program, const QStringList& args, const QString& workDir = {});
    /// Ends the child: closes stdin, @p terminate sends SIGTERM (Windows: kill), kills it
    /// after @p graceMs. finished() still comes, with expected = true.
    void stop(int graceMs, bool terminate = true);
    /// Writes to stdin; false when not running.
    bool write(const QByteArray& data);
    void closeWriteChannel();

    [[nodiscard]] bool running() const;
    /// ms since start(); 0 when never started
    [[nodiscard]] qint64 uptimeMs() const;
    [[nodiscard]] QString program() const { return m_program; }

    /// Signals every running sidecar of this process (SIGTERM, @p hard = SIGKILL;
    /// Windows: kill). Any thread: the daemon's quit deadline ends them before it exits.
    static void signalAll(bool hard);

signals:
    /// One line of output, trimmed, without the newline.
    void lineRead(const QByteArray& line);
    /// The child is gone. @p expected: after stop(). Not emitted for FailedToStart.
    void finished(int exitCode, bool crashed, bool expected);
    void failedToStart();

private:
    void onRead();
    void onFinished();
    /// Tracks the child's pid for signalAll(); 0 = none.
    void setTrackedPid(qint64 pid);

    QPointer<QProcess> m_proc;
    QByteArray m_buf;
    QElapsedTimer m_startedAt;
    QString m_program;
    bool m_stopping = false;
    qint64 m_trackedPid = 0;
};

} // namespace eMule
