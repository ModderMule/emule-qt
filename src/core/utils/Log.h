#pragma once

/// @file Log.h
/// @brief Logging system replacing MFC Log.h / CLogFile.
///
/// Provides:
///   LogFile  — rotating file logger (replaces CLogFile)
///   Log-level enums and convenience logging functions that route to
///   Qt's logging infrastructure via QLoggingCategory.

#include "Types.h"
#include "DebugUtils.h"

#include <QFile>
#include <QMutex>
#include <QString>
#include <QTextStream>
#include <QtLogging>

#include <cstdint>
#include <limits>

namespace eMule {

// ---------------------------------------------------------------------------
// Log priority / flags (matching original values for compat)
// ---------------------------------------------------------------------------

enum class LogPriority : int {
    VeryLow  = 0,
    Low      = 1,
    Default  = 2,
    High     = 3,
    VeryHigh = 4
};

enum LogFlag : uint32 {
    LogInfo     = 0x00,
    LogWarning  = 0x01,
    LogError    = 0x02,
    LogSuccess  = 0x03,
    LogTypeMask = 0x03,

    LogDefault  = 0x00,
    LogDebug    = 0x10,
    LogStatusBar = 0x20,
    LogDontNotify = 0x40,
};

// ---------------------------------------------------------------------------
// LogFile — rotating file logger
// ---------------------------------------------------------------------------

/// Thread-safe rotating log file, replaces MFC CLogFile.
class LogFile {
public:
    LogFile();
    ~LogFile();

    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

    /// Create a new log file.
    bool create(const QString& filePath, std::size_t maxSize = 1024 * 1024);
    bool open();
    bool close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] const QString& filePath() const;

    void setMaxFileSize(std::size_t maxSize);

    /// Append a message to the log file.
    bool log(const QString& message);

    /// Rotate the log file when it exceeds the maximum size.
    void startNewLogFile();

private:
    /// Internal rotation called while m_mutex is already held.
    void rotateLocked();
    /// Internal close called while m_mutex is already held.
    void closeLocked();
    /// Internal open called while m_mutex is already held.
    bool openLocked();
    mutable QMutex m_mutex;
    QFile m_file;
    QTextStream m_stream;
    QString m_filePath;
    std::size_t m_bytesWritten = 0;
    std::size_t m_maxFileSize = std::numeric_limits<std::size_t>::max();
    qint64 m_startTime = 0;
};

// ---------------------------------------------------------------------------
// Convenience logging functions
// ---------------------------------------------------------------------------

/// Log a message at Info level.
void logInfo(const QString& msg);

/// Log a message at Warning level.
void logWarning(const QString& msg);

/// Log a message at Error level.
void logError(const QString& msg);

/// Log a debug message (only in debug/verbose mode).
void logDebug(const QString& msg);

/// Usenet/NNTP variants of the above: emule.usenet, shown in the GUI's Usenet
/// tab and written to the _Usenet file.
void logUsenet(const QString& msg);
void logUsenetWarning(const QString& msg);
void logUsenetError(const QString& msg);
void logUsenetDebug(const QString& msg);

/// ed3k (packmule) variants: emule.ed3k, the GUI's ed3k tab (debug: ed3k Verbose) and
/// the _Ed3k file. Never log a key, seed or network name.
void logEd3k(const QString& msg);
void logEd3kWarning(const QString& msg);
void logEd3kError(const QString& msg);
void logEd3kDebug(const QString& msg);

/// The ed3k node's Kad: emule.ed3k.kad, the ed3k Kad tab and the _Ed3kKad file.
void logEd3kKad(const QString& msg);
void logEd3kKadWarning(const QString& msg);
void logEd3kKadDebug(const QString& msg);

/// Bans and anti-leecher hits: emule.leecher, the GUI's Leecher tab and the _Leecher file.
/// One line per event (Xtreme Anti-Leecher-Log).
void logLeecher(const QString& msg);

/// Log to the main Log tab and the GUI status bar (MFC LOG_STATUSBAR).
void logStatusInfo(const QString& msg);

/// Warning variant of logStatusInfo().
void logStatusWarning(const QString& msg);

/// Error variant of logStatusInfo().
void logStatusError(const QString& msg);

// ---------------------------------------------------------------------------
// Console output format
// ---------------------------------------------------------------------------

/// Give this process's console output the format
/// `HH:mm:ss.zzz [tag] category: message`.
///
/// @p processTag names the process ("core" / "gui") so the two can be told
/// apart when they share a terminal — scripts/debug-gui.sh runs both. It is
/// padded to four columns so the category always starts at the same offset.
///
/// Only the default handler is affected; a handler installed with
/// qInstallMessageHandler still receives the unformatted message.
void installConsoleMessagePattern(const QString& processTag);

// ---------------------------------------------------------------------------
// Rotating log file sink
// ---------------------------------------------------------------------------
//
// Seven files per process — `<baseName>.log`, `<baseName>_Verbose.log`,
// `<baseName>_Kad.log`, `<baseName>_Usenet.log`, `<baseName>_Ed3k.log`,
// `<baseName>_Ed3kKad.log` and `<baseName>_Leecher.log` — mirroring the reference's
// theLog / theVerboseLog plus dedicated Kad, Usenet, ed3k and leecher logs. Both message
// handlers feed it, so what lands on disk is what the GUI log tabs show.

/// Open (or with @p enabled false, close) this process's seven log files in
/// @p dir. Safe to call repeatedly — the settings are user-togglable.
/// Reports an open failure via logError() and leaves the sink closed.
void applyLogFileSink(const QString& dir, const QString& baseName,
                      bool enabled, uint32 maxSize);

/// Append one line to the sink, or do nothing if it is closed. Thread-safe.
/// emule.kad goes to the _Kad file, emule.usenet to _Usenet, emule.ed3k to _Ed3k and
/// emule.ed3k.kad to _Ed3kKad and emule.leecher to _Leecher whatever the severity; of the rest,
/// QtDebugMsg goes to the _Verbose file and every other severity to the main one.
void writeToLogFileSink(QtMsgType type, const char* category, const QString& msg);

/// Route every emule.* message into the sink, chaining to the handler already
/// installed. Call it at the top of main(): installed there it also catches the
/// startup lines emitted before the daemon's log forwarder or the GUI's
/// LogWidget exist, which their own handlers necessarily miss.
void installLogFileMessageHandler();

/// Close all seven files.
void closeLogFileSink();

// ---------------------------------------------------------------------------
// Gated server-verbose logging (server TCP/UDP/search handshake detail)
// ---------------------------------------------------------------------------
//
// Mirrors the Kad logging helper (KadLog): a single toggle gates a dedicated
// logging category (lcEmuleServerVerbose → "emule.serverv") that the GUI routes
// to the Verbose tab. Kept independent of the global `verbose` pref so it can be
// switched on to diagnose a connect without enabling the full debug firehose.

/// Log a server TCP/UDP/search verbose line via qCDebug(lcEmuleServerVerbose)
/// when server-verbose logging is enabled.
void logServerVerbose(const QString& msg);

/// Enable/disable server-verbose logging (the emit-site gate).
void setServerVerboseLogging(bool enabled);

/// Check whether server-verbose logging is enabled.
[[nodiscard]] bool isServerVerboseLoggingEnabled();

} // namespace eMule
