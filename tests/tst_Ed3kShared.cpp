/// @file tst_Ed3kShared.cpp
/// @brief eMule's shared files over ed3k (src/ed3k/Ed3kShared): node A shares a file of a
/// fake share list, node B fetches it by its root; the hash cache spares a re-hash.

#include "Ed3kJoinBase.h"
#include "Ed3kSession.h"
#include "Ed3kShared.h"

#include <QDir>
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

// shared.dat: a second start shares from the cache; a file written since is hashed again
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
        QVERIFY(QFile::exists(shared.cachePath()));
        QCOMPARE(QFile(shared.cachePath()).permissions() & (QFileDevice::ReadOther | QFileDevice::ReadGroup), QFileDevice::Permissions{});
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

// The engine's queue policy (ed3k_queue_auto, on from the session's start) with one slot:
// one downloader is served, the other queued (rank ≥ 1), then served; the host answers
// no FileRequested itself
void tst_Ed3kShared::theUploadQueueServesInTurn()
{
    const QByteArray data = payload(int(6 * kBlock + 77), 4);
    QTemporaryDir da, db, dc;
    QVERIFY(da.isValid() && db.isValid() && dc.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("q.bin"));
    QVERIFY(writeFile(path, data));

    Ed3kSession a, b, c;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QVERIFY(c.start(loopback(dc.path())));
    a.setUploadSlots(1);
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
    for (Ed3kSession* s : {&b, &c})
        connect(s, &Ed3kSession::eventPolled, this, [&](const ::ed3k::Event& ev, bool*) {
            if (const auto* q = std::get_if<::ed3k::event::QueueRank>(&ev))
                ranks.push_back(q->rank);
        });

    Fetcher fb(b, db.path()), fc(c, dc.path());
    for (Fetcher* f : {&fb, &fc}) {
        f->setSearchEnabled(false);
        f->start();
    }
    QVERIFY(fb.fetch(root, std::uint64_t(data.size()), sourceOf(a)));
    QVERIFY(fc.fetch(root, std::uint64_t(data.size()), sourceOf(a)));
    QTRY_VERIFY_WITH_TIMEOUT(fb.got == data && fc.got == data, 60000);
    QCOMPARE(hostAnswers, 0);
    QVERIFY(std::ranges::count(ranks, 0u) >= 2);                                   // both served
    QVERIFY(std::ranges::any_of(ranks, [](std::uint32_t r) { return r >= 1; }));   // one waited
    const auto stats = a.uploadQueueStats();
    QVERIFY(stats);
    QCOMPARE(stats->slotCount, 1u);

    fb.stop();
    fc.stop();
    shared.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kShared)
#include "tst_Ed3kShared.moc"
