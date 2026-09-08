/// @file tst_MenuIcons.cpp
/// @brief Every icon the GUI names must actually be in the resource file.
///
/// A missing `:/icons/...` is invisible: QIcon on an absent resource is a valid,
/// null icon, so the menu entry simply renders without a picture and nothing is
/// logged. `TransferPanel` asked for a `Resume.ico` that has never existed —
/// the file is called `Start.ico`, which the same file uses correctly a hundred
/// lines earlier — and the Assign-To-Category menu quietly lost its Resume icon.
///
/// This scans the sources rather than the running widgets on purpose. A test
/// that built each panel's context menu would need the whole GUI linked, would
/// only cover the menus it happened to build, and would still miss an icon named
/// in a code path it did not take.
///
/// Two reference forms exist and both are checked:
///   - `":/icons/Foo.ico"` written out in full, and
///   - a bare `"Foo.ico"` handed to `menuIcon()` / the panels' `ico` lambdas,
///     which prepend `":/icons/"`.
/// A name built at runtime (`":/icons/Connected%1%2.ico"`) matches neither and
/// is deliberately out of scope: there is no literal to check.

#include "TestHelpers.h"

#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTest>

namespace {

QString repoRoot()
{
    return QStringLiteral(EMULE_STRINGIFY(EMULE_PROJECT_DATA_DIR) "/..");
}

/// Every resource path the .qrc actually defines, as `/prefix/alias`.
///
/// Parsed by hand rather than with QXmlStreamReader because the aliases have to
/// be read from the *source* .qrc: asking the resource system would only prove
/// that the test binary's own resources are consistent, and it has none.
QSet<QString> qrcResourcePaths(QString& error)
{
    QSet<QString> paths;
    QFile qrc(repoRoot() + QStringLiteral("/resources/emuleqt.qrc"));
    if (!qrc.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QStringLiteral("cannot read %1: %2").arg(qrc.fileName(), qrc.errorString());
        return paths;
    }

    static const QRegularExpression prefixRe(
        QStringLiteral("<qresource\\s+prefix=\"([^\"]+)\""));
    static const QRegularExpression aliasRe(
        QStringLiteral("<file\\s+alias=\"([^\"]+)\""));

    QString prefix;
    while (!qrc.atEnd()) {
        const QString line = QString::fromUtf8(qrc.readLine());
        if (const auto m = prefixRe.match(line); m.hasMatch())
            prefix = m.captured(1);
        if (const auto m = aliasRe.match(line); m.hasMatch())
            paths.insert(prefix + QLatin1Char('/') + m.captured(1));
    }
    return paths;
}

struct IconRef {
    QString resourcePath;   ///< what the code will ask QIcon for
    QString where;          ///< file:line, so a failure names the call site
};

QList<IconRef> iconRefsInGuiSources()
{
    // A name plus an extension: anything with a % in it is built at runtime and
    // cannot be checked from a literal.
    static const QRegularExpression fullRe(
        QStringLiteral(":/([A-Za-z0-9_]+)/([A-Za-z0-9_]+\\.ico)"));
    static const QRegularExpression bareRe(
        QStringLiteral("\"([A-Za-z0-9_]+\\.ico)\""));

    QList<IconRef> refs;
    const QString guiDir = repoRoot() + QStringLiteral("/src/gui");
    QDirIterator it(guiDir, {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        const QString shortName = QStringLiteral("src/gui/")
                                  + QDir(guiDir).relativeFilePath(path);
        int lineNo = 0;
        while (!f.atEnd()) {
            ++lineNo;
            const QString line = QString::fromUtf8(f.readLine());
            const QString where = QStringLiteral("%1:%2").arg(shortName).arg(lineNo);

            for (auto m = fullRe.globalMatch(line); m.hasNext();) {
                const auto hit = m.next();
                refs.append({QLatin1Char('/') + hit.captured(1) + QLatin1Char('/')
                                 + hit.captured(2),
                             where});
            }
            // menuIcon("Foo.ico") and the panels' ico("Foo.ico", ...) lambdas.
            for (auto m = bareRe.globalMatch(line); m.hasNext();) {
                const auto hit = m.next();
                refs.append({QStringLiteral("/icons/") + hit.captured(1), where});
            }
        }
    }
    return refs;
}

} // namespace

class tst_MenuIcons : public QObject {
    Q_OBJECT

private slots:
    void everyIconTheGuiAsksForExistsInTheQrc();
};

void tst_MenuIcons::everyIconTheGuiAsksForExistsInTheQrc()
{
    QString error;
    const QSet<QString> defined = qrcResourcePaths(error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(!defined.isEmpty(), "parsed no aliases out of emuleqt.qrc");

    const QList<IconRef> refs = iconRefsInGuiSources();
    QVERIFY2(!refs.isEmpty(), "found no icon references under src/gui");

    QStringList dangling;
    for (const IconRef& ref : refs) {
        if (!defined.contains(ref.resourcePath))
            dangling.append(QStringLiteral("%1 (%2)").arg(ref.resourcePath, ref.where));
    }
    dangling.removeDuplicates();
    dangling.sort();

    QVERIFY2(dangling.isEmpty(),
             qPrintable(QStringLiteral("icon(s) named in the GUI but absent from "
                                       "resources/emuleqt.qrc:\n  %1")
                            .arg(dangling.join(QStringLiteral("\n  ")))));
}

QTEST_MAIN(tst_MenuIcons)
#include "tst_MenuIcons.moc"
