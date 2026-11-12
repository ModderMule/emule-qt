#pragma once

/// @file CrashHandler.h
/// @brief Cross-platform crash dump handler.
///
/// Catches fatal signals (Unix) or unhandled exceptions (Windows) and writes
/// a crash dump file to a configurable directory before terminating. Also
/// std::terminate (uncaught exception, its text in the file) and, on Windows,
/// abort / pure virtual call / invalid CRT parameter, which bypass the filter.

#include <QString>

namespace eMule {

class CrashHandler {
public:
    /// Install platform-specific crash handlers.
    /// @param crashDir  Directory where crash files will be written (created if needed).
    static void install(const QString& crashDir);

    /// Returns the configured crash directory.
    static QString crashDir();

    CrashHandler() = delete;
};

} // namespace eMule
