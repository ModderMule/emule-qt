#pragma once

/// @file LegacyConfigImport.h
/// @brief Import of an eMule (MFC) or aMule config dir, asked for in the first start wizard.
///
/// The daemon finds the installs on its own host (discover()). Settings go through
/// a request file in the config dir and a core restart: run() carries it out before
/// anything reads the config dir, so known.met, credits and the user hash load as
/// copied. Data files already there are moved to a backup folder first; the settings
/// files (preferences.yml, uistate.yml, preferences.ini) are copied there too. The old temp
/// dirs are handed to PartFileConvert once the daemon runs. The source is only read,
/// so the old client keeps working.

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace eMule {

class Preferences;

namespace LegacyConfigImport {

/// Asked for by the wizard, carried out on the next start, then removed.
inline constexpr auto kRequestFileName = "mfc-import.yml";
/// Record of the last import; also carries the temp dirs still waiting for the
/// part-file converter.
inline constexpr auto kStateFileName = "mfc-import.done";

enum class SourceKind {
    Emule = 0,   ///< preferences.ini (Win32 profile API)
    Amule = 1,   ///< amule.conf (wxFileConfig)
};

/// One install found on this host, or picked by the user.
struct Source {
    SourceKind kind = SourceKind::Emule;
    QString configDir;
    /// Wine prefix the install lives in; its C:\ paths map into it. Empty = native.
    QString winePrefix;
    QString nick;
    QStringList tempDirs;   ///< existing ones only, mapped to this host
    int partFiles = 0;      ///< .part.met files in tempDirs
};

/// Where discover() looks. Empty entries are skipped; tests pass temp dirs.
struct SearchRoots {
    QString home;
    QString appData;        ///< Windows %APPDATA%
    QString localAppData;   ///< Windows %LOCALAPPDATA%
    QString programData;    ///< Windows %PROGRAMDATA%
    QStringList programFiles;
    QStringList emuleExeDirs;   ///< from the registry: ed2k handler, autostart
    bool windowsLayout = false; ///< look in AppData/ProgramData/Program Files
    bool unixLayout = false;    ///< look in ~/.aMule, ~/Library, Flatpak, Wine
    bool macLayout = false;
};

enum class Result {
    NoRequest,      ///< no request file, nothing to do
    SourceMissing,  ///< requested dir has no preferences.ini / amule.conf
    Imported,
};

struct Outcome {
    Result result = Result::NoRequest;
    QString sourceDir;
    int filesCopied = 0;
    int filesBackedUp = 0;      ///< own data files moved aside (replaced by the source's)
    int settingsBackedUp = 0;   ///< preferences.yml, uistate.yml, preferences.ini copied
    QString backupDir;      ///< where this profile's own files went, if any
    int prefsApplied = 0;
    int categories = 0;     ///< without the implicit "All"
    int sharedDirs = 0;
    QStringList pendingTempDirs;
    bool userHashDerived = false;   ///< source had no hash: ours set to the ed3k-derived one
};

/// The roots of this host and user.
[[nodiscard]] SearchRoots hostRoots();

/// Every eMule and aMule config dir under @p roots, the most likely one first.
[[nodiscard]] QList<Source> discover(const SearchRoots& roots);
[[nodiscard]] inline QList<Source> discover() { return discover(hostRoots()); }

/// A dir the user picked: its config dir, the dir above one, or a Wine prefix.
[[nodiscard]] std::optional<Source> probe(const QString& dir);

/// `<configDir>/mfc-import.yml`.
[[nodiscard]] QString requestFilePath(const QString& configDir);

/// Ask for an import on the next start. @p parts queues @p source's tempDirs too
/// (the detected ones, or what the user put in their place).
bool writeRequest(const QString& configDir, const Source& source, bool parts);

/// Stage A. Run before LegacyIdentityImport::run() and before anything reads
/// the config dir. Logs the outcome, saves @p prefs, removes the request.
[[nodiscard]] Outcome run(const QString& configDir, Preferences& prefs);

/// Stage B. The old temp dirs recorded by run(), cleared from the state file in
/// the same step — the caller queues them with PartFileConvert.
[[nodiscard]] QStringList takePendingTempDirs(const QString& configDir);

} // namespace LegacyConfigImport
} // namespace eMule
