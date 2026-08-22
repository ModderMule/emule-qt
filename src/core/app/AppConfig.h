#pragma once

/// @file AppConfig.h
/// @brief Application config directory helpers.
///
/// Centralises platform-specific config directory resolution and
/// first-run seeding of bundled data files (nodes.dat, webserver assets,
/// eMule.tmpl, etc.).

#include "utils/Types.h"   // pulls in generated config.h (EMULE_VERSION_STRING)

#include <QString>
#include <QLatin1StringView>

namespace eMule {

/// Application version string — single source of truth for daemon, GUI, and web server.
/// Derived from the CMake PROJECT_VERSION via config.h so it cannot drift.
inline constexpr QLatin1StringView kAppVersion{EMULE_VERSION_STRING};

/// User-Agent header value for all outgoing HTTP requests. Applied by
/// eMule::Http::applyDefaults() (net/HttpDefaults.h) rather than by hand: a caller
/// that forgets does not go out anonymous, it goes out as Qt's "Mozilla/5.0".
inline const QString kUserAgent = QStringLiteral("eMuleQt/") + kAppVersion;

/// Project website — base for the port test, bug report and version check endpoints.
inline constexpr QLatin1StringView kWebsiteUrl{"https://emule-qt.org"};

/// Port test page. Reachable over both IPv4 and IPv6 and reports each family separately, unlike
/// porttest.emule-project.net which has no AAAA record and can only ever answer for IPv4.
/// Accepts tcpport, udpport and — because the server observes only the family the browser used —
/// optional ip4/ip6 hints for the other one.
inline constexpr QLatin1StringView kPortTestPath{"/test-ports/"};


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

    /// Override the config directory. Must be called before configDir().
    static void setConfigDirOverride(const QString& path);

    /// The active override, or an empty string when none is set.
    /// Preferences::configDir() consults this so a --config run redirects
    /// every consumer, not just the preferences.yml lookup in main().
    [[nodiscard]] static QString configDirOverride();

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