#include "pch.h"
/// @file Ed2kSchemeHandler.cpp
/// @brief Platform-specific ed2k:// URL scheme registration, ed3k: with it.

#include "app/Ed2kSchemeHandler.h"
#include "app/UiState.h"
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

// Windows, Linux: the stored switch, in the shared entry with the other types
void registerEd2kUrlScheme()
{
    theUiState.setAssociateEd2kLinks(true);
    QString error;
    if (!gui::FileAssociation::applyFileTypes(savedFileTypes(), error)) {
        logWarning(QStringLiteral("Cannot register the ed2k:// handler: %1").arg(error));
        return;
    }
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
    return {.nzb = nzb, .torrent = torrent, .ed2k = theUiState.associateEd2kLinks().value_or(legacyEd2k)};
}

void applyDesktopIntegration()
{
    gui::FileAssociation::installLauncher();

    // Idempotent: takes an association back from an app that took it, and drops
    // it promptly once switched off. macOS keeps the choice itself.
    if (!gui::FileAssociation::isRuntimeRegistration())
        return;
    QString error;
    if (!gui::FileAssociation::applyFileTypes(savedFileTypes(), error)) {
        // never fatal, never a dialog: a desktop that refuses is no reason not to start
        logWarning(QStringLiteral("Could not update the file associations: %1").arg(error));
    }
}

} // namespace eMule
