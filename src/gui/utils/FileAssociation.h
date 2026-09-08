#pragma once

/// @file FileAssociation.h
/// @brief Registering .nzb with the desktop, on all three platforms.
///
/// The application registers *itself*, because there is nothing else to do it
/// with: bundle-win.ps1 produces a bare zip and bundle-linux.sh a bare tarball,
/// and neither has an installer to hang a registration step off. That is also
/// why every write here is per-user -- HKEY_CURRENT_USER and $XDG_DATA_HOME --
/// since a portable archive has no moment at which to ask for elevation.
///
/// Split the way IndexerQuery is split from IndexerClient, and for the same
/// reason: the two generators below are pure, so what registration *would*
/// write can be tested on any platform, including the ones where the writers do
/// not compile in.
///
/// macOS is declarative and needs no writer at all -- Info.plist's
/// CFBundleDocumentTypes is the whole mechanism, and LaunchServices registers
/// the bundle when it is installed.

#include <QList>
#include <QString>

namespace eMule::gui::FileAssociation {

/// One file the Linux registration writes.
struct DesktopFile {
    QString path;       ///< absolute, under $XDG_DATA_HOME
    QString contents;
};

/// One value the Windows registration sets.
struct RegistryValue {
    QString key;        ///< relative to HKEY_CURRENT_USER\\Software\\Classes
    QString name;       ///< empty means the key's default value
    QString value;
};

/// The XDG desktop entry and MIME definition for @p exePath.
///
/// @p dataHome is $XDG_DATA_HOME, so a test can point it somewhere harmless.
[[nodiscard]] QList<DesktopFile> nzbDesktopFiles(const QString& exePath,
                                                 const QString& dataHome);

/// The per-user registry values for @p exePath, relative to Software\\Classes.
[[nodiscard]] QList<RegistryValue> nzbRegistryValues(const QString& exePath);

/// Whether this platform registers at runtime at all. False on macOS, where the
/// bundle declares its own types and there is nothing to write.
[[nodiscard]] bool isRuntimeRegistration();

/// Claim .nzb for this executable. Idempotent, so it can run at every start and
/// take the association back from an application that took it away.
bool registerNzbFileType(QString& error);

/// Give it up again. Idempotent.
bool unregisterNzbFileType(QString& error);

/// The MIME type. NZB has no IANA registration; this is the de-facto one.
inline constexpr auto kNzbMimeType = "application/x-nzb";

} // namespace eMule::gui::FileAssociation
