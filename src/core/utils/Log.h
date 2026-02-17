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

} // namespace eMule
