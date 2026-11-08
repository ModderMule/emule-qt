/// @file FileAssociation.cpp
/// @brief Registering .nzb, .torrent, .dlc, magnet:, ed3k: and ed2k: with the desktop — implementation.

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

#ifdef Q_OS_MACOS
#include <CoreServices/CoreServices.h>
#endif

namespace eMule::gui::FileAssociation {

namespace {

constexpr auto kProgId = "eMuleQt.nzb";
constexpr auto kTorrentProgId = "eMuleQt.torrent";
constexpr auto kDlcProgId = "eMuleQt.dlc";
constexpr auto kDesktopFileName = "emuleqt.desktop";
constexpr auto kMimeFileName = "emuleqt-nzb.xml";
constexpr auto kTorrentMimeFileName = "emuleqt-torrent.xml";
constexpr auto kDlcMimeFileName = "emuleqt-dlc.xml";

/// What the desktop should start. The Linux tarball runs foo.bin through a foo
/// script that sets LD_LIBRARY_PATH; the bare .bin finds no libraries (#111).
QString exePath()
{
    const QString self = QCoreApplication::applicationFilePath();
#ifdef Q_OS_LINUX
    if (self.endsWith(QLatin1String(".bin")) && qEnvironmentVariableIsSet("EMULEQT_BUNDLE_LIBS")) {
        const QFileInfo script(self.chopped(4));
        if (script.isFile() && script.isExecutable())
            return script.absoluteFilePath();
    }
#endif
    return QDir::toNativeSeparators(self);
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
    // ed3k goes with ed2k; each switch on its own (#111)
    if (types.ed2k)
        out << QStringLiteral("x-scheme-handler/ed2k") << QStringLiteral("x-scheme-handler/ed3k");
    if (types.dlc)
        out << QLatin1String(kDlcMimeType);
    return out;
}

QString withoutMimeDefaults(const QString& mimeapps, const QStringList& mimeTypes)
{
    const QString ours = QLatin1String(kDesktopFileName);
    QStringList out;
    bool removedSection = false;
    for (const QString& line : mimeapps.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('['))) {
            removedSection = trimmed == QLatin1String("[Removed Associations]");
            out << line;
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (removedSection || eq <= 0 || trimmed.startsWith(QLatin1Char('#'))
            || !mimeTypes.contains(line.left(eq).trimmed())) {
            out << line;
            continue;
        }
        QStringList ids = line.mid(eq + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (QString& id : ids)
            id = id.trimmed();
        if (ids.removeAll(ours) == 0) {
            out << line;
            continue;
        }
        if (!ids.isEmpty())
            out << line.left(eq + 1) + ids.join(QLatin1Char(';')) + QLatin1Char(';');
    }
    return out.join(QLatin1Char('\n'));
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
    if (types.dlc) {
        files.append({QDir(dataHome).filePath(QStringLiteral("mime/packages/%1").arg(kDlcMimeFileName)),
                      QStringLiteral(
                          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
                          "  <mime-type type=\"%1\">\n"
                          "    <comment>DLC link container</comment>\n"
                          "    <glob pattern=\"*.dlc\"/>\n"
                          "  </mime-type>\n"
                          "</mime-info>\n")
                          .arg(QLatin1String(kDlcMimeType))});
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

void installLauncher(bool createMissing)
{
    const QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        return;

    const DesktopFile launcher = launcherDesktopFile(exePath(), dataHome);
    if (QFile existing(launcher.path); existing.open(QIODevice::ReadOnly)) {
        // one that claims types belongs to applyFileTypes(); a bare one is
        // rewritten only when stale (Exec= of an old .bin start, #111)
        const QByteArray contents = existing.readAll();
        if (contents.contains("MimeType=") || contents == launcher.contents.toUtf8()) {
            installDesktopIcons();   // only writes art that changed
            return;
        }
    } else if (!createMissing) {
        return;   // deleted by the user: stays deleted (#121)
    }

    installDesktopIcons();
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
void installLauncher(bool) {}

#endif

QString launcherExec()
{
    return quoteExec(exePath());
}

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
    // a URL scheme, like ed2k (Ed2kSchemeHandler)
    const auto urlScheme = [&](const QString& scheme, const QString& name) {
        out.append({scheme, {}, name});
        out.append({scheme, QStringLiteral("URL Protocol"), {}});
        out.append({scheme + QStringLiteral("/DefaultIcon"), {}, icon});
        out.append({scheme + QStringLiteral("/shell/open/command"), {}, command});
    };
    if (types.torrent) {
        fileType(QStringLiteral(".torrent"), QLatin1String(kTorrentProgId), QStringLiteral("BitTorrent File"));
        urlScheme(QStringLiteral("magnet"), QStringLiteral("URL:Magnet Link"));
    }
    if (types.ed2k) {
        urlScheme(QStringLiteral("ed2k"), QStringLiteral("URL:eD2K Protocol"));
        urlScheme(QStringLiteral("ed3k"), QStringLiteral("URL:eD3K Protocol"));
    }
    if (types.dlc)
        fileType(QStringLiteral(".dlc"), QLatin1String(kDlcProgId), QStringLiteral("DLC Link Container"));
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

FileTypes typesInDesktopEntry(const QString& contents)
{
    for (const QString& line : contents.split(QLatin1Char('\n'))) {
        if (!line.startsWith(QLatin1String("MimeType=")))
            continue;
        const QStringList mimes = line.mid(9).trimmed().split(QLatin1Char(';'), Qt::SkipEmptyParts);
        return {.nzb = mimes.contains(QLatin1String(kNzbMimeType)),
                .torrent = mimes.contains(QLatin1String(kTorrentMimeType)),
                .ed2k = mimes.contains(QLatin1String("x-scheme-handler/ed2k")),
                .dlc = mimes.contains(QLatin1String(kDlcMimeType))};
    }
    return {};
}

#if defined(Q_OS_LINUX)
FileTypes claimedTypes()
{
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        return {};
    QFile file(desktopFiles(exePath(), dataHome, {}).constFirst().path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return typesInDesktopEntry(QString::fromUtf8(file.readAll()));
}
#elif defined(Q_OS_WIN)
FileTypes claimedTypes()
{
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"), QSettings::NativeFormat);
    const auto ownsExt = [&](const QString& ext, const char* progId) {
        return classes.value(ext + QStringLiteral("/.")).toString() == QLatin1String(progId);
    };
    const auto ownsScheme = [&](const QString& scheme) {
        return classes.value(scheme + QStringLiteral("/shell/open/command/.")).toString()
            .contains(QLatin1String("emuleqt"), Qt::CaseInsensitive);
    };
    return {.nzb = ownsExt(QStringLiteral(".nzb"), kProgId),
            .torrent = ownsExt(QStringLiteral(".torrent"), kTorrentProgId),
            .ed2k = ownsScheme(QStringLiteral("ed2k")),
            .dlc = ownsExt(QStringLiteral(".dlc"), kDlcProgId)};
}
#elif !defined(Q_OS_MACOS)
FileTypes claimedTypes()
{
    return {};
}
#endif

#ifdef Q_OS_MACOS

// The LaunchServices default-handler calls are deprecated since macOS 12 but
// still the only C API; NSWorkspace's replacement is Objective-C and async.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace {

/// One thing LaunchServices keeps a default app for.
struct Target {
    bool scheme;    ///< URL scheme, else a file extension
    QString name;   ///< "magnet", or "torrent" without the dot
};

QList<Target> targetsOf(FileTypes types)
{
    QList<Target> out;
    if (types.nzb)
        out.append({false, QStringLiteral("nzb")});
    if (types.torrent) {
        out.append({false, QStringLiteral("torrent")});
        out.append({true, QStringLiteral("magnet")});
    }
    if (types.ed2k) {
        out.append({true, QStringLiteral("ed2k")});
        out.append({true, QStringLiteral("ed3k")});
    }
    if (types.dlc)
        out.append({false, QStringLiteral("dlc")});
    return out;
}

/// Takes ownership of a Create/Copy result.
QString takeString(CFStringRef s)
{
    if (!s)
        return {};
    const QString out = QString::fromCFString(s);
    CFRelease(s);
    return out;
}

QString ownBundleId()
{
    const CFStringRef id = CFBundleGetIdentifier(CFBundleGetMainBundle());   // Get rule
    return id ? QString::fromCFString(id) : QString();
}

/// LaunchServices keys a file type by UTI: the one an installed app declared
/// for the extension (Transmission's org.bittorrent.torrent), else a dynamic one.
QString keyOf(const Target& t)
{
    if (t.scheme)
        return t.name;
    const CFStringRef ext = t.name.toCFString();
    const QString uti = takeString(
        UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, ext, nullptr));
    CFRelease(ext);
    return uti;
}

QString defaultHandler(const Target& t)
{
    const CFStringRef key = keyOf(t).toCFString();
    const QString out = takeString(t.scheme ? LSCopyDefaultHandlerForURLScheme(key)
                                            : LSCopyDefaultRoleHandlerForContentType(key, kLSRolesAll));
    CFRelease(key);
    return out;
}

QStringList allHandlers(const Target& t)
{
    const CFStringRef key = keyOf(t).toCFString();
    const CFArrayRef list = t.scheme ? LSCopyAllHandlersForURLScheme(key)
                                     : LSCopyAllRoleHandlersForContentType(key, kLSRolesAll);
    CFRelease(key);
    QStringList out;
    if (!list)
        return out;
    for (CFIndex i = 0; i < CFArrayGetCount(list); ++i)
        out << QString::fromCFString(static_cast<CFStringRef>(CFArrayGetValueAtIndex(list, i)));
    CFRelease(list);
    return out;
}

OSStatus setHandler(const Target& t, const QString& bundleId)
{
    const CFStringRef key = keyOf(t).toCFString();
    const CFStringRef id = bundleId.toCFString();
    const OSStatus status = t.scheme ? LSSetDefaultHandlerForURLScheme(key, id)
                                     : LSSetDefaultRoleHandlerForContentType(key, kLSRolesAll, id);
    CFRelease(id);
    CFRelease(key);
    return status;
}

bool isOurs(const Target& t, const QString& ownId)
{
    return defaultHandler(t).compare(ownId, Qt::CaseInsensitive) == 0;
}

bool ownsAll(FileTypes types, const QString& ownId)
{
    const QList<Target> targets = targetsOf(types);
    return std::ranges::all_of(targets, [&](const Target& t) { return isOurs(t, ownId); });
}

QString label(const Target& t)
{
    if (t.scheme)
        return QString(t.name + QLatin1Char(':'));
    return QString(QLatin1Char('.') + t.name);
}

} // namespace

FileTypes claimedTypes()
{
    const QString ownId = ownBundleId();
    if (ownId.isEmpty())
        return {};
    return {.nzb = ownsAll({.nzb = true}, ownId),
            .torrent = ownsAll({.torrent = true}, ownId),
            .ed2k = ownsAll({.ed2k = true}, ownId),
            .dlc = ownsAll({.dlc = true}, ownId)};
}

bool applyFileTypes(FileTypes types, QString& error, bool /*setDefault*/)
{
    const QString ownId = ownBundleId();
    if (ownId.isEmpty()) {
        error = QObject::tr("Not started from the app bundle.");
        return false;
    }

    const QList<Target> wantedTargets = targetsOf(types);
    QStringList failed;
    for (const Target& t : targetsOf(FileTypes::all())) {
        const bool wanted = std::ranges::any_of(wantedTargets, [&](const Target& w) {
            return w.scheme == t.scheme && w.name == t.name;
        });
        if (wanted == isOurs(t, ownId))
            continue;   // only touch what changes: no system prompt for a no-op

        QString next = ownId;
        if (!wanted) {
            // hand it on to whatever else declares it; none, and it stays ours
            next.clear();
            for (const QString& id : allHandlers(t)) {
                if (id.compare(ownId, Qt::CaseInsensitive) != 0) {
                    next = id;
                    break;
                }
            }
            if (next.isEmpty()) {
                logInfo(QStringLiteral("No other app opens %1, it stays with eMule Qt").arg(label(t)));
                continue;
            }
        }
        const OSStatus status = setHandler(t, next);
        if (status != noErr) {
            failed << label(t);
            logWarning(QStringLiteral("Cannot set the default app of %1 to %2 (OSStatus %3)")
                           .arg(label(t), next)
                           .arg(status));
        }
    }
    if (!failed.isEmpty()) {
        error = QObject::tr("LaunchServices refused %1.").arg(failed.join(QStringLiteral(", ")));
        return false;
    }
    return true;
}

#pragma clang diagnostic pop

bool isStale(FileTypes)
{
    return false;
}

bool unregisterFileTypes(QString& error)
{
    return applyFileTypes({}, error);
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

/// Gives up a URL scheme while its command still starts this executable.
void dropScheme(QSettings& classes, const QString& scheme)
{
    const QString command = classes.value(scheme + QStringLiteral("/shell/open/command/.")).toString();
    if (command.contains(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()), Qt::CaseInsensitive))
        classes.remove(scheme);
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

bool isStale(FileTypes types)
{
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"), QSettings::NativeFormat);
    for (const auto& value : registryValues(exePath(), types)) {
        if (!value.key.endsWith(QLatin1String("/shell/open/command")))
            continue;
        const QString current = classes.value(value.key + QStringLiteral("/.")).toString();
        // gone or taken by another app: not ours to rewrite
        if (!current.isEmpty() && current != value.value && current.contains(QLatin1String("emuleqt"), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

bool applyFileTypes(FileTypes types, QString& error, bool /*setDefault*/)
{
    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"),
                      QSettings::NativeFormat);

    if (!types.nzb)
        dropFileType(classes, QStringLiteral(".nzb"), QLatin1String(kProgId));
    if (!types.torrent) {
        dropFileType(classes, QStringLiteral(".torrent"), QLatin1String(kTorrentProgId));
        dropScheme(classes, QStringLiteral("magnet"));
    }
    if (!types.ed2k) {
        dropScheme(classes, QStringLiteral("ed2k"));
        dropScheme(classes, QStringLiteral("ed3k"));
    }
    if (!types.dlc)
        dropFileType(classes, QStringLiteral(".dlc"), QLatin1String(kDlcProgId));
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

/// Takes us out as handler of @p mimeTypes in the user's mimeapps.list files: a
/// type switched off must not keep pointing at emuleqt.desktop (#121).
void dropMimeDefaults(const QString& dataHome, const QStringList& mimeTypes)
{
    if (mimeTypes.isEmpty())
        return;
    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QStringList lists{QDir(dataHome).filePath(QStringLiteral("applications/mimeapps.list"))};
    if (!configHome.isEmpty())
        lists.prepend(QDir(configHome).filePath(QStringLiteral("mimeapps.list")));
    for (const QString& path : lists) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString before = QString::fromUtf8(file.readAll());
        file.close();
        const QString after = withoutMimeDefaults(before, mimeTypes);
        if (after == before)
            continue;
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            logWarning(QStringLiteral("Cannot write %1").arg(path));
            continue;
        }
        file.write(after.toUtf8());
    }
}

void refreshDatabases(const QString& dataHome)
{
    refresh(QStringLiteral("update-mime-database"),
            {QDir(dataHome).filePath(QStringLiteral("mime"))});
    refresh(QStringLiteral("update-desktop-database"),
            {QDir(dataHome).filePath(QStringLiteral("applications"))});
}

} // namespace

bool isStale(FileTypes types)
{
    if (!types.any())
        return false;
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        return false;
    const DesktopFile entry = desktopFiles(exePath(), dataHome, types).constFirst();
    QFile file(entry.path);
    // gone: deleted by the user, not ours to put back
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.readAll() != entry.contents.toUtf8();
}

bool applyFileTypes(FileTypes types, QString& error, bool setDefault)
{
    const QString dataHome = dataHomeOrError(error);
    if (dataHome.isEmpty())
        return false;
    // nothing claimed: drop the entry, and leave a bare launcher where there was one
    if (!types.any()) {
        const bool hadEntry = QFileInfo::exists(desktopFiles(exePath(), dataHome, {}).constFirst().path);
        const bool ok = unregisterFileTypes(error);
        if (ok && hadEntry)
            installLauncher(true);
        return ok;
    }

    installDesktopIcons();

    // the mime definitions of every type, so those switched off can be removed
    const QList<DesktopFile> all = desktopFiles(exePath(), dataHome, FileTypes::all());
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

    QStringList dropped = xdgMimeTypes(FileTypes::all());
    for (const QString& type : xdgMimeTypes(types))
        dropped.removeAll(type);
    dropMimeDefaults(dataHome, dropped);

    refreshDatabases(dataHome);
    // the default handler too, or another client keeps magnet: and .torrent
    if (setDefault && !QStandardPaths::findExecutable(QStringLiteral("xdg-mime")).isEmpty())
        QProcess::execute(QStringLiteral("xdg-mime"),
                          QStringList{QStringLiteral("default"), QLatin1String(kDesktopFileName)} + xdgMimeTypes(types));
    return true;
}

bool unregisterFileTypes(QString& error)
{
    const QString dataHome = dataHomeOrError(error);
    if (dataHome.isEmpty())
        return false;

    for (const auto& entry : desktopFiles(exePath(), dataHome, FileTypes::all()))
        QFile::remove(entry.path);
    dropMimeDefaults(dataHome, xdgMimeTypes(FileTypes::all()));

    refreshDatabases(dataHome);
    return true;
}

#endif

} // namespace eMule::gui::FileAssociation
