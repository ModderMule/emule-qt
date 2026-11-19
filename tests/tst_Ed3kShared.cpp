/// @file tst_Ed3kShared.cpp
/// @brief eMule's shared files over ed3k (src/ed3k/Ed3kShared): node A shares a file of a
/// fake share list, node B fetches it by its root; the hash cache spares a re-hash.

#include "files/KnownFile.h"
#include "prefs/Preferences.h"
#include "utils/BackgroundFileWriter.h"
#include "utils/DiskLoadLimiter.h"
#include "utils/SystemMemory.h"
#include "Ed3kJoinBase.h"
#include "Ed3kSession.h"
#include "Ed3kShared.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QCryptographicHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <limits>
#include <thread>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

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

/// A join of the same node that shares a file (a landed hoster/Usenet file is its join's).
class Holder final : public Ed3kJoinBase {
public:
    QString path;

    Holder(Ed3kSession& s, const QString& dir, const QString& tag) : Ed3kJoinBase(s, dir, tag, nullptr) {}
    void start() override { beginStart(); }
    void stop() override { closeAll(); }
    std::optional<::ed3k::Root> share(const QString& p)
    {
        path = p;
        auto h = hashNow(p);
        if (!h || !shareMember(::ed3k::SharedFile{h->root, h->size, "held", std::nullopt, std::nullopt, ::ed3k::Have::complete(),
                                                  std::span<const std::byte>{h->hashes}},
                               QStringLiteral("h"), 0))
            return std::nullopt;
        // a join publishes its source: Kad holds the root as shared then (alias.md §3)
        if (!publishShared(::ed3k::FileInfo{h->root, h->size, "held", std::nullopt, 1, 0, 0, 0, 0, 0}))
            return std::nullopt;
        return h->root;
    }

protected:
    std::vector<std::uint64_t> wantedBlocks(const Download&) const override { return {}; }
    void onFileBlock(const Download&, const ::ed3k::event::Block&) override {}
    void onBlobFetched(const QString&, int, const ::ed3k::Root&, const QByteArray&) override {}
    std::optional<FileRead> sharedRead(const Shared&, std::uint64_t, std::uint32_t) const override { return std::nullopt; }
    bool hasOwner(const QString& o) const override { return o == QStringLiteral("h"); }
    void saveOwner(const QString&) override {}
};

/// eD2K's file hash: MD4 of the data under one part; else MD4 of the part MD4s (a size on
/// a part boundary adds MD4 of nothing)
QByteArray ed2kHashOf(const QByteArray& data)
{
    constexpr qsizetype part = 9728000;
    const auto md4 = [](QByteArrayView b) { return QCryptographicHash::hash(b, QCryptographicHash::Md4); };
    if (data.size() < part)
        return md4(data);
    QByteArray parts;
    for (qsizetype at = 0; at <= data.size(); at += part)
        parts += md4(QByteArrayView(data).sliced(at, std::min(part, data.size() - at)));
    return md4(parts);
}

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
    void theListedDateSparesAStat();
    void unlistedCacheEntriesArePrunedOffTheMainThread();
    void aRemovedFileIsNotShared();
    void aDeclinedFileIsSharedNotPublished();
    void aListingWithdrawsAndUnlistingPublishes();
    void theUploadQueueServesInTurn();
    void keywordsArePublishedUnlessHeldBack();
    void anUnreadableFileIsHashedAgainLater();
    void anAnonymousFileDownloadedElsewhereIsToldOnce();
    void lowRamKeepsBlockHashesOnDiskOnly();
    void tornBlockHashesAreHashedAgain();
    void theHashRamBudgetFollowsRamAndPrefs();
    void twoDisksHashAtOnce();
    void aSideHashIsSharedWithoutASecondRead_data();
    void aSideHashIsSharedWithoutASecondRead();
    void aStaleSideHashIsHashedAgain();
    void ed3kOnlyFilesAreSharedWithoutAnEd2kName();
    void aLandedFileKeptOutGetsItsEd2kNameAlone();
    void benchHashRun_data();
    void benchHashRun();
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
    // search rows find the share by root (Ed3kBridge::sharedRootIndex)
    int seen = 0;
    shared.forEachSharedRoot([&](const QByteArray& m, const ::ed3k::Root& r) {
        ++seen;
        QCOMPARE(m, md4);
        QCOMPARE(r, *root);
    });
    QCOMPARE(seen, 1);

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
    // #200: moved and its date 1 h off (DST, as known.met allows): still a hit
    {
        const QString moved = QDir(da.path()).filePath(QStringLiteral("C-moved.bin"));
        QVERIFY(QFile::rename(path, moved));
        {
            QFile f(moved);
            const QDateTime shifted = QFileInfo(moved).lastModified().addSecs(3600);
            QVERIFY(f.open(QIODevice::ReadWrite) && f.setFileTime(shifted, QFileDevice::FileModificationTime));
        }
        src.entries.front().path = moved;
        Ed3kShared shared(a, src, da.path());
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(shared.hashCount(), 0);
        QCOMPARE(*shared.rootOf(md4), first);
        shared.stop();
        QVERIFY(QFile::rename(moved, path));
        src.entries.front().path = path;
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

// #200: the share list's date (stat'ed off the main thread) picks the cached entry, a
// new file is stat'ed when hashed; both are served (the read worker checks the ms date)
void tst_Ed3kShared::theListedDateSparesAStat()
{
    const QByteArray data = payload(int(kBlock + 777), 8);
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("d.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    FakeSource src;
    const QByteArray md4(16, char(0x3a));
    SharedEntry e{md4, path, QStringLiteral("d.bin"), std::uint64_t(data.size())};
    e.fileDate = QFileInfo(path).lastModified().toSecsSinceEpoch();
    src.entries.push_back(e);

    ::ed3k::Root first;
    {
        // nothing cached: hashed, its date read then
        Ed3kShared shared(a, src, da.path());
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        QCOMPARE(shared.hashCount(), 1);
        first = *shared.rootOf(md4);
        QCOMPARE(first, rootOf(data));
        shared.stop();
        eMule::BackgroundFileWriter::instance().flush();
    }
    {
        // the listed date matches the cached one: shared at once
        Ed3kShared shared(a, src, da.path());
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(shared.hashCount(), 0);
        QCOMPARE(*shared.rootOf(md4), first);
        Fetcher fetch(b, db.path());
        fetch.setSearchEnabled(false);
        fetch.start();
        QVERIFY(fetch.fetch(first, std::uint64_t(data.size()), sourceOf(a)));
        QTRY_COMPARE_WITH_TIMEOUT(fetch.got.size(), data.size(), 30000);
        QVERIFY(fetch.got == data);
        fetch.stop();
        shared.stop();
    }
    {
        // a listed date 1 h off: stat'ed, still a hit (DST)
        src.entries.front().fileDate += 3600;
        Ed3kShared shared(a, src, da.path());
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(shared.hashCount(), 0);
        shared.stop();
    }
}

void tst_Ed3kShared::unlistedCacheEntriesArePrunedOffTheMainThread()
{
    // #200: the stat of unlisted entries ran on every save; now once after the full scan, on a pool thread
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QByteArray keep = payload(int(kBlock + 11), 5), gone = payload(int(kBlock + 22), 6);
    const QString keepPath = QDir(da.path()).filePath(QStringLiteral("keep.bin"));
    const QString gonePath = QDir(da.path()).filePath(QStringLiteral("gone.bin"));
    QVERIFY(writeFile(keepPath, keep) && writeFile(gonePath, gone));
    const QDateTime goneDate = QFileInfo(gonePath).lastModified();
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    FakeSource src;
    const QByteArray keepMd4(16, char(0x51)), goneMd4(16, char(0x52));
    const SharedEntry keepEntry{keepMd4, keepPath, QStringLiteral("keep.bin"), std::uint64_t(keep.size())};
    const SharedEntry goneEntry{goneMd4, gonePath, QStringLiteral("gone.bin"), std::uint64_t(gone.size())};
    src.entries = {keepEntry, goneEntry};
    {
        Ed3kShared shared(a, src, da.path());
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 20000);
        shared.stop();
    }
    // both unlisted, one file gone: the scan's check drops only that one
    QVERIFY(QFile::remove(gonePath));
    src.entries.clear();
    {
        Ed3kShared shared(a, src, da.path());
        shared.start();
        shared.onListScanned(true);
        QTest::qWait(500);   // pool check + queued result
        shared.stop();
        eMule::BackgroundFileWriter::instance().flush();
    }
    // same file and date again: the kept entry spares a hash, the dropped one does not
    QVERIFY(writeFile(gonePath, gone));
    {
        QFile f(gonePath);
        QVERIFY(f.open(QIODevice::ReadWrite) && f.setFileTime(goneDate, QFileDevice::FileModificationTime));
    }
    src.entries = {keepEntry, goneEntry};
    {
        Ed3kShared shared(a, src, da.path());
        QSignalSpy spy(&shared, &Ed3kShared::fileShared);
        shared.start();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 20000);
        QCOMPARE(shared.hashCount(), 1);
        shared.stop();
    }
}

// declined-records.md §2: a file of decline.txt (here by its eD2K hash) is served, not published
void tst_Ed3kShared::aDeclinedFileIsSharedNotPublished()
{
    const QByteArray data = payload(int(kBlock / 2), 9);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("declined.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    const QByteArray md4(16, char(0x47));
    DeclineList list;
    list.ed2k.push_back({});
    std::copy(md4.cbegin(), md4.cend(), list.ed2k.front().begin());
    a.setDeclined(list);
    FakeSource src;
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("declined.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(a, src, da.path());
    QSignalSpy spy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    QVERIFY(shared.rootOf(md4));
    QVERIFY(!shared.keywordsPublished(md4));
    QVERIFY(!shared.aliasPublished(md4));
    shared.stop();
    a.stop();
}

// declined-records.md §2: a list change takes back what is out (source, keywords, ed2k ALIAS)
// and puts it out again once off the list; a listed MD4's root goes to the engine as a root
void tst_Ed3kShared::aListingWithdrawsAndUnlistingPublishes()
{
    const QByteArray data = payload(int(kBlock / 2), 11);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("listed later.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    const QByteArray md4(16, char(0x48));
    FakeSource src;
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("listed later.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(a, src, da.path());
    a.setDeclineRootsOf([&shared](const DeclineList& list) { return shared.rootsOfListed(list); });
    QSignalSpy spy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
    const auto root = shared.rootOf(md4);
    QVERIFY(root);
    QVERIFY(shared.keywordsPublished(md4));
    QVERIFY(shared.aliasPublished(md4));

    // listed by its MD4: withdrawn at once, still served; its root declined by the engine too
    DeclineList list;
    list.ed2k.push_back({});
    std::copy(md4.cbegin(), md4.cend(), list.ed2k.front().begin());
    a.setDeclined(list);
    QVERIFY(!shared.keywordsPublished(md4));
    QVERIFY(!shared.aliasPublished(md4));
    QVERIFY(shared.rootOf(md4));
    QVERIFY(a.declined().listsRoot(*root));
    QVERIFY(!a.ownDeclined().listsRoot(*root));

    // off the list: published again
    a.setDeclined({});
    QVERIFY(shared.keywordsPublished(md4));
    QVERIFY(shared.aliasPublished(md4));
    shared.stop();
    a.stop();
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
void tst_Ed3kShared::aSideHashIsSharedWithoutASecondRead_data()
{
    QTest::addColumn<quint64>("pipeFrom");
    QTest::newRow("inline") << std::numeric_limits<quint64>::max();
    QTest::newRow("own thread") << quint64(0);
}

void tst_Ed3kShared::aSideHashIsSharedWithoutASecondRead()
{
    QFETCH(quint64, pipeFrom);
    // over two pipe chunks and a short one
    const QByteArray data = payload(int(kBlock * 9 + 333), 7);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("s.bin"));
    QVERIFY(writeFile(path, data));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    FakeSource src;
    Ed3kShared shared(a, src, da.path());
    const SideHasherFactory factory = shared.sideHasherFactory(pipeFrom);
    QVERIFY(!factory(path, quint64(data.size()), false));   // stopped: no hasher
    shared.start();
    QVERIFY(!factory(path, 0, true));
    {
        // eD2K failed mid-file: dropped without finish(), nothing hangs
        auto dropped = factory(path, quint64(data.size()), false);
        dropped->update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())});
    }

    auto side = factory(path, quint64(data.size()), false);
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
    auto side = shared.sideHasherFactory()(path, quint64(data.size()), false);
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

namespace {

/// Written past the page cache, so the hash reads hit the disk.
bool writeUncached(const QString& path, qint64 size, int seed)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
#ifdef Q_OS_MACOS
    ::fcntl(f.handle(), F_NOCACHE, 1);
#endif
    QByteArray chunk = payload(1 << 20, seed);
    for (qint64 at = 0; at < size; at += chunk.size()) {
        std::memcpy(chunk.data(), &at, sizeof at);   // no two chunks alike
        if (f.write(chunk.constData(), std::min<qint64>(chunk.size(), size - at)) <= 0)
            return false;
    }
    f.flush();
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    ::fdatasync(f.handle());
    ::posix_fadvise(f.handle(), 0, 0, POSIX_FADV_DONTNEED);
#endif
    return true;
}

int envInt(const char* name, int fallback)
{
    bool ok = false;
    const int v = qEnvironmentVariableIntValue(name, &ok);
    return ok && v > 0 ? v : fallback;
}

} // namespace

// eD2K disabled: the share list keys a file by its root's head (KnownFile::isEd3kOnly), the
// side hasher gives that root with ed3k off too, its block hashes kept for the next start
// (no second read); shared without an ed2k name or ALIAS
void tst_Ed3kShared::ed3kOnlyFilesAreSharedWithoutAnEd2kName()
{
    const QByteArray data = payload(int(kBlock + 777), 7);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    const QString path = QDir(da.path()).filePath(QStringLiteral("only.bin"));
    QVERIFY(writeFile(path, data));
    const auto root = rootOf(data);
    const QByteArray rootBytes(reinterpret_cast<const char*>(root.bytes.data()), qsizetype(root.bytes.size()));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    FakeSource src;
    {
        Ed3kShared shared(a, src, da.path());
        const SideHasherFactory factory = shared.sideHasherFactory();
        QVERIFY(!factory(path, quint64(data.size()), false));   // not serving
        auto side = factory(path, quint64(data.size()), true);
        QVERIFY(side);
        side->update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())});
        QCOMPARE(side->root(), rootBytes);
        // was: stopped: nothing kept (read again at start)
        side->finish(rootBytes.left(16), path, quint64(data.size()), QFileInfo(path).lastModified().toMSecsSinceEpoch());
        QCoreApplication::processEvents();   // the queued result
        QCOMPARE(shared.hashCount(), 0);
    }   // ~Ed3kShared: saved
    SharedEntry e{rootBytes.left(16), path, QStringLiteral("only.bin"), std::uint64_t(data.size())};
    e.ed3kOnly = true;
    src.entries.push_back(e);
    Ed3kShared shared(a, src, da.path());
    QSignalSpy spy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QCOMPARE(spy.count(), 1);   // from the cache, no read
    QCOMPARE(shared.hashCount(), 0);
    QCOMPARE(*shared.rootOf(e.md4), root);
    QVERIFY(shared.holdsRoot(root));
    QVERIFY(!shared.aliasPublished(e.md4));
    QVERIFY(shared.keywordsPublished(e.md4));
    QCOMPARE(shared.counts().published, std::size_t(1));
    shared.stop();
}

// a landed file kept out of eD2K: read once for its eD2K hash, only its ed2k ALIAS published
// (its join shares the root); eD2K disabled: nothing new read, known ones stay published
void tst_Ed3kShared::aLandedFileKeptOutGetsItsEd2kNameAlone()
{
    QTemporaryDir da, dh;
    QVERIFY(da.isValid() && dh.isValid());
    const QByteArray small = payload(int(kBlock + 4321), 8);
    const QByteArray onPart = payload(9728000, 9);   // exactly one eD2K part
    const QString smallPath = QDir(da.path()).filePath(QStringLiteral("small.bin"));
    const QString partPath = QDir(da.path()).filePath(QStringLiteral("part.bin"));
    QVERIFY(writeFile(smallPath, small));
    QVERIFY(writeFile(partPath, onPart));
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    Holder holdSmall(a, dh.path(), QStringLiteral("hold1")), holdPart(a, dh.path(), QStringLiteral("hold2"));
    holdSmall.start();
    holdPart.start();
    QVERIFY(holdSmall.share(smallPath));
    FakeSource src;
    {
        Ed3kShared shared(a, src, da.path());
        shared.setAliasOnlyFiles({smallPath, partPath});
        shared.start();
        QTRY_COMPARE_WITH_TIMEOUT(shared.aliasOnlyEd2k(partPath).size(), 16, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(shared.aliasPublished(Ed3kShared::pathKeyOf(smallPath)), 20000);
        QCOMPARE(shared.aliasOnlyEd2k(smallPath), ed2kHashOf(small));
        QCOMPARE(shared.aliasOnlyEd2k(partPath), ed2kHashOf(onPart));
        QCOMPARE(shared.hashCount(), 2);
        // not served here: no source of its own, its join holds the root
        QCOMPARE(shared.counts().total, std::size_t(0));
        QVERIFY(!shared.holdsRoot(rootOf(small)));
        // the other root is not shared by anyone yet: waits, published once its join shares it
        QVERIFY(!shared.aliasPublished(Ed3kShared::pathKeyOf(partPath)));
        QVERIFY(holdPart.share(partPath));
        QTRY_VERIFY_WITH_TIMEOUT(shared.aliasPublished(Ed3kShared::pathKeyOf(partPath)), 30000);
        // taken off the list: withdrawn
        shared.setAliasOnlyFiles({smallPath});
        QVERIFY(!shared.aliasPublished(Ed3kShared::pathKeyOf(partPath)));
        shared.stop();
        eMule::BackgroundFileWriter::instance().flush();
    }
    // eD2K disabled: the known one is published from the cache, a new one is not read
    const QByteArray fresh = payload(int(kBlock + 99), 10);
    const QString freshPath = QDir(da.path()).filePath(QStringLiteral("fresh.bin"));
    QVERIFY(writeFile(freshPath, fresh));
    Holder holdFresh(a, dh.path(), QStringLiteral("hold3"));
    holdFresh.start();
    QVERIFY(holdFresh.share(freshPath));
    Ed3kShared shared(a, src, da.path());
    shared.setEd2kHashing(false);
    shared.setAliasOnlyFiles({smallPath, freshPath});
    shared.start();
    QVERIFY(shared.aliasPublished(Ed3kShared::pathKeyOf(smallPath)));
    QTest::qWait(300);
    QCOMPARE(shared.hashCount(), 0);
    QVERIFY(!shared.aliasPublished(Ed3kShared::pathKeyOf(freshPath)));
    // eD2K on again: read now
    shared.setEd2kHashing(true);
    QTRY_VERIFY_WITH_TIMEOUT(shared.aliasPublished(Ed3kShared::pathKeyOf(freshPath)), 20000);
    QCOMPARE(shared.aliasOnlyEd2k(freshPath), ed2kHashOf(fresh));
    shared.stop();
    holdSmall.stop();
    holdPart.stop();
    holdFresh.stop();
}

void tst_Ed3kShared::benchHashRun_data()
{
    QTest::addColumn<bool>("newFiles");
    QTest::addColumn<bool>("perDisk");
    QTest::addColumn<bool>("pipe");
    QTest::addColumn<bool>("aichPipe");
    QTest::newRow("library: one at a time (0.7.25)") << false << false << false << false;
    QTest::newRow("library: one per disk") << false << true << false << false;
    QTest::newRow("new files: two reads, ed3k one at a time (0.7.25)") << true << false << false << false;
    QTest::newRow("new files: one read, side hash inline (0.7.26)") << true << true << false << false;
    QTest::newRow("new files: one read, side hash own thread") << true << true << true << false;
    QTest::newRow("new files: one read, side hash + AICH own threads") << true << true << true << true;
}

// EMULE_BENCH_HASH_DIRS=dir1:dir2 (one per disk); _FILES per disk, _MIB per file, _LOAD %
void tst_Ed3kShared::benchHashRun()
{
    const QStringList dirs = qEnvironmentVariable("EMULE_BENCH_HASH_DIRS").split(u':', Qt::SkipEmptyParts);
    if (dirs.isEmpty())
        QSKIP("set EMULE_BENCH_HASH_DIRS");
    QFETCH(bool, newFiles);
    QFETCH(bool, perDisk);
    QFETCH(bool, pipe);
    QFETCH(bool, aichPipe);
    const auto aichWas = KnownFile::aichPipeFrom.exchange(aichPipe ? 16ull << 20 : std::numeric_limits<uint64>::max());
    const auto restoreAich = qScopeGuard([aichWas] { KnownFile::aichPipeFrom = aichWas; });
    const int perDir = envInt("EMULE_BENCH_HASH_FILES", 2);
    const qint64 fileSize = qint64(envInt("EMULE_BENCH_HASH_MIB", 1024)) << 20;
    thePrefs.setHashingDiskLoad(envInt("EMULE_BENCH_HASH_LOAD", 80));

    // fresh files each row: the last row's reads are cached
    std::vector<std::unique_ptr<QTemporaryDir>> roots;
    FakeSource src;
    std::vector<std::vector<SharedEntry>> byDisk(dirs.size());
    static int seed = 100;
    for (int d = 0; d < dirs.size(); ++d) {
        roots.push_back(std::make_unique<QTemporaryDir>(QDir(dirs[d]).filePath(QStringLiteral("emuleqt-hashbench-XXXXXX"))));
        QVERIFY(roots.back()->isValid());
        for (int i = 0; i < perDir; ++i) {
            const QString name = QStringLiteral("f%1.bin").arg(i);
            const QString path = QDir(roots.back()->path()).filePath(name);
            QVERIFY2(writeUncached(path, fileSize, ++seed), qPrintable(path));
            QByteArray md4(16, Qt::Uninitialized);
            for (int b = 0; b < 16; ++b)
                md4[b] = char(seed * 7 + b);
            byDisk[d].push_back(SharedEntry{md4, path, name, std::uint64_t(fileSize)});
        }
    }
    const int total = int(dirs.size()) * perDir;

    QTemporaryDir cfg;
    QVERIFY(cfg.isValid());
    Ed3kSession a;
    QVERIFY(a.start(loopback(cfg.path())));
    Ed3kShared shared(a, src, cfg.path());
    if (!perDisk)
        shared.setVolumeKeyFn([](const QString&) { return QStringLiteral("one"); });
    QSignalSpy spy(&shared, &Ed3kShared::fileShared);
    QElapsedTimer t;
    std::atomic<int> ed2kDone = 0;
    std::vector<std::thread> workers;
    t.start();
    if (!newFiles) {
        for (const auto& disk : byDisk)
            src.entries.insert(src.entries.end(), disk.begin(), disk.end());
        shared.start();
    } else {
        // eD2K hashes one file per disk (as in 0.7.25); ed3k gets each file as eD2K lists it
        shared.start();
        const SideHasherFactory factory = perDisk ? shared.sideHasherFactory(pipe ? Ed3kShared::kSidePipeFrom : std::numeric_limits<quint64>::max())
                                                  : SideHasherFactory{};
        for (const auto& disk : byDisk) {
            workers.emplace_back([&, disk, factory] {   // factory: this block ends before the workers
                DiskLoadLimiter::lowerThisThread();
                for (const auto& e : disk) {
                    const QFileInfo fi(e.path);
                    std::unique_ptr<SideHash> side = factory ? factory(e.path, e.size, false) : nullptr;
                    KnownFile kf;
                    if (!kf.createFromFile(fi.absolutePath(), fi.fileName(), {}, nullptr, nullptr, nullptr, side.get()))
                        continue;
                    if (side)
                        side->finish(e.md4, e.path, e.size, fi.lastModified().toMSecsSinceEpoch());
                    QMetaObject::invokeMethod(&shared, [&, e] {
                        src.entries.push_back(e);
                        shared.onFileAdded(e);
                    }, Qt::QueuedConnection);
                    ++ed2kDone;
                }
            });
        }
    }
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() == total, 3'600'000);
    const qint64 ms = t.elapsed();
    for (auto& w : workers)
        w.join();
    const double gib = double(total) * double(fileSize) / double(1 << 30);
    qInfo("%s: %d disk(s) × %d × %lld MiB = %.1f GiB in %.1f s → %.0f MB/s, ed3k reads %d",
          QTest::currentDataTag(), int(dirs.size()), perDir, fileSize >> 20, gib, ms / 1000.0,
          double(total) * double(fileSize) / 1e6 / (ms / 1000.0), shared.hashCount());
    QCOMPARE(int(ed2kDone), newFiles ? total : 0);
    shared.stop();
}

#include "tst_Ed3kShared.moc"
