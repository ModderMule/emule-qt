#include "pch.h"
/// @file Ed2kSchemeHandler.cpp
/// @brief Platform-specific ed2k:// URL scheme registration.

#include "app/Ed2kSchemeHandler.h"
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

    OSStatus status = LSSetDefaultHandlerForURLScheme(CFSTR("ed2k"), bundleId);
    if (status == noErr)
        logInfo(QStringLiteral("Registered as ed2k:// handler"));
    else
        logWarning(QStringLiteral("Failed to register ed2k:// handler (OSStatus %1)").arg(status));
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
    const QString path = desktopFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logWarning(QStringLiteral("Cannot write %1").arg(path));
        return;
    }

    QTextStream out(&f);
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=eMule Qt\n"
        << "Exec=" << QCoreApplication::applicationFilePath() << " %u\n"
        << "Icon=emuleqt\n"
        << "Terminal=false\n"
        << "MimeType=x-scheme-handler/ed2k;\n"
        << "NoDisplay=true\n";
    f.close();

    QProcess::execute(QStringLiteral("xdg-mime"),
                      {QStringLiteral("default"), QStringLiteral("emuleqt.desktop"),
                       QStringLiteral("x-scheme-handler/ed2k")});

    logInfo(QStringLiteral("Registered as ed2k:// handler"));
}

bool isEd2kSchemeRegistered()
{
    return QFile::exists(desktopFilePath());
}

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

#elif defined(Q_OS_WIN)

void registerEd2kUrlScheme()
{
    const QString appPath = QCoreApplication::applicationFilePath().replace(QLatin1Char('/'), QLatin1Char('\\'));

    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\ed2k"), QSettings::NativeFormat);
    reg.setValue(QStringLiteral("Default"), QStringLiteral("URL:eD2K Protocol"));
    reg.setValue(QStringLiteral("URL Protocol"), QString());
    reg.setValue(QStringLiteral("shell/open/command/Default"),
                 QStringLiteral("\"%1\" \"%2\"").arg(appPath, QStringLiteral("%1")));

    logInfo(QStringLiteral("Registered as ed2k:// handler"));
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
