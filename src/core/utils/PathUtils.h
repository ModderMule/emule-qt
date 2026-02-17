#pragma once

/// @file PathUtils.h
/// @brief Portable path utilities replacing Windows GetModuleFileName / SHGetFolderPath.
///
/// Replaces MFC path helpers used across ~17 files:
///   GetModuleFileName  → executablePath()
///   SHGetFolderPath    → appDirectory(AppDir)
///   slosh() / unslosh()→ ensureTrailingSeparator / removeTrailingSeparator

#include <QString>

#include <cstdint>

namespace eMule {

/// Application directory roles (resolved via QStandardPaths).
enum class AppDir {
    Config,     ///< User configuration (QStandardPaths::AppConfigLocation)
    Temp,       ///< Temporary files (QStandardPaths::TempLocation + /eMule)
    Incoming,   ///< Default incoming folder (AppConfig + /Incoming)
    Log,        ///< Log files (AppConfig + /Logs)
    Data,       ///< Application data (QStandardPaths::AppDataLocation)
    Cache,      ///< Cache files (QStandardPaths::CacheLocation)
};

/// Resolve an application directory path.  Creates the directory if it
/// does not exist.  Returns an empty string on failure.
[[nodiscard]] QString appDirectory(AppDir dir);

/// Full path to the running executable.
[[nodiscard]] QString executablePath();

/// Directory containing the running executable.
[[nodiscard]] QString executableDir();

/// Ensure the path ends with a directory separator (replaces MFC slosh()).
[[nodiscard]] QString ensureTrailingSeparator(const QString& path);

/// Remove any trailing directory separator (replaces MFC unslosh()).
[[nodiscard]] QString removeTrailingSeparator(const QString& path);

/// Return the canonical (absolute, no symlinks, no "." or "..") path.
/// Returns an empty string if the path does not exist.
[[nodiscard]] QString canonicalPath(const QString& path);

/// Compare two paths for equality (case-sensitivity depends on platform).
[[nodiscard]] bool pathsEqual(const QString& a, const QString& b);

/// Free disk space in bytes on the volume containing @p path.
/// Returns 0 on error.
[[nodiscard]] std::uint64_t freeDiskSpace(const QString& path);

/// Sanitize a file name by removing or replacing invalid characters.
[[nodiscard]] QString sanitizeFilename(const QString& name);

} // namespace eMule
