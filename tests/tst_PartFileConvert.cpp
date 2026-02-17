/// @file tst_PartFileConvert.cpp
/// @brief Tests for files/PartFileConvert — format detection, job management.

#include "TestHelpers.h"
#include "files/PartFileConvert.h"

#include <QFile>
#include <QTest>
#include <QTemporaryDir>

#include <cstring>

using namespace eMule;

class tst_PartFileConvert : public QObject {
    Q_OBJECT

private slots:
    void scanFolderToAdd_findsPartMet();
    void scanFolderToAdd_recursive();
    void addJob_queuesJob();
    void removeAllJobs_clears();
    void removeJob_specific();
    void formatDetection();
    void startStopThread();
    void processQueue_startsThread();
};

void tst_PartFileConvert::scanFolderToAdd_findsPartMet()
{
    eMule::testing::TempDir tmpDir;

    // Create a .part.met file with valid header
    const QString partMetFile = tmpDir.filePath(QStringLiteral("test.part.met"));
    QFile f(partMetFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    // Write PARTFILE_VERSION header (0xE0)
    char header = static_cast<char>(0xE0);
    f.write(&header, 1);
    f.write(QByteArray(100, '\0')); // padding
    f.close();

    PartFileConvert::removeAllJobs();
    PartFileConvert::scanFolderToAdd(tmpDir.path());

    QVERIFY(PartFileConvert::jobCount() >= 1);
    PartFileConvert::removeAllJobs();
}

void tst_PartFileConvert::scanFolderToAdd_recursive()
{
    eMule::testing::TempDir tmpDir;

    // Create subdirectory with a .part.met file
    QDir dir(tmpDir.path());
    dir.mkdir(QStringLiteral("subdir"));

    const QString partMetFile = tmpDir.filePath(QStringLiteral("subdir/deep.part.met"));
    QFile f(partMetFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    char header = static_cast<char>(0xE0);
    f.write(&header, 1);
    f.write(QByteArray(100, '\0'));
    f.close();

    PartFileConvert::removeAllJobs();
    PartFileConvert::scanFolderToAdd(tmpDir.path(), true);

    QVERIFY(PartFileConvert::jobCount() >= 1);
    PartFileConvert::removeAllJobs();
}

void tst_PartFileConvert::addJob_queuesJob()
{
    PartFileConvert::removeAllJobs();

    ConvertJob job;
    job.folder = QStringLiteral("/tmp");
    job.filename = QStringLiteral("test.part.met");
    job.state = ConvertStatus::Queued;
    PartFileConvert::addJob(std::move(job));

    QCOMPARE(PartFileConvert::jobCount(), 1);

    auto retrieved = PartFileConvert::jobAt(0);
    QCOMPARE(retrieved.folder, QStringLiteral("/tmp"));
    QCOMPARE(retrieved.filename, QStringLiteral("test.part.met"));

    PartFileConvert::removeAllJobs();
}

void tst_PartFileConvert::removeAllJobs_clears()
{
    PartFileConvert::removeAllJobs();

    ConvertJob job;
    job.folder = QStringLiteral("/tmp");
    PartFileConvert::addJob(std::move(job));
    PartFileConvert::addJob(ConvertJob{});

    QCOMPARE(PartFileConvert::jobCount(), 2);
    PartFileConvert::removeAllJobs();
    QCOMPARE(PartFileConvert::jobCount(), 0);
}

void tst_PartFileConvert::removeJob_specific()
{
    PartFileConvert::removeAllJobs();

    ConvertJob job1;
    job1.filename = QStringLiteral("first.part.met");
    PartFileConvert::addJob(std::move(job1));

    ConvertJob job2;
    job2.filename = QStringLiteral("second.part.met");
    PartFileConvert::addJob(std::move(job2));

    QCOMPARE(PartFileConvert::jobCount(), 2);
    PartFileConvert::removeJob(0);
    QCOMPARE(PartFileConvert::jobCount(), 1);
    QCOMPARE(PartFileConvert::jobAt(0).filename, QStringLiteral("second.part.met"));

    PartFileConvert::removeAllJobs();
}

void tst_PartFileConvert::formatDetection()
{
    eMule::testing::TempDir tmpDir;

    // Create file with PARTFILE_VERSION header (0xE0)
    {
        const QString path = tmpDir.filePath(QStringLiteral("default.part.met"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        char header = static_cast<char>(0xE0);
        f.write(&header, 1);
        f.write(QByteArray(50, '\0'));
        f.close();
        QCOMPARE(PartFileConvert::detectFormat(path), 1); // DefaultOld
    }

    // Create file with PARTFILE_SPLITTEDVERSION (0xE1)
    {
        const QString path = tmpDir.filePath(QStringLiteral("split.part.met"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        char header = static_cast<char>(0xE1);
        f.write(&header, 1);
        f.write(QByteArray(50, '\0'));
        f.close();
        QCOMPARE(PartFileConvert::detectFormat(path), 2); // Splitted
    }

    // Create file with MET_HEADER (0x0E)
    {
        const QString path = tmpDir.filePath(QStringLiteral("newold.part.met"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        char header = static_cast<char>(0x0E);
        f.write(&header, 1);
        f.write(QByteArray(50, '\0'));
        f.close();
        QCOMPARE(PartFileConvert::detectFormat(path), 3); // NewOld
    }

    // Unknown format
    {
        const QString path = tmpDir.filePath(QStringLiteral("unknown.dat"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(50, '\xFF'));
        f.close();
        QCOMPARE(PartFileConvert::detectFormat(path), 0); // Unknown
    }
}

void tst_PartFileConvert::startStopThread()
{
    PartFileConvert::removeAllJobs();

    // Start and immediately stop — should not crash
    PartFileConvert::startThread();
    QTest::qWait(50); // give thread a moment to start
    PartFileConvert::stopThread();
}

void tst_PartFileConvert::processQueue_startsThread()
{
    PartFileConvert::removeAllJobs();

    // Add a queued job
    ConvertJob job;
    job.folder = QStringLiteral("/tmp");
    job.filename = QStringLiteral("test.part.met");
    job.format = 1;
    job.state = ConvertStatus::Queued;
    PartFileConvert::addJob(std::move(job));

    // processQueue should start the thread
    PartFileConvert::processQueue();
    QTest::qWait(100); // give thread time to process

    // Stop and clean up
    PartFileConvert::stopThread();
    PartFileConvert::removeAllJobs();
}

QTEST_MAIN(tst_PartFileConvert)
#include "tst_PartFileConvert.moc"
