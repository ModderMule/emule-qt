#pragma once

/// @file ConfigBackup.h
/// @brief Copy of the config folder into a new dated folder (#268, Tools › Backup Config).
///
/// Everything the profile needs to come back: preferences, identity keys, credits, known
/// files, download/torrent/Usenet/hoster state, ed3k keys and sets. Left out: logs, locks,
/// temp files, crash dumps and what the app downloads again (I2P netDb, resolver/java
/// sidecars). A snapshot of a running core: files are copied as they are on disk.

#include <QString>

#include <cstdint>
#include <expected>

namespace eMule {

struct ConfigBackupResult {
    QString folder;          ///< the backup folder made
    int files = 0;
    std::int64_t bytes = 0;
    int failed = 0;          ///< files that could not be copied (busy, gone meanwhile)
};

namespace ConfigBackup {

/// Copies @p configDir into @p destParent/eMuleQt-config-<yyyyMMdd-HHmmss>. Error: the
/// destination is missing, not writable, or inside the config folder. Any thread.
[[nodiscard]] std::expected<ConfigBackupResult, QString> run(const QString& configDir, const QString& destParent);

/// true: @p relPath (config-relative, '/' separators) is left out of a backup.
[[nodiscard]] bool skipped(const QString& relPath);

} // namespace ConfigBackup
} // namespace eMule
