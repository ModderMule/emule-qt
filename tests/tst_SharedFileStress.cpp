/// @file tst_SharedFileStress.cpp
/// @brief Stress test for a very large share (issue #46): hash 50k small files, then
///        make sure a reload and a restart hash none of them again.
///
/// Off by default — it writes 50k files and runs for minutes. Enable with
/// `EMULE_STRESS_HASH=1`; `EMULE_STRESS_HASH_FILES=<n>` changes the file count.

#include "TestHelpers.h"
#include "crypto/AICHHashSet.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>

using namespace eMule;
using namespace eMule::testing;

class tst_SharedFileStress : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void hashManyFiles_neverRehashed();

private:
    static constexpr int kDirs = 10;
    static int fileCount();
    static QString fileName(int i);
    static void createFiles(const QStringList& dirs, int count);
    /// Rewrite known.met as MorphXT does: every record padded with spreadbar tags.
    static void writeMorphKnownMet(const QString& configDir, const KnownFileList& known);
    /// Pump events until all is hashed and @p target files are shared.
    static bool waitForHashed(SharedFileList& shared, int target, int timeoutMs);
};

QString tst_SharedFileStress::fileName(int i)
{
#ifndef Q_OS_WIN
    // Characters Windows forbids: the record must keep them, or it never matches again
    if (i % 1000 == 7)
        return QStringLiteral("file:%1?.bin").arg(i, 6, 10, QLatin1Char('0'));
#endif
    return QStringLiteral("file_%1.bin").arg(i, 6, 10, QLatin1Char('0'));
}

int tst_SharedFileStress::fileCount()
{
    const int n = qEnvironmentVariableIntValue("EMULE_STRESS_HASH_FILES");
    return n > 0 ? n : 50000;
}

void tst_SharedFileStress::createFiles(const QStringList& dirs, int count)
{
    for (const QString& dir : dirs)
        QDir().mkpath(dir);
    QByteArray data(1024, 'X');
    for (int i = 0; i < count; ++i) {
        // unique content, so no two files share a hash
        data[0] = static_cast<char>(i & 0xFF);
        data[1] = static_cast<char>((i >> 8) & 0xFF);
        data[2] = static_cast<char>((i >> 16) & 0xFF);
        QFile f(dirs[i % dirs.size()] + u'/' + fileName(i));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data.constData(), 512 + (i % 512));   // varied sizes too
    }
}

void tst_SharedFileStress::writeMorphKnownMet(const QString& configDir, const KnownFileList& known)
{
    // ~12 KB per record, as in the reporter's 600 MB known.met of 50k files
    constexpr int kSpreadRanges = 300;
    SafeFile file(configDir + QStringLiteral("/known.met"), QIODevice::WriteOnly);
    file.writeUInt8(MET_HEADER_I64TAGS);
    file.writeUInt32(static_cast<uint32>(known.count()));
    known.forEachFile([&](const KnownFile* kf) {
        file.writeUInt32(static_cast<uint32>(kf->utcFileDate()));
        kf->fileIdentifier().writeMD4HashsetToFile(file);
        file.writeUInt32(4 + 3 * kSpreadRanges);
        Tag(FT_FILENAME, kf->fileName()).writeTagToFile(file, UTF8Mode::OptBOM);
        Tag(FT_FILESIZE, static_cast<uint32>(kf->fileSize())).writeTagToFile(file);
        Tag(FT_ULPRIORITY, uint32{5}).writeTagToFile(file);
        Tag(QByteArray("Spreadbar"), uint32{1}).writeTagToFile(file);
        for (int r = 0; r < kSpreadRanges; ++r) {
            const QByteArray index = QByteArray::number(r);
            const auto start = static_cast<uint64>(r) * 10;
            Tag(QByteArray(1, '\x70') + index, start).writeTagToFile(file);
            Tag(QByteArray(1, '\x71') + index, start + 5).writeTagToFile(file);
            Tag(QByteArray(1, '\x72') + index, uint64{3}).writeTagToFile(file);
        }
    });
    file.commit(false);
}

bool tst_SharedFileStress::waitForHashed(SharedFileList& shared, int target, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while ((shared.getHashingCount() > 0 || shared.getCount() < target)
           && timer.elapsed() < timeoutMs)
        QTest::qWait(20);
    return shared.getHashingCount() == 0 && shared.getCount() >= target;
}

void tst_SharedFileStress::initTestCase()
{
    if (qEnvironmentVariableIntValue("EMULE_STRESS_HASH") == 0)
        QSKIP("stress test off; set EMULE_STRESS_HASH=1 to run");
    loadProjectEnv();
}

void tst_SharedFileStress::hashManyFiles_neverRehashed()
{
    const int count = fileCount();
    TempDir tmp;
    const QString configDir = tmp.path() + QStringLiteral("/config");
    QDir().mkpath(configDir);
    QStringList dirs;
    for (int d = 0; d < kDirs; ++d)
        dirs << tmp.path() + QStringLiteral("/share%1").arg(d);

    QElapsedTimer timer;
    timer.start();
    createFiles(dirs, count);
    if (QTest::currentTestFailed())
        return;
    qInfo("created %d files in %lld ms", count, timer.elapsed());

    // AICH sets go to known2_64.met as in the daemon
    AICHRecoveryHashSet::setKnown2MetPath(configDir + QStringLiteral("/known2_64.met"));
    thePrefs.setIncomingDir(dirs.front());
    thePrefs.setSharedDirs(dirs.mid(1));

    // 1. First run: everything is hashed
    {
        KnownFileList known;
        QVERIFY(known.init(configDir));
        SharedFileList shared(&known);

        timer.restart();
        shared.reload();
        const qint64 scanMs = timer.elapsed();
        QVERIFY2(waitForHashed(shared, count, 1'800'000),
                 qPrintable(QStringLiteral("hashed %1/%2, %3 still queued")
                                .arg(shared.getCount()).arg(count).arg(shared.getHashingCount())));
        const qint64 hashMs = timer.elapsed();
        qInfo("first scan %lld ms, hashed %d files in %lld ms (%.0f files/s)", scanMs, count,
              hashMs, count * 1000.0 / static_cast<double>(std::max<qint64>(hashMs, 1)));
        QCOMPARE(static_cast<int>(known.count()), count);

        // 2. Reload with nothing changed: nothing is hashed, nothing leaves or joins
        QSignalSpy added(&shared, &SharedFileList::fileAdded);
        QSignalSpy removed(&shared, &SharedFileList::fileRemoved);
        timer.restart();
        shared.reload();
        const qint64 reloadMs = timer.elapsed();
        QCOMPARE(shared.getHashingCount(), 0);
        QTest::qWait(200);
        QCOMPARE(added.count(), 0);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(shared.getCount(), count);
        qInfo("unchanged reload %lld ms", reloadMs);

        // 3. One file changes: only that one is hashed again
        {
            QFile f(dirs[1] + u'/' + fileName(1));
            QVERIFY(f.open(QIODevice::Append));
            f.write("changed");
            f.close();
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600 * 5),
                                  QFileDevice::FileModificationTime));
        }
        shared.reload();
        QCOMPARE(shared.getHashingCount(), 1);
        QVERIFY(waitForHashed(shared, count, 60'000));
        QCOMPARE(shared.getCount(), count);

        timer.restart();
        known.save();
        qInfo("known.met saved in %lld ms (%lld bytes)", timer.elapsed(),
              QFileInfo(configDir + QStringLiteral("/known.met")).size());
    }

    // 4. Restart: known.met is read back and nothing is hashed
    {
        KnownFileList known;
        timer.restart();
        QVERIFY(known.init(configDir));
        const qint64 loadMs = timer.elapsed();
        qInfo("known.met loaded in %lld ms", loadMs);
        QVERIFY(static_cast<int>(known.count()) >= count);   // the changed file's old record may stay

        SharedFileList shared(&known);
        timer.restart();
        shared.reload();
        qInfo("restart scan %lld ms", timer.elapsed());
        QCOMPARE(shared.getHashingCount(), 0);
        QCOMPARE(shared.getCount(), count);

        // 5. A MorphXT known.met (issue #46): dates one DST hour off, records padded
        // with spreadbar tags. Read fast, and still nothing to hash.
        known.forEachFile([](const KnownFile* f) {
            const_cast<KnownFile*>(f)->setUtcFileDate(f->utcFileDate() + 3600);
        });
        writeMorphKnownMet(configDir, known);
    }
    {
        KnownFileList known;
        timer.restart();
        QVERIFY(known.init(configDir));
        const qint64 loadMs = timer.elapsed();
        const qint64 metBytes = QFileInfo(configDir + QStringLiteral("/known.met")).size();
        qInfo("MorphXT known.met (%lld bytes) loaded in %lld ms (%.1f MB/s)", metBytes, loadMs,
              static_cast<double>(metBytes) / 1000.0 / static_cast<double>(std::max<qint64>(loadMs, 1)));
        SharedFileList shared(&known);
        shared.reload();
        QCOMPARE(shared.getHashingCount(), 0);
        QCOMPARE(shared.getCount(), count);
    }
}

QTEST_GUILESS_MAIN(tst_SharedFileStress)
#include "tst_SharedFileStress.moc"
