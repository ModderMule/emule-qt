#pragma once

/// @file AppConfig.h
/// @brief Application config directory helpers.
///
/// Centralises platform-specific config directory resolution and
/// first-run seeding of bundled data files (nodes.dat, webserver assets,
/// eMule.tmpl, etc.).

#include <QString>

namespace eMule {

class AppConfig {
public:
    /// Returns the platform-specific user config directory, creating it
    /// if it does not exist yet.
    ///   macOS:  ~/eMuleQt/Config
    ///   Windows (multiUserSharing=2, default): <exe-dir>/config  (portable)
    ///   Windows (multiUserSharing=0): per-user %APPDATA%
    ///   Windows (multiUserSharing=1): all-users %ProgramData%
    ///   Other:  QStandardPaths::AppConfigLocation
    [[nodiscard]] static QString configDir();

#ifdef Q_OS_WIN
    /// Returns the cached multiUserSharing value (0=per-user, 1=all-users,
    /// 2=program-dir).  Determined once on first call to configDir() by
    /// peeking at <exe-dir>/config/preferences.yml.  Default is 2.
    [[nodiscard]] static int multiUserSharingMode();
#endif

    /// Seed bundled config data into @p configDir.
    ///
    /// Looks for a bundled config directory next to the running binary
    /// (app bundle Resources/config/ or dev-build source tree) and
    /// recursively copies any files that don't already exist in the
    /// user's config directory.
    static void seedBundledData(const QString& configDir);
};

} // namespace eMule