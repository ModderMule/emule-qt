#include <QTest>

#include "TestHelpers.h"
#include "utils/PathUtils.h"

/// @brief Tests for PathUtils.h path utility functions.
class PathUtilsTest : public QObject {
    Q_OBJECT

private slots:
    void testExecutablePath()
    {
        const QString path = eMule::executablePath();
        QVERIFY(!path.isEmpty());
        QVERIFY(QFile::exists(path));
    }

    void testExecutableDir()
    {
        const QString dir = eMule::executableDir();
        QVERIFY(!dir.isEmpty());
        QVERIFY(QDir(dir).exists());
    }

    void testEnsureTrailingSeparatorEmpty()
    {
        QCOMPARE(eMule::ensureTrailingSeparator(QString()), QString());
    }

    void testEnsureTrailingSeparatorAdds()
    {
        QCOMPARE(eMule::ensureTrailingSeparator(QStringLiteral("/tmp/foo")),
                 QStringLiteral("/tmp/foo/"));
    }

    void testEnsureTrailingSeparatorIdempotent()
    {
        QCOMPARE(eMule::ensureTrailingSeparator(QStringLiteral("/tmp/foo/")),
                 QStringLiteral("/tmp/foo/"));
    }

    void testRemoveTrailingSeparatorEmpty()
    {
        QCOMPARE(eMule::removeTrailingSeparator(QString()), QString());
    }

    void testRemoveTrailingSeparatorRemoves()
    {
        QCOMPARE(eMule::removeTrailingSeparator(QStringLiteral("/tmp/foo/")),
                 QStringLiteral("/tmp/foo"));
    }

    void testRemoveTrailingSeparatorPreservesRoot()
    {
        // Root "/" should not be stripped to empty
        QCOMPARE(eMule::removeTrailingSeparator(QStringLiteral("/")),
                 QStringLiteral("/"));
    }

    void testCanonicalPath()
    {
        // The temp dir definitely exists
        const QString canonical = eMule::canonicalPath(QStringLiteral("/tmp"));
        QVERIFY(!canonical.isEmpty());
    }

    void testCanonicalPathNonExistent()
    {
        const QString result = eMule::canonicalPath(
            QStringLiteral("/surely/this/does/not/exist/qwerty12345"));
        QVERIFY(result.isEmpty());
    }

    void testPathsEqualSamePath()
    {
        // A path should equal itself
        QVERIFY(eMule::pathsEqual(QStringLiteral("/tmp"), QStringLiteral("/tmp")));
    }

    void testPathsEqualNonExistent()
    {
        // Non-existent paths should return false
        QVERIFY(!eMule::pathsEqual(
            QStringLiteral("/nonexistent_a_12345"),
            QStringLiteral("/nonexistent_b_12345")));
    }

    void testSanitizeFilenameClean()
    {
        QCOMPARE(eMule::sanitizeFilename(QStringLiteral("readme.txt")),
                 QStringLiteral("readme.txt"));
    }

    void testSanitizeFilenameInvalidChars()
    {
        const QString result = eMule::sanitizeFilename(QStringLiteral("file:name?.txt"));
        QVERIFY(!result.contains(QChar(u':')));
        QVERIFY(!result.contains(QChar(u'?')));
    }

    void testSanitizeFilenameLeadingDots()
    {
        const QString result = eMule::sanitizeFilename(QStringLiteral("...hidden"));
        QVERIFY(!result.startsWith(QChar(u'.')));
    }

    void testAppDirectoryConfig()
    {
        const QString dir = eMule::appDirectory(eMule::AppDir::Config);
        QVERIFY(!dir.isEmpty());
        QVERIFY(QDir(dir).exists());
    }

    void testAppDirectoryTemp()
    {
        const QString dir = eMule::appDirectory(eMule::AppDir::Temp);
        QVERIFY(!dir.isEmpty());
        QVERIFY(QDir(dir).exists());
    }

    void testFreeDiskSpace()
    {
        const auto space = eMule::freeDiskSpace(QStringLiteral("/tmp"));
        // /tmp should have some free space
        QVERIFY(space > 0);
    }
};

QTEST_MAIN(PathUtilsTest)
#include "tst_PathUtils.moc"
