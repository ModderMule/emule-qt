/// @file tst_Ed3kDownloads.cpp
/// @brief Plain ed3k downloads (src/ed3k/Ed3kDownloads) and their IPC (Ed3kDownloadsBridge):
/// node A shares a file of a fake share list, node B downloads it by its root into a part
/// file, verifies, moves and shares it; pause/resume; a part file taken up after a restart.

#include "utils/BackgroundFileWriter.h"
#include "Ed3kDownloads.h"
#include "Ed3kDownloadsBridge.h"
#include "Ed3kSession.h"
#include "Ed3kLocalScan.h"
#include "Ed3kShared.h"
#include "Ed3kStore.h"

#include "IpcMessage.h"
#include "utils/BlockRuns.h"
#include "utils/Opcodes.h"

#include <QCborArray>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace eMule;
using namespace eMule::ed3k;

namespace {

constexpr std::uint64_t kBlock = 256 * 1024;

QByteArray payload(int size, int seed = 0)
{
    QByteArray data(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 7 + (i >> 9) * 5 + seed * 31) & 0xFF);
    return data;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

class FakeSource final : public SharedFileSource {
public:
    std::vector<SharedEntry> entries;
    std::vector<SharedEntry> files() const override { return entries; }
    std::optional<SharedEntry> find(const QByteArray& md4) const override
    {
        for (const auto& e : entries)
            if (e.md4 == md4)
                return e;
        return std::nullopt;
    }
};

Ed3kSettings loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

::ed3k::Source sourceOf(const Ed3kSession& s)
{
    return ::ed3k::Source{*s.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort()), std::nullopt, {}};
}

/// Node A: one shared file.
struct Seeder {
    QTemporaryDir dir;
    Ed3kSession session;
    FakeSource src;
    std::unique_ptr<Ed3kShared> shared;
    ::ed3k::Root root;

    bool start(const QByteArray& data, const QString& name)
    {
        const QString path = QDir(dir.path()).filePath(name);
        if (!dir.isValid() || !writeFile(path, data) || !session.start(loopback(dir.path())))
            return false;
        const QByteArray md4(16, char(0x42));
        src.entries.push_back(SharedEntry{md4, path, name, std::uint64_t(data.size())});
        shared = std::make_unique<Ed3kShared>(session, src, dir.path());
        QSignalSpy spy(shared.get(), &Ed3kShared::fileShared);
        shared->start();
        if (!spy.wait(20000) && spy.isEmpty())
            return false;
        const auto r = shared->rootOf(md4);
        if (!r)
            return false;
        root = *r;
        return true;
    }

    /// Several files at once; their roots in the order given.
    bool startFiles(const std::vector<std::pair<QString, QByteArray>>& files)
    {
        if (!dir.isValid() || !session.start(loopback(dir.path())))
            return false;
        char tag = 0x50;
        for (const auto& [name, data] : files) {
            const QString path = QDir(dir.path()).filePath(name);
            if (!writeFile(path, data))
                return false;
            src.entries.push_back(SharedEntry{QByteArray(16, tag++), path, name, std::uint64_t(data.size())});
        }
        shared = std::make_unique<Ed3kShared>(session, src, dir.path());
        QSignalSpy spy(shared.get(), &Ed3kShared::fileShared);
        shared->start();
        if (!QTest::qWaitFor([&] { return spy.count() >= qsizetype(files.size()); }, 30000))
            return false;
        for (const auto& e : src.entries) {
            const auto r = shared->rootOf(e.md4);
            if (!r)
                return false;
            roots.push_back(*r);
        }
        return true;
    }
    std::vector<::ed3k::Root> roots;
};

} // namespace

class tst_Ed3kDownloads : public QObject {
    Q_OBJECT

private slots:
    void aFileIsDownloadedVerifiedAndShared();
    void pauseAndResume();
    void aCopyToAnotherVolumeRunsOnAWorker();
    void aPartFileIsTakenUpAfterARestart();
    void uncheckedHttpBytesAreCheckedByPeerValues();
    void aCopiedPartFileGoesOnElsewhere();
    void aMovedPartFileGoesOnInItsNewFolder();
    void anOldStateFileMovesToMetFiles();
    void theIpcListsAndControls();
    void cancelDeletesACompleteFile();
    void downloadsOfOneSourceTakeTurns();
    void aHigherPriorityGetsMoreBlocks();
    void aRaisedPriorityTakesTheSource();
    void queuedPrioritySwitchesWaitThirtyMinutes();
    void aDownloadGoesUnderTheKeyItIsGiven();
    void aSharedFileIsNotDownloadedAgain();
    void aLocalCopyIsReadFirst();
    void aFoundCopyFillsAnOpenDownload();
    void aChangedFoundCopyIsNotRead();
    void aFolderScanHashesOnlyFilesOfAWantedSize();
    void aFolderScanFillsTheDownloads();
    void aDeclinedRootIsAddedPaused();
    void previewPartsComeFirst();
    void renameAndAUrlSourceFromTheMenu();
    void swapA4afPutsAFileFirstAtItsSource();
    void aSourceLimitIsKept();
    void cleanup() { QTest::qWait(10); }
};

void tst_Ed3kDownloads::aFileIsDownloadedVerifiedAndShared()
{
    const QByteArray data = payload(int(5 * kBlock + 4321), 1);
    Seeder a;
    QVERIFY(a.start(data, QStringLiteral("movie one.bin")));

    QTemporaryDir db;
    QVERIFY(db.isValid());
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("incoming")));
    std::optional<Ed3kDownloadInfo> hooked;
    d.setCompletedHook([&](const Ed3kDownloadInfo& i) { hooked = i; });
    d.start();
    QVERIFY(d.isStarted());
    QSignalSpy done(&d, &Ed3kDownloads::completed);

    Ed3kDownloadSpec spec;
    spec.root = a.root;
    spec.size = std::uint64_t(data.size());
    spec.name = QStringLiteral("movie one.bin");
    QVERIFY(d.add(spec).has_value());
    QVERIFY(!d.add(spec).has_value());   // once
    d.addSourceTo(a.root, sourceOf(a.session));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);

    const auto info = d.info(a.root);
    QVERIFY(info);
    QCOMPARE(info->state, Ed3kDownloadInfo::State::Complete);
    QCOMPARE(info->path, QDir(db.path()).filePath(QStringLiteral("incoming/movie one.bin")));
    QVERIFY(readFile(info->path) == data);
    QVERIFY(!QFile::exists(info->partPath));
    QVERIFY(hooked && hooked->path == info->path);
    // shared and kept for a restart
    QVERIFY(d.sharesRoot(a.root));
    d.stop();
    a.shared->stop();
}

// EMULE_TEST_OTHER_VOLUME: a writable folder on another volume than the temp dir
void tst_Ed3kDownloads::aCopyToAnotherVolumeRunsOnAWorker()
{
    const QString other = qEnvironmentVariable("EMULE_TEST_OTHER_VOLUME");
    if (other.isEmpty())
        QSKIP("EMULE_TEST_OTHER_VOLUME not set");
    QTemporaryDir incoming(QDir(other).filePath(QStringLiteral("ed3k-copy-XXXXXX")));
    QVERIFY(incoming.isValid());
    const QByteArray data = payload(int(40 * kBlock + 5), 9);
    Seeder a;
    QVERIFY(a.start(data, QStringLiteral("c.bin")));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), incoming.path());
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);

    Ed3kDownloadSpec spec;
    spec.root = a.root;
    spec.size = std::uint64_t(data.size());
    spec.name = QStringLiteral("c.bin");
    QVERIFY(d.add(spec).has_value());
    d.addSourceTo(a.root, sourceOf(a.session));
    bool sawCopying = false;
    QTimer poll;
    poll.setInterval(0);
    QObject::connect(&poll, &QTimer::timeout, &d, [&] {
        if (const auto i = d.info(a.root); i && i->copying)
            sawCopying = true;
    });
    poll.start();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    poll.stop();

    QVERIFY(sawCopying);
    const auto info = d.info(a.root);
    QVERIFY(info);
    QCOMPARE(info->state, Ed3kDownloadInfo::State::Complete);
    QCOMPARE(info->completionPermille, -1);
    QVERIFY(readFile(info->path) == data);
    QVERIFY(!QFile::exists(info->partPath));
    QVERIFY(!QFile::exists(info->path + QStringLiteral(".completing")));
    d.stop();
    a.shared->stop();
}

void tst_Ed3kDownloads::pauseAndResume()
{
    const QByteArray data = payload(int(3 * kBlock + 17), 2);
    Seeder a;
    QVERIFY(a.start(data, QStringLiteral("p.bin")));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);

    Ed3kDownloadSpec spec{a.root, std::uint64_t(data.size()), QStringLiteral("p.bin"), {}, {}, 0, true, 1};
    QVERIFY(d.add(spec).has_value());
    QCOMPARE(d.info(a.root)->state, Ed3kDownloadInfo::State::Paused);
    d.addSourceTo(a.root, sourceOf(a.session));   // no download open: nothing asked
    QTest::qWait(1500);
    QCOMPARE(done.count(), 0);
    QCOMPARE(d.info(a.root)->done, std::uint64_t(0));
    QVERIFY(!d.pause(a.root));                     // paused already
    QVERIFY(d.resume(a.root));
    QCOMPARE(d.info(a.root)->state, Ed3kDownloadInfo::State::Downloading);
    d.addSourceTo(a.root, sourceOf(a.session));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    QVERIFY(readFile(d.info(a.root)->path) == data);
    // a complete row only leaves the list
    QVERIFY(d.cancel(a.root));
    QVERIFY(!d.contains(a.root));
    QVERIFY(QFile::exists(QDir(db.path()).filePath(QStringLiteral("in/p.bin"))));
    d.stop();
}

void tst_Ed3kDownloads::aPartFileIsTakenUpAfterARestart()
{
    const QByteArray data = payload(int(12 * kBlock + 99), 3);
    Seeder a;
    QVERIFY(a.start(data, QStringLiteral("r.bin")));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    const QString temp = QDir(db.path()).filePath(QStringLiteral("temp"));
    const QString in = QDir(db.path()).filePath(QStringLiteral("in"));
    std::uint64_t held = 0;
    {
        Ed3kDownloads d(b, db.path());
        d.setSearchEnabled(false);
        d.setDirs(temp, in);
        d.start();
        // paused at the first block: a part file with some of it
        connect(&d, &Ed3kDownloads::progress, &d, [&d](const ::ed3k::Root& root, std::uint64_t) { d.pause(root); });
        QVERIFY(d.add(Ed3kDownloadSpec{a.root, std::uint64_t(data.size()), QStringLiteral("r.bin"), {}, {}, 0, false, 1}).has_value());
        d.addSourceTo(a.root, sourceOf(a.session));
        QTRY_VERIFY_WITH_TIMEOUT(d.info(a.root)->state == Ed3kDownloadInfo::State::Paused, 60000);
        held = d.info(a.root)->done;
        QVERIFY(held > 0 && held < std::uint64_t(data.size()));
        QCOMPARE(d.info(a.root)->transferred, held);   // Transferred column
        QVERIFY(QFile::exists(d.info(a.root)->partPath));
        d.stop();
    }
    QCOMPARE(QDir(temp).entryList({QStringLiteral("*.ed3k.part.met")}, QDir::Files).size(), 1);
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(temp, in);
    const auto loaded = d.info(a.root);
    QVERIFY(loaded);
    QCOMPARE(loaded->state, Ed3kDownloadInfo::State::Paused);
    QCOMPARE(loaded->done, held);
    QCOMPARE(loaded->transferred, held);   // kept in the .met
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    QSignalSpy progress(&d, &Ed3kDownloads::progress);
    QVERIFY(d.resume(a.root));
    d.addSourceTo(a.root, sourceOf(a.session));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    // only the missing blocks came again
    QVERIFY(std::uint64_t(progress.count()) < (std::uint64_t(data.size()) + kBlock - 1) / kBlock);
    QVERIFY(readFile(d.info(a.root)->path) == data);
    QCOMPARE(d.info(a.root)->transferred, std::uint64_t(data.size()));   // both runs
    d.stop();
}

// a whole HTTP read kept over a restart (wholeAt): the block values of a peer check its bytes,
// matching blocks are held, a wrong one comes again
void tst_Ed3kDownloads::uncheckedHttpBytesAreCheckedByPeerValues()
{
    const QByteArray data = payload(int(12 * kBlock + 99), 11);
    const std::uint64_t blocks = (std::uint64_t(data.size()) + kBlock - 1) / kBlock;
    Seeder a;
    QVERIFY(a.start(data, QStringLiteral("w.bin")));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    const QString temp = QDir(db.path()).filePath(QStringLiteral("temp"));
    const QString in = QDir(db.path()).filePath(QStringLiteral("in"));
    QString part;
    {
        Ed3kDownloads d(b, db.path());
        d.setSearchEnabled(false);
        d.setDirs(temp, in);
        d.start();
        QVERIFY(d.add(Ed3kDownloadSpec{a.root, std::uint64_t(data.size()), QStringLiteral("w.bin"), {}, {}, 0, true, 1}).has_value());
        part = d.info(a.root)->partPath;
        d.stop();
    }
    // 8 blocks read whole, block 3 wrong
    constexpr std::uint64_t kUnchecked = 8 * kBlock;
    QByteArray prefix = data.left(qsizetype(kUnchecked));
    prefix[qsizetype(3 * kBlock + 5)] = char(prefix.at(qsizetype(3 * kBlock + 5)) ^ 0x5A);
    QDir().mkpath(temp);
    QVERIFY(writeFile(part, prefix));
    // wholeAt, then the source limit (quint32) and transferred (quint64) end the .met
    const QString met = part + QStringLiteral(".met");
    QTRY_VERIFY(QFile::exists(met));
    QByteArray raw = readFile(met);
    QVERIFY(raw.size() > 20);
    QByteArray tail;
    QDataStream(&tail, QIODevice::WriteOnly) << quint64(kUnchecked);
    raw.replace(raw.size() - 20, 8, tail);
    QVERIFY(writeFile(met, raw));

    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(temp, in);
    QCOMPARE(d.info(a.root)->done, std::uint64_t(0));   // not held before the check
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    QSignalSpy progress(&d, &Ed3kDownloads::progress);
    QVERIFY(d.resume(a.root));
    d.addSourceTo(a.root, sourceOf(a.session));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    QVERIFY(readFile(d.info(a.root)->path) == data);
    // one signal per block from the peer, one for the 7 checked ones
    QVERIFY2(std::uint64_t(progress.count()) < blocks, qPrintable(QString::number(progress.count())));
    d.stop();
}

// #187: "Move to" a paused download; it stays paused, its .met follows, it completes from there
void tst_Ed3kDownloads::aMovedPartFileGoesOnInItsNewFolder()
{
    const QByteArray data = payload(int(10 * kBlock + 3), 9);
    Seeder a;
    QVERIFY(a.start(data, QStringLiteral("m.bin")));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    const QString temp = QDir(db.path()).filePath(QStringLiteral("temp"));
    const QString other = QDir(db.path()).filePath(QStringLiteral("other"));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs({temp}, QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    bool once = true;
    connect(&d, &Ed3kDownloads::progress, &d, [&d, &once](const ::ed3k::Root& root, std::uint64_t) {
        if (std::exchange(once, false))
            d.pause(root);
    });
    QVERIFY(d.add(Ed3kDownloadSpec{a.root, std::uint64_t(data.size()), QStringLiteral("m.bin"), {}, {}, 0, false, 1}).has_value());
    d.addSourceTo(a.root, sourceOf(a.session));
    QTRY_VERIFY_WITH_TIMEOUT(d.info(a.root)->state == Ed3kDownloadInfo::State::Paused, 60000);
    const QString oldPart = d.info(a.root)->partPath;

    QCOMPARE(d.moveTo(a.root, other), QString());
    QTRY_VERIFY(d.info(a.root)->partPath.startsWith(other + u'/'));
    QCOMPARE(d.info(a.root)->state, Ed3kDownloadInfo::State::Paused);   // a pause stays
    QVERIFY(!QFile::exists(oldPart));
    QVERIFY(QFile::exists(d.info(a.root)->partPath));
    QTRY_VERIFY(QFile::exists(d.info(a.root)->partPath + QStringLiteral(".met")));
    QVERIFY(!QFile::exists(oldPart + QStringLiteral(".met")));

    QSignalSpy done(&d, &Ed3kDownloads::completed);
    QVERIFY(d.resume(a.root));
    d.addSourceTo(a.root, sourceOf(a.session));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    QVERIFY(readFile(d.info(a.root)->path) == data);
    d.stop();
}

void tst_Ed3kDownloads::aCopiedPartFileGoesOnElsewhere()
{
    // the .ed3k.part + .ed3k.part.met of one profile copied into the temp dir of another
    const QByteArray data = payload(int(10 * kBlock + 7), 5);
    Seeder a;
    QVERIFY(a.start(data, QStringLiteral("c.bin")));
    QTemporaryDir first;
    QString part;
    std::uint64_t held = 0;
    {
        Ed3kSession b;
        QVERIFY(b.start(loopback(first.path())));
        Ed3kDownloads d(b, first.path());
        d.setSearchEnabled(false);
        d.setDirs(QDir(first.path()).filePath(QStringLiteral("temp")), QDir(first.path()).filePath(QStringLiteral("in")));
        d.start();
        connect(&d, &Ed3kDownloads::progress, &d, [&d](const ::ed3k::Root& root, std::uint64_t) { d.pause(root); });
        QVERIFY(d.add(Ed3kDownloadSpec{a.root, std::uint64_t(data.size()), QStringLiteral("c.bin"), {}, {}, 0, false, 1}).has_value());
        d.addSourceTo(a.root, sourceOf(a.session));
        QTRY_VERIFY_WITH_TIMEOUT(d.info(a.root)->state == Ed3kDownloadInfo::State::Paused, 60000);
        held = d.info(a.root)->done;
        part = d.info(a.root)->partPath;
        d.stop();
    }
    QTemporaryDir second;
    const QString temp2 = QDir(second.path()).filePath(QStringLiteral("other temp"));
    const QString in2 = QDir(second.path()).filePath(QStringLiteral("in"));
    QVERIFY(QDir().mkpath(temp2));
    const QString name = QFileInfo(part).fileName();
    eMule::BackgroundFileWriter::instance().flush();   // saves are queued (report 43)
    QVERIFY(QFile::copy(part, QDir(temp2).filePath(name)));
    QVERIFY(QFile::copy(part + QStringLiteral(".met"), QDir(temp2).filePath(name + QStringLiteral(".met"))));

    Ed3kSession b;
    QVERIFY(b.start(loopback(second.path())));
    Ed3kDownloads d(b, second.path());
    d.setSearchEnabled(false);
    QVERIFY(!d.contains(a.root));
    // the second temp dir holds it
    d.setDirs(QStringList{QDir(second.path()).filePath(QStringLiteral("temp")), temp2}, in2);
    const auto loaded = d.info(a.root);
    QVERIFY(loaded);
    QCOMPARE(loaded->state, Ed3kDownloadInfo::State::Paused);
    QCOMPARE(loaded->done, held);
    QCOMPARE(loaded->name, QStringLiteral("c.bin"));
    QCOMPARE(loaded->partPath, QDir(temp2).filePath(name));
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    QVERIFY(d.resume(a.root));
    d.addSourceTo(a.root, sourceOf(a.session));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    QCOMPARE(QFileInfo(d.info(a.root)->path).absolutePath(), QFileInfo(in2).absoluteFilePath());
    QVERIFY(readFile(d.info(a.root)->path) == data);
    QVERIFY(!QFile::exists(QDir(temp2).filePath(name + QStringLiteral(".met"))));
    d.stop();
}

void tst_Ed3kDownloads::anOldStateFileMovesToMetFiles()
{
    // downloads.dat version 1 held the unfinished rows too
    QTemporaryDir db;
    const QString temp = QDir(db.path()).filePath(QStringLiteral("temp"));
    const QString in = QDir(db.path()).filePath(QStringLiteral("in"));
    QVERIFY(QDir().mkpath(temp));
    const QByteArray root(32, '\x5a');
    const QString part = QDir(temp).filePath(QStringLiteral("old.ed3k.part"));
    QByteArray raw;
    {
        QDataStream out(&raw, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << quint32(0x4533444c) << quint32(1) << qint32(5) << quint32(1);
        out << root << quint64(3 * kBlock) << QStringLiteral("old.bin") << QStringLiteral("dl") << qint32(0) << qint32(2)
            << qint32(1) << part << QDir(in).filePath(QStringLiteral("sub/old.bin")) << part << QByteArray(1, '\x01')
            << qint64(7) << qint64(0) << false << QString() << qint32(4);
    }
    QVERIFY(QDir().mkpath(stateDir(db.path())));
    QVERIFY(writeBlob(QDir(stateDir(db.path())).filePath(QStringLiteral("downloads.dat")), std::as_bytes(std::span{raw.constData(), std::size_t(raw.size())})));
    Ed3kSession b;
    {
        Ed3kDownloads d(b, db.path());
        d.setDirs(temp, in);
        eMule::BackgroundFileWriter::instance().flush();   // saves are queued (report 43)
        QVERIFY(QFile::exists(part + QStringLiteral(".met")));
    }
    // the .met alone carries it now
    QTemporaryDir other;
    const QString temp2 = QDir(other.path()).filePath(QStringLiteral("t"));
    const QString in2 = QDir(other.path()).filePath(QStringLiteral("i"));
    QVERIFY(QDir().mkpath(temp2));
    QVERIFY(QFile::copy(part + QStringLiteral(".met"), QDir(temp2).filePath(QStringLiteral("old.ed3k.part.met"))));
    Ed3kDownloads d(b, other.path());
    d.setDirs(temp2, in2);
    QCOMPARE(d.list().size(), std::size_t(1));
    const auto i = d.list().front();
    QCOMPARE(i.name, QStringLiteral("old.bin"));
    QCOMPARE(i.priority, 2);
    QCOMPARE(i.state, Ed3kDownloadInfo::State::Paused);
    QCOMPARE(i.done, kBlock);
    // the target under the incoming dir follows the incoming dir of this profile
    QCOMPARE(i.targetPath, QDir(in2).filePath(QStringLiteral("sub/old.bin")));
    QCOMPARE(i.partPath, QDir(temp2).filePath(QStringLiteral("old.ed3k.part")));
}

void tst_Ed3kDownloads::theIpcListsAndControls()
{
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("t")), QDir(db.path()).filePath(QStringLiteral("i")));
    d.start();
    Ed3kDownloadsBridge::setDownloads(&d);
    std::vector<Ipc::IpcMessage> replies;
    const auto reply = [&](const Ipc::IpcMessage& m) { replies.push_back(m); };

    ::ed3k::FileLink link;
    link.root.bytes.fill(0x5c);
    link.size = 1000;
    link.name = "linked file.txt";
    const auto text = ::ed3k::writeFileLink(link);
    QVERIFY(text.has_value());

    Ipc::IpcMessage add(Ipc::IpcMsgType::AddEd3kDownloadLink, 7);
    add.append(QString::fromStdString(*text));
    add.append(qint64(0));
    add.append(true);
    QVERIFY(Ed3kDownloadsBridge::handleRequest(add, reply));
    QVERIFY(replies.back().fieldBool(0));
    const QString rootHex = replies.back().fieldMap(1).value(QStringLiteral("root")).toString();
    QCOMPARE(rootHex, QStringLiteral("5c").repeated(32));

    // a torrent link stays the Torrents tab's; junk is refused
    Ipc::IpcMessage bad(Ipc::IpcMsgType::AddEd3kDownloadLink, 8);
    bad.append(QStringLiteral("ed3k:file?nothing"));
    QVERIFY(Ed3kDownloadsBridge::handleRequest(bad, reply));
    QVERIFY(!replies.back().fieldBool(0));
    QVERIFY(!replies.back().fieldMap(1).value(QStringLiteral("error")).toString().isEmpty());

    Ipc::IpcMessage list(Ipc::IpcMsgType::GetEd3kDownloads, 9);
    QVERIFY(Ed3kDownloadsBridge::handleRequest(list, reply));
    const QCborArray rows = replies.back().fieldMap(1).value(QStringLiteral("downloads")).toArray();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("linked file.txt"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("state")).toInteger(), 1);   // paused
    // the bar: one block, not held, as runs (BlockRuns.h); not running: nothing pending
    const QCborMap row = rows.at(0).toMap();
    QCOMPARE(row.value(QStringLiteral("blockSize")).toInteger(), qint64(kBlock));
    const auto blocks = decodeBlockRuns(row.value(QStringLiteral("blocks")).toByteArray(), 1);
    QVERIFY(blocks && *blocks == std::vector<bool>{false});
    QVERIFY(!row.contains(QStringLiteral("pending")));
    QVERIFY(!row.contains(QStringLiteral("avail")));

    Ipc::IpcMessage prio(Ipc::IpcMsgType::Ed3kDownloadControl, 10);
    prio.append(rootHex);
    prio.append(qint64(3));
    prio.append(qint64(2));
    QVERIFY(Ed3kDownloadsBridge::handleRequest(prio, reply));
    QVERIFY(replies.back().fieldBool(0));
    QCOMPARE(d.list().front().priority, 2);

    Ipc::IpcMessage cancel(Ipc::IpcMsgType::Ed3kDownloadControl, 11);
    cancel.append(rootHex);
    cancel.append(qint64(2));
    QVERIFY(Ed3kDownloadsBridge::handleRequest(cancel, reply));
    QVERIFY(d.list().empty());
    // not ours
    QVERIFY(!Ed3kDownloadsBridge::handleRequest(Ipc::IpcMessage(Ipc::IpcMsgType::GetEd3kStatus, 12), reply));
    Ed3kDownloadsBridge::setDownloads(nullptr);
    d.stop();
}

// Ed3kDownloadControl 7 = eD2K Cancel/Delete: a complete file goes off disk with its row;
// 2 (Clear Completed) only drops the row; 7 on an unfinished one cancels it
void tst_Ed3kDownloads::cancelDeletesACompleteFile()
{
    const QByteArray one = payload(int(2 * kBlock + 5), 7);
    const QByteArray two = payload(int(kBlock + 9), 8);
    Seeder a;
    QVERIFY(a.startFiles({{QStringLiteral("gone.bin"), one}, {QStringLiteral("kept.bin"), two}}));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    Ed3kDownloadsBridge::setDownloads(&d);
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[0], std::uint64_t(one.size()), QStringLiteral("gone.bin")}).has_value());
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[1], std::uint64_t(two.size()), QStringLiteral("kept.bin")}).has_value());
    d.addSourceTo(a.roots[0], sourceOf(a.session));
    d.addSourceTo(a.roots[1], sourceOf(a.session));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 60000);
    const QString gone = d.info(a.roots[0])->path;
    const QString kept = d.info(a.roots[1])->path;
    QVERIFY(QFile::exists(gone) && QFile::exists(kept));

    std::vector<Ipc::IpcMessage> replies;
    const auto reply = [&](const Ipc::IpcMessage& m) { replies.push_back(m); };
    const auto control = [&](const ::ed3k::Root& root, qint64 action) {
        Ipc::IpcMessage m(Ipc::IpcMsgType::Ed3kDownloadControl, 20);
        m.append(QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(root.bytes.data()),
                                                qsizetype(root.bytes.size())).toHex()));
        m.append(action);
        return Ed3kDownloadsBridge::handleRequest(m, reply) && replies.back().fieldBool(0);
    };

    QVERIFY(control(a.roots[0], 7));   // a done cancel is no "unknown download"
    QVERIFY(!d.contains(a.roots[0]));
    QVERIFY(!d.sharesRoot(a.roots[0]));
    QVERIFY(!QFile::exists(gone));
    QVERIFY(control(a.roots[1], 2));
    QVERIFY(!d.contains(a.roots[1]));
    QVERIFY(QFile::exists(kept));
    QVERIFY(!control(a.roots[0], 7));   // gone already

    // unfinished: 7 = plain cancel, the incoming dir untouched
    ::ed3k::Root other;
    other.bytes.fill(0x3d);
    QVERIFY(d.add(Ed3kDownloadSpec{other, 1000, QStringLiteral("open.bin"), {}, {}, 0, true, 1}).has_value());
    QVERIFY(control(other, 7));
    QVERIFY(!d.contains(other));
    QVERIFY(QFile::exists(kept));
    Ed3kDownloadsBridge::setDownloads(nullptr);
    d.stop();
    a.shared->stop();
}

// Protocol.md §12.5: one file stream per uploader (a second FileRequest replaces the first).
// Three downloads of one source take turns at it (Ed3kJoinBase's turns), the waiting ones by
// priority (a higher one takes the node from a lower holder at once), each without a source
// search again.
void tst_Ed3kDownloads::downloadsOfOneSourceTakeTurns()
{
    const QByteArray one = payload(int(6 * kBlock + 11), 4);
    const QByteArray two = payload(int(5 * kBlock + 333), 5);
    const QByteArray three = payload(int(3 * kBlock + 7), 6);
    Seeder a;
    QVERIFY(a.startFiles({{QStringLiteral("one.bin"), one}, {QStringLiteral("two.bin"), two}, {QStringLiteral("three.bin"), three}}));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);   // no Kad search: a stalled download would stay so
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);

    // one first (holds the node), then two (low) waits and three (high) takes the node
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[0], std::uint64_t(one.size()), QStringLiteral("one.bin"), {}, {}, 0, false, 1}).has_value());
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[1], std::uint64_t(two.size()), QStringLiteral("two.bin"), {}, {}, 0, false, 0}).has_value());
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[2], std::uint64_t(three.size()), QStringLiteral("three.bin"), {}, {}, 0, false, 2}).has_value());
    for (const auto& root : a.roots)
        d.addSourceTo(root, sourceOf(a.session));
    // eD2K's A4AF: one download holds the node, the other two count it as waiting there;
    // the holder's source row names them
    int waiting = 0;
    for (const auto& root : a.roots)
        waiting += d.info(root)->a4af;
    QCOMPARE(waiting, 2);
    int named = 0;
    for (const auto& root : a.roots)
        for (const auto& s : Ed3kJoinBase::sourcesAt(b, root))
            if (!s.otherFile)
                named += int(s.otherWants.size());
    QCOMPARE(named, 2);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 3, 30000);
    // was: one, three, two (the holder kept the node; now a higher waiter takes it)
    QVERIFY(done.at(0).at(0).value<::ed3k::Root>() == a.roots[2]);
    QVERIFY(done.at(1).at(0).value<::ed3k::Root>() == a.roots[0]);
    QVERIFY(done.at(2).at(0).value<::ed3k::Root>() == a.roots[1]);
    QVERIFY(readFile(d.info(a.roots[0])->path) == one);
    QVERIFY(readFile(d.info(a.roots[1])->path) == two);
    QVERIFY(readFile(d.info(a.roots[2])->path) == three);
    QCOMPARE(d.sourceSearches(), std::size_t(0));
    d.stop();
    a.shared->stop();
}

// Download priority (Ed3kJoinBase): two downloads of one source set, the low one asked first;
// the high one takes the source and has more blocks over the run. While it runs its row tells
// the blocks held, those asked for and the source's pieces.
void tst_Ed3kDownloads::aHigherPriorityGetsMoreBlocks()
{
    const QByteArray low = payload(int(10 * kBlock + 5), 7);
    const QByteArray high = payload(int(6 * kBlock + 9), 8);
    Seeder a;
    QVERIFY(a.startFiles({{QStringLiteral("low.bin"), low}, {QStringLiteral("high.bin"), high}}));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    std::map<::ed3k::Root, int> blocks;
    std::optional<Ed3kDownloadInfo> running;
    int lowWhenHighHeld = -1;   // low's blocks once every block of high is here
    connect(&d, &Ed3kDownloads::progress, &d, [&](const ::ed3k::Root& root, std::uint64_t) {
        ++blocks[root];
        if (root == a.roots[1] && !running)
            running = d.info(root);
        if (root == a.roots[1] && blocks[root] == 7)
            lowWhenHighHeld = blocks[a.roots[0]];
    });

    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[0], std::uint64_t(low.size()), QStringLiteral("low.bin"), {}, {}, 0, false, 4}).has_value());
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[1], std::uint64_t(high.size()), QStringLiteral("high.bin"), {}, {}, 0, false, 3}).has_value());
    d.addSourceTo(a.roots[0], sourceOf(a.session));   // the very low one holds the node first
    d.addSourceTo(a.roots[1], sourceOf(a.session));
    QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 30000);
    QVERIFY(done.at(0).at(0).value<::ed3k::Root>() == a.roots[1]);
    // the low one (10 blocks + 1) would have had all its blocks first without priority
    QVERIFY2(lowWhenHighHeld >= 0 && lowWhenHighHeld < 7, qPrintable(QString::number(lowWhenHighHeld)));
    // the row of the running one: a block held, the source holds every piece (complete)
    QVERIFY(running);
    QCOMPARE(running->blocks.size(), std::size_t(7));
    QCOMPARE(std::ranges::count(running->blocks, true), 1);
    QCOMPARE(running->pending.size(), std::size_t(7));
    QCOMPARE(running->availability, std::vector<std::uint8_t>{1});
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 30000);
    QVERIFY(readFile(d.info(a.roots[0])->path) == low);
    QVERIFY(readFile(d.info(a.roots[1])->path) == high);
    d.stop();
    a.shared->stop();
}

// setPriority while two downloads share a source: the raised one takes the node at once.
void tst_Ed3kDownloads::aRaisedPriorityTakesTheSource()
{
    const QByteArray first = payload(int(8 * kBlock + 1), 9);
    const QByteArray second = payload(int(4 * kBlock + 2), 10);
    Seeder a;
    QVERIFY(a.startFiles({{QStringLiteral("first.bin"), first}, {QStringLiteral("second.bin"), second}}));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[0], std::uint64_t(first.size()), QStringLiteral("first.bin"), {}, {}, 0, false, 1}).has_value());
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[1], std::uint64_t(second.size()), QStringLiteral("second.bin"), {}, {}, 0, false, 1}).has_value());
    d.addSourceTo(a.roots[0], sourceOf(a.session));
    d.addSourceTo(a.roots[1], sourceOf(a.session));   // waits: the same priority
    QVERIFY(d.setPriority(a.roots[1], 3));             // very high: takes the node
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 30000);
    QVERIFY(done.at(0).at(0).value<::ed3k::Root>() == a.roots[1]);
    QVERIFY(readFile(d.info(a.roots[0])->path) == first);
    d.stop();
    a.shared->stop();
}

// Protocol.md §12.5: while queued, a switch to another file counts in the uploader's 30 min
// budget (a 2nd one is BUSY, the uploader keeps the old stream): queued, one per 30 min; with
// a slot a switch keeps the turn and is always allowed.
void tst_Ed3kDownloads::queuedPrioritySwitchesWaitThirtyMinutes()
{
    using namespace std::chrono;
    const auto now = steady_clock::now();
    QVERIFY(Ed3kJoinBase::switchAllowed(true, std::nullopt, now));
    QVERIFY(!Ed3kJoinBase::switchAllowed(true, now - minutes(29), now));
    QVERIFY(Ed3kJoinBase::switchAllowed(true, now - minutes(30), now));
    QVERIFY(Ed3kJoinBase::switchAllowed(false, now - seconds(1), now));
}

// hops.md §1: a set's member goes under the set's key, never under both keys
// a root the share list serves: refused on add, an unfinished row of it dropped (part file
// gone), a complete one kept
// declined-records.md §2: a root or eD2K hash of decline.txt: added paused, the user told
void tst_Ed3kDownloads::aDeclinedRootIsAddedPaused()
{
    QTemporaryDir db;
    QVERIFY(db.isValid());
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    ::ed3k::Root listed, byMd4, other, askedPaused;
    listed.bytes.fill(0x31);
    byMd4.bytes.fill(0x32);
    other.bytes.fill(0x33);
    askedPaused.bytes.fill(0x34);
    const QByteArray md4(16, char(0x5a));
    DeclineList list;
    list.roots = {listed, askedPaused};
    list.ed2k.push_back({});
    std::copy(md4.cbegin(), md4.cend(), list.ed2k.front().begin());
    b.setDeclined(list);

    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    QSignalSpy told(&d, &Ed3kDownloads::declinedAdded);

    QVERIFY(d.add(Ed3kDownloadSpec{listed, 1000, QStringLiteral("listed.bin"), {}, {}, 0, false, 1}).has_value());
    QCOMPARE(d.info(listed)->state, Ed3kDownloadInfo::State::Paused);
    QCOMPARE(told.count(), 1);
    QCOMPARE(told.at(0).at(1).toString(), QStringLiteral("listed.bin"));

    Ed3kDownloadSpec spec{byMd4, 1000, QStringLiteral("md4.bin"), {}, {}, 0, false, 1};
    spec.md4 = md4;
    QVERIFY(d.add(spec).has_value());
    QCOMPARE(d.info(byMd4)->state, Ed3kDownloadInfo::State::Paused);
    QCOMPARE(told.count(), 2);

    // not listed: runs; asked paused: nothing to tell
    QVERIFY(d.add(Ed3kDownloadSpec{other, 1000, QStringLiteral("other.bin"), {}, {}, 0, false, 1}).has_value());
    QCOMPARE(d.info(other)->state, Ed3kDownloadInfo::State::Downloading);
    QVERIFY(d.add(Ed3kDownloadSpec{askedPaused, 1000, QStringLiteral("p.bin"), {}, {}, 0, true, 1}).has_value());
    QCOMPARE(told.count(), 2);
    // resumed by hand: downloads anyway
    QVERIFY(d.resume(listed));
    QCOMPARE(d.info(listed)->state, Ed3kDownloadInfo::State::Downloading);
    d.stop();
    b.stop();
}

void tst_Ed3kDownloads::aSharedFileIsNotDownloadedAgain()
{
    ::ed3k::Root root;   // any root: nothing is fetched
    root.bytes.fill(0x3c);
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    bool shared = false;
    d.setSharedElsewhere([&](const ::ed3k::Root& r) { return shared && r == root; });
    const Ed3kDownloadSpec spec{root, 3 * kBlock + 5, QStringLiteral("s.bin"), {}, {}, 0, true, 1};

    QVERIFY(d.add(spec).has_value());   // not shared yet
    const QString part = d.info(root)->partPath;
    QVERIFY(!d.releaseShare(root));     // nothing shares it: kept
    QVERIFY(d.contains(root));

    shared = true;
    QVERIFY(d.releaseShare(root));      // the share list took it: the duplicate goes
    QVERIFY(!d.contains(root));
    QVERIFY(!QFile::exists(part));
    QVERIFY(!QFile::exists(part + QStringLiteral(".met")));
    QVERIFY(!d.add(spec).has_value());  // and comes no more
    QVERIFY(!d.contains(root));
    d.stop();
}

// #224: the root is in Shared Files of the same node: its blocks are read from that file, no
// source is asked; the whole file is hashed at the end as for a peer's blocks
void tst_Ed3kDownloads::aLocalCopyIsReadFirst()
{
    const QByteArray data = payload(int(4 * kBlock + 999), 21);
    QTemporaryDir db;
    QVERIFY(db.isValid());
    const QString path = QDir(db.path()).filePath(QStringLiteral("have.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    FakeSource src;
    const QByteArray md4(16, char(0x61));
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("have.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(b, src, db.path());
    QSignalSpy sharedSpy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_VERIFY_WITH_TIMEOUT(!sharedSpy.isEmpty(), 20000);
    const auto root = shared.rootOf(md4);
    QVERIFY(root);

    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    Ed3kDownloadSpec spec;
    spec.root = *root;
    spec.size = std::uint64_t(data.size());
    spec.name = QStringLiteral("copy.bin");
    QVERIFY(d.add(spec).has_value());
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
    const auto info = d.info(*root);
    QVERIFY(info);
    QCOMPARE(info->state, Ed3kDownloadInfo::State::Complete);
    QCOMPARE(readFile(info->path), data);
    QCOMPARE(readFile(path), data);   // the shared file is untouched
    d.stop();
    shared.stop();
}

// #224 folder scan: a file found on disk (addFoundCopy) under the root of a download that has
// no source: it is read from there now, verified at the end
void tst_Ed3kDownloads::aFoundCopyFillsAnOpenDownload()
{
    const QByteArray data = payload(int(3 * kBlock + 123), 22);
    ::ed3k::Hasher h;
    QVERIFY(h.update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())}).has_value());
    const auto hashed = h.finish();
    QVERIFY(hashed.has_value());
    QTemporaryDir db;
    QVERIFY(db.isValid());
    const QString path = QDir(db.path()).filePath(QStringLiteral("elsewhere/found.bin"));
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QVERIFY(writeFile(path, data));
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    const Ed3kDownloadSpec spec{hashed->root, std::uint64_t(data.size()), QStringLiteral("found.bin"), {}, {}, 0, false, 1};
    QVERIFY(d.add(spec).has_value());
    // what a scan looks for
    const auto wanted = Ed3kJoinBase::wantedFiles(b);
    QVERIFY(std::ranges::find(wanted, std::pair{hashed->root, std::uint64_t(data.size())}) != wanted.end());
    QTest::qWait(300);
    QCOMPARE(done.count(), 0);

    Ed3kJoinBase::addFoundCopy(b, hashed->root, path, std::uint64_t(data.size()), QFileInfo(path).lastModified().toMSecsSinceEpoch());
    QCOMPARE(Ed3kJoinBase::foundCopies(b), 1);
    QCOMPARE(Ed3kJoinBase::localCopyPath(b, hashed->root, std::uint64_t(data.size())), path);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
    QCOMPARE(readFile(d.info(hashed->root)->path), data);
    d.stop();
}

// #224: a found copy changed since it was hashed (its date): not read, the download waits for
// the network
void tst_Ed3kDownloads::aChangedFoundCopyIsNotRead()
{
    const QByteArray data = payload(int(2 * kBlock + 7), 23);
    ::ed3k::Hasher h;
    QVERIFY(h.update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())}).has_value());
    const auto hashed = h.finish();
    QVERIFY(hashed.has_value());
    QTemporaryDir db;
    QVERIFY(db.isValid());
    const QString path = QDir(db.path()).filePath(QStringLiteral("found.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    const Ed3kDownloadSpec spec{hashed->root, std::uint64_t(data.size()), QStringLiteral("found.bin"), {}, {}, 0, false, 1};
    QVERIFY(d.add(spec).has_value());
    const qint64 wrongDate = QFileInfo(path).lastModified().toMSecsSinceEpoch() - 60'000;
    Ed3kJoinBase::addFoundCopy(b, hashed->root, path, std::uint64_t(data.size()), wrongDate);
    QTest::qWait(1000);
    const auto info = d.info(hashed->root);
    QVERIFY(info);
    QCOMPARE(info->state, Ed3kDownloadInfo::State::Downloading);
    QCOMPARE(info->done, std::uint64_t(0));
    d.stop();
}

// #224 folder scan (BiglyBT's Locate Files): only files of a wanted size are hashed; a decoy
// of that size is hashed and left, unfinished downloads' files are not looked at, the root
// found once is not looked for again
void tst_Ed3kDownloads::aFolderScanHashesOnlyFilesOfAWantedSize()
{
    const QByteArray data = payload(int(kBlock + 5), 31);
    ::ed3k::Hasher h;
    QVERIFY(h.update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())}).has_value());
    const auto hashed = h.finish();
    QVERIFY(hashed.has_value());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir d(dir.path());
    QVERIFY(d.mkpath(QStringLiteral("a/deep")) && d.mkpath(QStringLiteral("b")));
    QVERIFY(writeFile(d.filePath(QStringLiteral("a/deep/movie.mkv")), data));
    QVERIFY(writeFile(d.filePath(QStringLiteral("a/deep/copy.mkv")), data));              // the root once only
    QVERIFY(writeFile(d.filePath(QStringLiteral("b/decoy.bin")), payload(data.size(), 32)));   // same size
    QVERIFY(writeFile(d.filePath(QStringLiteral("b/small.bin")), payload(100, 33)));
    QVERIFY(writeFile(d.filePath(QStringLiteral("b/movie.mkv.part")), data));              // unfinished: skipped

    std::vector<std::pair<::ed3k::Root, QString>> found;
    std::atomic<bool> cancel{false};
    const auto r = Ed3kLocalScan::scan({dir.path()}, {{hashed->root, std::uint64_t(data.size())}}, cancel,
                                       [&](const ::ed3k::Root& root, const QString& path, std::uint64_t, qint64) {
                                           found.emplace_back(root, path);
                                       });
    QCOMPARE(r.files, 4);
    QCOMPARE(r.candidates, 3);
    QCOMPARE(r.matched, 1);
    // the folder with two candidates first: its first file matches, the rest of that size
    // is not hashed any more
    QCOMPARE(r.hashed, 1);
    QCOMPARE(found.size(), std::size_t(1));
    QCOMPARE(found[0].first, hashed->root);
    QVERIFY(found[0].second.contains(QStringLiteral("/a/deep/")));

    // cancelled: stops before hashing
    cancel = true;
    const auto none = Ed3kLocalScan::scan({dir.path()}, {{hashed->root, std::uint64_t(data.size())}}, cancel, {});
    QVERIFY(none.cancelled);
    QCOMPARE(none.matched, 0);
}

// #224: Ed3kLocalScan on a node: the root of an open download with no source found in a folder,
// the download completes from there
void tst_Ed3kDownloads::aFolderScanFillsTheDownloads()
{
    const QByteArray data = payload(int(3 * kBlock + 77), 34);
    ::ed3k::Hasher h;
    QVERIFY(h.update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())}).has_value());
    const auto hashed = h.finish();
    QVERIFY(hashed.has_value());
    QTemporaryDir db, folder;
    QVERIFY(db.isValid() && folder.isValid());
    QVERIFY(writeFile(QDir(folder.path()).filePath(QStringLiteral("old name.bin")), data));
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    const Ed3kDownloadSpec spec{hashed->root, std::uint64_t(data.size()), QStringLiteral("new.bin"), {}, {}, 0, false, 1};
    QVERIFY(d.add(spec).has_value());

    Ed3kLocalScan scan(b);
    QSignalSpy finished(&scan, &Ed3kLocalScan::finished);
    QVERIFY(scan.start({folder.path()}));
    QVERIFY(scan.running());
    QVERIFY(!scan.start({folder.path()}));   // one at a time
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    QCOMPARE(finished.at(0).at(0).value<Ed3kLocalScan::Result>().matched, 1);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
    QCOMPARE(readFile(d.info(hashed->root)->path), data);

    // nothing left to look for: done at once
    QVERIFY(!scan.start({folder.path()}));
    QCOMPARE(finished.count(), 2);
    d.stop();
}

void tst_Ed3kDownloads::aDownloadGoesUnderTheKeyItIsGiven()
{
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    std::vector<std::pair<::ed3k::Root, QByteArray>> anonAdded;
    d.setAnonAddedHook([&](const ::ed3k::Root& r, const QByteArray& md4, const QString&) { anonAdded.emplace_back(r, md4); });
    const auto rootOf = [](std::uint8_t n) {
        ::ed3k::Root r{};
        r.bytes.fill(n);
        return r;
    };
    Ed3kDownloadSpec spec;
    spec.size = 1000;
    spec.paused = true;
    // hiding off: nothing under the anonymous key
    spec.root = rootOf(0x51);
    spec.name = QStringLiteral("a.bin");
    spec.key = ::ed3k::KeyKind::Anon;
    QVERIFY(!d.add(spec));
    QVERIFY(!d.contains(spec.root));
    // the node key: recorded
    spec.key = ::ed3k::KeyKind::Node;
    QVERIFY(d.add(spec));
    QCOMPARE(b.hide().recorded(spec.root), std::optional(::ed3k::KeyKind::Node));
    QVERIFY(anonAdded.empty());
    // hiding on: a root recorded under the other key is refused
    b.setHideSettings(Ed3kHideSettings{true, 1, 25});
    QVERIFY(b.hide().hiding());
    spec.root = rootOf(0x52);
    spec.name = QStringLiteral("b.bin");
    b.hide().record(spec.root, ::ed3k::KeyKind::Node);
    spec.key = ::ed3k::KeyKind::Anon;
    QVERIFY(!d.add(spec));
    // a new one under the anonymous key while hiding (the set's, not the switch's)
    spec.root = rootOf(0x53);
    spec.name = QStringLiteral("c.bin");
    spec.md4 = QByteArray(16, char(0x7e));
    QVERIFY(d.add(spec));
    QCOMPARE(b.hide().recorded(spec.root), std::optional(::ed3k::KeyKind::Anon));
    QCOMPARE(d.keyOfRoot(spec.root), ::ed3k::KeyKind::Anon);
    // the exposure check is asked with the link's MD4 (hops.md §1 SHOULD)
    QCOMPARE(anonAdded.size(), std::size_t(1));
    QCOMPARE(anonAdded.front().second, spec.md4);
    // a node-key set member while hiding: the node key, not the switch's
    spec.root = rootOf(0x54);
    spec.name = QStringLiteral("d.bin");
    spec.key = ::ed3k::KeyKind::Node;
    QVERIFY(d.add(spec));
    QCOMPARE(d.keyOfRoot(spec.root), ::ed3k::KeyKind::Node);
    QCOMPARE(anonAdded.size(), std::size_t(1));
    d.stop();
    b.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kDownloads)
void tst_Ed3kDownloads::previewPartsComeFirst()
{
    // a film of three eD2K parts: the first and last part before the middle one
    const QByteArray data = payload(int(3 * PARTSIZE - 1000), 7);
    Seeder a;
    QVERIFY(a.start(data, QStringLiteral("film.avi")));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    const QString temp = QDir(db.path()).filePath(QStringLiteral("temp"));
    const QString in = QDir(db.path()).filePath(QStringLiteral("in"));
    {
        Ed3kDownloads d(b, db.path());
        d.setSearchEnabled(false);
        d.setDirs(temp, in);
        d.start();
        QVERIFY(d.add(Ed3kDownloadSpec{a.root, std::uint64_t(data.size()), QStringLiteral("film.avi"), {}, {}, 0, true, 1}).has_value());
        QVERIFY(d.setPreviewPrio(a.root, true));
        d.stop();
    }
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(temp, in);
    QVERIFY(d.info(a.root));
    QVERIFY(d.info(a.root)->previewPrio);   // kept in the .met
    d.start();
    const std::uint64_t tail = 2 * PARTSIZE;
    const auto preview = [&](std::size_t b) { return b * kBlock < PARTSIZE || b * kBlock >= tail; };
    bool middleFirst = false;
    bool middleSeen = false;
    bool previewSeen = false;
    bool previewEarly = false;   // possible before the first part was here
    QVERIFY(!d.info(a.root)->previewPossible);
    connect(&d, &Ed3kDownloads::progress, &d, [&](const ::ed3k::Root& root, std::uint64_t) {
        const auto i = d.info(root);
        if (i && i->previewPossible) {
            previewSeen = true;
            for (std::size_t b = 0; b * kBlock < PARTSIZE; ++b)
                previewEarly = previewEarly || !i->blocks[b];
        }
        if (!i || middleSeen)
            return;
        for (std::size_t b = 0; b < i->blocks.size(); ++b)
            if (i->blocks[b] && !preview(b))
                middleSeen = true;
        if (!middleSeen)
            return;
        // the first middle block: every preview block is here or asked for
        for (std::size_t b = 0; b < i->blocks.size(); ++b)
            if (preview(b) && !i->blocks[b] && !(b < i->pending.size() && i->pending[b]))
                middleFirst = true;
    });
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    QVERIFY(d.resume(a.root));
    d.addSourceTo(a.root, sourceOf(a.session));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 120000);
    QVERIFY(middleSeen);
    QVERIFY(!middleFirst);
    QVERIFY(previewSeen);    // a video with its first part: Preview on
    QVERIFY(!previewEarly);
    QVERIFY(!d.info(a.root)->previewPossible);   // complete: Open File instead
    d.stop();
}

// Transfers menu on an ed3k row: Mass Rename (the name to be, then the file on disk) and
// Add Sources with a URL (as ws=)
void tst_Ed3kDownloads::renameAndAUrlSourceFromTheMenu()
{
    const QByteArray data = payload(int(2 * kBlock + 5), 4);
    Seeder a;
    QVERIFY(a.start(data, QStringLiteral("r.bin")));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    QVERIFY(d.add(Ed3kDownloadSpec{a.root, std::uint64_t(data.size()), QStringLiteral("r.bin"), {}, {}, 0, true, 1}).has_value());
    QVERIFY(d.rename(a.root, QStringLiteral("renamed.bin")).has_value());
    QCOMPARE(d.info(a.root)->name, QStringLiteral("renamed.bin"));
    QVERIFY(!d.rename(a.root, QString()).has_value());
    QVERIFY(!d.addWebSource(a.root, QStringLiteral("ftp://192.0.2.1/r.bin")));
    QVERIFY(d.addWebSource(a.root, QStringLiteral("http://192.0.2.1/r.bin")));
    QVERIFY(d.resume(a.root));
    d.addSourceTo(a.root, sourceOf(a.session));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    const QString in = QDir(db.path()).filePath(QStringLiteral("in"));
    QCOMPARE(d.info(a.root)->path, QDir(in).filePath(QStringLiteral("renamed.bin")));
    QVERIFY(!d.addWebSource(a.root, QStringLiteral("http://192.0.2.1/r.bin")));   // complete
    // complete: the file on disk
    QVERIFY(d.rename(a.root, QStringLiteral("final.bin")).has_value());
    QVERIFY(QFile::exists(QDir(in).filePath(QStringLiteral("final.bin"))));
    QVERIFY(readFile(d.info(a.root)->path) == data);
    d.stop();
}

// eD2K's "Swap All A4AF Sources to This File": three files of one source, the same priority;
// the last asked goes first once swapped (else arrival order: three before two).
void tst_Ed3kDownloads::swapA4afPutsAFileFirstAtItsSource()
{
    const QByteArray one = payload(int(6 * kBlock + 11), 21);
    const QByteArray two = payload(int(5 * kBlock + 333), 22);
    const QByteArray three = payload(int(3 * kBlock + 7), 23);
    Seeder a;
    QVERIFY(a.startFiles({{QStringLiteral("one.bin"), one}, {QStringLiteral("two.bin"), two}, {QStringLiteral("three.bin"), three}}));
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(db.path()).filePath(QStringLiteral("temp")), QDir(db.path()).filePath(QStringLiteral("in")));
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[0], std::uint64_t(one.size()), QStringLiteral("one.bin"), {}, {}, 0, false, 1}).has_value());
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[1], std::uint64_t(two.size()), QStringLiteral("two.bin"), {}, {}, 0, false, 1}).has_value());
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[2], std::uint64_t(three.size()), QStringLiteral("three.bin"), {}, {}, 0, false, 1}).has_value());
    d.addSourceTo(a.roots[0], sourceOf(a.session));   // holds the node
    d.addSourceTo(a.roots[2], sourceOf(a.session));   // waits first
    d.addSourceTo(a.roots[1], sourceOf(a.session));   // waits second
    QCOMPARE(d.info(a.roots[1])->a4af, 1);
    QCOMPARE(d.swapA4af(a.roots[1]), std::optional<int>(1));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 3, 30000);
    std::vector<::ed3k::Root> order;
    for (const auto& c : done)
        order.push_back(c.at(0).value<::ed3k::Root>());
    const auto at = [&](const ::ed3k::Root& r) { return std::ranges::find(order, r) - order.begin(); };
    QVERIFY(at(a.roots[1]) < at(a.roots[2]));
    QVERIFY(readFile(d.info(a.roots[1])->path) == two);
    QVERIFY(!d.swapA4af(a.roots[1]));   // complete: nothing to swap
    d.stop();
    a.shared->stop();
}

// Source Limit... (eD2K's per-file limit, 0: by priority): clamped to 65535, on the row, set
// by the IPC route, kept in the .met; an unknown root refused.
void tst_Ed3kDownloads::aSourceLimitIsKept()
{
    ::ed3k::Root root;
    root.bytes.fill(0x4d);
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    const QString temp = QDir(db.path()).filePath(QStringLiteral("temp"));
    const QString in = QDir(db.path()).filePath(QStringLiteral("in"));
    {
        Ed3kDownloads d(b, db.path());
        d.setSearchEnabled(false);
        d.setDirs(temp, in);
        d.start();
        QVERIFY(d.add(Ed3kDownloadSpec{root, 3 * kBlock + 5, QStringLiteral("l.bin"), {}, {}, 0, true, 1}).has_value());
        QCOMPARE(d.info(root)->maxSources, 0);
        QVERIFY(d.setSourceLimit(root, 70000));
        QCOMPARE(d.info(root)->maxSources, 65535);
        QVERIFY(d.setSourceLimit(root, 2));
        // the row the GUI shows as "[2]", and the IPC route by the 64 hex root
        Ed3kDownloadsBridge::setDownloads(&d);
        std::vector<Ipc::IpcMessage> replies;
        const auto reply = [&](const Ipc::IpcMessage& m) { replies.push_back(m); };
        QVERIFY(Ed3kDownloadsBridge::handleRequest(Ipc::IpcMessage(Ipc::IpcMsgType::GetEd3kDownloads, 1), reply));
        const QCborArray rows = replies.back().fieldMap(1).value(QStringLiteral("downloads")).toArray();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("maxSources")).toInteger(), 2);
        QVERIFY(Ed3kDownloadsBridge::setSourceLimit(QString::fromLatin1(QByteArray(32, char(0x4d)).toHex()), 3));
        QCOMPARE(d.info(root)->maxSources, 3);
        QVERIFY(d.setSourceLimit(root, 2));
        Ed3kDownloadsBridge::setDownloads(nullptr);
        ::ed3k::Root none;
        none.bytes.fill(0x4e);
        QVERIFY(!d.setSourceLimit(none, 3));
        d.stop();
    }
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(temp, in);
    QVERIFY(d.info(root));
    QCOMPARE(d.info(root)->maxSources, 2);
    d.stop();
}

#include "tst_Ed3kDownloads.moc"
