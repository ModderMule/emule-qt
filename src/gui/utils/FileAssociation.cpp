/// @file FileAssociation.cpp
/// @brief Registering .nzb, .torrent and magnet: with the desktop — implementation.

#include "utils/FileAssociation.h"

#include "utils/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

#ifdef Q_OS_WIN
#include <QSettings>
#include <shlobj.h>
#endif

namespace eMule::gui::FileAssociation {

namespace {

constexpr auto kProgId = "eMuleQt.nzb";
constexpr auto kTorrentProgId = "eMuleQt.torrent";
constexpr auto kDesktopFileName = "emuleqt.desktop";
constexpr auto kMimeFileName = "emuleqt-nzb.xml";
constexpr auto kTorrentMimeFileName = "emuleqt-torrent.xml";

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

QStringList xdgMimeTypes(FileTypes types)
{
    QStringList out;
    if (types.nzb)
        out << QLatin1String(kNzbMimeType);
    if (types.torrent)
        out << QLatin1String(kTorrentMimeType) << QStringLiteral("x-scheme-handler/magnet");
    // The ed2k scheme comes along with any type. The macOS bundle has claimed it
    // since day one.
    if (types.any())
        out << QStringLiteral("x-scheme-handler/ed2k");
    return out;
}

QList<DesktopFile> desktopFiles(const QString& exePath, const QString& dataHome, FileTypes types)
{
    const QString desktop = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=eMule Qt\n"
        "Comment=Peer-to-peer and Usenet client\n"
        "Exec=%1 %U\n"
        "Icon=emuleqt\n"
        "StartupWMClass=emuleqt\n"
        "Terminal=false\n"
        "Categories=Network;FileTransfer;P2P;\n"
        "MimeType=%2;\n")
                               .arg(quoteExec(exePath), xdgMimeTypes(types).join(QLatin1Char(';')));

    QList<DesktopFile> files{
        {QDir(dataHome).filePath(QStringLiteral("applications/%1").arg(kDesktopFileName)), desktop}};
    if (types.nzb) {
        files.append({QDir(dataHome).filePath(QStringLiteral("mime/packages/%1").arg(kMimeFileName)),
                      QStringLiteral(
                          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
                          "  <mime-type type=\"%1\">\n"
                          "    <comment>NZB Usenet index file</comment>\n"
                          "    <glob pattern=\"*.nzb\"/>\n"
                          "    <sub-class-of type=\"application/xml\"/>\n"
                          "  </mime-type>\n"
                          "</mime-info>\n")
                          .arg(QLatin1String(kNzbMimeType))});
    }
    if (types.torrent) {
        // shared-mime-info knows the type already; this only makes sure of the glob
        files.append({QDir(dataHome).filePath(QStringLiteral("mime/packages/%1").arg(kTorrentMimeFileName)),
                      QStringLiteral(
                          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
                          "  <mime-type type=\"%1\">\n"
                          "    <comment>BitTorrent seed file</comment>\n"
                          "    <glob pattern=\"*.torrent\"/>\n"
                          "  </mime-type>\n"
                          "</mime-info>\n")
                          .arg(QLatin1String(kTorrentMimeType))});
    }
    return files;
}

QList<IconFile> hicolorIconFiles(const QString& dataHome)
{
    QList<IconFile> files;
    for (const int size : {16, 32, 48, 64, 128, 256}) {
        files.append({QDir(dataHome).filePath(
                          QStringLiteral("icons/hicolor/%1x%1/apps/%2.png")
                              .arg(size).arg(QLatin1String(kDesktopId))),
                      QStringLiteral(":/icons/app/emuleqt-%1.png").arg(size)});
    }
    return files;
}

DesktopFile launcherDesktopFile(const QString& exePath, const QString& dataHome)
{
    return {QDir(dataHome).filePath(QStringLiteral("applications/%1").arg(kDesktopFileName)),
            QStringLiteral(
                "[Desktop Entry]\n"
                "Type=Application\n"
                "Name=eMule Qt\n"
                "Comment=Peer-to-peer and Usenet client\n"
                "Exec=%1\n"
                "Icon=emuleqt\n"
                "StartupWMClass=emuleqt\n"
                "Terminal=false\n"
                "Categories=Network;FileTransfer;P2P;\n")
                .arg(quoteExec(exePath))};
}

#ifdef Q_OS_LINUX

void installDesktopIcons()
{
    const QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        return;

    bool changed = false;
    for (const auto& icon : hicolorIconFiles(dataHome)) {
        QFile source(icon.resource);
        if (!source.open(QIODevice::ReadOnly))
            continue;
        const QByteArray png = source.readAll();

        // Runs at every start, so only touch the disk when the art changed.
        QFile target(icon.path);
        if (target.open(QIODevice::ReadOnly) && target.readAll() == png)
            continue;
        target.close();

        if (!QDir().mkpath(QFileInfo(icon.path).absolutePath())
            || !target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            logWarning(QStringLiteral("Cannot write %1").arg(icon.path));
            continue;
        }
        target.write(png);
        changed = true;
    }

    if (changed) {
        refresh(QStringLiteral("gtk-update-icon-cache"),
                {QStringLiteral("-q"), QStringLiteral("-t"), QStringLiteral("-f"),
                 QDir(dataHome).filePath(QStringLiteral("icons/hicolor"))});
    }
}

void installLauncher()
{
    installDesktopIcons();

    const QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        return;

    const DesktopFile launcher = launcherDesktopFile(exePath(), dataHome);
    if (QFile::exists(launcher.path))
        return;

    QDir().mkpath(QFileInfo(launcher.path).absolutePath());
    QFile file(launcher.path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logWarning(QStringLiteral("Cannot write %1").arg(launcher.path));
        return;
    }
    file.write(launcher.contents.toUtf8());
    file.close();

    refresh(QStringLiteral("update-desktop-database"),
            {QDir(dataHome).filePath(QStringLiteral("applications"))});
}

#else

void installDesktopIcons() {}
void installLauncher() {}

#endif

QList<RegistryValue> registryValues(const QString& exePath, FileTypes types)
{
    const QString native = QDir::toNativeSeparators(exePath);
    // Built by concatenation, not arg(): the literal "%1" Windows substitutes
    // the dropped filename into would otherwise be competing with QString's
    // own placeholder of the same name.
    const QString command = QLatin1Char('"') + native + QStringLiteral("\" \"%1\"");
    const QString icon = QStringLiteral("\"%1\",0").arg(native);

    // Everything is relative to HKEY_CURRENT_USER\Software\Classes. HKLM would
    // need elevation, and a portable zip has no moment at which to ask for it.
    QList<RegistryValue> out;
    const auto fileType = [&](const QString& ext, const QString& progId, const QString& name) {
        out.append({ext, {}, progId});
        out.append({progId, {}, name});
        out.append({progId + QStringLiteral("/DefaultIcon"), {}, icon});
        out.append({progId + QStringLiteral("/shell/open/command"), {}, command});
    };
    if (types.nzb)
        fileType(QStringLiteral(".nzb"), QLatin1String(kProgId), QStringLiteral("NZB Usenet Index File"));
    if (types.torrent) {
        fileType(QStringLiteral(".torrent"), QLatin1String(kTorrentProgId), QStringLiteral("BitTorrent File"));
        // a URL scheme, like ed2k (Ed2kSchemeHandler)
        out.append({QStringLiteral("magnet"), {}, QStringLiteral("URL:Magnet Link")});
        out.append({QStringLiteral("magnet"), QStringLiteral("URL Protocol"), {}});
        out.append({QStringLiteral("magnet/DefaultIcon"), {}, icon});
        out.append({QStringLiteral("magnet/shell/open/command"), {}, command});
    }
    return out;
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

bool applyFileTypes(FileTypes types, QString& error)
{
    // Info.plist's CFBundleDocumentTypes is the whole mechanism; LaunchServices
    // reads it when the bundle is installed. Nothing to write, and saying so is
    // better than reporting a failure the user cannot act on.
    Q_UNUSED(types);
    Q_UNUSED(error);
    return true;
}

bool unregisterFileTypes(QString& error)
{
    Q_UNUSED(error);
    return true;
}

#elif defined(Q_OS_WIN)

namespace {

/// Gives up an extension and its ProgID, the extension only while it still points
/// at the ProgID: another application may have taken it since.
void dropFileType(QSettings& classes, const QString& ext, const QString& progId)
{
    if (classes.value(ext + QStringLiteral("/.")).toString() == progId)
        classes.remove(ext);
    classes.remove(progId);
}

/// Gives up magnet: while its command still starts this executable.
void dropMagnet(QSettings& classes)
{
    const QString command = classes.value(QStringLiteral("magnet/shell/open/command/.")).toString();
    if (command.contains(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()), Qt::CaseInsensitive))
        classes.remove(QStringLiteral("magnet"));
}

bool finish(QSettings& classes, QString& error)
{
    classes.sync();
    if (classes.status() != QSettings::NoError) {
        error = QObject::tr("Could not write the file association to the registry.");
        return false;
    }
    // Without this Explorer keeps showing the old icon and handler until logoff.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

} // namespace

bool applyFileTypes(FileTypes types, QString& error)
{
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"),
                      QSettings::NativeFormat);

    if (!types.nzb)
        dropFileType(classes, QStringLiteral(".nzb"), QLatin1String(kProgId));
    if (!types.torrent) {
        dropFileType(classes, QStringLiteral(".torrent"), QLatin1String(kTorrentProgId));
        dropMagnet(classes);
    }
    for (const auto& value : registryValues(exePath(), types)) {
        // Pick the leaf first, and keep the join in a QString: with
        // QT_USE_QSTRINGBUILDER a ternary over the two concatenations has two
        // different builder types, and setValue() takes a QAnyStringView that a
        // builder cannot reach in one conversion.
        const QString leaf = value.name.isEmpty() ? QStringLiteral(".") : value.name;
        const QString path = value.key + QLatin1Char('/') + leaf;
        classes.setValue(path, value.value);
    }
    return finish(classes, error);
}

bool unregisterFileTypes(QString& error)
{
    return applyFileTypes({}, error);
}

#else

namespace {

QString dataHomeOrError(QString& error)
{
    const QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        error = QObject::tr("No writable data directory.");
    return dataHome;
}

void refreshDatabases(const QString& dataHome)
{
    refresh(QStringLiteral("update-mime-database"),
            {QDir(dataHome).filePath(QStringLiteral("mime"))});
    refresh(QStringLiteral("update-desktop-database"),
            {QDir(dataHome).filePath(QStringLiteral("applications"))});
}

} // namespace

bool applyFileTypes(FileTypes types, QString& error)
{
    const QString dataHome = dataHomeOrError(error);
    if (dataHome.isEmpty())
        return false;
    // nothing claimed: drop the entry; installLauncher() writes one that claims nothing
    if (!types.any())
        return unregisterFileTypes(error);

    installDesktopIcons();

    // the mime definitions of every type, so those switched off can be removed
    const QList<DesktopFile> all = desktopFiles(exePath(), dataHome, {true, true, true});
    const QList<DesktopFile> wanted = desktopFiles(exePath(), dataHome, types);
    for (const auto& entry : all)
        if (std::none_of(wanted.begin(), wanted.end(), [&](const DesktopFile& w) { return w.path == entry.path; }))
            QFile::remove(entry.path);

    for (const auto& entry : wanted) {
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

    refreshDatabases(dataHome);
    // the default handler too, or another client keeps magnet: and .torrent
    if (!QStandardPaths::findExecutable(QStringLiteral("xdg-mime")).isEmpty())
        QProcess::execute(QStringLiteral("xdg-mime"),
                          QStringList{QStringLiteral("default"), QLatin1String(kDesktopFileName)} + xdgMimeTypes(types));
    return true;
}

bool unregisterFileTypes(QString& error)
{
    const QString dataHome = dataHomeOrError(error);
    if (dataHome.isEmpty())
        return false;

    for (const auto& entry : desktopFiles(exePath(), dataHome, {true, true, true}))
        QFile::remove(entry.path);

    refreshDatabases(dataHome);
    return true;
}

#endif

} // namespace eMule::gui::FileAssociation
