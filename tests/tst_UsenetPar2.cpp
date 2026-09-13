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

#include "post/Par2NameIndex.h"
#include "post/Par2Verifier.h"

#include <QCryptographicHash>
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
    void listsTheRecoverySetWithoutScanningAnyData();
    void theListedHash16kMatchesTheFilesFirst16k();
    void aFileShorterThan16kListsTheSameHashTwice();
    void aTruncatedIndexIsReportedRatherThanCrashing();
    void aDamagedObfuscatedFileCostsOnlyTheBlocksItLost();
    void aRepairedObfuscatedFileLeavesItsOldNameBehind();
    void theNameIndexKeysOnLengthAndTheOpeningBytesTogether();
    void twoIdenticalFilesInASetAreLeftUnnamed();
    void aFileWhoseOpeningBytesAreNotThereYetIsNotMatched();
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

    // par2 does not delete what it repaired over: RenameTargetFiles() moves the
    // damaged file aside as "payload.bin.1" and purgefiles is off. This happens
    // on every repaired release, not only obfuscated ones, and the work folder
    // is what gets published -- so the result has to name them.
    QCOMPARE(fixed.backupFiles.size(), 1);
    QCOMPARE(QFileInfo(fixed.backupFiles.first()).fileName(),
             QStringLiteral("payload.bin.1"));
    QVERIFY(QFileInfo::exists(fixed.backupFiles.first()));
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

// ---------------------------------------------------------------------------
// listFiles -- the recovery set's own names
// ---------------------------------------------------------------------------

void TestUsenetPar2::listsTheRecoverySetWithoutScanningAnyData()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray a = patternData(30000, 'a');
    const QByteArray b = patternData(12000, 'b');
    QVERIFY(writeFile(QDir(dir.path()).filePath(QStringLiteral("Real.Name.part1.rar")), a));
    QVERIFY(writeFile(QDir(dir.path()).filePath(QStringLiteral("Real.Name.part2.rar")), b));
    QVERIFY(createPar2Set(dir.path(), QStringLiteral("rel"),
                          {QStringLiteral("Real.Name.part1.rar"),
                           QStringLiteral("Real.Name.part2.rar")},
                          /*blockSize*/ 4000, /*recoveryBlocks*/ 2));

    // Delete the payload outright. The names come from the packets, so a set
    // can describe files that are not on disk yet -- which is the entire point:
    // during a download they are not.
    QVERIFY(QFile::remove(QDir(dir.path()).filePath(QStringLiteral("Real.Name.part1.rar"))));
    QVERIFY(QFile::remove(QDir(dir.path()).filePath(QStringLiteral("Real.Name.part2.rar"))));

    Par2Verifier v;
    const Par2FileList list = v.listFiles(QDir(dir.path()).filePath(QStringLiteral("rel.par2")),
                                          dir.path());

    QVERIFY2(list.ok(), qPrintable(list.message));
    QCOMPARE(list.outcome, Par2Outcome::Listed);
    QCOMPARE(list.files.size(), 2);
    QVERIFY(list.blockSize > 0);

    QStringList names;
    for (const Par2SetFile& f : list.files) {
        names << f.fileName;
        QVERIFY(f.recoverable);
        QCOMPARE(f.hash16k.size(), 16);
        QCOMPARE(f.hashFull.size(), 16);
    }
    names.sort();
    QCOMPARE(names, QStringList({QStringLiteral("Real.Name.part1.rar"),
                                 QStringLiteral("Real.Name.part2.rar")}));

    for (const Par2SetFile& f : list.files) {
        const qint64 expect = f.fileName.endsWith(QStringLiteral("part1.rar")) ? a.size()
                                                                              : b.size();
        QCOMPARE(f.size, expect);
    }
#endif
}

void TestUsenetPar2::theListedHash16kMatchesTheFilesFirst16k()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray data = patternData(30000, 'h');   // comfortably over 16 KiB
    QVERIFY(writeFile(QDir(dir.path()).filePath(QStringLiteral("Big.bin")), data));
    QVERIFY(createPar2Set(dir.path(), QStringLiteral("rel"),
                          {QStringLiteral("Big.bin")}, 4000, 1));

    Par2Verifier v;
    const Par2FileList list = v.listFiles(QDir(dir.path()).filePath(QStringLiteral("rel.par2")),
                                          dir.path());
    QVERIFY2(list.ok(), qPrintable(list.message));
    QCOMPARE(list.files.size(), 1);

    // The case that fails the moment anyone reaches for MD5Hash::print(), which
    // emits hash[15] first. Nothing would ever match, silently.
    const QByteArray expect16k =
        QCryptographicHash::hash(data.left(int(kPar2Hash16kBytes)), QCryptographicHash::Md5);
    QCOMPARE(list.files.first().hash16k.toHex(), expect16k.toHex());

    const QByteArray expectFull = QCryptographicHash::hash(data, QCryptographicHash::Md5);
    QCOMPARE(list.files.first().hashFull.toHex(), expectFull.toHex());
#endif
}

void TestUsenetPar2::aFileShorterThan16kListsTheSameHashTwice()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray data = patternData(5000, 's');    // under the 16 KiB window
    QVERIFY(writeFile(QDir(dir.path()).filePath(QStringLiteral("Small.bin")), data));
    QVERIFY(createPar2Set(dir.path(), QStringLiteral("rel"),
                          {QStringLiteral("Small.bin")}, 1000, 1));

    Par2Verifier v;
    const Par2FileList list = v.listFiles(QDir(dir.path()).filePath(QStringLiteral("rel.par2")),
                                          dir.path());
    QVERIFY2(list.ok(), qPrintable(list.message));
    QCOMPARE(list.files.size(), 1);

    // A short file hashes whole for both fields, so a matcher keyed on hash16k
    // still identifies it -- it does not need a special case.
    const QByteArray whole = QCryptographicHash::hash(data, QCryptographicHash::Md5);
    QCOMPARE(list.files.first().hash16k.toHex(), whole.toHex());
    QCOMPARE(list.files.first().hashFull.toHex(), whole.toHex());
#endif
}

void TestUsenetPar2::aTruncatedIndexIsReportedRatherThanCrashing()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QVERIFY(writeFile(QDir(dir.path()).filePath(QStringLiteral("Real.bin")),
                      patternData(20000, 't')));
    QVERIFY(createPar2Set(dir.path(), QStringLiteral("rel"),
                          {QStringLiteral("Real.bin")}, 4000, 1));

    const QString index = QDir(dir.path()).filePath(QStringLiteral("rel.par2"));
    const QByteArray whole = readFile(index);
    QVERIFY(whole.size() > 64);
    QVERIFY(writeFile(index, whole.left(whole.size() / 2)));

    // CreateSourceFileList() pushes its map lookup unconditionally, so a set
    // that lost a description packet leaves a null in `sourcefiles` rather than
    // a shorter vector. Either a partial list or a refusal is a correct answer;
    // dereferencing that null is not.
    Par2Verifier v;
    const Par2FileList list = v.listFiles(index, dir.path());
    QVERIFY(list.outcome == Par2Outcome::Listed
            || list.outcome == Par2Outcome::NoPar2Files
            || list.outcome == Par2Outcome::Error);
    for (const Par2SetFile& f : list.files) {
        QVERIFY(!f.fileName.isEmpty());
        QCOMPARE(f.hash16k.size(), 16);
    }
#endif
}

// ---------------------------------------------------------------------------
// The obfuscated file par2 was never told about
// ---------------------------------------------------------------------------

namespace {
#ifdef EMULE_HAVE_PAR2
/// A release as an obfuscated post leaves it: right bytes for the most part,
/// meaningless name, and a hole where an article did not arrive.
struct ObfuscatedSet {
    QString index;
    QString obfuscated;
    QByteArray original;
};

bool buildDamagedObfuscatedSet(const QString& dir, ObfuscatedSet& out, int recoveryBlocks = 6)
{
    const QString real = QDir(dir).filePath(QStringLiteral("payload.bin"));
    out.original = patternData(60000, 'x');
    if (!writeFile(real, out.original))
        return false;
    if (!createPar2Set(dir, QStringLiteral("rel"), {QStringLiteral("payload.bin")},
                       /*blockSize*/ 4000, recoveryBlocks))
        return false;

    // Two blocks' worth of zeros at a block boundary: right length, wrong bytes.
    QByteArray damaged = out.original;
    damaged.replace(20000, 8000, QByteArray(8000, '\0'));
    if (!writeFile(real, damaged))
        return false;

    out.index = QDir(dir).filePath(QStringLiteral("rel.par2"));
    out.obfuscated = QDir(dir).filePath(QStringLiteral("abcd1234efgh5678.bin"));
    return QFile::rename(real, out.obfuscated);
}
#endif
} // namespace

void TestUsenetPar2::aDamagedObfuscatedFileCostsOnlyTheBlocksItLost()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ObfuscatedSet set;
    QVERIFY(buildDamagedObfuscatedSet(dir.path(), set));

    Par2Verifier v;

    // The rename pass cannot rescue this one: renameonly stops at the first
    // partial match (par2repairer.cpp:1801), so a damaged obfuscated file keeps
    // its meaningless name.
    const Par2Result renamed = v.rename(set.index, dir.path());
    QCOMPARE(renamed.renamedFiles, 0);
    QVERIFY(QFileInfo::exists(set.obfuscated));

    const Par2Result check = v.verify(set.index, dir.path());

    // The defect: with no extra files handed to it, par2 never sees this file at
    // all and books every one of its blocks as missing. missingBlocks is what
    // UsenetQueue::requestPar2Volumes() spends the user's allowance on, so the
    // difference between 2 and 15 is real money -- and, when recovery is short,
    // a repairable release declared dead.
    QCOMPARE(check.sourceBlocks, 15);
    QCOMPARE(check.missingBlocks, 2);        // was 15: the whole file
    QCOMPARE(check.availableBlocks, 13);
    QCOMPARE(check.blocksStillNeeded(), 0);
    QCOMPARE(check.outcome, Par2Outcome::RepairPossible);

    // par2 still counts the *file* as missing, and that is not a bug to fix
    // here: ePartialMatch on an extra file records its blocks but sets no target
    // file (par2repairer.cpp:1499), so nothing owns the name. The blocks are
    // what the queue spends money on; the file tally is bookkeeping.
    QCOMPARE(check.missingFiles, 1);
    QCOMPARE(check.completeFiles, 0);
#endif
}

void TestUsenetPar2::aRepairedObfuscatedFileLeavesItsOldNameBehind()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ObfuscatedSet set;
    QVERIFY(buildDamagedObfuscatedSet(dir.path(), set));

    Par2Verifier v;
    const Par2Result fixed = v.repair(set.index, dir.path());
    QCOMPARE(fixed.outcome, Par2Outcome::Repaired);

    const QString real = QDir(dir.path()).filePath(QStringLiteral("payload.bin"));
    QVERIFY(QFileInfo::exists(real));
    QCOMPARE(readFile(real), set.original);

    // RenameTargetFiles() renames a damaged target with DiskFile::Rename(void),
    // which appends ".1" -- it does not delete it, because purgefiles is off.
    // The result has to name them or the post-processor publishes a
    // known-damaged copy of the payload beside the repaired one.
    // ...and the obfuscated copy is still sitting there. This is the second,
    // *unreported* leftover: with no target file to rename, par2 built the real
    // name from recovery data and never touched the hex one, so backupFiles is
    // empty and a publisher walking the work folder would offer ED2K peers a
    // known-damaged copy of the payload. Naming it needs the recovery set's own
    // file list -- see Par2NameIndex.
    QVERIFY(QFileInfo::exists(set.obfuscated));
    QVERIFY(fixed.backupFiles.isEmpty());
#endif
}

// ---------------------------------------------------------------------------
// Par2NameIndex
// ---------------------------------------------------------------------------

void TestUsenetPar2::theNameIndexKeysOnLengthAndTheOpeningBytesTogether()
{
    const QByteArray a = patternData(40000, 'a');
    const QByteArray b = patternData(40000, 'b');   // same length, different bytes

    const auto entry = [](const QString& name, const QByteArray& data) {
        Par2SetFile f;
        f.fileName = name;
        f.size = data.size();
        f.hash16k = QCryptographicHash::hash(data.left(int(kPar2Hash16kBytes)),
                                             QCryptographicHash::Md5);
        f.hashFull = QCryptographicHash::hash(data, QCryptographicHash::Md5);
        return f;
    };

    Par2NameIndex index;
    index.setFiles({entry(QStringLiteral("first.rar"), a),
                    entry(QStringLiteral("second.rar"), b)});
    QCOMPARE(index.size(), 2);

    // Length alone would be useless here, and that is not a contrived case:
    // every volume of a RAR set is the same length except the last.
    QCOMPARE(index.match(a.size(), QCryptographicHash::hash(
                             a.left(int(kPar2Hash16kBytes)), QCryptographicHash::Md5)),
             QStringLiteral("first.rar"));
    QCOMPARE(index.match(b.size(), QCryptographicHash::hash(
                             b.left(int(kPar2Hash16kBytes)), QCryptographicHash::Md5)),
             QStringLiteral("second.rar"));

    // The right bytes at the wrong length is not a match either.
    QVERIFY(index.match(a.size() + 1,
                        QCryptographicHash::hash(a.left(int(kPar2Hash16kBytes)),
                                                 QCryptographicHash::Md5)).isEmpty());

    // One name, one file.
    QVERIFY(index.claim(QStringLiteral("first.rar")));
    QVERIFY(!index.claim(QStringLiteral("first.rar")));
}

void TestUsenetPar2::twoIdenticalFilesInASetAreLeftUnnamed()
{
    // A set can hold two byte-identical files under different names -- two copies
    // of an .nfo is the everyday case. They are indistinguishable by length and
    // opening bytes, which is all we have, so guessing would swap them.
    const QByteArray same = patternData(20000, 'n');

    Par2SetFile one;
    one.fileName = QStringLiteral("readme.nfo");
    one.size = same.size();
    one.hash16k = QCryptographicHash::hash(same.left(int(kPar2Hash16kBytes)),
                                           QCryptographicHash::Md5);

    Par2SetFile two = one;
    two.fileName = QStringLiteral("info.nfo");

    Par2NameIndex index;
    index.setFiles({one, two});
    QVERIFY(index.match(same.size(), one.hash16k).isEmpty());
}

void TestUsenetPar2::aFileWhoseOpeningBytesAreNotThereYetIsNotMatched()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray whole = patternData(40000, 'p');
    Par2SetFile f;
    f.fileName = QStringLiteral("Real.mkv");
    f.size = whole.size();
    f.hash16k = QCryptographicHash::hash(whole.left(int(kPar2Hash16kBytes)),
                                         QCryptographicHash::Md5);

    Par2NameIndex index;
    index.setFiles({f});

    // A scratch file with only part of its opening window written. Reading it
    // short and hashing whatever is there would produce a confident wrong answer.
    const QString path = QDir(dir.path()).filePath(QStringLiteral("scratch.part"));
    QVERIFY(writeFile(path, whole.left(int(kPar2Hash16kBytes) - 1)));
    QVERIFY(index.matchFile(path, whole.size()).isEmpty());

    QVERIFY(writeFile(path, whole));
    QCOMPARE(index.matchFile(path, whole.size()), QStringLiteral("Real.mkv"));

    // A file shorter than the window hashes whole, so the window is its length.
    QCOMPARE(Par2NameIndex::bytesNeededFor(5000), qint64(5000));
    QCOMPARE(Par2NameIndex::bytesNeededFor(40000), kPar2Hash16kBytes);
}

QTEST_MAIN(TestUsenetPar2)
#include "tst_UsenetPar2.moc"
