#pragma once

/// @file AppConfig.h
/// @brief Application config directory helpers.
///
/// Centralises platform-specific config directory resolution and
/// first-run seeding of bundled data files (nodes.dat, webserver assets,
/// eMule.tmpl, etc.).

#include "utils/Types.h"   // pulls in generated config.h (EMULE_VERSION_STRING)

#include <QString>
#include <QStringList>
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

    /// What one seeding pass did. Counts, so a caller can log a single line
    /// and a test can assert on the outcome instead of on side effects.
    struct SeedReport {
        int seeded    = 0;   ///< file was missing
        int refreshed = 0;   ///< bundled copy moved on, ours was untouched
        int preserved = 0;   ///< user edited it, left alone
        int conflicts = 0;   ///< user edited it *and* the bundle moved -> .new written
        int pruned    = 0;   ///< no longer bundled, removed
    };

    /// Seed and refresh bundled config data in @p configDir.
    ///
    /// Finds the bundled config directory next to the running binary and
    /// delegates to seedFrom(). Does nothing when there is no bundle.
    static SeedReport seedBundledData(const QString& configDir);

    /// Sync @p configDir against @p bundleDir.
    ///
    /// Managed assets (eMule.tmpl, webserver/*) track the bundle; the files in
    /// kSeedOnce are live data the app rewrites and are only ever copied when
    /// missing. Staleness is decided on SHA-256, three ways, against the
    /// manifest written by the last pass -- never on mtime, which git does not
    /// preserve across a checkout and which cannot tell "the bundle moved on"
    /// from "the user edited their copy". A hand-edited file is never
    /// overwritten; it gets a .new sibling instead.
    ///
    /// Single pass, guarded by a QLockFile because the daemon and the GUI both
    /// call this within moments of each other.
    static SeedReport seedFrom(const QString& bundleDir, const QString& configDir);

    /// Bundle locations to try, most specific first, for a binary in @p appDir.
    /// Split out so the three shipped layouts can be asserted from any host --
    /// they are pure path arithmetic and need no Linux or Windows to check.
    [[nodiscard]] static QStringList bundleCandidates(const QString& appDir);

    /// Where to look for emuleqt_*.qm, most specific first, for a binary in
    /// @p appDir. Same shape and same rule as bundleCandidates(): a real bundle
    /// wins, the local build tree is always last and is empty in a packaged build.
    ///
    /// Returns the whole list, not the first hit that works: main() wants the first
    /// directory a translator loads from, but the Options dialog has to union the
    /// locales it finds across all of them.
    [[nodiscard]] static QStringList langCandidates(const QString& appDir);
};

} // namespace eMule