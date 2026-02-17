/// @file tst_PartFileWriteThread.cpp
/// @brief Tests for files/PartFileWriteThread — async buffered I/O.

#include "TestHelpers.h"
#include "files/PartFileWriteThread.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

#include <cstring>

using namespace eMule;

class tst_PartFileWriteThread : public QObject {
    Q_OBJECT

private slots:
    void enqueueAndWrite_basic();
    void multipleWrites_sequential();
    void stopThread_graceful();
    void writeError_signal();
};

void tst_PartFileWriteThread::enqueueAndWrite_basic()
{
    eMule::testing::TempDir tmpDir;
    const QString filePath = tmpDir.filePath(QStringLiteral("test.part"));

    // Create the file first
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.resize(1024);
        f.close();
    }

    PartFileWriteThread thread;
    QSignalSpy completedSpy(&thread, &PartFileWriteThread::writeCompleted);

    thread.start();

    WriteJob job;
    job.partFile = nullptr;
    job.filePath = filePath;
    job.offset = 100;
    job.data = {0x41, 0x42, 0x43, 0x44}; // "ABCD"

    thread.enqueueWrite(std::move(job));

    QVERIFY(completedSpy.wait(5000));
    QCOMPARE(completedSpy.count(), 1);

    // Verify the data was written
    QFile result(filePath);
    QVERIFY(result.open(QIODevice::ReadOnly));
    result.seek(100);
    QByteArray readBack = result.read(4);
    QCOMPARE(readBack, QByteArray("ABCD"));

    thread.requestStop();
    thread.wait();
}

void tst_PartFileWriteThread::multipleWrites_sequential()
{
    eMule::testing::TempDir tmpDir;
    const QString filePath = tmpDir.filePath(QStringLiteral("multi.part"));

    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.resize(1024);
        f.close();
    }

    PartFileWriteThread thread;
    QSignalSpy completedSpy(&thread, &PartFileWriteThread::writeCompleted);

    thread.start();

    // Write at offset 0
    WriteJob job1;
    job1.filePath = filePath;
    job1.offset = 0;
    job1.data = {0x01, 0x02, 0x03};
    thread.enqueueWrite(std::move(job1));

    // Write at offset 100
    WriteJob job2;
    job2.filePath = filePath;
    job2.offset = 100;
    job2.data = {0x04, 0x05, 0x06};
    thread.enqueueWrite(std::move(job2));

    // Wait for both
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 2, 5000);

    // Verify
    QFile result(filePath);
    QVERIFY(result.open(QIODevice::ReadOnly));

    result.seek(0);
    QByteArray data1 = result.read(3);
    QCOMPARE(data1.at(0), char(0x01));
    QCOMPARE(data1.at(1), char(0x02));
    QCOMPARE(data1.at(2), char(0x03));

    result.seek(100);
    QByteArray data2 = result.read(3);
    QCOMPARE(data2.at(0), char(0x04));
    QCOMPARE(data2.at(1), char(0x05));
    QCOMPARE(data2.at(2), char(0x06));

    thread.requestStop();
    thread.wait();
}

void tst_PartFileWriteThread::stopThread_graceful()
{
    PartFileWriteThread thread;
    thread.start();

    // Immediately stop — should not hang or crash
    thread.requestStop();
    QVERIFY(thread.wait(5000));
}

void tst_PartFileWriteThread::writeError_signal()
{
    PartFileWriteThread thread;
    QSignalSpy errorSpy(&thread, &PartFileWriteThread::writeError);

    thread.start();

    WriteJob job;
    job.filePath = QStringLiteral("/nonexistent/path/file.part");
    job.offset = 0;
    job.data = {0x01};
    thread.enqueueWrite(std::move(job));

    QVERIFY(errorSpy.wait(5000));
    QCOMPARE(errorSpy.count(), 1);

    thread.requestStop();
    thread.wait();
}

QTEST_MAIN(tst_PartFileWriteThread)
#include "tst_PartFileWriteThread.moc"
