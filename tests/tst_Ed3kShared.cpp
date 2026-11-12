/// @file tst_Ed3kShared.cpp
/// @brief eMule's shared files over ed3k (src/ed3k/Ed3kShared): node A shares a file of a
/// fake share list, node B fetches it by its root; the hash cache spares a re-hash.

#include "utils/BackgroundFileWriter.h"
#include "utils/SystemMemory.h"
#include "Ed3kJoinBase.h"
#include "Ed3kSession.h"
#include "Ed3kShared.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstring>

using namespace eMule;
using namespace eMule::ed3k;

namespace {

constexpr std::uint64_t kBlock = 256 * 1024;

QByteArray payload(int size, int seed = 0)
{
    QByteArray data(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 13 + (i >> 7) * 3 + seed * 29) & 0xFF);
    return data;
}

::ed3k::Root rootOf(const QByteArray& data)
{
    ::ed3k::Hasher h;
    (void)h.update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())});
    auto done = h.finish();
    return done ? done->root : ::ed3k::Root{};
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

/// A share list by hand.
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

/// Node B's side: fetches a root into memory over the base's download path.
class Fetcher final : public Ed3kJoinBase {
public:
    QByteArray got;

    Fetcher(Ed3kSession& s, const QString& dir) : Ed3kJoinBase(s, dir, QStringLiteral("fetch"), nullptr) {}
    void start() override { beginStart(); }
    void stop() override { closeAll(); }
    bool fetch(const ::ed3k::Root& root, std::uint64_t size, const ::ed3k::Source& from)
    {
        if (!fetchBlob(root, size, QStringLiteral("t"), 0))
            return false;
        addSource(root, from);
        return true;
    }

protected:
    std::vector<std::uint64_t> wantedBlocks(const Download&) const override { return {}; }
    void onFileBlock(const Download&, const ::ed3k::event::Block&) override {}
    void onBlobFetched(const QString&, int, const ::ed3k::Root&, const QByteArray& data) override { got = data; }
    std::optional<FileRead> sharedRead(const Shared&, std::uint64_t, std::uint32_t) const override { return std::nullopt; }
    bool hasOwner(const QString&) const override { return false; }
    void saveOwner(const QString&) override {}
};

Ed3kSettings loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.udpPort = 0;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

::ed3k::Source sourceOf(const Ed3kSession& s)
{
    return ::ed3k::Source{*s.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort()), std::nullopt, {}};
}

} // namespace

class tst_Ed3kShared : public QObject {
    Q_OBJECT

private slots:
    void ed2kAliasIsMd4AndSizeBigEndian();
    void aSharedFileIsFetchedByItsRoot();
    void theCacheSparesAHashAfterARestart();
    void aRemovedFileIsNotShared();
    void theUploadQueueServesInTurn();
    void keywordsArePublishedUnlessHeldBack();
    void anUnreadableFileIsHashedAgainLater();
    void anAnonymousFileDownloadedElsewhereIsToldOnce();
    void lowRamKeepsBlockHashesOnDiskOnly();
    void aVersion1CacheIsMigrated();
    void tornBlockHashesAreHashedAgain();
    void theHashRamBudgetFollowsRamAndPrefs();
    void twoDisksHashAtOnce();
    void aSideHashIsSharedWithoutASecondRead();
    void aStaleSideHashIsHashedAgain();
    void cleanup();
};

void tst_Ed3kShared::cleanup()
{
    QTest::qWait(10);
}

// alias.md §2: id = MD4 ‖ size as 8 B big endian; Alias fields 1, 2, 4, 5, 6 (rel SAME left out)
void tst_Ed3kShared::ed2kAliasIsMd4AndSizeBigEndian()
{
    const QByteArray md4(16, char(0xab));
    ::ed3k::Root root;
    root.bytes.fill(0x11);
    const auto a = Ed3kShared::ed2kAlias(md4, 0x0102030405ULL, root, QStringLiteral("x"));
    QByteArray expect = QByteArray::fromHex("0a046564326b") + QByteArray::fromHex("1218") + md4 + QByteArray::fromHex("0000000102030405");
    expect += QByteArray::fromHex("2220") + QByteArray(32, char(0x11));
    expect += QByteArray::fromHex("2885888c9010") + QByteArray::fromHex("320178");
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(a.data()), qsizetype(a.size())), expect);
    // a name over 255 B is left out (alias.md §3)
    const auto b = Ed3kShared::ed2kAlias(md4, 1, root, QString(300, QLatin1Char('n')));
    QCOMPARE(b.size(), std::size_t(6 + 2 + 24 + 2 + 32 + 2));
}

void tst_Ed3kShared::aSharedFileIsFetchedByItsRoot()
{
    const QByteArray data = payload(int(3 * kBlock + 12345), 1);
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("shared.bin"));
    QVERIFY(writeFile(path, data));

    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    FakeSource src;
    const QByteArray md4(16, char(0x5a));
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("shared.bin"), std::uint64_t(data.size())});

    Ed3kShared shared(a, src, da.path());
    QSignalSpy spy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QCOMPARE(shared.hashCount(), 1);
    const auto root = shared.rootOf(md4);
    QVERIFY(root);
    QCOMPARE(*root, rootOf(data));
    QVERIFY(shared.aliasPublished(md4));

    Fetcher fetch(b, db.path());
    fetch.setSearchEnabled(false);
    fetch.start();
    QVERIFY(fetch.fetch(*root, std::uint64_t(data.size()), sourceOf(a)));
    QTRY_COMPARE_WITH_TIMEOUT(fetch.got.size(), data.size(), 30000);
    QVERIFY(fetch.got == data);
    fetch.stop();
    shared.stop();
}

// hash cache: a second start shares from the cache; a file written since is hashed again
void tst_Ed3kShared::theCacheSparesAHashAfterARestart()
{
    const QByteArray data = payload(int(kBlock + 999), 2);
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("c.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    FakeSource src;
    const QByteArray md4(16, char(0x33));
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("c.bin"), std::uint64_t(data.size())});

    ::ed3k::Root first;
    {
        Ed3kShared shared(a, src, da.path());
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        first = *shared.rootOf(md4);
        shared.stop();
        eMule::BackgroundFileWriter::instance().flush();   // saves are queued (report 43)
        QVERIFY(QFile::exists(shared.cachePath()));
#ifdef Q_OS_UNIX
        // Windows: Qt maps only the read-only bit, no owner-only DACL yet (Ed3kStore ToDo)
        QCOMPARE(QFile(shared.cachePath()).permissions() & (QFileDevice::ReadOther | QFileDevice::ReadGroup), QFileDevice::Permissions{});
#endif
    }
    {
        Ed3kShared shared(a, src, da.path());
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QCOMPARE(spy.count(), 1);   // at once, from the cache
        QCOMPARE(shared.hashCount(), 0);
        QCOMPARE(*shared.rootOf(md4), first);
        // still served after a cache hit
        Fetcher fetch(b, db.path());
        fetch.setSearchEnabled(false);
        fetch.start();
        QVERIFY(fetch.fetch(first, std::uint64_t(data.size()), sourceOf(a)));
        QTRY_COMPARE_WITH_TIMEOUT(fetch.got.size(), data.size(), 30000);
        QVERIFY(fetch.got == data);
        fetch.stop();
        shared.stop();
    }
    // written since: size and date differ, so hashed again
    QTest::qWait(20);
    const QByteArray other = payload(int(kBlock + 5000), 3);
    QVERIFY(writeFile(path, other));
    src.entries.front().size = std::uint64_t(other.size());
    {
        Ed3kShared shared(a, src, da.path());
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        QCOMPARE(shared.hashCount(), 1);
        QCOMPARE(*shared.rootOf(md4), rootOf(other));
        shared.stop();
    }
}

void tst_Ed3kShared::aRemovedFileIsNotShared()
{
    const QByteArray data = payload(int(kBlock / 2), 4);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("r.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    FakeSource src;
    const QByteArray md4(16, char(0x44));
    Ed3kShared shared(a, src, da.path());
    shared.setHoldBack(true);
    QSignalSpy spy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QVERIFY(shared.isStarted());
    // added after the start, as the share list does
    const SharedEntry e{md4, path, QStringLiteral("r.bin"), std::uint64_t(data.size())};
    src.entries.push_back(e);
    shared.onFileAdded(e);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QVERIFY(shared.rootOf(md4));
    QVERIFY(!shared.aliasPublished(md4));   // holding back (§6): no ALIAS record

    // moved: same size and date, same root, no hash
    const QString moved = QDir(da.path()).filePath(QStringLiteral("r2.bin"));
    QVERIFY(QFile::rename(path, moved));
    src.entries.front().path = moved;
    shared.onFileRelocated(src.entries.front());
    QVERIFY(shared.rootOf(md4));
    QCOMPARE(shared.hashCount(), 1);

    shared.onFileRemoved(md4);
    QVERIFY(!shared.rootOf(md4));
    // #122: unshared and shared again unchanged (a torrent check): no second hash
    shared.onFileAdded(src.entries.front());
    QVERIFY(shared.rootOf(md4));
    QCOMPARE(shared.hashCount(), 1);
    shared.onFileRemoved(md4);
    src.entries.clear();
    QVERIFY(!shared.rootOf(md4));

    // pref off: nothing shared, back on: shared again
    src.entries.push_back(SharedEntry{md4, moved, QStringLiteral("r2.bin"), std::uint64_t(data.size())});
    shared.setEnabled(false);
    QVERIFY(!shared.isStarted());
    shared.setEnabled(true);
    QVERIFY(shared.isStarted());
    QTRY_VERIFY_WITH_TIMEOUT(shared.rootOf(md4).has_value(), 20000);
    shared.stop();
}

// The engine's queue policy (ed3k_queue_auto, on from the session's start) with 2 slots
// (its minimum, §12.5): two downloaders are served, the third queued (rank ≥ 1), then
// served; the host answers no FileRequested itself
void tst_Ed3kShared::theUploadQueueServesInTurn()
{
    const QByteArray data = payload(int(6 * kBlock + 77), 4);
    QTemporaryDir da, db, dc, dd;
    QVERIFY(da.isValid() && db.isValid() && dc.isValid() && dd.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("q.bin"));
    QVERIFY(writeFile(path, data));

    Ed3kSession a, b, c, d;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QVERIFY(c.start(loopback(dc.path())));
    QVERIFY(d.start(loopback(dd.path())));
    a.setUploadSlots(2);
    FakeSource src;
    const QByteArray md4(16, char(0x71));
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("q.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(a, src, da.path());
    QSignalSpy spy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    const auto root = *shared.rootOf(md4);

    // every FileRequested on A was answered by the engine
    int hostAnswers = 0;
    connect(&a, &Ed3kSession::eventPolled, this, [&](const ::ed3k::Event& ev, bool*) {
        if (const auto* r = std::get_if<::ed3k::event::FileRequested>(&ev); r && r->first && !r->autoAnswered)
            ++hostAnswers;
    });
    std::vector<std::uint32_t> ranks;
    for (Ed3kSession* s : {&b, &c, &d})
        connect(s, &Ed3kSession::eventPolled, this, [&](const ::ed3k::Event& ev, bool*) {
            if (const auto* q = std::get_if<::ed3k::event::QueueRank>(&ev))
                ranks.push_back(q->rank);
        });

    Fetcher fb(b, db.path()), fc(c, dc.path()), fd(d, dd.path());
    for (Fetcher* f : {&fb, &fc, &fd}) {
        f->setSearchEnabled(false);
        f->start();
    }
    QVERIFY(fb.fetch(root, std::uint64_t(data.size()), sourceOf(a)));
    QVERIFY(fc.fetch(root, std::uint64_t(data.size()), sourceOf(a)));
    QVERIFY(fd.fetch(root, std::uint64_t(data.size()), sourceOf(a)));
    QTRY_VERIFY_WITH_TIMEOUT(fb.got == data && fc.got == data && fd.got == data, 60000);
    QCOMPARE(hostAnswers, 0);
    QVERIFY(std::ranges::count(ranks, 0u) >= 3);                                   // all served
    QVERIFY(std::ranges::any_of(ranks, [](std::uint32_t r) { return r >= 1; }));   // one waited
    const auto stats = a.uploadQueueStats();
    QVERIFY(stats);
    QCOMPARE(stats->slotCount, 2u);

    fb.stop();
    fc.stop();
    fd.stop();
    shared.stop();
}

// a shared file is found by ed3k search: its keywords go out with the source (Publish::Both)
void tst_Ed3kShared::keywordsArePublishedUnlessHeldBack()
{
    const QByteArray data = payload(int(kBlock + 77), 6);
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("holiday photos.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    // A knows B: its keyword stores have somewhere to go
    const ::ed3k::Node bNode{*b.nodeId(), ::ed3k::Address::v4(127, 0, 0, 1, b.udpPort())};
    a.post([&](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1}); });
    QTRY_VERIFY_WITH_TIMEOUT(a.contactCount() > 0, 20000);
    std::uint64_t keywordFiles = 0;
    connect(&a, &Ed3kSession::eventPolled, this, [&](const ::ed3k::Event& ev, bool*) {
        if (const auto* k = std::get_if<::ed3k::event::KeywordsPublished>(&ev))
            keywordFiles += k->files;
    }, Qt::DirectConnection);

    FakeSource src;
    const QByteArray md4(16, char(0x61));
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("holiday photos.bin"), std::uint64_t(data.size())});
    {
        Ed3kShared shared(a, src, da.path());
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        QVERIFY(shared.keywordsPublished(md4));
        // 2 nodes = small network: Kad stores held SMALL_NET_S (45 s) after the first answer
        QTRY_VERIFY_WITH_TIMEOUT(keywordFiles > 0, 60000);
        shared.stop();
    }
    {
        Ed3kShared shared(a, src, da.path());
        shared.setHoldBack(true);
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        QVERIFY(shared.rootOf(md4));
        QVERIFY(!shared.keywordsPublished(md4));   // the source only
        QVERIFY(!shared.aliasPublished(md4));
        shared.stop();
    }
    a.disconnect(this);
}

// a file that cannot be read when it is hashed is tried again later (kRehashMs)
void tst_Ed3kShared::anUnreadableFileIsHashedAgainLater()
{
#ifndef Q_OS_UNIX
    QSKIP("needs a file without read permission");
#else
    const QByteArray data = payload(int(kBlock + 4321), 7);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("locked.bin"));
    QVERIFY(writeFile(path, data));
    QVERIFY(QFile::setPermissions(path, QFileDevice::Permissions{}));
    QFile probe(path);
    if (probe.open(QIODevice::ReadOnly))
        QSKIP("runs with rights that read any file");
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    FakeSource src;
    const QByteArray md4(16, char(0x62));
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("locked.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(a, src, da.path());
    shared.setRehashDelay(300);
    QSignalSpy spy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(shared.hashCount(), 1, 20000);
    QTest::qWait(100);
    QCOMPARE(spy.count(), 0);
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QCOMPARE(shared.hashCount(), 2);
    QCOMPARE(*shared.rootOf(md4), rootOf(data));
    shared.stop();
#endif
}

// hops.md §1 SHOULD: a file of the anonymous key starting on eD2K too: the user is told once
void tst_Ed3kShared::anAnonymousFileDownloadedElsewhereIsToldOnce()
{
    const QByteArray data = payload(int(kBlock / 2 + 77), 9);
    const QByteArray other = payload(int(kBlock / 2 + 99), 10);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("anon.bin"));
    const QString path2 = QDir(da.path()).filePath(QStringLiteral("node.bin"));
    QVERIFY(writeFile(path, data));
    QVERIFY(writeFile(path2, other));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    // under the anonymous key, hiding off: hashed, not shared (nothing told yet)
    a.hide().record(rootOf(data), ::ed3k::KeyKind::Anon);
    FakeSource src;
    const QByteArray md4(16, char(0x44));
    const QByteArray md4b(16, char(0x45));
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("anon.bin"), std::uint64_t(data.size())});
    src.entries.push_back(SharedEntry{md4b, path2, QStringLiteral("node.bin"), std::uint64_t(other.size())});
    Ed3kShared shared(a, src, da.path());
    QSignalSpy told(&shared, &Ed3kShared::anonExposed);
    shared.start();
    QTRY_VERIFY_WITH_TIMEOUT(shared.hashedRootOf(md4) && shared.hashedRootOf(md4b), 20000);
    QVERIFY(shared.holdsRoot(rootOf(data)));
    QCOMPARE(told.count(), 0);
    QCOMPARE(shared.keyOfFile(md4), std::optional(::ed3k::KeyKind::Anon));
    // the node key's file: nothing
    QVERIFY(!shared.tellDownloadElsewhere(md4b, QStringLiteral("node.bin"), QStringLiteral("eD2K")));
    QVERIFY(!shared.tellDownloadElsewhere(QByteArray(16, char(0x46)), QStringLiteral("x"), QStringLiteral("eD2K")));
    // the anonymous one: once
    QVERIFY(shared.tellDownloadElsewhere(md4, QStringLiteral("anon.bin"), QStringLiteral("eD2K")));
    QVERIFY(!shared.tellDownloadElsewhere(md4, QStringLiteral("anon.bin"), QStringLiteral("eD2K")));
    QCOMPARE(told.count(), 1);
    QCOMPARE(told.at(0).at(1).toString(), QStringLiteral("eD2K"));
    shared.stop();
    a.stop();
}

// low RAM: the engine's copy only; shared and served from shared-hashes.dat, also after a restart
void tst_Ed3kShared::lowRamKeepsBlockHashesOnDiskOnly()
{
    const QByteArray data = payload(int(3 * kBlock + 777), 5);
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("low.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    FakeSource src;
    const QByteArray md4(16, char(0x51));
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("low.bin"), std::uint64_t(data.size())});
    {
        Ed3kShared shared(a, src, da.path());
        shared.setLowRamCheck([] { return true; });
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        QCOMPARE(shared.hashesInRam(), std::size_t(0));
        QVERIFY(QFileInfo(shared.hashesPath()).size() > 8);
        shared.stop();
        eMule::BackgroundFileWriter::instance().flush();
    }
    {
        Ed3kShared shared(a, src, da.path());
        shared.setLowRamCheck([] { return true; });
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        // was: QCOMPARE(spy.count(), 1);   // from the cache, values read from disk
        QCOMPARE(spy.count(), 1);   // from the cache; values read from disk when the peer asks (WantHashes)
        QCOMPARE(shared.hashCount(), 0);
        QCOMPARE(shared.hashesInRam(), std::size_t(0));
        QCOMPARE(*shared.rootOf(md4), rootOf(data));
        Fetcher fetch(b, db.path());
        fetch.setSearchEnabled(false);
        fetch.start();
        QVERIFY(fetch.fetch(*shared.rootOf(md4), std::uint64_t(data.size()), sourceOf(a)));
        QTRY_COMPARE_WITH_TIMEOUT(fetch.got.size(), data.size(), 30000);
        QVERIFY(fetch.got == data);
        fetch.stop();
        shared.stop();
        eMule::BackgroundFileWriter::instance().flush();
    }
    {
        // enough RAM, no hash run: not kept (the engine holds its own)
        // was: kept once read (hashesInRam 1)
        Ed3kShared shared(a, src, da.path());
        shared.setLowRamCheck([] { return false; });
        shared.start();
        QVERIFY(shared.rootOf(md4).has_value());
        QCOMPARE(shared.hashesInRam(), std::size_t(0));
        shared.stop();
    }
    {
        // enough RAM, fresh hash: no copy here once stored (the engine holds it within its budget)
        // was: kept while hashing, dropped when the run ends
        QTemporaryDir dc;
        QVERIFY(dc.isValid());
        Ed3kShared shared(a, src, dc.path());
        shared.setLowRamCheck([] { return false; });
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        QCOMPARE(shared.hashCount(), 1);
        QTRY_COMPARE(shared.hashesInRam(), std::size_t(0));
        shared.stop();
    }
    a.stop();
    b.stop();
}

// shared.dat (v1, values inline) becomes index + values file without a hash
// ToDo: remove with Ed3kShared::migrateV1
void tst_Ed3kShared::aVersion1CacheIsMigrated()
{
    const QByteArray data = payload(int(2 * kBlock + 1234), 7);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("v1.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    FakeSource src;
    const QByteArray md4(16, char(0x61));
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("v1.bin"), std::uint64_t(data.size())});

    ::ed3k::Hasher hasher;
    QVERIFY(hasher.update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())}));
    auto done = hasher.finish();
    QVERIFY(done.has_value());
    const auto hashBytes = done->hashes.bytes();
    {
        Ed3kShared shared(a, src, da.path());
        const QString old = QFileInfo(shared.cachePath()).dir().filePath(QStringLiteral("shared.dat"));
        QVERIFY(QDir().mkpath(QFileInfo(old).absolutePath()));
        QFile f(old);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QDataStream out(&f);
        out.setVersion(QDataStream::Qt_6_0);
        out << quint32(0x45335348) << quint32(1) << quint32(1);
        out << md4 << path << quint64(data.size()) << QFileInfo(path).lastModified().toMSecsSinceEpoch()
            << QByteArray(reinterpret_cast<const char*>(done->root.bytes.data()), qsizetype(done->root.bytes.size()))
            << QByteArray(reinterpret_cast<const char*>(hashBytes.data()), qsizetype(hashBytes.size()));
        f.close();

        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(shared.hashCount(), 0);
        QCOMPARE(*shared.rootOf(md4), done->root);
        QVERIFY(!QFile::exists(old));
        QVERIFY(QFile::exists(shared.cachePath()));
        QCOMPARE(QFileInfo(shared.hashesPath()).size(), qint64(8 + hashBytes.size()));
        shared.stop();
    }
    a.stop();
}

// values that do not match their check (a torn write): read as a miss, hashed again
void tst_Ed3kShared::tornBlockHashesAreHashedAgain()
{
    const QByteArray data = payload(int(2 * kBlock + 99), 9);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("torn.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    FakeSource src;
    const QByteArray md4(16, char(0x71));
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("torn.bin"), std::uint64_t(data.size())});
    QString hashes;
    {
        Ed3kShared shared(a, src, da.path());
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        hashes = shared.hashesPath();
        shared.stop();
        eMule::BackgroundFileWriter::instance().flush();
    }
    {
        QFile f(hashes);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.size() > 8);
        QVERIFY(f.seek(8));
        QCOMPARE(f.write(QByteArray(int(f.size() - 8), char(0x5A))), f.size() - 8);
    }
    {
        // was: the start read them, a miss: hashed at once (hashCount 1)
        // now shared from the index; the first peer's HashRequest reads them, they fail their
        // check (HashesLost) and the file is hashed again
        Ed3kSession b;
        QTemporaryDir db;
        QVERIFY(db.isValid());
        QVERIFY(b.start(loopback(db.path())));
        Ed3kShared shared(a, src, da.path());
        shared.setLowRamCheck([] { return true; });
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(shared.hashCount(), 0);
        {
            Fetcher fetch(b, db.path());
            fetch.setSearchEnabled(false);
            fetch.start();
            QVERIFY(fetch.fetch(rootOf(data), std::uint64_t(data.size()), sourceOf(a)));
            QTRY_COMPARE_WITH_TIMEOUT(shared.hashCount(), 1, 30000);
            fetch.stop();
        }
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 20000);
        QCOMPARE(*shared.rootOf(md4), rootOf(data));
        // stored again, whole: served from disk
        Fetcher fetch(b, db.path());
        fetch.setSearchEnabled(false);
        fetch.start();
        QVERIFY(fetch.fetch(rootOf(data), std::uint64_t(data.size()), sourceOf(a)));
        QTRY_COMPARE_WITH_TIMEOUT(fetch.got.size(), data.size(), 30000);
        QVERIFY(fetch.got == data);
        fetch.stop();
        shared.stop();
        b.stop();
    }
    a.stop();
}

// auto = 2 % of RAM within 64–512 MiB; a set value; 0 while RAM is low
void tst_Ed3kShared::theHashRamBudgetFollowsRamAndPrefs()
{
    constexpr std::uint64_t MiB = 1ull << 20;
    QCOMPARE(Ed3kShared::autoHashRam(0), 256 * MiB);
    QCOMPARE(Ed3kShared::autoHashRam(2048 * MiB), 64 * MiB);
    QCOMPARE(Ed3kShared::autoHashRam(16384 * MiB), 16384 * MiB / 100 * 2);
    QCOMPARE(Ed3kShared::autoHashRam(32768 * MiB), 512 * MiB);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    FakeSource src;
    Ed3kShared shared(a, src, da.path());
    bool low = false;
    shared.setLowRamCheck([&low] { return low; });
    shared.setHashRamMiB(100);
    QCOMPARE(shared.hashRamBudget(), 100 * MiB);
    low = true;
    QCOMPARE(shared.hashRamBudget(), std::uint64_t(0));
    low = false;
    shared.setHashRamMiB(0);
    const auto mem = eMule::systemMemory();
    QCOMPARE(shared.hashRamBudget(), Ed3kShared::autoHashRam(mem ? mem->total : 0));
    a.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kShared)
// one file per disk at a time, disks in parallel
void tst_Ed3kShared::twoDisksHashAtOnce()
{
    QTemporaryDir da;
    QVERIFY(da.isValid());
    QVERIFY(QDir(da.path()).mkpath(QStringLiteral("A")) && QDir(da.path()).mkpath(QStringLiteral("B")));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    FakeSource src;
    const auto put = [&](const QString& disk, const QString& name, int seed) {
        const QByteArray data = payload(int(kBlock * 3 + 17), seed);
        const QString path = QDir(da.path()).filePath(disk + u'/' + name);
        QVERIFY(writeFile(path, data));
        src.entries.push_back(SharedEntry{QByteArray(16, char(0x40 + seed)), path, name, std::uint64_t(data.size())});
    };
    put(QStringLiteral("A"), QStringLiteral("a1.bin"), 1);
    put(QStringLiteral("A"), QStringLiteral("a2.bin"), 2);
    put(QStringLiteral("B"), QStringLiteral("b1.bin"), 3);

    Ed3kShared shared(a, src, da.path());
    shared.setVolumeKeyFn([](const QString& dir) { return QFileInfo(dir).fileName(); });
    QSignalSpy spy(&shared, &Ed3kShared::fileShared);
    shared.start();
    // A and B at once; A's second file waits for its first
    QCOMPARE(shared.hashingNow(), 2);
    QCOMPARE(shared.hashCount(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 3, 20000);
    QCOMPARE(shared.hashCount(), 3);
    QCOMPARE(shared.hashingNow(), 0);
    shared.stop();
}

// eD2K's read hashed it for ed3k too: listed, it is shared without a read of its own
void tst_Ed3kShared::aSideHashIsSharedWithoutASecondRead()
{
    const QByteArray data = payload(int(kBlock * 2 + 333), 7);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("s.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    FakeSource src;
    Ed3kShared shared(a, src, da.path());
    const SideHasherFactory factory = shared.sideHasherFactory();
    QVERIFY(!factory(path, quint64(data.size())));   // stopped: no hasher
    shared.start();
    QVERIFY(!factory(path, 0));

    auto side = factory(path, quint64(data.size()));
    QVERIFY(side);
    // in eD2K's 64 KiB reads
    for (qsizetype at = 0; at < data.size(); at += 64 * 1024) {
        const QByteArray part = data.mid(at, 64 * 1024);
        side->update({reinterpret_cast<const std::byte*>(part.constData()), std::size_t(part.size())});
    }
    const QByteArray md4(16, char(0x51));
    side->finish(md4, path, quint64(data.size()), QFileInfo(path).lastModified().toMSecsSinceEpoch());
    QCoreApplication::processEvents();   // the queued result, as before hashingFinished

    QSignalSpy spy(&shared, &Ed3kShared::fileShared);
    const SharedEntry entry{md4, path, QStringLiteral("s.bin"), std::uint64_t(data.size())};
    src.entries.push_back(entry);
    shared.onFileAdded(entry);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(shared.hashCount(), 0);
    QCOMPARE(*shared.rootOf(md4), rootOf(data));
    shared.stop();
}

// the file changed after eD2K read it: the side hash is dropped, ed3k reads it itself
void tst_Ed3kShared::aStaleSideHashIsHashedAgain()
{
    const QByteArray data = payload(int(kBlock + 77), 8);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("t.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    FakeSource src;
    Ed3kShared shared(a, src, da.path());
    shared.start();
    auto side = shared.sideHasherFactory()(path, quint64(data.size()));
    QVERIFY(side);
    side->update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())});
    const QByteArray md4(16, char(0x52));
    side->finish(md4, path, quint64(data.size()), QFileInfo(path).lastModified().toMSecsSinceEpoch() - 5000);
    QCoreApplication::processEvents();

    QSignalSpy spy(&shared, &Ed3kShared::fileShared);
    const SharedEntry entry{md4, path, QStringLiteral("t.bin"), std::uint64_t(data.size())};
    src.entries.push_back(entry);
    shared.onFileAdded(entry);
    QCOMPARE(spy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QCOMPARE(shared.hashCount(), 1);
    QCOMPARE(*shared.rootOf(md4), rootOf(data));
    shared.stop();
}

#include "tst_Ed3kShared.moc"
