#include "pch.h"
/// @file Ed2kSchemeHandler.cpp
/// @brief Platform-specific ed2k:// URL scheme registration, ed3k: with it.

#include "app/Ed2kSchemeHandler.h"
#include "app/AutoStart.h"
#include "app/UiState.h"
#include "prefs/Preferences.h"
#include "utils/FileAssociation.h"
#include "utils/Log.h"

#include <QCoreApplication>

#ifdef Q_OS_MACOS
#include <CoreServices/CoreServices.h>
#endif

#ifdef Q_OS_LINUX
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#endif

#ifdef Q_OS_WIN
#include <QSettings>
#endif

namespace eMule {

namespace {
int toBits(gui::FileAssociation::FileTypes t);
} // namespace

// ---------------------------------------------------------------------------
// macOS
// ---------------------------------------------------------------------------

#ifdef Q_OS_MACOS

void registerEd2kUrlScheme()
{
    CFStringRef bundleId = CFBundleGetIdentifier(CFBundleGetMainBundle());
    if (!bundleId) {
        logWarning(QStringLiteral("Cannot register ed2k scheme: no bundle identifier"));
        return;
    }

    for (const CFStringRef scheme : {CFSTR("ed2k"), CFSTR("ed3k")}) {
        const OSStatus status = LSSetDefaultHandlerForURLScheme(scheme, bundleId);
        const QString name = QString::fromCFString(scheme);
        if (status == noErr)
            logInfo(QStringLiteral("Registered as %1: handler").arg(name));
        else
            logWarning(QStringLiteral("Failed to register %1: handler (OSStatus %2)").arg(name).arg(status));
    }
}

bool isEd2kSchemeRegistered()
{
    CFStringRef bundleId = CFBundleGetIdentifier(CFBundleGetMainBundle());
    if (!bundleId)
        return false;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CFStringRef handler = LSCopyDefaultHandlerForURLScheme(CFSTR("ed2k"));
#pragma clang diagnostic pop
    if (!handler)
        return false;

    bool match = (CFStringCompare(handler, bundleId, kCFCompareCaseInsensitive) == kCFCompareEqualTo);
    CFRelease(handler);
    return match;
}

// ---------------------------------------------------------------------------
// Linux
// ---------------------------------------------------------------------------

#elif defined(Q_OS_LINUX)

static QString desktopFilePath()
{
    return QDir::homePath() + QStringLiteral("/.local/share/applications/emuleqt.desktop");
}

bool isEd2kSchemeRegistered()
{
    QFile f(desktopFilePath());
    return f.open(QIODevice::ReadOnly) && f.readAll().contains("x-scheme-handler/ed2k");
}

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

#elif defined(Q_OS_WIN)

bool isEd2kSchemeRegistered()
{
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\ed2k\\shell\\open\\command"),
                  QSettings::NativeFormat);
    return !reg.value(QStringLiteral("Default")).toString().isEmpty();
}

#else

bool isEd2kSchemeRegistered()
{
    return false;
}

#endif

#ifndef Q_OS_MACOS

// Windows, Linux: the stored switch, in the shared entry with the other types.
// Asked for: claims the links again even when nothing changed.
void registerEd2kUrlScheme()
{
    theUiState.setAssociateEd2kLinks(true);
    if (!wantedFileTypes().ed2k) {
        logWarning(QStringLiteral("eD2K, Kad and ed3k are all off: ed2k:// links not claimed"));
        return;
    }
    if (applyFileTypesNow(/*setDefault*/ true))
        logInfo(QStringLiteral("Registered as ed2k:// and ed3k: handler"));
}

#endif

gui::FileAssociation::FileTypes savedFileTypes()
{
    const bool nzb = theUiState.associateNzbFiles();
    const bool torrent = theUiState.associateTorrentFiles();
    // not stored yet: what the older builds did. Linux claimed ed2k with any
    // other type, Windows only after the Options button
#ifdef Q_OS_LINUX
    const bool legacyEd2k = nzb || torrent;
#else
    const bool legacyEd2k = isEd2kSchemeRegistered();
#endif
    return {.nzb = nzb,
            .torrent = torrent,
            .ed2k = theUiState.associateEd2kLinks().value_or(legacyEd2k),
            .dlc = theUiState.associateDlcFiles()};
}

gui::FileAssociation::FileTypes enabledNetworkTypes()
{
    return {.nzb = thePrefs.usenetEnabled(),
            .torrent = thePrefs.btEnabled(),
            .ed2k = thePrefs.networkED2K() || thePrefs.kadEnabled() || thePrefs.ed3kEnabled(),
            // the Hoster tab opens them: ed3k and hoster downloads on, a build with the resolver
#if defined(EMULE_HAVE_ED3K) && defined(EMULE_HAVE_HOSTER_JD)
            .dlc = thePrefs.hosterDownloadsOn()
#else
            .dlc = false
#endif
    };
}

gui::FileAssociation::FileTypes wantedFileTypes()
{
    return savedFileTypes().masked(enabledNetworkTypes());
}

void applyDesktopIntegration(bool createLauncher)
{
    if (createLauncher)
        theUiState.setLauncherInstalled(true);
    gui::FileAssociation::installLauncher(createLauncher);
    refreshAutoStart();   // an old entry started the bare .bin (#129)

    // macOS keeps the choice itself
    if (!gui::FileAssociation::isRuntimeRegistration())
        return;
    // Only a change is applied: a handler the user picked elsewhere stays (#121)
    const auto wanted = wantedFileTypes();
    const auto applied = theUiState.appliedFileTypes();
    if (!applied || fromBits(*applied) != wanted)
        applyFileTypesNow(/*setDefault*/ true);
    else if (gui::FileAssociation::isStale(wanted))
        applyFileTypesNow(/*setDefault*/ false);   // moved install: new path, same claim
}

void adoptClaimedFileTypes()
{
    if (!gui::FileAssociation::isRuntimeRegistration() || theUiState.appliedFileTypes())
        return;
    // older builds saved the default switches (all on) as if chosen: take what our
    // entries claim now as the choice, and write nothing (#153)
    const auto claimed = gui::FileAssociation::claimedTypes();
    theUiState.setAssociateNzbFiles(claimed.nzb);
    theUiState.setAssociateTorrentFiles(claimed.torrent);
    theUiState.setAssociateEd2kLinks(claimed.ed2k);
    theUiState.setAssociateDlcFiles(claimed.dlc);
    theUiState.setAppliedFileTypes(toBits(claimed));
}

void claimAllFileTypes()
{
    theUiState.setAssociateNzbFiles(true);
    theUiState.setAssociateTorrentFiles(true);
    theUiState.setAssociateEd2kLinks(true);
    theUiState.setAssociateDlcFiles(true);
    applyDesktopIntegration(/*createLauncher*/ true);
}

gui::FileAssociation::FileTypes fromBits(int bits)
{
    return {.nzb = (bits & 1) != 0, .torrent = (bits & 2) != 0, .ed2k = (bits & 4) != 0, .dlc = (bits & 8) != 0};
}

bool applyFileTypesNow(bool setDefault)
{
    const auto wanted = wantedFileTypes();
    QString error;
    if (!gui::FileAssociation::applyFileTypes(wanted, error, setDefault)) {
        // never fatal, never a dialog: a desktop that refuses is no reason not to start
        logWarning(QStringLiteral("Could not update the file associations: %1").arg(error));
        return false;
    }
    theUiState.setAppliedFileTypes(toBits(wanted));
    return true;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

namespace {

int toBits(gui::FileAssociation::FileTypes t)
{
    return (t.nzb ? 1 : 0) | (t.torrent ? 2 : 0) | (t.ed2k ? 4 : 0) | (t.dlc ? 8 : 0);
}

} // namespace

} // namespace eMule
