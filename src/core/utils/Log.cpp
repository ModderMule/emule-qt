#include "pch.h"
/// @file Log.cpp
/// @brief Logging system implementation.

#include "Log.h"

#include <QDateTime>
#include <QDir>
#include <QMutexLocker>

#include <cstring>

namespace eMule {

// ---------------------------------------------------------------------------
// LogFile — public API
// ---------------------------------------------------------------------------

LogFile::LogFile() = default;

LogFile::~LogFile()
{
    close();
}

bool LogFile::create(const QString& filePath, std::size_t maxSize)
{
    QMutexLocker lock(&m_mutex);
    closeLocked();
    m_filePath = filePath;
    m_maxFileSize = maxSize;
    return openLocked();
}

bool LogFile::open()
{
    QMutexLocker lock(&m_mutex);
    return openLocked();
}

bool LogFile::close()
{
    QMutexLocker lock(&m_mutex);
    closeLocked();
    return true;
}

bool LogFile::isOpen() const
{
    QMutexLocker lock(&m_mutex);
    return m_file.isOpen();
}

const QString& LogFile::filePath() const
{
    return m_filePath;
}

void LogFile::setMaxFileSize(std::size_t maxSize)
{
    QMutexLocker lock(&m_mutex);
    if (maxSize < 0x10000)
        m_maxFileSize = (maxSize == 0) ? std::numeric_limits<std::size_t>::max() : 0x10000;
    else
        m_maxFileSize = maxSize;
}

bool LogFile::log(const QString& message)
{
    QMutexLocker lock(&m_mutex);
    if (!m_file.isOpen())
        return false;

    // Milliseconds, so a file line can be lined up against the console output —
    // both carry the same resolution (see installConsoleMessagePattern).
    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString line = QStringLiteral("%1: %2\n").arg(timestamp, message);

    const QByteArray utf8 = line.toUtf8();
    m_stream << line;
    m_stream.flush();
    m_bytesWritten += static_cast<std::size_t>(utf8.size());

    if (m_bytesWritten >= m_maxFileSize)
        rotateLocked();

    return true;
}

void LogFile::startNewLogFile()
{
    QMutexLocker lock(&m_mutex);
    rotateLocked();
}

// ---------------------------------------------------------------------------
// LogFile — private (caller must hold m_mutex)
// ---------------------------------------------------------------------------

bool LogFile::openLocked()
{
    if (m_file.isOpen())
        return true;

    m_file.setFileName(m_filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;

    m_stream.setDevice(&m_file);
    m_bytesWritten = static_cast<std::size_t>(m_file.size());
    return true;
}

void LogFile::closeLocked()
{
    if (!m_file.isOpen())
        return;

    m_stream.flush();
    m_stream.setDevice(nullptr);
    m_file.close();
    m_bytesWritten = 0;
}

void LogFile::rotateLocked()
{
    closeLocked();

    // Exactly one generation is kept. The reference names every backup after the
    // time the log was started (srchybrid/Log.cpp:396) and so never deletes any;
    // here the previous .bak is dropped so an unattended daemon cannot fill the
    // config directory.
    const QString bakPath = m_filePath + QStringLiteral(".bak");
    QFile::remove(bakPath);

    if (!QFile::rename(m_filePath, bakPath))
        QFile::remove(m_filePath);

    openLocked();
}

// ---------------------------------------------------------------------------
// Convenience logging functions
// ---------------------------------------------------------------------------

void logInfo(const QString& msg)
{
    qCInfo(lcEmuleGeneral).noquote() << msg;
}

void logWarning(const QString& msg)
{
    qCWarning(lcEmuleGeneral).noquote() << msg;
}

void logError(const QString& msg)
{
    qCCritical(lcEmuleGeneral).noquote() << msg;
}

void logDebug(const QString& msg)
{
    qCDebug(lcEmuleGeneral).noquote() << msg;
}

// ---------------------------------------------------------------------------
// Console message pattern
// ---------------------------------------------------------------------------

void installConsoleMessagePattern(const QString& processTag)
{
    // Built by concatenation rather than QString::arg: arg() rescans its result
    // for further placeholders, and the pattern is nothing but %{...} tokens.
    // Qt applies this in the default handler only — the message handlers the
    // daemon and the GUI install receive the raw text, so the log tabs and the
    // IPC push are unaffected. QT_MESSAGE_PATTERN still overrides it.
    qSetMessagePattern(QStringLiteral("%{time HH:mm:ss.zzz} [")
                       + processTag.leftJustified(4)
                       + QStringLiteral("] %{if-category}%{category}: %{endif}%{message}"));
}

// ---------------------------------------------------------------------------
// Rotating log file sink
// ---------------------------------------------------------------------------
//
// Three files per process, mirroring the reference's theLog / theVerboseLog
// (srchybrid/Emule.cpp:528-533) plus a dedicated Kad log. Disabled until a
// process opts in.

namespace {

QMutex s_sinkMutex;
LogFile s_sinkLog;            ///< non-debug lines
LogFile s_sinkVerboseLog;     ///< debug lines, except Kad
LogFile s_sinkKadLog;         ///< emule.kad, whatever its severity
bool s_sinkEnabled = false;

} // namespace

void applyLogFileSink(const QString& dir, const QString& baseName,
                      bool enabled, uint32 maxSize)
{
    QString failedPath;
    {
        QMutexLocker lock(&s_sinkMutex);
        s_sinkEnabled = false;
        s_sinkLog.close();
        s_sinkVerboseLog.close();
        s_sinkKadLog.close();

        if (!enabled)
            return;

        const QString base = QDir(dir).filePath(baseName);
        const QString logPath = base + QStringLiteral(".log");
        const QString verbosePath = base + QStringLiteral("_Verbose.log");
        const QString kadPath = base + QStringLiteral("_Kad.log");

        if (!s_sinkLog.create(logPath, maxSize))
            failedPath = logPath;
        else if (!s_sinkVerboseLog.create(verbosePath, maxSize))
            failedPath = verbosePath;
        else if (!s_sinkKadLog.create(kadPath, maxSize))
            failedPath = kadPath;
        else
            s_sinkEnabled = true;

        if (!s_sinkEnabled) {
            s_sinkLog.close();
            s_sinkVerboseLog.close();
            s_sinkKadLog.close();
        }
    }

    // Reported outside the lock, and never from writeToLogFileSink(): that runs
    // inside the Qt message handler, where logging again would recurse.
    if (!failedPath.isEmpty())
        logError(QStringLiteral("Cannot open log file %1 — disk logging is off").arg(failedPath));
}

void writeToLogFileSink(QtMsgType type, const char* category, const QString& msg)
{
    QMutexLocker lock(&s_sinkMutex);
    if (!s_sinkEnabled)
        return;

    const QString line = category && *category
        ? QStringLiteral("%1: %2").arg(QString::fromLatin1(category), msg)
        : msg;

    // Kad is its own channel everywhere else — its own GUI tab, its own filter
    // rule — so it gets its own file rather than drowning the verbose log, which
    // it would otherwise dominate. Tested before the severity split so a future
    // qCWarning(lcEmuleKad) lands here too, exactly as it would in the Kad tab.
    if (category && std::strcmp(category, "emule.kad") == 0)
        s_sinkKadLog.log(line);
    // Debug is the reference's LOG_DEBUG: it goes to the verbose log only.
    else if (type == QtDebugMsg)
        s_sinkVerboseLog.log(line);
    else
        s_sinkLog.log(line);
}

void closeLogFileSink()
{
    QMutexLocker lock(&s_sinkMutex);
    s_sinkEnabled = false;
    s_sinkLog.close();
    s_sinkVerboseLog.close();
    s_sinkKadLog.close();
}

namespace {

QtMessageHandler s_sinkPreviousHandler = nullptr;
bool s_sinkHandlerInstalled = false;

void sinkMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Same filter as the GUI log tabs, so the file is a faithful record of them
    // and Qt's own qt.* noise stays out.
    const char* cat = context.category ? context.category : "";
    if (std::strncmp(cat, "emule.", 6) == 0)
        writeToLogFileSink(type, cat, msg);

    if (s_sinkPreviousHandler)
        s_sinkPreviousHandler(type, context, msg);
}

} // namespace

void installLogFileMessageHandler()
{
    // Installing twice would make the handler chain to itself and recurse.
    if (s_sinkHandlerInstalled)
        return;
    s_sinkHandlerInstalled = true;
    s_sinkPreviousHandler = qInstallMessageHandler(sinkMessageHandler);
}

// ---------------------------------------------------------------------------
// Gated server-verbose logging
// ---------------------------------------------------------------------------

static bool s_serverVerboseEnabled = false;

void logServerVerbose(const QString& msg)
{
    if (s_serverVerboseEnabled)
        qCDebug(lcEmuleServerVerbose).noquote() << msg;
}

void setServerVerboseLogging(bool enabled)
{
    s_serverVerboseEnabled = enabled;
}

bool isServerVerboseLoggingEnabled()
{
    return s_serverVerboseEnabled;
}

} // namespace eMule
