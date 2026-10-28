/// @file tst_Ed3kDownloads.cpp
/// @brief Plain ed3k downloads (src/ed3k/Ed3kDownloads) and their IPC (Ed3kDownloadsBridge):
/// node A shares a file of a fake share list, node B downloads it by its root into a part
/// file, verifies, moves and shares it; pause/resume; a part file taken up after a restart.

#include "Ed3kDownloads.h"
#include "Ed3kDownloadsBridge.h"
#include "Ed3kSession.h"
#include "Ed3kShared.h"
#include "Ed3kStore.h"

#include "IpcMessage.h"

#include <QCborArray>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
    void aPartFileIsTakenUpAfterARestart();
    void theIpcListsAndControls();
    void downloadsOfOneSourceTakeTurns();
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
        QVERIFY(QFile::exists(d.info(a.root)->partPath));
        d.stop();
    }
    QVERIFY(QFile::exists(QDir(stateDir(db.path())).filePath(QStringLiteral("downloads.dat"))));
    Ed3kDownloads d(b, db.path());
    d.setSearchEnabled(false);
    d.setDirs(temp, in);
    const auto loaded = d.info(a.root);
    QVERIFY(loaded);
    QCOMPARE(loaded->state, Ed3kDownloadInfo::State::Paused);
    QCOMPARE(loaded->done, held);
    d.start();
    QSignalSpy done(&d, &Ed3kDownloads::completed);
    QSignalSpy progress(&d, &Ed3kDownloads::progress);
    QVERIFY(d.resume(a.root));
    d.addSourceTo(a.root, sourceOf(a.session));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    // only the missing blocks came again
    QVERIFY(std::uint64_t(progress.count()) < (std::uint64_t(data.size()) + kBlock - 1) / kBlock);
    QVERIFY(readFile(d.info(a.root)->path) == data);
    d.stop();
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

// Protocol.md §12.5: one file stream per uploader (a second FileRequest replaces the first).
// Three downloads of one source take turns at it (Ed3kJoinBase's turns), the waiting ones by
// priority, each without a source search again.
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

    // one first (holds the node), then two (low) and three (high) wait
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[0], std::uint64_t(one.size()), QStringLiteral("one.bin"), {}, {}, 0, false, 1}).has_value());
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[1], std::uint64_t(two.size()), QStringLiteral("two.bin"), {}, {}, 0, false, 0}).has_value());
    QVERIFY(d.add(Ed3kDownloadSpec{a.roots[2], std::uint64_t(three.size()), QStringLiteral("three.bin"), {}, {}, 0, false, 2}).has_value());
    for (const auto& root : a.roots)
        d.addSourceTo(root, sourceOf(a.session));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 3, 30000);
    QVERIFY(done.at(0).at(0).value<::ed3k::Root>() == a.roots[0]);
    QVERIFY(done.at(1).at(0).value<::ed3k::Root>() == a.roots[2]);
    QVERIFY(done.at(2).at(0).value<::ed3k::Root>() == a.roots[1]);
    QVERIFY(readFile(d.info(a.roots[0])->path) == one);
    QVERIFY(readFile(d.info(a.roots[1])->path) == two);
    QVERIFY(readFile(d.info(a.roots[2])->path) == three);
    QCOMPARE(d.sourceSearches(), std::size_t(0));
    d.stop();
    a.shared->stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kDownloads)
#include "tst_Ed3kDownloads.moc"
