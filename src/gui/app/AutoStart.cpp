#include "pch.h"
#include "app/AutoStart.h"

#include "utils/FileAssociation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#ifdef Q_OS_WIN
#include <QSettings>
#endif

namespace eMule {

#ifdef Q_OS_MACOS

static const QString kPlistName = QStringLiteral("org.emule.emuleqt.plist");

static QString launchAgentPath()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/") + kPlistName;
}

void setAutoStart(bool enabled)
{
    const QString path = launchAgentPath();

    if (!enabled) {
        QFile::remove(path);
        return;
    }

    // Ensure the LaunchAgents directory exists
    QDir().mkpath(QDir::homePath() + QStringLiteral("/Library/LaunchAgents"));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream ts(&file);
    ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
       << "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
       << "<plist version=\"1.0\">\n"
       << "<dict>\n"
       << "    <key>Label</key>\n"
       << "    <string>org.emule.emuleqt</string>\n"
       << "    <key>ProgramArguments</key>\n"
       << "    <array>\n"
       << "        <string>" << QCoreApplication::applicationFilePath() << "</string>\n"
       << "    </array>\n"
       << "    <key>RunAtLoad</key>\n"
       << "    <true/>\n"
       << "</dict>\n"
       << "</plist>\n";
}

bool isAutoStartEnabled()
{
    return QFile::exists(launchAgentPath());
}

void refreshAutoStart() {}

#elif defined(Q_OS_LINUX)

static QString desktopFilePath()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return configDir + QStringLiteral("/autostart/emuleqt.desktop");
}

/// The launcher script, not the bare .bin: that one finds no bundled libs (#111, #129)
static QByteArray desktopFileContents()
{
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=eMule Qt\n"
                          "Exec=%1\n"
                          "Icon=emuleqt\n"
                          "X-GNOME-Autostart-enabled=true\n")
        .arg(gui::FileAssociation::launcherExec())
        .toUtf8();
}

static void writeDesktopFile(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    file.write(desktopFileContents());
}

void setAutoStart(bool enabled)
{
    const QString path = desktopFilePath();
    if (!enabled) {
        QFile::remove(path);
        return;
    }
    writeDesktopFile(path);
}

bool isAutoStartEnabled()
{
    return QFile::exists(desktopFilePath());
}

void refreshAutoStart()
{
    const QString path = desktopFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;   // off: stays off
    if (file.readAll() == desktopFileContents())
        return;
    file.close();
    writeDesktopFile(path);
}

#elif defined(Q_OS_WIN)

static const QString kRegKey =
    QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
static const QString kValueName = QStringLiteral("eMuleQt");

void setAutoStart(bool enabled)
{
    QSettings reg(kRegKey, QSettings::NativeFormat);
    if (enabled)
        reg.setValue(kValueName,
                     QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    else
        reg.remove(kValueName);
}

bool isAutoStartEnabled()
{
    QSettings reg(kRegKey, QSettings::NativeFormat);
    return reg.contains(kValueName);
}

void refreshAutoStart() {}

#else

// Stub for unsupported platforms
void setAutoStart(bool) {}
bool isAutoStartEnabled() { return false; }
void refreshAutoStart() {}

#endif

} // namespace eMule
