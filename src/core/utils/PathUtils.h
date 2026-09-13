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
#include <optional>

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
///
/// ⚠️ 0 therefore means *either* "full" or "could not tell" — an unmounted
/// volume, a drive letter that is gone. A guard that has to act on the answer
/// wants tryFreeDiskSpace() instead, which keeps the two apart.
[[nodiscard]] std::uint64_t freeDiskSpace(const QString& path);

/// Free disk space in bytes, or nothing when the volume could not be read.
///
/// The distinction freeDiskSpace() cannot make. Pausing downloads because a path
/// is unreadable is a different decision from pausing them because the disk is
/// full, and only the caller knows which way it wants to be wrong.
[[nodiscard]] std::optional<std::uint64_t> tryFreeDiskSpace(const QString& path);

/// Sanitize a file name by removing or replacing invalid characters.
[[nodiscard]] QString sanitizeFilename(const QString& name);

} // namespace eMule
