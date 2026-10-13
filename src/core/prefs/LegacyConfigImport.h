#pragma once

/// @file LegacyConfigImport.h
/// @brief One-time import of an MFC eMule config directory, requested by the installer.
///
/// The Windows installer runs elevated and cannot write the user's profile, so it
/// only drops a request (`mfc-import.yml`) next to the exe. The daemon carries it
/// out once per user on its next start: data files are copied, preferences.ini,
/// Category.ini and shareddir.dat are mapped into preferences.yml, and the old
/// temp dirs are handed to PartFileConvert once the daemon runs. The source is
/// only read, so the MFC install keeps working.

#include <QString>
#include <QStringList>

namespace eMule {

class Preferences;

namespace LegacyConfigImport {

inline constexpr auto kRequestFileName = "mfc-import.yml";
/// Per-user record that the request was handled; also carries the temp dirs
/// still waiting for the part-file converter.
inline constexpr auto kStateFileName = "mfc-import.done";

enum class Result {
    NoRequest,      ///< no request file, nothing to do
    AlreadyDone,    ///< handled on an earlier start
    ProfileInUse,   ///< config dir already holds known.met / clients.met, left alone
    SourceMissing,  ///< requested dir has no preferences.ini for this user
    Imported,
};

struct Outcome {
    Result result = Result::NoRequest;
    QString sourceDir;
    int filesCopied = 0;
    int prefsApplied = 0;
    int categories = 0;     ///< without the implicit "All"
    int sharedDirs = 0;
    QStringList pendingTempDirs;
};

/// Where the installer leaves its request: `<exe-dir>/config/mfc-import.yml`.
[[nodiscard]] QString requestFilePath();

/// Stage A. Run before LegacyIdentityImport::run() and before anything reads
/// the config dir. Logs the outcome and saves @p prefs when something changed.
[[nodiscard]] Outcome run(const QString& requestFile, const QString& configDir, Preferences& prefs);

/// Stage B. The old temp dirs recorded by run(), cleared from the state file in
/// the same step — the caller queues them with PartFileConvert.
[[nodiscard]] QStringList takePendingTempDirs(const QString& configDir);

} // namespace LegacyConfigImport
} // namespace eMule
