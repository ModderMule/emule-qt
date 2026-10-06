/// @file tst_AICHSyncThread.cpp
/// @brief known2_64.met gets its hashsets: from hashing a file, and from the startup sync.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "crypto/AICHData.h"
#include "crypto/AICHHashSet.h"
#include "crypto/AICHSyncThread.h"
#include "crypto/FileIdentifier.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;

class tst_AICHSyncThread : public QObject {
    Q_OBJECT

private slots:
    void init();
    void hashingAFileStoresItsRecoverySet();
    void syncBuildsTheSetOfAFileThatHasNone();
    void syncCutsOffATornRecord();
    void purgeDropsOnlyWhatNothingRefersTo();
    void purgeLeavesACleanFileUntouched();

private:
    [[nodiscard]] QString known2Path() const { return m_dir->filePath(QStringLiteral("known2_64.met")); }
    QString writeFile(const QString& name, qsizetype size, char fill);

    std::unique_ptr<eMule::testing::TempDir> m_dir;
};

void tst_AICHSyncThread::init()
{
    // A fresh directory per test: a new path also drops the static hash index.
    m_dir = std::make_unique<eMule::testing::TempDir>();
    AICHRecoveryHashSet::setKnown2MetPath(known2Path());
}

QString tst_AICHSyncThread::writeFile(const QString& name, qsizetype size, char fill)
{
    const QString path = m_dir->filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    QByteArray data(size, fill);
    for (qsizetype i = 0; i < size; i += 4099)
        data[i] = static_cast<char>(i / 4099);
    f.write(data);
    return path;
}

// MFC CreateFromFile, srchybrid/KnownFile.cpp:456-470. The set used to be built and
// thrown away, with the "available" flag set regardless.
void tst_AICHSyncThread::hashingAFileStoresItsRecoverySet()
{
    const QString path = writeFile(QStringLiteral("hashed.bin"), 3 * EMBLOCKSIZE + 100, 'h');
    QVERIFY(!path.isEmpty());

    KnownFile file;
    QVERIFY(file.createFromFile(m_dir->path(), QStringLiteral("hashed.bin")));

    QVERIFY(file.fileIdentifier().hasAICHHash());
    QVERIFY2(file.isAICHRecoverHashSetAvailable(), "the set must be stored, not just announced");
    QVERIFY(AICHRecoveryHashSet::isStored(file.fileIdentifier().getAICHHash()));
    QVERIFY(QFileInfo(known2Path()).size() > 1);

    // And it reads back, which is what serving recovery data needs.
    AICHRecoveryHashSet stored(file.fileSize());
    stored.setMasterHash(file.fileIdentifier().getAICHHash(), EAICHStatus::HashSetComplete);
    QVERIFY(stored.loadHashSet());
}

void tst_AICHSyncThread::syncBuildsTheSetOfAFileThatHasNone()
{
    const QString path = writeFile(QStringLiteral("old.bin"), 2 * EMBLOCKSIZE + 7, 'o');
    QVERIFY(!path.isEmpty());

    // Known from an earlier run that stored nothing: has its hashes, no known2 record.
    AICHRecoveryHashSet::setKnown2MetPath(QString());
    auto* file = new KnownFile();
    QVERIFY(file->createFromFile(m_dir->path(), QStringLiteral("old.bin")));
    QVERIFY(!file->isAICHRecoverHashSetAvailable());
    const AICHHash master = file->fileIdentifier().getAICHHash();
    AICHRecoveryHashSet::setKnown2MetPath(known2Path());
    QVERIFY(!AICHRecoveryHashSet::isStored(master));

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    QVERIFY(knownFiles.safeAddKFile(file));
    QVERIFY(shared.safeAddKFile(file));

    AICHSyncThread sync(m_dir->path(), &shared);
    QSignalSpy synced(&sync, &AICHSyncThread::syncComplete);
    QSignalSpy hashed(&sync, &AICHSyncThread::fileHashed);
    sync.start();
    QVERIFY(hashed.wait(10000));

    QCOMPARE(synced.count(), 1);
    QCOMPARE(synced.first().at(0).toInt(), 1);
    QVERIFY(hashed.first().at(1).toBool());
    QVERIFY(AICHRecoveryHashSet::isStored(master));
    QVERIFY(file->isAICHRecoverHashSetAvailable());
    QCOMPARE(file->fileIdentifier().getAICHHash(), master);

    sync.requestStop();
    QVERIFY(sync.wait(5000));
}

void tst_AICHSyncThread::syncCutsOffATornRecord()
{
    QVERIFY(!writeFile(QStringLiteral("good.bin"), EMBLOCKSIZE + 50, 'g').isEmpty());
    KnownFile file;
    QVERIFY(file.createFromFile(m_dir->path(), QStringLiteral("good.bin")));
    const qint64 goodSize = QFileInfo(known2Path()).size();
    QVERIFY(goodSize > 1);

    // A crash mid-append: a master hash and a count promising more than is there.
    {
        QFile f(known2Path());
        QVERIFY(f.open(QIODevice::Append));
        f.write(QByteArray(kAICHHashSize, '\x55'));
        const quint32 count = 1000;
        f.write(reinterpret_cast<const char*>(&count), sizeof count);
        f.write(QByteArray(30, '\x66'));
    }
    QVERIFY(QFileInfo(known2Path()).size() > goodSize);

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    AICHSyncThread sync(m_dir->path(), &shared);
    QSignalSpy synced(&sync, &AICHSyncThread::syncComplete);
    sync.start();
    QVERIFY(synced.wait(10000));
    sync.requestStop();
    QVERIFY(sync.wait(5000));

    QCOMPARE(QFileInfo(known2Path()).size(), goodSize);
    QVERIFY(AICHRecoveryHashSet::isStored(file.fileIdentifier().getAICHHash()));
}

// MFC AICHSyncThread.cpp:163-247. known2 only ever grew.
void tst_AICHSyncThread::purgeDropsOnlyWhatNothingRefersTo()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // known: stays. Hashed before the "session" below starts.
    QVERIFY(!writeFile(QStringLiteral("known.bin"), EMBLOCKSIZE * 2 + 10, 'k').isEmpty());
    auto* known = new KnownFile();
    QVERIFY(known->createFromFile(m_dir->path(), QStringLiteral("known.bin")));
    knownFiles.safeAddKFile(known);

    // orphan: its file is in no list any more
    QVERIFY(!writeFile(QStringLiteral("orphan.bin"), EMBLOCKSIZE * 3 + 10, 'o').isEmpty());
    KnownFile orphan;
    QVERIFY(orphan.createFromFile(m_dir->path(), QStringLiteral("orphan.bin")));
    const AICHHash orphanHash = orphan.fileIdentifier().getAICHHash();
    const qint64 withOrphan = QFileInfo(known2Path()).size();

    // an older copy of the known file's record, as a crash-and-rehash leaves behind
    {
        QFile f(known2Path());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray all = f.readAll();
        f.close();
        const QByteArray master(reinterpret_cast<const char*>(
                                    known->fileIdentifier().getAICHHash().getRawHash()), kAICHHashSize);
        const qsizetype at = all.indexOf(master);
        QVERIFY(at == 1);
        const qsizetype len = kAICHHashSize + 4 + 3 * kAICHHashSize;   // three block hashes
        QVERIFY(f.open(QIODevice::Append));
        f.write(all.mid(at, len));
    }
    const qint64 before = QFileInfo(known2Path()).size();
    QVERIFY(before > withOrphan);

    // a new process: nothing counts as saved in this session
    AICHRecoveryHashSet::setKnown2MetPath(QString());
    AICHRecoveryHashSet::setKnown2MetPath(known2Path());

    // hashed while the sync runs its course: in no list yet, and must survive
    QVERIFY(!writeFile(QStringLiteral("fresh.bin"), EMBLOCKSIZE + 10, 'f').isEmpty());
    KnownFile fresh;
    QVERIFY(fresh.createFromFile(m_dir->path(), QStringLiteral("fresh.bin")));

    AICHSyncThread sync(m_dir->path(), &shared);
    sync.setPurgeSource(&knownFiles);
    QSignalSpy purged(&sync, &AICHSyncThread::purged);
    sync.start();
    QVERIFY(purged.wait(10000));
    sync.requestStop();
    QVERIFY(sync.wait(5000));

    QCOMPARE(purged.first().at(0).toUInt(), 2u);   // the orphan and the older duplicate
    QVERIFY(QFileInfo(known2Path()).size() < before);
    QVERIFY(!AICHRecoveryHashSet::isStored(orphanHash));
    QVERIFY(AICHRecoveryHashSet::isStored(fresh.fileIdentifier().getAICHHash()));

    // what stayed can still be read back from its new place
    for (KnownFile* file : {known, &fresh}) {
        AICHRecoveryHashSet stored(file->fileSize());
        stored.setMasterHash(file->fileIdentifier().getAICHHash(), EAICHStatus::HashSetComplete);
        QVERIFY2(stored.loadHashSet(), qPrintable(file->fileName()));
    }
}

void tst_AICHSyncThread::purgeLeavesACleanFileUntouched()
{
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    QVERIFY(!writeFile(QStringLiteral("known.bin"), EMBLOCKSIZE + 10, 'k').isEmpty());
    auto* known = new KnownFile();
    QVERIFY(known->createFromFile(m_dir->path(), QStringLiteral("known.bin")));
    knownFiles.safeAddKFile(known);

    AICHRecoveryHashSet::setKnown2MetPath(QString());
    AICHRecoveryHashSet::setKnown2MetPath(known2Path());
    const QDateTime stamp = QFileInfo(known2Path()).lastModified();
    const qint64 size = QFileInfo(known2Path()).size();

    {
        AICHSyncThread sync(m_dir->path(), &shared);
        sync.setPurgeSource(&knownFiles);
        QSignalSpy synced(&sync, &AICHSyncThread::syncComplete);
        QSignalSpy purged(&sync, &AICHSyncThread::purged);
        sync.start();
        QVERIFY(synced.wait(10000));
        QTest::qWait(200);
        QCOMPARE(purged.count(), 0);
    }
    QCOMPARE(QFileInfo(known2Path()).size(), size);
    QCOMPARE(QFileInfo(known2Path()).lastModified(), stamp);

    // an empty known list reads as "known.met did not load": nothing is dropped
    KnownFileList empty;
    {
        AICHSyncThread sync(m_dir->path(), &shared);
        sync.setPurgeSource(&empty);
        QSignalSpy synced(&sync, &AICHSyncThread::syncComplete);
        QSignalSpy purged(&sync, &AICHSyncThread::purged);
        sync.start();
        QVERIFY(synced.wait(10000));
        QTest::qWait(200);
        QCOMPARE(purged.count(), 0);
    }
    QCOMPARE(QFileInfo(known2Path()).size(), size);
}

QTEST_MAIN(tst_AICHSyncThread)
#include "tst_AICHSyncThread.moc"
