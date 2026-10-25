#pragma once

/// @file InstallerCopy.h
/// @brief Copies the Windows setup the installer left behind into the Incoming dir.
///
/// The Windows installer runs elevated and cannot know the user's Incoming dir,
/// so it leaves a copy of itself in `<exe-dir>/setup/`. The daemon puts that copy
/// into the Incoming dir on its next start, once per user and setup version, so it
/// gets shared like any other completed file.

#include <QString>

namespace eMule::InstallerCopy {

/// Per-user record of the setup copied last time (its file name).
inline constexpr auto kStateFileName = "installer-copy.done";

enum class Result {
    NoInstaller,    ///< nothing staged next to the exe
    AlreadyDone,    ///< this setup was handled on an earlier start
    AlreadyThere,   ///< Incoming already had a file of that name; only recorded
    Copied,
    Failed,         ///< copy or state write failed, retried on the next start
};

struct Outcome {
    Result result = Result::NoInstaller;
    QString fileName;   ///< setup file name, empty for NoInstaller
};

/// Where the installer leaves its copy: `<exe-dir>/setup`.
[[nodiscard]] QString stagedDir();

/// Run before the daemon starts, so the first shared-files scan finds the copy.
/// Logs the outcome. A setup the user deleted from Incoming is not copied again.
Outcome run(const QString& stagedDir, const QString& configDir, const QString& incomingDir);

} // namespace eMule::InstallerCopy
