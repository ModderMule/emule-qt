/// @file tst_ShareableFile.cpp
/// @brief Tests for files/ShareableFile — path, shared directory, file type, info summary.

#include "TestHelpers.h"
#include "files/ShareableFile.h"

#include <QTest>

using namespace eMule;

class tst_ShareableFile : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void setPath();
    void setFilePath();
    void sharedDirectory_default();
    void sharedDirectory_shellLink();
    void verifiedFileType();
    void infoSummary();
};

void tst_ShareableFile::construct_default()
{
    ShareableFile f;
    QVERIFY(f.path().isEmpty());
    QVERIFY(f.filePath().isEmpty());
    QVERIFY(f.sharedDirectory().isEmpty());
    QVERIFY(!f.isShellLinked());
    QCOMPARE(f.verifiedFileType(), FileType::Unknown);
}

void tst_ShareableFile::setPath()
{
    ShareableFile f;
    f.setPath(QStringLiteral("/some/dir"));
    QCOMPARE(f.path(), QStringLiteral("/some/dir"));
}

void tst_ShareableFile::setFilePath()
{
    ShareableFile f;
    f.setFilePath(QStringLiteral("/some/dir/file.txt"));
    QCOMPARE(f.filePath(), QStringLiteral("/some/dir/file.txt"));
}

void tst_ShareableFile::sharedDirectory_default()
{
    ShareableFile f;
    f.setPath(QStringLiteral("/share/music"));
    // When no shared dir is set, sharedDirectory() returns path()
    QCOMPARE(f.sharedDirectory(), QStringLiteral("/share/music"));
    QVERIFY(!f.isShellLinked());
}

void tst_ShareableFile::sharedDirectory_shellLink()
{
    ShareableFile f;
    f.setPath(QStringLiteral("/original/path"));
    f.setSharedDirectory(QStringLiteral("/linked/path"));
    QCOMPARE(f.sharedDirectory(), QStringLiteral("/linked/path"));
    QVERIFY(f.isShellLinked());
}

void tst_ShareableFile::verifiedFileType()
{
    ShareableFile f;
    QCOMPARE(f.verifiedFileType(), FileType::Unknown);
    f.setVerifiedFileType(FileType::AudioMpeg);
    QCOMPARE(f.verifiedFileType(), FileType::AudioMpeg);
    f.setVerifiedFileType(FileType::VideoMkv);
    QCOMPARE(f.verifiedFileType(), FileType::VideoMkv);
}

void tst_ShareableFile::infoSummary()
{
    ShareableFile f;
    f.setFileName(QStringLiteral("test.mp3"));
    f.setFileSize(1024 * 1024);
    f.setPath(QStringLiteral("/music"));

    QString summary = f.infoSummary();
    QVERIFY(summary.contains(QStringLiteral("test.mp3")));
    QVERIFY(summary.contains(QStringLiteral("1048576")));
    QVERIFY(summary.contains(QStringLiteral("/music")));
}

QTEST_MAIN(tst_ShareableFile)
#include "tst_ShareableFile.moc"
