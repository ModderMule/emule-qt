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
#include <utility>
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

void registerEd2kUrlScheme()
{
    // the shared desktop entry: written with the other types, so it keeps them
    QString error;
    if (!gui::FileAssociation::applyFileTypes(
            {theUiState.associateNzbFiles(), theUiState.associateTorrentFiles(), true}, error)) {
        logWarning(QStringLiteral("Cannot register the ed2k:// handler: %1").arg(error));
        return;
    }
    logInfo(QStringLiteral("Registered as ed2k:// handler"));
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

void registerEd2kUrlScheme()
{
    const QString appPath = QCoreApplication::applicationFilePath().replace(QLatin1Char('/'), QLatin1Char('\\'));

    for (const auto& [scheme, name] : {std::pair{QStringLiteral("ed2k"), QStringLiteral("URL:eD2K Protocol")},
                                       std::pair{QStringLiteral("ed3k"), QStringLiteral("URL:eD3K Protocol")}}) {
        QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\") + scheme, QSettings::NativeFormat);
        reg.setValue(QStringLiteral("Default"), name);
        reg.setValue(QStringLiteral("URL Protocol"), QString());
        reg.setValue(QStringLiteral("shell/open/command/Default"),
                     QStringLiteral("\"%1\" \"%2\"").arg(appPath, QStringLiteral("%1")));
    }

    logInfo(QStringLiteral("Registered as ed2k:// and ed3k: handler"));
}

bool isEd2kSchemeRegistered()
{
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\ed2k\\shell\\open\\command"),
                  QSettings::NativeFormat);
    return !reg.value(QStringLiteral("Default")).toString().isEmpty();
}

#else

void registerEd2kUrlScheme()
{
    logWarning(QStringLiteral("ed2k:// URL scheme registration not supported on this platform"));
}

bool isEd2kSchemeRegistered()
{
    return false;
}

#endif

} // namespace eMule
