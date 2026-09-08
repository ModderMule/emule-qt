/// @file tst_FileAssociation.cpp
/// @brief What registering .nzb would write, on every platform at once.
///
/// The two generators are pure, which is the point: the Windows and Linux
/// writers only compile on their own platform, so this is the only part of the
/// feature that can be checked from a developer machine. It covers the two
/// mistakes that are invisible until someone with a space in their home
/// directory tries it, and the one that would need an administrator.

#include "utils/FileAssociation.h"

#include <QTemporaryDir>
#include <QTest>

using namespace eMule::gui;

class tst_FileAssociation : public QObject {
    Q_OBJECT

private slots:
    void theDesktopFileClaimsNzbAndTheEd2kScheme();
    void theDesktopEntryQuotesAnExecPathWithSpaces();
    void theMimeDefinitionDeclaresTheNzbGlob();
    void theRegistryValuesStayUnderHkeyCurrentUser();
    void theOpenCommandPassesTheDroppedFile();
    void macOsNeedsNoRuntimeRegistration();
};

namespace {

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
    const auto files = FileAssociation::nzbDesktopFiles(QStringLiteral("/opt/emuleqt"),
                                                        QStringLiteral("/home/u/.local/share"));
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
    const auto files = FileAssociation::nzbDesktopFiles(
        QStringLiteral("/home/u/My Apps/eMule Qt/emuleqt"),
        QStringLiteral("/home/u/.local/share"));

    const QString desktop = contentsOf(files, QStringLiteral("emuleqt.desktop"));
    QVERIFY2(desktop.contains(QStringLiteral("Exec=\"/home/u/My Apps/eMule Qt/emuleqt\" %U")),
             qPrintable(desktop));
}

void tst_FileAssociation::theMimeDefinitionDeclaresTheNzbGlob()
{
    const auto files = FileAssociation::nzbDesktopFiles(QStringLiteral("/opt/emuleqt"),
                                                        QStringLiteral("/home/u/.local/share"));
    const QString mime = contentsOf(files, QStringLiteral("emuleqt-nzb.xml"));

    QVERIFY(!mime.isEmpty());
    QVERIFY(mime.contains(QStringLiteral("*.nzb")));
    QVERIFY(mime.contains(QLatin1String(FileAssociation::kNzbMimeType)));
    QVERIFY(mime.contains(QStringLiteral("mime/packages/")) == false);
}

void tst_FileAssociation::theRegistryValuesStayUnderHkeyCurrentUser()
{
    const auto values = FileAssociation::nzbRegistryValues(
        QStringLiteral("C:\\Apps\\eMuleQt\\emuleqt.exe"));
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
    const auto values = FileAssociation::nzbRegistryValues(
        QStringLiteral("C:\\Program Files\\eMule Qt\\emuleqt.exe"));

    QString command;
    for (const auto& value : values)
        if (value.key.endsWith(QStringLiteral("shell/open/command")))
            command = value.value;

    // Both quoted: the executable because its path has spaces, and %1 because
    // the file being opened may too. And %1 has to survive verbatim -- it is
    // Windows' placeholder, and it collides with QString::arg's.
    QCOMPARE(command, QStringLiteral("\"C:\\Program Files\\eMule Qt\\emuleqt.exe\" \"%1\""));
}

void tst_FileAssociation::macOsNeedsNoRuntimeRegistration()
{
#ifdef Q_OS_MACOS
    // Info.plist's CFBundleDocumentTypes is the whole mechanism there, so the
    // calls succeed by doing nothing rather than reporting a failure the user
    // cannot act on.
    QVERIFY(!FileAssociation::isRuntimeRegistration());
    QString error;
    QVERIFY(FileAssociation::registerNzbFileType(error));
    QVERIFY(error.isEmpty());
    QVERIFY(FileAssociation::unregisterNzbFileType(error));
#else
    QVERIFY(FileAssociation::isRuntimeRegistration());
#endif
}

QTEST_MAIN(tst_FileAssociation)
#include "tst_FileAssociation.moc"
