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
#include <QElapsedTimer>
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
    s.smallNetS = 1;   // a few nodes: connected 1 s after the first answer, not 45 s
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
    void eachPublisherIsASource();
    void theFilterTakesSizeExtensionAndSources();
    void aSharedFileIsFound();
    void aSharedFilesNoteAndMediaAreFound();
    void clipUtf8KeepsWholeCharacters();
    void aNoteAuthorShowsItsNodeId();
    void withoutANodeTheSearchEnds();
    void aChannelSearchTakesChannelFilesOnly();
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
    // sources: the complete ones; partial on top
    QCOMPARE(row->sourceCount(), 7u);
    QCOMPARE(row->completeSourceCount(), 5u);
    QCOMPARE(row->getIntTagValue(FT_MEDIA_LENGTH), 200u);
    // the child copy keeps the root
    SearchFile child(row.get());
    QVERIFY(child.isEd3kResult());
    // no name or no size: no row
    f.name.clear();
    QVERIFY(!Ed3kSearch::toSearchFile(f));
}

void tst_Ed3kSearch::eachPublisherIsASource()
{
    const auto file = [](std::uint8_t root, std::uint8_t who, std::uint32_t sources, std::uint32_t partial) {
        ::ed3k::FoundFile f;
        f.root.bytes.fill(root);
        f.publisher.bytes.fill(who);
        f.size = 1000;
        f.name = "a.mkv";
        f.sources = sources;
        f.partial = partial;
        return f;
    };
    // root 1: three publishers, two of them older (0/0); root 2: one claims 4, one holds part
    std::vector<::ed3k::FoundFile> files{file(1, 1, 0, 0), file(1, 2, 0, 0), file(1, 3, 1, 0), file(2, 1, 4, 0), file(2, 2, 0, 1)};
    files.push_back(file(3, 1, 0, 0));
    files.back().sites = 6;   // an index node counted 6 sites
    const auto avail = Ed3kSearch::availabilityOf(files);
    QCOMPARE(avail.size(), std::size_t(3));
    const auto at = [&](std::uint8_t root) { return avail.at(QByteArray(32, char(root))); };
    QCOMPARE(at(1).complete, 3u);
    QCOMPARE(at(1).partial, 0u);
    QCOMPARE(at(2).complete, 4u);
    QCOMPARE(at(2).partial, 1u);
    QCOMPARE(at(3).complete, 6u);
    const auto a = at(2);
    const auto row = Ed3kSearch::toSearchFile(files[3], &a);
    QVERIFY(row);
    QCOMPARE(row->sourceCount(), 5u);
    QCOMPARE(row->completeSourceCount(), 4u);
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
    QVERIFY(!(*it)->ed3kJudged().hidden);

    // declined-records.md §2: a root of the searcher's decline list: its row hidden, by decline.txt
    c.setDeclined(DeclineList{.roots = {*root}});
    rows.clear();
    ended.reset();
    search.setSinks([&](uint32, std::unique_ptr<SearchFile> f) { rows.push_back(std::move(f)); },
                    [&](uint32, const QString& error) { ended = error; });
    search.startMetaSearch(43, SearchType::Ed3k, p, {}, {});
    QTRY_VERIFY_WITH_TIMEOUT(ended.has_value(), 60000);
    const auto declined = std::ranges::find_if(rows, [&](const auto& r) {
        return r->ed3kRoot() == QByteArray(reinterpret_cast<const char*>(root->bytes.data()), 32);
    });
    QVERIFY(declined != rows.end());
    QVERIFY((*declined)->ed3kJudged().hidden);
    QCOMPARE((*declined)->ed3kJudged().by, QString::fromLatin1(kDeclineFile));
    c.setDeclined({});
    search.setSinks([&](uint32 id, std::unique_ptr<SearchFile> f) {
        QCOMPARE(id, 42u);
        rows.push_back(std::move(f));
    }, [&](uint32 id, const QString& error) {
        QCOMPARE(id, 42u);
        ended = error;
    });

    // a cancelled search reports nothing
    ended.reset();
    search.startMetaSearch(42, SearchType::Ed3k, p, {}, {});
    search.cancelMetaSearch(42);
    QTest::qWait(3000);
    QVERIFY(!ended);
    a.disconnect(this);
    shared.stop();
}

void tst_Ed3kSearch::aSharedFilesNoteAndMediaAreFound()
{
    QTemporaryDir da, db, dc;
    QVERIFY(da.isValid() && db.isValid() && dc.isValid());
    QByteArray data(200 * 1024, 'n');
    const QString path = QDir(da.path()).filePath(QStringLiteral("silver harbour.mp3"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(data) == data.size());
    }
    Ed3kSession a, b, c;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QVERIFY(c.start(loopback(dc.path())));
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
    const QByteArray md4(16, char(0x55));
    SharedEntry entry{md4, path, QStringLiteral("silver harbour.mp3"), std::uint64_t(data.size())};
    entry.rating = 4;
    entry.comment = QStringLiteral("clean rip");
    entry.duration = 215;
    entry.bitrate = 320;
    entry.artist = QStringLiteral("Harbour Band");
    entry.album = QStringLiteral("Tides");
    entry.title = QStringLiteral("Silver Harbour");
    entry.codec = QStringLiteral("mp3");
    src.entries.push_back(entry);
    Ed3kShared shared(a, src, da.path());
    shared.setNoteNick([] { return QStringLiteral("Mule"); });
    QSignalSpy sharedSpy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(sharedSpy.count(), 1, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(stored > 0, 30000);
    const auto root = shared.rootOf(md4);
    QVERIFY(root);
    const QByteArray rootBytes(reinterpret_cast<const char*>(root->bytes.data()), 32);

    // the keyword record names length and bitrate: the row's media tags
    Ed3kSearch search(c);
    std::vector<std::unique_ptr<SearchFile>> rows;
    std::optional<QString> ended;
    search.setSinks([&](uint32, std::unique_ptr<SearchFile> f) { rows.push_back(std::move(f)); },
                    [&](uint32, const QString& error) { ended = error; });
    SearchParams p;
    p.type = SearchType::Ed3k;
    p.expression = QStringLiteral("harbour");
    search.startMetaSearch(51, SearchType::Ed3k, p, {}, {});
    QTRY_VERIFY_WITH_TIMEOUT(ended.has_value(), 60000);
    const auto it = std::ranges::find_if(rows, [&](const auto& r) { return r->ed3kRoot() == rootBytes; });
    QVERIFY(it != rows.end());
    QCOMPARE((*it)->getIntTagValue(FT_MEDIA_LENGTH), 215u);
    QCOMPARE((*it)->getIntTagValue(FT_MEDIA_BITRATE), 320u);
    QCOMPARE((*it)->getStrTagValue(FT_MEDIA_ARTIST), QStringLiteral("Harbour Band"));
    QCOMPARE((*it)->getStrTagValue(FT_MEDIA_ALBUM), QStringLiteral("Tides"));
    QCOMPARE((*it)->getStrTagValue(FT_MEDIA_TITLE), QStringLiteral("Silver Harbour"));
    QCOMPARE((*it)->getStrTagValue(FT_MEDIA_CODEC), QStringLiteral("mp3"));

    // the NOTE record (§10.7): rating and comment under the root, by A
    struct Found {
        QByteArray root;
        std::uint32_t rating = 0;
        QString text;
        QString nick;
    };
    std::vector<Found> notes;
    search.setNoteSink([&](const QByteArray& r, const QByteArray&, std::uint32_t rating, const QString& text, const QString& nick) {
        notes.push_back({r, rating, text, nick});
    });
    // the note may land after the keywords: ask again until it is there (one lookup per root
    // at a time: refused while one runs)
    QElapsedTimer waited;
    waited.start();
    while (notes.empty() && waited.elapsed() < 60000) {
        (void)search.searchNotes(rootBytes);
        QTest::qWait(500);
    }
    QVERIFY(!notes.empty());
    QCOMPARE(notes.front().root, rootBytes);
    QCOMPARE(notes.front().rating, 4u);
    QCOMPARE(notes.front().text, QStringLiteral("clean rip"));
    QCOMPARE(notes.front().nick, QStringLiteral("Mule"));
    a.disconnect(this);
    shared.stop();
}

void tst_Ed3kSearch::clipUtf8KeepsWholeCharacters()
{
    QCOMPARE(Ed3kShared::clipUtf8(QStringLiteral("abc"), 3), QStringLiteral("abc"));
    QCOMPARE(Ed3kShared::clipUtf8(QStringLiteral("abcd"), 3), QStringLiteral("abc"));
    // ä is 2 bytes: not cut in half
    QCOMPARE(Ed3kShared::clipUtf8(QStringLiteral("aä"), 2), QStringLiteral("a"));
    // 😀 is 4 bytes and a surrogate pair: dropped whole
    QCOMPARE(Ed3kShared::clipUtf8(QStringLiteral("a😀"), 4), QStringLiteral("a"));
    QCOMPARE(Ed3kShared::clipUtf8(QStringLiteral("a😀"), 5), QStringLiteral("a😀"));
}

void tst_Ed3kSearch::aNoteAuthorShowsItsNodeId()
{
    const QByteArray id = QByteArray::fromHex("a1b2c3d4e5f60718293a4b5c6d7e8f90");
    QCOMPARE(Ed3kSearch::noteAuthor(QStringLiteral("Mule"), id), QStringLiteral("Mule (a1b2c3d4)"));
    QVERIFY(Ed3kSearch::noteAuthor({}, id).isEmpty());
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

void tst_Ed3kSearch::aChannelSearchTakesChannelFilesOnly()
{
    // SearchType::Ed3kLive (live.md §1.2): a channel file's name ends in .ed3klive, ≤ 64 KiB
    ::ed3k::FoundFile f;
    f.size = 900;
    f.name = "Harbour Cam.ed3klive";
    QVERIFY(Ed3kSearch::isChannelFile(f));
    f.name = "Harbour Cam.ED3KLIVE";
    QVERIFY(Ed3kSearch::isChannelFile(f));
    f.size = 64 * 1024 + 1;
    QVERIFY(!Ed3kSearch::isChannelFile(f));
    f.size = 900;
    f.name = "Harbour Cam.mkv";
    QVERIFY(!Ed3kSearch::isChannelFile(f));
    // without a fetcher the row shows what its name says
    f.name = "Harbour Cam.ed3klive";
    auto row = Ed3kSearch::toSearchFile(f);
    QVERIFY(row);
    Ed3kSearch::applyChannel(*row, nullptr);
    QCOMPARE(row->getStrTagValue(FT_MEDIA_TITLE), QStringLiteral("Harbour Cam"));
    QVERIFY(row->getStrTagValue(FT_MEDIA_CODEC).isEmpty());
}

QTEST_GUILESS_MAIN(tst_Ed3kSearch)
#include "tst_Ed3kSearch.moc"
