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
// Three files per process — `<baseName>.log`, `<baseName>_Verbose.log` and
// `<baseName>_Kad.log` — mirroring the reference's theLog / theVerboseLog plus a
// dedicated Kad log. Both message handlers feed it, so what lands on disk is
// what the GUI log tabs show, Kad tab included.

/// Open (or with @p enabled false, close) this process's three log files in
/// @p dir. Safe to call repeatedly — the settings are user-togglable.
/// Reports an open failure via logError() and leaves the sink closed.
void applyLogFileSink(const QString& dir, const QString& baseName,
                      bool enabled, uint32 maxSize);

/// Append one line to the sink, or do nothing if it is closed. Thread-safe.
/// emule.kad goes to the _Kad file whatever its severity; of the rest,
/// QtDebugMsg goes to the _Verbose file and every other severity to the main one.
void writeToLogFileSink(QtMsgType type, const char* category, const QString& msg);

/// Route every emule.* message into the sink, chaining to the handler already
/// installed. Call it at the top of main(): installed there it also catches the
/// startup lines emitted before the daemon's log forwarder or the GUI's
/// LogWidget exist, which their own handlers necessarily miss.
void installLogFileMessageHandler();

/// Close all three files.
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
