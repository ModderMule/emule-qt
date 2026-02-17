/// @file tst_PreviewThread.cpp
/// @brief Tests for media/PreviewThread — background copy + app launch.

#include "TestHelpers.h"
#include "media/PreviewThread.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace Qt::StringLiterals;

class tst_PreviewThread : public QObject {
    Q_OBJECT

private slots:
    void copyAndLaunch_success();
    void errorOnMissingSource();
    void errorOnBadCommand();
    void tempFileCleanup();
};

void tst_PreviewThread::copyAndLaunch_success()
{
    eMule::testing::TempDir tmpDir;

    // Create a source file with known content
    const QString srcPath = tmpDir.filePath(u"source.part"_s);
    {
        QFile f(srcPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray data(4096, 'A');
        f.write(data);
        f.close();
    }

    const QString dstPath = tmpDir.filePath(u"preview.avi"_s);

    PreviewRequest req;
    req.sourceFilePath = srcPath;
    req.destFilePath = dstPath;
    req.bytesToCopy = 2048;
    req.app.title = u"echo"_s;
    req.app.command = u"/bin/echo"_s;
    req.app.commandArgs = u"preview"_s;

    PreviewThread thread(std::move(req));
    QSignalSpy startedSpy(&thread, &PreviewThread::previewStarted);
    QSignalSpy finishedSpy(&thread, &PreviewThread::previewFinished);
    QSignalSpy errorSpy(&thread, &PreviewThread::previewError);

    thread.start();
    QVERIFY(thread.wait(10'000));

    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);

    // Check the app title was passed
    QCOMPARE(startedSpy[0][0].toString(), u"echo"_s);

    // The exit code for /bin/echo should be 0
    QCOMPARE(finishedSpy[0][1].toInt(), 0);

    // Temp file should be cleaned up
    QVERIFY(!QFile::exists(dstPath));
}

void tst_PreviewThread::errorOnMissingSource()
{
    eMule::testing::TempDir tmpDir;

    PreviewRequest req;
    req.sourceFilePath = u"/nonexistent/file.part"_s;
    req.destFilePath = tmpDir.filePath(u"preview.avi"_s);
    req.bytesToCopy = 1024;
    req.app.title = u"test"_s;
    req.app.command = u"/bin/echo"_s;

    PreviewThread thread(std::move(req));
    QSignalSpy errorSpy(&thread, &PreviewThread::previewError);

    thread.start();
    QVERIFY(thread.wait(10'000));

    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy[0][0].toString().contains(u"source"_s, Qt::CaseInsensitive));
}

void tst_PreviewThread::errorOnBadCommand()
{
    eMule::testing::TempDir tmpDir;

    // Create a valid source
    const QString srcPath = tmpDir.filePath(u"source.part"_s);
    {
        QFile f(srcPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(1024, 'X'));
        f.close();
    }

    const QString dstPath = tmpDir.filePath(u"preview.avi"_s);

    PreviewRequest req;
    req.sourceFilePath = srcPath;
    req.destFilePath = dstPath;
    req.bytesToCopy = 512;
    req.app.title = u"bad"_s;
    req.app.command = u"/nonexistent/bad_command"_s;

    PreviewThread thread(std::move(req));
    QSignalSpy errorSpy(&thread, &PreviewThread::previewError);

    thread.start();
    QVERIFY(thread.wait(15'000));

    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy[0][0].toString().contains(u"start"_s, Qt::CaseInsensitive));
}

void tst_PreviewThread::tempFileCleanup()
{
    eMule::testing::TempDir tmpDir;

    const QString srcPath = tmpDir.filePath(u"source.part"_s);
    {
        QFile f(srcPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(2048, 'B'));
        f.close();
    }

    const QString dstPath = tmpDir.filePath(u"cleanup_test.avi"_s);

    PreviewRequest req;
    req.sourceFilePath = srcPath;
    req.destFilePath = dstPath;
    req.bytesToCopy = 1024;
    req.app.title = u"true"_s;
    req.app.command = u"/usr/bin/true"_s;

    PreviewThread thread(std::move(req));
    thread.start();
    QVERIFY(thread.wait(10'000));

    // Temp file should have been removed after preview finished
    QVERIFY(!QFile::exists(dstPath));
}

QTEST_MAIN(tst_PreviewThread)
#include "tst_PreviewThread.moc"
