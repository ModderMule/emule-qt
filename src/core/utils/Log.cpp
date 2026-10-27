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

void logUsenet(const QString& msg)
{
    qCInfo(lcEmuleUsenet).noquote() << msg;
}

void logUsenetWarning(const QString& msg)
{
    qCWarning(lcEmuleUsenet).noquote() << msg;
}

void logUsenetError(const QString& msg)
{
    qCCritical(lcEmuleUsenet).noquote() << msg;
}

void logUsenetDebug(const QString& msg)
{
    qCDebug(lcEmuleUsenet).noquote() << msg;
}

void logEd3k(const QString& msg)
{
    qCInfo(lcEmuleEd3k).noquote() << msg;
}

void logEd3kWarning(const QString& msg)
{
    qCWarning(lcEmuleEd3k).noquote() << msg;
}

void logEd3kError(const QString& msg)
{
    qCCritical(lcEmuleEd3k).noquote() << msg;
}

void logEd3kDebug(const QString& msg)
{
    qCDebug(lcEmuleEd3k).noquote() << msg;
}

void logEd3kKad(const QString& msg)
{
    qCInfo(lcEmuleEd3kKad).noquote() << msg;
}

void logEd3kKadWarning(const QString& msg)
{
    qCWarning(lcEmuleEd3kKad).noquote() << msg;
}

void logEd3kKadDebug(const QString& msg)
{
    qCDebug(lcEmuleEd3kKad).noquote() << msg;
}

void logStatusInfo(const QString& msg)
{
    qCInfo(lcEmuleStatus).noquote() << msg;
}

void logStatusWarning(const QString& msg)
{
    qCWarning(lcEmuleStatus).noquote() << msg;
}

void logStatusError(const QString& msg)
{
    qCCritical(lcEmuleStatus).noquote() << msg;
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
// Six files per process, mirroring the reference's theLog / theVerboseLog
// (srchybrid/Emule.cpp:528-533) plus dedicated Kad, Usenet, ed3k and ed3k Kad logs.
// Disabled until a process opts in.

namespace {

/// One file per channel; kSinkSuffix names it after the base name.
enum Sink { SinkLog, SinkVerbose, SinkKad, SinkUsenet, SinkEd3k, SinkEd3kKad, SinkCount };
constexpr const char* kSinkSuffix[SinkCount] = {".log", "_Verbose.log", "_Kad.log", "_Usenet.log",
                                                "_Ed3k.log", "_Ed3kKad.log"};

QMutex s_sinkMutex;
LogFile s_sinks[SinkCount];   ///< SinkLog: non-debug lines; SinkVerbose: debug lines of the rest
bool s_sinkEnabled = false;

void closeSinksLocked()
{
    for (auto& f : s_sinks)
        f.close();
}

} // namespace

void applyLogFileSink(const QString& dir, const QString& baseName,
                      bool enabled, uint32 maxSize)
{
    QString failedPath;
    {
        QMutexLocker lock(&s_sinkMutex);
        s_sinkEnabled = false;
        closeSinksLocked();

        if (!enabled)
            return;

        const QString base = QDir(dir).filePath(baseName);
        for (int i = 0; i < SinkCount && failedPath.isEmpty(); ++i) {
            const QString path = base + QLatin1StringView(kSinkSuffix[i]);
            if (!s_sinks[i].create(path, maxSize))
                failedPath = path;
        }
        s_sinkEnabled = failedPath.isEmpty();
        if (!s_sinkEnabled)
            closeSinksLocked();
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
    // Usenet, ed3k and ed3k Kad likewise (ed3k: its debug lines too).
    const auto is = [category](const char* name) { return category && std::strcmp(category, name) == 0; };
    if (is("emule.kad"))
        s_sinks[SinkKad].log(line);
    else if (is("emule.usenet"))
        s_sinks[SinkUsenet].log(line);
    else if (is("emule.ed3k"))
        s_sinks[SinkEd3k].log(line);
    else if (is("emule.ed3k.kad"))
        s_sinks[SinkEd3kKad].log(line);
    // Debug is the reference's LOG_DEBUG: it goes to the verbose log only.
    else if (type == QtDebugMsg)
        s_sinks[SinkVerbose].log(line);
    else
        s_sinks[SinkLog].log(line);
}

void closeLogFileSink()
{
    QMutexLocker lock(&s_sinkMutex);
    s_sinkEnabled = false;
    closeSinksLocked();
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
