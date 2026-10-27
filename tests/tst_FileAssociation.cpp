/// @file tst_FileAssociation.cpp
/// @brief What registering .nzb, .torrent and magnet: would write, on every platform at once.
///
/// The two generators are pure, which is the point: the Windows and Linux
/// writers only compile on their own platform, so this is the only part of the
/// feature that can be checked from a developer machine. It covers the two
/// mistakes that are invisible until someone with a space in their home
/// directory tries it, and the one that would need an administrator.

#include "utils/FileAssociation.h"

#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace eMule::gui;

class tst_FileAssociation : public QObject {
    Q_OBJECT

private slots:
    void theDesktopFileClaimsNzbAndTheEd2kScheme();
    void theDesktopEntryQuotesAnExecPathWithSpaces();
    void theMimeDefinitionDeclaresTheNzbGlob();
    void theRegistryValuesStayUnderHkeyCurrentUser();
    void theOpenCommandPassesTheDroppedFile();
    void macOsIsNotReappliedAtStart();
    void theIconsLandWhereTheDesktopEntryLooks();
    void theLauncherEntryClaimsNothing();
    void everyDesktopEntryNamesTheWindowClass();
    void torrentClaimsTheFileTypeAndTheMagnetScheme();
    void typesSwitchedOffAreLeftOut();
    void theMagnetSchemeIsAUrlProtocol();
};

namespace {

const FileAssociation::FileTypes kNzb{.nzb = true};
const FileAssociation::FileTypes kTorrent{.torrent = true};
const FileAssociation::FileTypes kAll{.nzb = true, .torrent = true};

QString contentsOf(const QList<FileAssociation::DesktopFile>& files, const QString& endsWith)
{
    for (const auto& file : files)
        if (file.path.endsWith(endsWith))
            return file.contents;
    return {};
}

} // namespace

void tst_FileAssociation::theDesktopFileClaimsNzbAndTheEd2kScheme()
{
    const auto files = FileAssociation::desktopFiles(QStringLiteral("/opt/emuleqt"),
                                                     QStringLiteral("/home/u/.local/share"), kNzb);
    QCOMPARE(files.size(), 2);

    const QString desktop = contentsOf(files, QStringLiteral("emuleqt.desktop"));
    QVERIFY(!desktop.isEmpty());
    QVERIFY(desktop.contains(QLatin1String(FileAssociation::kNzbMimeType)));

    // The ed2k scheme comes along for free: the macOS bundle has claimed it
    // since day one and Linux never has.
    QVERIFY(desktop.contains(QStringLiteral("x-scheme-handler/ed2k")));

    // %U rather than %f, because a file manager may hand over a file:// URL --
    // which is why CommandLineExec has to resolve one.
    QVERIFY(desktop.contains(QStringLiteral("%U")));

    // Written where XDG looks, not next to the binary.
    QVERIFY(files.first().path.startsWith(QStringLiteral("/home/u/.local/share/")));
}

void tst_FileAssociation::theDesktopEntryQuotesAnExecPathWithSpaces()
{
    // An unquoted Exec= line with a space in it produces a launcher that
    // silently starts nothing -- and "Application Support" and "Program Files"
    // make that the normal case rather than the exotic one.
    const auto files = FileAssociation::desktopFiles(
        QStringLiteral("/home/u/My Apps/eMule Qt/emuleqt"),
        QStringLiteral("/home/u/.local/share"), kNzb);

    const QString desktop = contentsOf(files, QStringLiteral("emuleqt.desktop"));
    QVERIFY2(desktop.contains(QStringLiteral("Exec=\"/home/u/My Apps/eMule Qt/emuleqt\" %U")),
             qPrintable(desktop));
}

void tst_FileAssociation::theMimeDefinitionDeclaresTheNzbGlob()
{
    const auto files = FileAssociation::desktopFiles(QStringLiteral("/opt/emuleqt"),
                                                     QStringLiteral("/home/u/.local/share"), kNzb);
    const QString mime = contentsOf(files, QStringLiteral("emuleqt-nzb.xml"));

    QVERIFY(!mime.isEmpty());
    QVERIFY(mime.contains(QStringLiteral("*.nzb")));
    QVERIFY(mime.contains(QLatin1String(FileAssociation::kNzbMimeType)));
    QVERIFY(mime.contains(QStringLiteral("mime/packages/")) == false);
}

void tst_FileAssociation::theRegistryValuesStayUnderHkeyCurrentUser()
{
    const auto values = FileAssociation::registryValues(
        QStringLiteral("C:\\Apps\\eMuleQt\\emuleqt.exe"), kAll);
    QVERIFY(!values.isEmpty());

    for (const auto& value : values) {
        // Everything is relative to HKEY_CURRENT_USER\Software\Classes. An
        // absolute key, or one naming HKEY_LOCAL_MACHINE, would need elevation
        // -- and a portable zip has no moment at which to ask for it.
        QVERIFY2(!value.key.startsWith(QStringLiteral("HKEY")), qPrintable(value.key));
        QVERIFY2(!value.key.contains(QStringLiteral("LOCAL_MACHINE")), qPrintable(value.key));
        QVERIFY(!value.key.isEmpty());
    }

    // The extension points at the ProgID, not straight at the executable.
    bool sawExtension = false;
    for (const auto& value : values) {
        if (value.key == QStringLiteral(".nzb")) {
            sawExtension = true;
            QVERIFY(!value.value.endsWith(QStringLiteral(".exe")));
        }
    }
    QVERIFY(sawExtension);
}

void tst_FileAssociation::theOpenCommandPassesTheDroppedFile()
{
    const auto values = FileAssociation::registryValues(
        QStringLiteral("C:\\Program Files\\eMule Qt\\emuleqt.exe"), kNzb);

    QString command;
    for (const auto& value : values)
        if (value.key.endsWith(QStringLiteral("shell/open/command")))
            command = value.value;

    // Both quoted: the executable because its path has spaces, and %1 because
    // the file being opened may too. And %1 has to survive verbatim -- it is
    // Windows' placeholder, and it collides with QString::arg's.
    QCOMPARE(command, QStringLiteral("\"C:\\Program Files\\eMule Qt\\emuleqt.exe\" \"%1\""));
}

void tst_FileAssociation::macOsIsNotReappliedAtStart()
{
#ifdef Q_OS_MACOS
    // The system keeps the default app; the Options check boxes read it back.
    // A test binary has no bundle id, so it claims nothing and changes nothing
    // -- which is also why this never touches the developer's own defaults.
    QVERIFY(!FileAssociation::isRuntimeRegistration());
    const auto claimed = FileAssociation::claimedTypes();
    QVERIFY(!claimed.any());
    QString error;
    QVERIFY(!FileAssociation::applyFileTypes(kAll, error));
    QVERIFY(!error.isEmpty());
#else
    QVERIFY(FileAssociation::isRuntimeRegistration());
#endif
}

void tst_FileAssociation::theIconsLandWhereTheDesktopEntryLooks()
{
    const auto icons = FileAssociation::hicolorIconFiles(QStringLiteral("/home/u/.local/share"));
    QVERIFY(!icons.isEmpty());

    // `Icon=emuleqt` is a theme lookup: hicolor/<N>x<N>/apps/emuleqt.png, and
    // the directory has to name the size the file really is.
    bool saw48 = false;
    for (const auto& icon : icons) {
        QVERIFY2(icon.path.startsWith(QStringLiteral("/home/u/.local/share/icons/hicolor/")),
                 qPrintable(icon.path));
        QVERIFY2(icon.path.endsWith(QStringLiteral("/apps/emuleqt.png")), qPrintable(icon.path));
        QVERIFY(icon.resource.startsWith(QStringLiteral(":/icons/app/emuleqt-")));
        if (icon.path.contains(QStringLiteral("/48x48/"))) {
            saw48 = true;
            QVERIFY(icon.resource.endsWith(QStringLiteral("-48.png")));
        }
    }
    // The size the spec requires every application to provide.
    QVERIFY(saw48);
}

void tst_FileAssociation::theLauncherEntryClaimsNothing()
{
    const auto launcher = FileAssociation::launcherDesktopFile(
        QStringLiteral("/home/u/My Apps/emuleqt"), QStringLiteral("/home/u/.local/share"));

    QCOMPARE(launcher.path,
             QStringLiteral("/home/u/.local/share/applications/emuleqt.desktop"));
    QVERIFY(launcher.contents.contains(QStringLiteral("Exec=\"/home/u/My Apps/emuleqt\"\n")));
    QVERIFY(launcher.contents.contains(QStringLiteral("Icon=emuleqt\n")));

    // Written at every first start without asking, so it must not take a file
    // type or a URL scheme away from whatever has it.
    QVERIFY(!launcher.contents.contains(QStringLiteral("MimeType")));
}

void tst_FileAssociation::everyDesktopEntryNamesTheWindowClass()
{
    // The desktop ties a window to its launcher, and so to its icon, by this.
    const QString wmClass =
        QStringLiteral("StartupWMClass=%1\n").arg(QLatin1String(FileAssociation::kDesktopId));

    const QString dataHome = QStringLiteral("/home/u/.local/share");
    const auto files = FileAssociation::desktopFiles(QStringLiteral("/opt/emuleqt"), dataHome, kAll);
    QVERIFY(contentsOf(files, QStringLiteral("emuleqt.desktop")).contains(wmClass));
    QVERIFY(FileAssociation::launcherDesktopFile(QStringLiteral("/opt/emuleqt"), dataHome)
                .contents.contains(wmClass));
}

void tst_FileAssociation::torrentClaimsTheFileTypeAndTheMagnetScheme()
{
    const auto files = FileAssociation::desktopFiles(QStringLiteral("/opt/emuleqt"),
                                                     QStringLiteral("/home/u/.local/share"), kTorrent);
    const QString desktop = contentsOf(files, QStringLiteral("emuleqt.desktop"));
    QVERIFY(desktop.contains(QLatin1String(FileAssociation::kTorrentMimeType)));
    QVERIFY(desktop.contains(QStringLiteral("x-scheme-handler/magnet")));
    QVERIFY(desktop.contains(QStringLiteral("x-scheme-handler/ed2k")));

    const QString mime = contentsOf(files, QStringLiteral("emuleqt-torrent.xml"));
    QVERIFY(mime.contains(QStringLiteral("*.torrent")));
    QVERIFY(FileAssociation::xdgMimeTypes(kTorrent).contains(QStringLiteral("x-scheme-handler/magnet")));
    // ed3k:file?…bt= links go to the Torrents tab, so the torrent switch claims ed3k too
    QVERIFY(desktop.contains(QStringLiteral("x-scheme-handler/ed3k")));
    QVERIFY(!FileAssociation::xdgMimeTypes(kNzb).contains(QStringLiteral("x-scheme-handler/ed3k")));
}

void tst_FileAssociation::typesSwitchedOffAreLeftOut()
{
    // one shared entry: claiming .nzb alone must not claim torrents, and back
    const auto nzb = FileAssociation::desktopFiles(QStringLiteral("/opt/emuleqt"),
                                                   QStringLiteral("/home/u/.local/share"), kNzb);
    QCOMPARE(nzb.size(), 2);
    QVERIFY(!contentsOf(nzb, QStringLiteral("emuleqt.desktop")).contains(QStringLiteral("magnet")));
    QVERIFY(contentsOf(nzb, QStringLiteral("emuleqt-torrent.xml")).isEmpty());

    const auto all = FileAssociation::desktopFiles(QStringLiteral("/opt/emuleqt"),
                                                   QStringLiteral("/home/u/.local/share"), kAll);
    QCOMPARE(all.size(), 3);
    const QString desktop = contentsOf(all, QStringLiteral("emuleqt.desktop"));
    QVERIFY(desktop.contains(QLatin1String(FileAssociation::kNzbMimeType)));
    QVERIFY(desktop.contains(QLatin1String(FileAssociation::kTorrentMimeType)));

    // ed2k alone (the "Handle eD2K links" button) keeps the entry, claiming nothing else
    const auto ed2k = FileAssociation::desktopFiles(QStringLiteral("/opt/emuleqt"),
                                                    QStringLiteral("/home/u/.local/share"), {.ed2k = true});
    QCOMPARE(ed2k.size(), 1);
    QVERIFY(contentsOf(ed2k, QStringLiteral("emuleqt.desktop")).contains(QStringLiteral("MimeType=x-scheme-handler/ed2k;\n")));

    QVERIFY(FileAssociation::registryValues(QStringLiteral("C:\\e.exe"), {}).isEmpty());
    for (const auto& value : FileAssociation::registryValues(QStringLiteral("C:\\e.exe"), kNzb))
        QVERIFY2(!value.key.contains(QStringLiteral("torrent")) && !value.key.startsWith(QStringLiteral("magnet"))
                     && !value.key.startsWith(QStringLiteral("ed3k")),
                 qPrintable(value.key));
}

void tst_FileAssociation::theMagnetSchemeIsAUrlProtocol()
{
    const auto values = FileAssociation::registryValues(
        QStringLiteral("C:\\Program Files\\eMule Qt\\emuleqt.exe"), kTorrent);
    bool urlProtocol = false;
    QString command;
    QString torrentProgId;
    for (const auto& value : values) {
        if (value.key == QStringLiteral("magnet") && value.name == QStringLiteral("URL Protocol"))
            urlProtocol = value.value.isEmpty();
        if (value.key == QStringLiteral("magnet/shell/open/command"))
            command = value.value;
        if (value.key == QStringLiteral(".torrent"))
            torrentProgId = value.value;
    }
    QVERIFY(urlProtocol);
    QCOMPARE(command, QStringLiteral("\"C:\\Program Files\\eMule Qt\\emuleqt.exe\" \"%1\""));
    QVERIFY(std::ranges::any_of(values, [](const FileAssociation::RegistryValue& v) {
        return v.key == QStringLiteral("ed3k") && v.name == QStringLiteral("URL Protocol");
    }));
    QCOMPARE(torrentProgId, QStringLiteral("eMuleQt.torrent"));
}

QTEST_MAIN(tst_FileAssociation)
#include "tst_FileAssociation.moc"
