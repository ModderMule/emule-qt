/// @file tst_Ed3kSearch.cpp
/// @brief ed3k keyword search (src/ed3k/Ed3kSearch): node A shares a file (its keywords
/// go out, Ed3kShared), node C's search finds it through node B; rows and filters.

#include "Ed3kSearch.h"
#include "Ed3kSession.h"
#include "Ed3kShared.h"

#include "search/SearchFile.h"
#include "search/SearchParams.h"
#include "utils/Opcodes.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;
using namespace eMule::ed3k;

namespace {

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

::ed3k::Node nodeOf(const Ed3kSession& s)
{
    return ::ed3k::Node{*s.nodeId(), ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort())};
}

} // namespace

class tst_Ed3kSearch : public QObject {
    Q_OBJECT

private slots:
    void aFoundFileIsAnEd3kRow();
    void theFilterTakesSizeExtensionAndSources();
    void aSharedFileIsFound();
    void withoutANodeTheSearchEnds();
};

void tst_Ed3kSearch::aFoundFileIsAnEd3kRow()
{
    ::ed3k::FoundFile f;
    f.root.bytes.fill(0xa1);
    f.size = 123456;
    f.name = "summer song.mp3";
    f.sources = 5;
    f.partial = 2;
    f.duration = 200;
    const auto row = Ed3kSearch::toSearchFile(f);
    QVERIFY(row);
    QVERIFY(row->isEd3kResult());
    QVERIFY(!row->isMetaResult());
    QCOMPARE(row->ed3kRoot(), QByteArray(32, char(0xa1)));
    QVERIFY(row->ed3kEd2k().isEmpty());
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(row->fileHash()), 16), QByteArray(16, char(0xa1)));
    QCOMPARE(row->fileName(), QStringLiteral("summer song.mp3"));
    QCOMPARE(static_cast<uint64>(row->fileSize()), uint64(123456));
    QCOMPARE(row->sourceCount(), 5u);
    QCOMPARE(row->completeSourceCount(), 3u);
    QCOMPARE(row->getIntTagValue(FT_MEDIA_LENGTH), 200u);
    // the child copy keeps the root
    SearchFile child(row.get());
    QVERIFY(child.isEd3kResult());
    // no name or no size: no row
    f.name.clear();
    QVERIFY(!Ed3kSearch::toSearchFile(f));
}

void tst_Ed3kSearch::theFilterTakesSizeExtensionAndSources()
{
    SearchParams p;
    p.extension = QStringLiteral(".mkv");
    p.minSize = 10;
    p.maxSize = 1000;
    p.availability = 3;
    const auto f = Ed3kSearch::filterOf(p);
    QCOMPARE(f.ext, std::string("mkv"));
    QCOMPARE(f.minSize, std::uint64_t(10));
    QCOMPARE(f.maxSize, std::uint64_t(1000));
    QCOMPARE(f.minSources, 3u);
    QVERIFY(f.none.empty());
}

void tst_Ed3kSearch::aSharedFileIsFound()
{
    QTemporaryDir da, db, dc;
    QVERIFY(da.isValid() && db.isValid() && dc.isValid());
    QByteArray data(300 * 1024, 'x');
    const QString path = QDir(da.path()).filePath(QStringLiteral("golden lighthouse.bin"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(data) == data.size());
    }
    Ed3kSession a, b, c;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QVERIFY(c.start(loopback(dc.path())));
    // A and C know B: the keyword record goes there, the search asks there
    const ::ed3k::Node bNode = nodeOf(b);
    a.post([&](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1}); });
    c.post([&](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1}); });
    QTRY_VERIFY_WITH_TIMEOUT(a.contactCount() > 0 && c.contactCount() > 0, 20000);

    std::uint64_t stored = 0;
    connect(&a, &Ed3kSession::eventPolled, this, [&](const ::ed3k::Event& ev, bool*) {
        if (const auto* k = std::get_if<::ed3k::event::KeywordsPublished>(&ev))
            stored += k->stored;
    }, Qt::DirectConnection);
    FakeSource src;
    const QByteArray md4(16, char(0x77));
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("golden lighthouse.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(a, src, da.path());
    QSignalSpy sharedSpy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(sharedSpy.count(), 1, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(stored > 0, 30000);
    const auto root = shared.rootOf(md4);
    QVERIFY(root);

    Ed3kSearch search(c);
    std::vector<std::unique_ptr<SearchFile>> rows;
    std::optional<QString> ended;
    search.setSinks([&](uint32 id, std::unique_ptr<SearchFile> f) {
        QCOMPARE(id, 41u);
        rows.push_back(std::move(f));
    }, [&](uint32 id, const QString& error) {
        QCOMPARE(id, 41u);
        ended = error;
    });
    SearchParams p;
    p.type = SearchType::Ed3k;
    p.expression = QStringLiteral("lighthouse");
    search.startMetaSearch(41, SearchType::Ed3k, p, {}, {});
    QVERIFY(search.isRunning(41));
    QTRY_VERIFY_WITH_TIMEOUT(ended.has_value(), 60000);
    QVERIFY(ended->isEmpty());
    QVERIFY(!search.isRunning(41));
    QVERIFY(!rows.empty());
    const auto it = std::ranges::find_if(rows, [&](const auto& r) {
        return r->ed3kRoot() == QByteArray(reinterpret_cast<const char*>(root->bytes.data()), 32);
    });
    QVERIFY(it != rows.end());
    QCOMPARE((*it)->fileName(), QStringLiteral("golden lighthouse.bin"));
    QCOMPARE(static_cast<uint64>((*it)->fileSize()), uint64(data.size()));
    // the publisher named its MD4 (Ed3kShared)
    QCOMPARE((*it)->ed3kEd2k(), md4);

    // a cancelled search reports nothing
    ended.reset();
    search.startMetaSearch(42, SearchType::Ed3k, p, {}, {});
    search.cancelMetaSearch(42);
    QTest::qWait(3000);
    QVERIFY(!ended);
    a.disconnect(this);
    shared.stop();
}

void tst_Ed3kSearch::withoutANodeTheSearchEnds()
{
    Ed3kSession off;
    Ed3kSearch search(off);
    std::optional<QString> ended;
    search.setSinks([](uint32, std::unique_ptr<SearchFile>) {}, [&](uint32, const QString& e) { ended = e; });
    SearchParams p;
    p.expression = QStringLiteral("anything");
    search.startMetaSearch(1, SearchType::Ed3k, p, {}, {});
    QVERIFY(!ended);   // never before returning
    QTRY_VERIFY(ended.has_value());
    QVERIFY(!ended->isEmpty());
}

QTEST_GUILESS_MAIN(tst_Ed3kSearch)
#include "tst_Ed3kSearch.moc"
