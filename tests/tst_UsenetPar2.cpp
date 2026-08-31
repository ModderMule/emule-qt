/// @file tst_UsenetPar2.cpp
/// @brief Par2Verifier against real recovery sets, generated in-process.
///
/// The sets are built with Par2::par2create from the same library that verifies
/// them, so there is no binary fixture to check in and no par2 CLI to install.
///
/// What these cases are really guarding is the two ways a par2 wrapper fails
/// silently. A basepath without a trailing separator makes every source file
/// read as missing, which looks exactly like a genuinely destroyed release. And
/// missingBlocks is the number the queue uses to decide how many recovery
/// volumes to fetch, so a wrapper that reports it off by any amount either
/// wastes bandwidth or gives up on a repairable download.

#include "TestHelpers.h"

#include "post/Par2Verifier.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#ifdef EMULE_HAVE_PAR2
#  include <par2/libpar2.h>
#  include <sstream>
#endif

using namespace eMule;
using namespace eMule::usenet;

namespace {

/// Deterministic filler: a repeating pattern compresses and hashes predictably,
/// and a random one would make a failure impossible to reproduce.
QByteArray patternData(int size, char seed)
{
    QByteArray data(size, seed);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 31 + seed * 7) & 0xFF);
    return data;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

#ifdef EMULE_HAVE_PAR2
/// Build a par2 set covering @p files, with @p recoveryBlocks recovery blocks.
///
/// @p baseName carries **no** extension: par2create appends ".par2"
/// unconditionally, so passing "rel.par2" yields "rel.par2.par2" and a set
/// nothing later can find. It also emits unpadded volume names (rel.vol0+1.par2)
/// where QuickPar and MultiPar pad them (rel.vol000+01.par2) — both spellings
/// occur in the wild and the volume parser has to take either.
bool createPar2Set(const QString& dir, const QString& baseName,
                   const QStringList& files, int blockSize, int recoveryBlocks)
{
    std::ostringstream sout;
    std::ostringstream serr;

    std::vector<std::string> extra;
    for (const QString& f : files)
        extra.push_back(QDir(dir).filePath(f).toStdString());

    QString base = QDir(dir).absolutePath();
    if (!base.endsWith(u'/'))
        base += u'/';

    const Par2::Result rc = Par2::par2create(
        sout, serr, Par2::nlSilent,
        /*memorylimit*/ 256,
        base.toStdString(),
        /*nthreads*/ 2, /*filethreads*/ 1,
        QDir(dir).filePath(baseName).toStdString(),
        extra,
        quint64(blockSize),
        /*firstblock*/ 0,
        Par2::scVariable,
        /*recoveryfilecount*/ 0,
        quint32(recoveryBlocks));

    if (rc != Par2::eSuccess)
        qWarning("par2create failed: %d %s", int(rc), serr.str().c_str());
    return rc == Par2::eSuccess;
}
#endif

} // namespace

class TestUsenetPar2 : public QObject {
    Q_OBJECT

private slots:
    void reportsUnavailableWithoutTheLibrary();
    void chooseIndexFilePrefersTheNonVolumeMember();
    void verifiesAnIntactSet();
    void repairsADamagedFile();
    void reportsBlocksNeededWhenRecoveryIsShort();
    void renameOnlyRestoresAnObfuscatedName();
};

// ---------------------------------------------------------------------------

void TestUsenetPar2::reportsUnavailableWithoutTheLibrary()
{
    // The build flag and the runtime answer must agree, or the pipeline will
    // decide it can repair and then quietly do nothing.
#ifdef EMULE_HAVE_PAR2
    QVERIFY(Par2Verifier::available());
#else
    QVERIFY(!Par2Verifier::available());

    Par2Verifier v;
    QCOMPARE(v.verify(QStringLiteral("/nonexistent.par2"), QDir::tempPath()).outcome,
             Par2Outcome::Unavailable);
#endif
}

void TestUsenetPar2::chooseIndexFilePrefersTheNonVolumeMember()
{
    // Order deliberately puts a volume first: picking the first entry rather
    // than the index one still loads the set, but starts from a file with no
    // file list in it.
    const QStringList paths{
        QStringLiteral("/tmp/rel.vol000+01.par2"),
        QStringLiteral("/tmp/rel.vol001+02.par2"),
        QStringLiteral("/tmp/rel.par2"),
    };
    QCOMPARE(Par2Verifier::chooseIndexFile(paths), QStringLiteral("/tmp/rel.par2"));

    QVERIFY(Par2Verifier::chooseIndexFile({}).isEmpty());

    // A set whose index file never arrived: fall back to the shortest volume
    // name rather than refusing to try.
    QCOMPARE(Par2Verifier::chooseIndexFile({QStringLiteral("/tmp/rel.vol001+02.par2"),
                                            QStringLiteral("/tmp/rel.vol000+01.par2")}),
             QStringLiteral("/tmp/rel.vol001+02.par2"));
}

void TestUsenetPar2::verifiesAnIntactSet()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QVERIFY(writeFile(QDir(dir.path()).filePath(QStringLiteral("data1.bin")),
                      patternData(40000, 'a')));
    QVERIFY(writeFile(QDir(dir.path()).filePath(QStringLiteral("data2.bin")),
                      patternData(25000, 'b')));
    QVERIFY(createPar2Set(dir.path(), QStringLiteral("rel"),
                          {QStringLiteral("data1.bin"), QStringLiteral("data2.bin")},
                          /*blockSize*/ 4000, /*recoveryBlocks*/ 4));

    Par2Verifier v;
    const Par2Result r = v.verify(QDir(dir.path()).filePath(QStringLiteral("rel.par2")),
                                  dir.path());

    QCOMPARE(r.outcome, Par2Outcome::Clean);
    QCOMPARE(r.missingBlocks, 0);
    QCOMPARE(r.damagedFiles, 0);
    QCOMPARE(r.completeFiles, 2);
    QVERIFY(r.ok());
#endif
}

void TestUsenetPar2::repairsADamagedFile()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString target = QDir(dir.path()).filePath(QStringLiteral("payload.bin"));
    const QByteArray original = patternData(60000, 'x');
    QVERIFY(writeFile(target, original));
    QVERIFY(createPar2Set(dir.path(), QStringLiteral("rel"),
                          {QStringLiteral("payload.bin")},
                          /*blockSize*/ 4000, /*recoveryBlocks*/ 6));

    // Zero a run in the middle, which is exactly the shape a missing article
    // leaves behind: right length, wrong bytes.
    QByteArray damaged = original;
    damaged.replace(20000, 8000, QByteArray(8000, '\0'));
    QVERIFY(writeFile(target, damaged));

    Par2Verifier v;

    int lastPercent = -1;
    v.setProgressCallback([&](int percent, const QString&) { lastPercent = percent; });

    const Par2Result check = v.verify(QDir(dir.path()).filePath(QStringLiteral("rel.par2")),
                                      dir.path());
    QCOMPARE(check.outcome, Par2Outcome::RepairPossible);
    QVERIFY(check.damagedFiles > 0);
    QVERIFY(check.missingBlocks > 0);
    // par2cmdline's own test for "repairable", and the reason recoveryBlocks and
    // availableBlocks are separate fields: the second counts intact *source*
    // blocks and says nothing about whether a repair can run.
    QVERIFY(check.recoveryBlocks >= check.missingBlocks);
    QCOMPARE(check.blocksStillNeeded(), 0);

    // Per mille arrives from the library; anything above 100 means the /10 was
    // dropped and every progress bar in the GUI would sit pegged.
    QVERIFY(lastPercent >= 0);
    QVERIFY(lastPercent <= 100);

    const Par2Result fixed = v.repair(QDir(dir.path()).filePath(QStringLiteral("rel.par2")),
                                      dir.path());
    QCOMPARE(fixed.outcome, Par2Outcome::Repaired);
    QVERIFY(fixed.ok());

    QCOMPARE(readFile(target), original);
#endif
}

void TestUsenetPar2::reportsBlocksNeededWhenRecoveryIsShort()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString target = QDir(dir.path()).filePath(QStringLiteral("payload.bin"));
    const QByteArray original = patternData(60000, 'y');
    QVERIFY(writeFile(target, original));

    // One recovery block against damage that will span several.
    QVERIFY(createPar2Set(dir.path(), QStringLiteral("rel"),
                          {QStringLiteral("payload.bin")},
                          /*blockSize*/ 4000, /*recoveryBlocks*/ 1));

    QByteArray damaged = original;
    damaged.replace(4000, 20000, QByteArray(20000, '\0'));
    QVERIFY(writeFile(target, damaged));

    Par2Verifier v;
    const Par2Result r = v.verify(QDir(dir.path()).filePath(QStringLiteral("rel.par2")),
                                  dir.path());

    // The distinction the whole on-demand par2 design rests on: not "broken",
    // but "broken and short by exactly this many blocks".
    QCOMPARE(r.outcome, Par2Outcome::NeedMoreBlocks);
    QVERIFY(r.missingBlocks > r.recoveryBlocks);
    // The exact number of extra recovery blocks the queue has to go and fetch.
    QCOMPARE(r.blocksStillNeeded(), r.missingBlocks - r.recoveryBlocks);
    QVERIFY(!r.ok());
#endif
}

void TestUsenetPar2::renameOnlyRestoresAnObfuscatedName()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray original = patternData(30000, 'z');
    QVERIFY(writeFile(QDir(dir.path()).filePath(QStringLiteral("Real.Name.mkv")), original));
    QVERIFY(createPar2Set(dir.path(), QStringLiteral("rel"),
                          {QStringLiteral("Real.Name.mkv")},
                          /*blockSize*/ 4000, /*recoveryBlocks*/ 2));

    // What an obfuscated post actually leaves on disk: right bytes, meaningless
    // name. Neither the verifier nor the unpacker can do anything with it until
    // the par2 metadata hands the name back.
    const QString obfuscated = QDir(dir.path()).filePath(
        QStringLiteral("abcd1234efgh5678.bin"));
    QVERIFY(QFile::rename(QDir(dir.path()).filePath(QStringLiteral("Real.Name.mkv")),
                          obfuscated));
    QVERIFY(!QFileInfo::exists(QDir(dir.path()).filePath(QStringLiteral("Real.Name.mkv"))));

    Par2Verifier v;
    const Par2Result r = v.rename(QDir(dir.path()).filePath(QStringLiteral("rel.par2")),
                                  dir.path());

    QVERIFY(QFileInfo::exists(QDir(dir.path()).filePath(QStringLiteral("Real.Name.mkv"))));
    QCOMPARE(readFile(QDir(dir.path()).filePath(QStringLiteral("Real.Name.mkv"))), original);
    QVERIFY(r.renamedFiles > 0 || r.completeFiles > 0);
#endif
}

QTEST_MAIN(TestUsenetPar2)
#include "tst_UsenetPar2.moc"
