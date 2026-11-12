#include <QStorageInfo>
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
        const QString canonical = eMule::canonicalPath(QDir::tempPath());   // no /tmp on Windows
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
        QVERIFY(eMule::pathsEqual(QDir::tempPath(), QDir::tempPath()));
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
        const auto space = eMule::freeDiskSpace(QDir::tempPath());
        // the temp dir should have some free space
        QVERIFY(space > 0);
    }

    /// freeDiskSpace() answers 0 for both "full" and "could not tell", and a
    /// guard that pauses downloads has to know which it is looking at.
    void anUnreadableVolumeIsTellableFromAFullOne()
    {
        QVERIFY(eMule::tryFreeDiskSpace(QDir::tempPath()).has_value());
        QVERIFY(!eMule::tryFreeDiskSpace(QString()).has_value());
        QCOMPARE(eMule::freeDiskSpace(QString()), std::uint64_t(0));
    }

    /// A scratch or incoming directory is created on first use, so the question
    /// is asked about a path that does not exist yet. QStorageInfo calls that
    /// invalid; answering "cannot measure" would park a fresh install forever.
    void aDirectoryThatDoesNotExistYetAnswersForItsVolume()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString unborn =
            QDir(tmp.path()).filePath(QStringLiteral("temp/Usenet/not/made/yet"));

        const auto space = eMule::tryFreeDiskSpace(unborn);
        QVERIFY2(space.has_value(), "a path that does not exist yet read as unmeasurable");
        // Same volume, not the same byte count: other processes write between the two reads
        const auto ref = eMule::tryFreeDiskSpace(tmp.path());
        QVERIFY(ref.has_value());
        const std::uint64_t drift = *space > *ref ? *space - *ref : *ref - *space;
        QVERIFY2(drift < (std::uint64_t{1} << 30), "answered for a different volume");
    }

    /// Windows builds put the binaries in the build root, beside build/hoster-resolver/: the
    /// build-tree file is looked for in the app dir itself too, not only above it.
    void bundledFileInTheAppDirsBuildTree()
    {
        const QDir app(QCoreApplication::applicationDirPath());
        QTemporaryDir sub(app.filePath(QStringLiteral("bundled-XXXXXX")));
        QVERIFY(sub.isValid());
        const QString file = QDir(sub.path()).filePath(QStringLiteral("helper.bin"));
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        const QString rel = QDir(sub.path()).dirName() + QStringLiteral("/helper.bin");
        QCOMPARE(eMule::findBundledFile("EMULE_TEST_NO_SUCH_VAR", {}, rel), QDir::cleanPath(file));
        QVERIFY(eMule::findBundledFile("EMULE_TEST_NO_SUCH_VAR", {}, QStringLiteral("no/such/helper.bin")).isEmpty());
    }

    /// Report 39: the root is QStorageInfo's, asked once per path; a missing dir answers empty.
    void cachedVolumeRootMatchesStorageInfo()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = eMule::cachedVolumeRoot(tmp.path());
        QCOMPARE(root, QStorageInfo(tmp.path()).rootPath());
        QCOMPARE(eMule::cachedVolumeRoot(tmp.path() + u'/'), root);   // same key after cleanPath
        QVERIFY(eMule::cachedVolumeRoot(tmp.path() + QStringLiteral("/no/such/dir")).isEmpty());
    }

    /// Usenet preview mp4 tail: a file with data already in it, marked sparse, then written
    /// 1 GiB past EOF, must leave the gap a hole (no 1 GiB of zeros on disk).
    void aFileWithDataMarkedSparseLeavesAHoleBeforeAFarWrite()
    {
#ifndef Q_OS_WIN
        QSKIP("only Windows needs marking a file sparse");
#else
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("video.mp4"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(QByteArray(64 * 1024, 'h')), 64 * 1024);
        }
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        if (!eMule::markFileSparse(f))
            QSKIP("temp volume cannot hold sparse files (FAT32/exFAT)");
        f.close();

        QFile w(path);
        QVERIFY(w.open(QIODevice::ReadWrite));
        constexpr qint64 kGap = qint64(1) << 30;
        QVERIFY(w.seek(kGap));
        QCOMPARE(w.write(QByteArray(64 * 1024, 't')), 64 * 1024);
        w.close();

        QCOMPARE(QFileInfo(path).size(), kGap + 64 * 1024);
        QVERIFY2(eMule::diskFileSize(path) < 16 * 1024 * 1024,
                 qPrintable(QString::number(eMule::diskFileSize(path))));
#endif
    }
};

QTEST_MAIN(PathUtilsTest)
#include "tst_PathUtils.moc"
