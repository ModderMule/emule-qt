/// @file FileAssociation.cpp
/// @brief Registering .nzb with the desktop — implementation.

#include "utils/FileAssociation.h"

#include "utils/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <QSettings>
#include <shlobj.h>
#endif

namespace eMule::gui::FileAssociation {

namespace {

constexpr auto kProgId = "eMuleQt.nzb";
constexpr auto kDesktopFileName = "emuleqt.desktop";
constexpr auto kMimeFileName = "emuleqt-nzb.xml";

QString exePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

/// Quote an Exec= path the way the desktop-entry spec wants.
///
/// Without this an executable under a directory with a space in its name — which
/// on macOS and Windows is the normal case — produces a launcher that silently
/// starts nothing.
QString quoteExec(const QString& path)
{
    QString escaped = path;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

#ifndef Q_OS_MACOS
/// Run a desktop database refresh if the tool is there. Its absence is not an
/// error: it only means the desktop notices at the next login.
void refresh(const QString& tool, const QStringList& args)
{
    if (QStandardPaths::findExecutable(tool).isEmpty())
        return;
    QProcess::execute(tool, args);
}
#endif

} // namespace

QList<DesktopFile> nzbDesktopFiles(const QString& exePath, const QString& dataHome)
{
    const QString desktop = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=eMule Qt\n"
        "Comment=Peer-to-peer and Usenet client\n"
        "Exec=%1 %U\n"
        "Icon=emuleqt\n"
        "Terminal=false\n"
        "Categories=Network;FileTransfer;P2P;\n"
        // The ed2k scheme comes along for free here. The macOS bundle has
        // claimed it since day one and Linux never has.
        "MimeType=%2;x-scheme-handler/ed2k;\n")
                               .arg(quoteExec(exePath), QLatin1String(kNzbMimeType));

    const QString mime = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
        "  <mime-type type=\"%1\">\n"
        "    <comment>NZB Usenet index file</comment>\n"
        "    <glob pattern=\"*.nzb\"/>\n"
        "    <sub-class-of type=\"application/xml\"/>\n"
        "  </mime-type>\n"
        "</mime-info>\n")
                             .arg(QLatin1String(kNzbMimeType));

    return {
        {QDir(dataHome).filePath(QStringLiteral("applications/%1").arg(kDesktopFileName)),
         desktop},
        {QDir(dataHome).filePath(QStringLiteral("mime/packages/%1").arg(kMimeFileName)), mime},
    };
}

QList<RegistryValue> nzbRegistryValues(const QString& exePath)
{
    const QString progId = QLatin1String(kProgId);
    const QString native = QDir::toNativeSeparators(exePath);

    // Everything is relative to HKEY_CURRENT_USER\Software\Classes. HKLM would
    // need elevation, and a portable zip has no moment at which to ask for it.
    return {
        {QStringLiteral(".nzb"), {}, progId},
        {progId, {}, QStringLiteral("NZB Usenet Index File")},
        {progId + QStringLiteral("/DefaultIcon"), {},
         QStringLiteral("\"%1\",0").arg(native)},
        // Built by concatenation, not arg(): the literal "%1" Windows substitutes
        // the dropped filename into would otherwise be competing with QString's
        // own placeholder of the same name.
        {progId + QStringLiteral("/shell/open/command"), {},
         QLatin1Char('"') + native + QStringLiteral("\" \"%1\"")},
    };
}

bool isRuntimeRegistration()
{
#ifdef Q_OS_MACOS
    return false;
#else
    return true;
#endif
}

#ifdef Q_OS_MACOS

bool registerNzbFileType(QString& error)
{
    // Info.plist's CFBundleDocumentTypes is the whole mechanism; LaunchServices
    // reads it when the bundle is installed. Nothing to write, and saying so is
    // better than reporting a failure the user cannot act on.
    Q_UNUSED(error);
    return true;
}

bool unregisterNzbFileType(QString& error)
{
    Q_UNUSED(error);
    return true;
}

#elif defined(Q_OS_WIN)

bool registerNzbFileType(QString& error)
{
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"),
                      QSettings::NativeFormat);

    for (const auto& value : nzbRegistryValues(exePath())) {
        // Pick the leaf first, and keep the join in a QString: with
        // QT_USE_QSTRINGBUILDER a ternary over the two concatenations has two
        // different builder types, and setValue() takes a QAnyStringView that a
        // builder cannot reach in one conversion.
        const QString leaf = value.name.isEmpty() ? QStringLiteral(".") : value.name;
        const QString path = value.key + QLatin1Char('/') + leaf;
        classes.setValue(path, value.value);
    }
    classes.sync();

    if (classes.status() != QSettings::NoError) {
        error = QObject::tr("Could not write the file association to the registry.");
        return false;
    }

    // Without this Explorer keeps showing the old icon and handler until logoff.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool unregisterNzbFileType(QString& error)
{
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"),
                      QSettings::NativeFormat);

    // Only give up .nzb when it is still ours: another application may have
    // taken it since, and stealing it back to delete it would be worse than
    // leaving it alone.
    if (classes.value(QStringLiteral(".nzb/.")).toString() == QLatin1String(kProgId))
        classes.remove(QStringLiteral(".nzb"));
    classes.remove(QLatin1String(kProgId));
    classes.sync();

    if (classes.status() != QSettings::NoError) {
        error = QObject::tr("Could not remove the file association from the registry.");
        return false;
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

#else

bool registerNzbFileType(QString& error)
{
    const QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty()) {
        error = QObject::tr("No writable data directory.");
        return false;
    }

    for (const auto& entry : nzbDesktopFiles(exePath(), dataHome)) {
        if (!QDir().mkpath(QFileInfo(entry.path).absolutePath())) {
            error = QObject::tr("Could not create %1.").arg(QFileInfo(entry.path).path());
            return false;
        }
        QFile file(entry.path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            error = file.errorString();
            return false;
        }
        file.write(entry.contents.toUtf8());
    }

    refresh(QStringLiteral("update-mime-database"),
            {QDir(dataHome).filePath(QStringLiteral("mime"))});
    refresh(QStringLiteral("update-desktop-database"),
            {QDir(dataHome).filePath(QStringLiteral("applications"))});
    return true;
}

bool unregisterNzbFileType(QString& error)
{
    const QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty()) {
        error = QObject::tr("No writable data directory.");
        return false;
    }

    for (const auto& entry : nzbDesktopFiles(exePath(), dataHome))
        QFile::remove(entry.path);

    refresh(QStringLiteral("update-mime-database"),
            {QDir(dataHome).filePath(QStringLiteral("mime"))});
    refresh(QStringLiteral("update-desktop-database"),
            {QDir(dataHome).filePath(QStringLiteral("applications"))});
    return true;
}

#endif

} // namespace eMule::gui::FileAssociation
