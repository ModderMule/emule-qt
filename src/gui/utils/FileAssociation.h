#pragma once

/// @file FileAssociation.h
/// @brief Registering .nzb, .torrent, magnet:, ed3k: and ed2k: with the desktop, on all three platforms.
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
/// macOS declares the types in Info.plist (LaunchServices registers the bundle
/// when it is installed), but only at Alternate rank. Becoming the *default*
/// app is a LaunchServices call, made on request only: the Options check boxes
/// show the live default (claimedTypes()) and nothing is re-applied at start,
/// so the app never takes a type from another one by itself.

#include <QList>
#include <QString>
#include <QStringList>

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

/// What to claim. Each type the user can switch off on its own (Options).
struct FileTypes {
    bool nzb = false;
    /// .torrent (application/x-bittorrent) and the magnet: scheme
    bool torrent = false;
    /// the ed2k: scheme. Linux keeps it in the shared desktop entry; Ed2kSchemeHandler
    /// sets it, and it comes along with any other type. The ed3k: scheme goes with it
    /// (Options → General)
    bool ed2k = false;

    [[nodiscard]] bool any() const { return nzb || torrent || ed2k; }
};

/// The XDG desktop entry and the MIME definitions of @p types for @p exePath.
/// One entry for every type, so claiming one never drops another.
///
/// @p dataHome is $XDG_DATA_HOME, so a test can point it somewhere harmless.
[[nodiscard]] QList<DesktopFile> desktopFiles(const QString& exePath, const QString& dataHome,
                                              FileTypes types);

/// The MIME types an XDG desktop gets told to open with us (`xdg-mime default`).
[[nodiscard]] QStringList xdgMimeTypes(FileTypes types);

/// One icon the Linux launcher needs in the hicolor theme.
struct IconFile {
    QString path;       ///< absolute, under $XDG_DATA_HOME
    QString resource;   ///< the qrc PNG it is a copy of
};

/// Where `Icon=emuleqt` is looked up, one file per size we ship.
[[nodiscard]] QList<IconFile> hicolorIconFiles(const QString& dataHome);

/// A launcher entry that claims nothing: no MIME types, no schemes. It exists
/// so the desktop can put an icon on our window -- Wayland has no other way
/// than a .desktop file named after the app id.
[[nodiscard]] DesktopFile launcherDesktopFile(const QString& exePath,
                                              const QString& dataHome);

/// Copy the icons into the user's hicolor theme. Linux only, a no-op elsewhere.
void installDesktopIcons();

/// installDesktopIcons(), plus the launcher entry when there is none yet. An
/// existing emuleqt.desktop is left alone: it may carry a registration.
void installLauncher();

/// The per-user registry values of @p types for @p exePath, relative to Software\\Classes.
[[nodiscard]] QList<RegistryValue> registryValues(const QString& exePath, FileTypes types);

/// Whether the choice is stored and re-applied at every start (Windows, Linux).
/// False on macOS, where the system keeps the default app and claimedTypes()
/// reads it back.
[[nodiscard]] bool isRuntimeRegistration();

/// The types this app is the default for right now. macOS only (a type counts
/// when every extension and scheme in it is ours); {} elsewhere.
[[nodiscard]] FileTypes claimedTypes();

/// Claims @p types for this executable and gives up the others (only while still
/// ours). Idempotent, so it can run at every start and take an association back
/// from an application that took it away. On macOS giving up hands the type to
/// another app that declares it; with none there, it stays ours.
bool applyFileTypes(FileTypes types, QString& error);

/// Gives up every type. Idempotent (uninstall, --unregister-file-types).
bool unregisterFileTypes(QString& error);

/// The MIME type. NZB has no IANA registration; this is the de-facto one.
inline constexpr auto kNzbMimeType = "application/x-nzb";
inline constexpr auto kTorrentMimeType = "application/x-bittorrent";

/// Basename of the .desktop file, the icon, and the Wayland app id / WM class.
/// They have to agree or the desktop cannot tie the window to its launcher.
inline constexpr auto kDesktopId = "emuleqt";

} // namespace eMule::gui::FileAssociation
