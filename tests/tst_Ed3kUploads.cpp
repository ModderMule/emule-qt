/// @file tst_Ed3kUploads.cpp
/// @brief The ed3k upload queue view (Ed3kUploads, Ed3kUploadsBridge): the queue events as
/// rows, observed only (never handled); real nodes on loopback: a file of A fetched by three
/// nodes over two slots shows holders and a waiting node, named, and the rows end.

#include "Ed3kJoinBase.h"
#include "Ed3kSession.h"
#include "Ed3kShared.h"
#include "Ed3kUploads.h"
#include "Ed3kUploadsBridge.h"

#include <QCborMap>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

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

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
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

/// A downloader: fetches a root into memory over the base's download path.
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

::ed3k::NodeId node(std::uint8_t b)
{
    ::ed3k::NodeId n;
    n.bytes.fill(b);
    return n;
}

} // namespace

class tst_Ed3kUploads : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void eventsBecomeRowsAndStayUnhandled();
    void aQueueOfRealNodesShowsAndEnds();
};

void tst_Ed3kUploads::cleanup()
{
    Ed3kUploadsBridge::setUploads(nullptr);
}

void tst_Ed3kUploads::eventsBecomeRowsAndStayUnhandled()
{
    namespace e = ::ed3k::event;
    Ed3kSession s;   // not started: the events by hand
    Ed3kUploads up(s);
    ::ed3k::Root root;
    root.bytes.fill(7);
    up.setNameLookup([root](const ::ed3k::Root& r) -> std::optional<std::pair<QString, std::uint64_t>> {
        if (r == root)
            return std::pair{QStringLiteral("film.mkv"), std::uint64_t(1234)};
        return std::nullopt;
    });
    QSignalSpy changed(&up, &Ed3kUploads::changed);
    const auto send = [&s](const ::ed3k::Event& ev) {
        bool handled = false;
        emit s.eventPolled(ev, &handled);
        return handled;
    };
    // two ask: both wait, the first is place 1; nobody marks them handled here
    QVERIFY(!send(e::FileRequested{node(1), root, std::nullopt, true, false, true}));
    QVERIFY(!send(e::FileRequested{node(2), root, std::nullopt, true, false, true}));
    QCOMPARE(up.waiting().size(), std::size_t(2));
    QCOMPARE(up.waiting().front().node, node(1));
    QCOMPARE(up.waiting().front().place, 1);
    QCOMPARE(up.waiting().front().fileName, QStringLiteral("film.mkv"));
    QCOMPARE(up.waiting().front().fileSize, std::uint64_t(1234));
    // node 1's blocks go out: it holds a slot, with the bytes of the turn
    QVERIFY(!send(e::SlotUsed{node(1), 300'000, false}));
    QCOMPARE(up.uploading().size(), std::size_t(1));
    QCOMPARE(up.uploading().front().bytes, std::uint64_t(300'000));
    QCOMPARE(up.waiting().size(), std::size_t(1));
    QCOMPARE(up.waiting().front().place, 1);
    // a failed offer leaves node 2 waiting; its queue entry ends: gone
    QVERIFY(!send(e::SlotOfferResult{node(2), false}));
    QCOMPARE(up.waiting().size(), std::size_t(1));
    QVERIFY(!send(e::QueueEntryEnded{node(2)}));
    QVERIFY(up.waiting().empty());
    // the slot ends
    QVERIFY(!send(e::SlotEnded{node(1), ::ed3k::SlotEnd::Taken, 300'000}));
    QCOMPARE(up.size(), std::size_t(0));
    QVERIFY(changed.count() >= 5);

    // GetUploads' rows
    QVERIFY(!send(e::FileRequested{node(3), root, std::nullopt, true, true, true}));
    Ed3kUploadsBridge::setUploads(&up);
    QCborArray uploading, waiting;
    Ed3kUploadsBridge::appendRows(uploading, waiting);
    QCOMPARE(uploading.size(), 0);
    QCOMPARE(waiting.size(), 1);
    const QCborMap row = waiting.at(0).toMap();
    QCOMPARE(row.value(QStringLiteral("network")).toString(), QStringLiteral("ed3k"));
    QCOMPARE(row.value(QStringLiteral("uploadState")).toString(), QStringLiteral("OnQueue"));
    QCOMPARE(row.value(QStringLiteral("userHash")).toString(), QStringLiteral("03").repeated(16));
    QCOMPARE(row.value(QStringLiteral("uploadFileName")).toString(), QStringLiteral("film.mkv"));
    QCOMPARE(row.value(QStringLiteral("queueRank")).toInteger(), 1);
    QVERIFY(row.value(QStringLiteral("lan")).toBool());
}

void tst_Ed3kUploads::aQueueOfRealNodesShowsAndEnds()
{
    const QByteArray data = payload(int(8 * kBlock + 77), 4);
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

    Ed3kUploads up(a);
    up.setNameLookup([&shared](const ::ed3k::Root& r) -> std::optional<std::pair<QString, std::uint64_t>> {
        const auto e = shared.entryOfRoot(r);
        return e ? std::optional(std::pair{e->name, e->size}) : std::nullopt;
    });
    std::size_t mostHolding = 0;
    bool sawWaiting = false;
    bool named = false;
    connect(&up, &Ed3kUploads::changed, this, [&] {
        const auto h = up.uploading();
        mostHolding = std::max(mostHolding, h.size());
        sawWaiting = sawWaiting || !up.waiting().empty();
        for (const auto& r : h)
            named = named || (r.fileName == QStringLiteral("q.bin") && r.fileSize == std::uint64_t(data.size()));
    });

    Fetcher fb(b, db.path()), fc(c, dc.path()), fd(d, dd.path());
    for (Fetcher* f : {&fb, &fc, &fd}) {
        f->setSearchEnabled(false);
        f->start();
    }
    QVERIFY(fb.fetch(root, std::uint64_t(data.size()), sourceOf(a)));
    QVERIFY(fc.fetch(root, std::uint64_t(data.size()), sourceOf(a)));
    QVERIFY(fd.fetch(root, std::uint64_t(data.size()), sourceOf(a)));
    // the joins still answer the blocks: the view only observes
    QTRY_VERIFY_WITH_TIMEOUT(fb.got == data && fc.got == data && fd.got == data, 60000);
    QVERIFY(mostHolding >= 1);
    QVERIFY(mostHolding <= 2);   // the two slots
    QVERIFY(sawWaiting);         // three nodes, two slots: one waited
    QVERIFY(named);
    // done: the downloaders close their streams, the slots end
    fb.stop();
    fc.stop();
    fd.stop();
    // an idle slot ends after about a minute (SlotEnd::Idle) when the close is not seen
    QTRY_COMPARE_WITH_TIMEOUT(up.uploading().size(), std::size_t(0), 150000);
    shared.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kUploads)
#include "tst_Ed3kUploads.moc"
