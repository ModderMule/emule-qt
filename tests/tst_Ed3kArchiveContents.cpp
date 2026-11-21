/// @file tst_Ed3kArchiveContents.cpp
/// @brief Archive contents records (hoster.md §6.1): reading and hashing a set, the first
/// volumes of a release, extracting only chosen entries, and a listing published by one
/// node and found by another over Kad (three loopback nodes, the middle one stores).

#include "Ed3kArchiveContents.h"
#include "RarFixtures.h"
#include "Ed3kSession.h"
#include "Ed3kLocalSharer.h"
#include "archive/ArchiveContentsHook.h"
#include "archive/ArchiveReader.h"
#include "archive/ExternalUnpacker.h"
#include "post/UsenetUnpacker.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <archive.h>
#include <archive_entry.h>

using namespace eMule;
using namespace eMule::ed3k;

namespace {

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

::ed3k::Node nodeOf(const Ed3kSession& s)
{
    return ::ed3k::Node{*s.nodeId(), ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort())};
}

bool writeZip(const QString& path, const QList<QPair<QString, QByteArray>>& members)
{
    auto* a = archive_write_new();
    archive_write_set_format_zip(a);
    if (archive_write_open_filename(a, path.toUtf8().constData()) != ARCHIVE_OK) {
        archive_write_free(a);
        return false;
    }
    bool ok = true;
    for (const auto& [name, data] : members) {
        auto* entry = archive_entry_new();
        archive_entry_set_pathname(entry, name.toUtf8().constData());
        archive_entry_set_size(entry, data.size());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        ok = ok && archive_write_header(a, entry) == ARCHIVE_OK
             && archive_write_data(a, data.constData(), size_t(data.size())) == data.size();
        archive_entry_free(entry);
    }
    archive_write_close(a);
    archive_write_free(a);
    return ok;
}

::ed3k::Root rootOf(const QByteArray& data)
{
    ::ed3k::Hasher h;
    (void)h.update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())));
    return h.finish()->root;
}

::ed3k::Root rootOfFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? rootOf(f.readAll()) : ::ed3k::Root{};
}

/// 7-Zip's `a` into @p archive; empty: no 7zz here.
bool make7z(const QString& tool, const QString& archive, const QString& dir, const QStringList& extra)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(tool, QStringList{QStringLiteral("a"), QStringLiteral("-y"), QStringLiteral("-bd")} + extra
                      + QStringList{archive, QStringLiteral(".")});
    return p.waitForFinished(60000) && p.exitCode() == 0;
}

/// Every listing a tap made while the hook was set.
struct HookCapture {
    std::vector<ArchiveListing> listings;
    HookCapture()
    {
        setArchiveContentsHook([this]() -> std::unique_ptr<ArchiveContentsTap> {
            return std::make_unique<Ed3kContentsTap>([this](ArchiveListing l) { listings.push_back(std::move(l)); });
        });
    }
    ~HookCapture() { setArchiveContentsHook({}); }
};

QByteArray payload(int size, int seed)
{
    QByteArray b(size, '\0');
    for (int i = 0; i < size; ++i)
        b[i] = char((i * 31 + seed * 7) & 0xff);
    return b;
}

} // namespace

class tst_Ed3kArchiveContents : public QObject {
    Q_OBJECT

private slots:
    void firstVolumesPickTheOpeningVolume();
    void volumeSetsListEveryPartInOrder();
    void readingHashesEveryEntry();
    void onlyChosenEntriesAreExtracted();
    void publishedContentsAreFound();
    void entriesOnDiskAreMarked();
    void unpackedEntriesAreComparedWithTheManifest();
    void unpackReadFeedsTheRecord();
    void splitVolumesHashedInTheSameRead();
    void encryptedSetsAreListedWithThePassword();
    void rarSetsAreTappedInTheUnpackRead_data();
    void rarSetsAreTappedInTheUnpackRead();
};

void tst_Ed3kArchiveContents::firstVolumesPickTheOpeningVolume()
{
    const QStringList names{QStringLiteral("a.part1.rar"),   QStringLiteral("a.part2.rar"), QStringLiteral("b.r00"),
                            QStringLiteral("b.rar"),         QStringLiteral("c.zip"),       QStringLiteral("x.part2.rar"),
                            QStringLiteral("dir/d.7z.001"), QStringLiteral("dir/d.7z.002"), QStringLiteral("readme.nfo")};
    const QList<int> firsts = Ed3kArchiveContents::firstVolumes(names);
    QCOMPARE(firsts, (QList<int>{0, 3, 4, 6}));   // x has no first volume
}

void tst_Ed3kArchiveContents::volumeSetsListEveryPartInOrder()
{
    const QStringList names{QStringLiteral("a.part2.rar"), QStringLiteral("b.r01"),       QStringLiteral("a.part1.rar"),
                            QStringLiteral("b.rar"),       QStringLiteral("b.r00"),       QStringLiteral("c.zip"),
                            QStringLiteral("x.part2.rar"), QStringLiteral("readme.nfo")};
    // opening volume first, then by volume; sets by their opening volume; x is headless
    QCOMPARE(Ed3kArchiveContents::volumeSets(names), (QList<QList<int>>{{2, 0}, {3, 4, 1}, {5}}));
}

void tst_Ed3kArchiveContents::readingHashesEveryEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray big = payload(600 * 1024, 1);
    const QString zip = dir.filePath(QStringLiteral("release.zip"));
    QVERIFY(writeZip(zip, {{QStringLiteral("movie/feature.mkv"), big}, {QStringLiteral("readme.txt"), "hello"}}));

    const auto listing = Ed3kArchiveContents::readArchive({zip}, {});
    QVERIFY(listing);
    QCOMPARE(listing->firstName, QStringLiteral("release.zip"));
    QFile f(zip);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(listing->first, rootOf(f.readAll()));
    QCOMPARE(int(listing->entries.size()), 2);
    QCOMPARE(listing->entries[0].path, QStringLiteral("movie/feature.mkv"));
    QCOMPARE(listing->entries[0].size, std::uint64_t(big.size()));
    QCOMPARE(listing->entries[0].root, rootOf(big));
    QCOMPARE(listing->entries[1].root, rootOf("hello"));
    QVERIFY(listing->password.isEmpty());

    // a path the manifest refuses: no listing
    const QString bad = dir.filePath(QStringLiteral("bad.zip"));
    QVERIFY(writeZip(bad, {{QStringLiteral("a"), "1"}, {QStringLiteral("a/b"), "2"}}));
    QVERIFY(!Ed3kArchiveContents::readArchive({bad}, {}));
}

void tst_Ed3kArchiveContents::onlyChosenEntriesAreExtracted()
{
    QTemporaryDir src, dest;
    QVERIFY(src.isValid() && dest.isValid());
    QVERIFY(writeZip(src.filePath(QStringLiteral("set.zip")),
                     {{QStringLiteral("keep/one.bin"), "1"}, {QStringLiteral("drop.bin"), "2"}, {QStringLiteral("keep/two.bin"), "3"}}));
    usenet::UsenetUnpacker unpacker;
    usenet::UsenetUnpacker::Options options;
    options.onlyEntries.insert(QStringLiteral("set.zip"), {QStringLiteral("keep/one.bin"), QStringLiteral("keep/two.bin")});
    const auto r = unpacker.unpackAll(src.path(), dest.path(), options);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(QFile::exists(dest.filePath(QStringLiteral("keep/one.bin"))));
    QVERIFY(QFile::exists(dest.filePath(QStringLiteral("keep/two.bin"))));
    QVERIFY(!QFile::exists(dest.filePath(QStringLiteral("drop.bin"))));
    QCOMPARE(r.extractedFiles.size(), 2);

    // ArchiveReader's filter alone
    ArchiveReader reader;
    QVERIFY(reader.open(src.filePath(QStringLiteral("set.zip"))));
    reader.setEntryFilter([](const QString& p) { return p == QLatin1String("drop.bin"); });
    QTemporaryDir out;
    QVERIFY(reader.extractAll(out.path()));
    QCOMPARE(reader.extractedFiles().size(), 1);
}

// #224: entries whose root a join of the node serves from disk (Shared Files) or a folder
// scan found are marked; all of them: the archive need not be downloaded
void tst_Ed3kArchiveContents::entriesOnDiskAreMarked()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray ep = payload(300 * 1024, 7);
    const QByteArray nfo = payload(500, 8);
    const QString epPath = dir.filePath(QStringLiteral("ep1.mkv"));
    const QString nfoPath = dir.filePath(QStringLiteral("x/info.nfo"));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("x"))));
    for (const auto& [path, data] : {std::pair{epPath, ep}, std::pair{nfoPath, nfo}}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(data), data.size());
    }
    Ed3kSession node;
    QVERIFY(node.start(loopback(dir.filePath(QStringLiteral("cfg")))));
    eMule::ed3k::test::LocalSharer local(node, dir.filePath(QStringLiteral("cfg")));
    local.start();
    QVERIFY(local.shareFile(epPath, ep));
    Ed3kArchiveContents contents(node, dir.filePath(QStringLiteral("cfg")));
    contents.start();

    Ed3kArchiveContents::Found f;
    f.name = QStringLiteral("show.rar");
    f.state = Ed3kArchiveContents::State::Found;
    f.entries.push_back({QStringLiteral("show/ep1.mkv"), std::uint64_t(ep.size()), rootOf(ep)});
    f.entries.push_back({QStringLiteral("show/info.nfo"), std::uint64_t(nfo.size()), rootOf(nfo)});
    std::vector<Ed3kArchiveContents::Found> found{f};
    contents.markLocal(found);
    QCOMPARE(found[0].localPaths.size(), std::size_t(2));
    QCOMPARE(found[0].localPaths[0], epPath);
    QVERIFY(found[0].localPaths[1].isEmpty());
    QVERIFY(!found[0].allLocal());
    // a size other than the file's: not that file
    QVERIFY(contents.localPathOf(rootOf(ep), std::uint64_t(ep.size()) + 1).isEmpty());

    // the second one found by a folder scan
    Ed3kJoinBase::addFoundCopy(node, rootOf(nfo), nfoPath, std::uint64_t(nfo.size()), QFileInfo(nfoPath).lastModified().toMSecsSinceEpoch());
    contents.markLocal(found);
    QCOMPARE(found[0].localPaths[1], nfoPath);
    QVERIFY(found[0].allLocal());
    contents.stop();
    local.stop();
}

void tst_Ed3kArchiveContents::publishedContentsAreFound()
{
    QTemporaryDir da, db, dc, work;
    QVERIFY(da.isValid() && db.isValid() && dc.isValid() && work.isValid());
    const QByteArray feature = payload(300 * 1024, 4);
    const QString zip = work.filePath(QStringLiteral("show.zip"));
    QVERIFY(writeZip(zip, {{QStringLiteral("show/ep1.mkv"), feature}, {QStringLiteral("show/info.nfo"), "nfo"}}));
    const auto listing = Ed3kArchiveContents::readArchive({zip}, {});
    QVERIFY(listing);

    Ed3kSession a, b, c;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QVERIFY(c.start(loopback(dc.path())));
    // A and C know B: the record is stored there, the lookup asks there
    const ::ed3k::Node bNode = nodeOf(b);
    a.post([&](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1}); });
    c.post([&](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1}); });
    QTRY_VERIFY_WITH_TIMEOUT(a.contactCount() > 0 && c.contactCount() > 0, 20000);

    Ed3kArchiveContents publisher(a, da.path());
    publisher.start();
    QVERIFY(publisher.publishListing(*listing));
    QCOMPARE(publisher.published(), 1);

    Ed3kArchiveContents searcher(c, dc.path());
    searcher.start();
    // the record needs a moment to be stored at B: ask again until found
    std::optional<std::vector<Ed3kArchiveContents::Found>> answer;
    for (int attempt = 0; attempt < 6; ++attempt) {
        std::optional<std::vector<Ed3kArchiveContents::Found>> got;
        Ed3kArchiveContents::Request r;
        r.volumes.push_back({QStringLiteral("show.zip"), listing->first, {}, {}});
        QVERIFY(searcher.lookup(std::move(r), [&](const std::vector<Ed3kArchiveContents::Found>& f) { got = f; }, 20000));
        QTRY_VERIFY_WITH_TIMEOUT(got.has_value(), 30000);
        if (!got->empty() && got->front().state == Ed3kArchiveContents::State::Found) {
            answer = got;
            break;
        }
        QTest::qWait(2000);
    }
    QVERIFY(answer);
    const auto& found = answer->front();
    QCOMPARE(found.name, QStringLiteral("show.zip"));
    QCOMPARE(int(found.entries.size()), 2);
    QCOMPARE(found.entries[0].path, QStringLiteral("show/ep1.mkv"));
    QCOMPARE(found.entries[0].root, rootOf(feature));
    QCOMPARE(found.entries[0].size, std::uint64_t(feature.size()));
    QVERIFY(found.first && *found.first == listing->first);

    // §6.1 "Unpacking is the check": the archive read here differs from what was found:
    // publisher ignored, listing dropped
    QVERIFY(searcher.holdsFoundListing(listing->first));
    ArchiveListing unpacked = *listing;
    unpacked.entries.back().size += 1;
    const auto check = searcher.checkListing(unpacked);
    QVERIFY(check && !check->matches());
    QCOMPARE(check->kind, std::uint32_t(ED3K_CONTENTS_DIFFERS));
    QCOMPARE(searcher.mismatches(), 1);
    QVERIFY(!searcher.holdsFoundListing(listing->first));
    QVERIFY(!searcher.checkListing(unpacked));   // checked once

    // nothing published for another volume
    std::optional<std::vector<Ed3kArchiveContents::Found>> none;
    Ed3kArchiveContents::Request r;
    r.volumes.push_back({QStringLiteral("other.zip"), rootOf("other"), {}, {}});
    QVERIFY(searcher.lookup(std::move(r), [&](const std::vector<Ed3kArchiveContents::Found>& f) { none = f; }, 30000));
    QTRY_VERIFY_WITH_TIMEOUT(none.has_value(), 40000);
    QVERIFY(none->front().state == Ed3kArchiveContents::State::NotPublished
            || none->front().state == Ed3kArchiveContents::State::TimedOut);

    searcher.stop();
    publisher.stop();
    a.stop();
    b.stop();
    c.stop();
}

void tst_Ed3kArchiveContents::unpackedEntriesAreComparedWithTheManifest()
{
    const auto entry = [](const char* path, const QByteArray& data) {
        return ArchiveEntry{QString::fromLatin1(path), std::uint64_t(data.size()), rootOf(data)};
    };
    const std::vector<ArchiveEntry> listed{entry("a/one.bin", "one"), entry("b.txt", "bee")};
    std::vector<::ed3k::ContentEntry> c;
    for (const auto& e : listed)
        c.push_back({e.path.toStdString(), e.size, e.root});
    const auto manifest = ::ed3k::contentsManifest(c);
    QVERIFY(manifest);
    const QByteArray file(reinterpret_cast<const char*>(manifest->file.data()), qsizetype(manifest->file.size()));

    const auto same = Ed3kArchiveContents::compareListing(file, listed);
    QVERIFY(same && same->matches());
    auto changed = listed;
    changed[1].root = rootOf("other");
    const auto differs = Ed3kArchiveContents::compareListing(file, changed);
    QVERIFY(differs && differs->kind == ED3K_CONTENTS_DIFFERS);
    auto extra = listed;
    extra.push_back(entry("c.bin", "sea"));
    const auto notListed = Ed3kArchiveContents::compareListing(file, extra);
    QVERIFY(notListed && notListed->kind == ED3K_CONTENTS_NOT_LISTED);
    QCOMPARE(notListed->index, std::uint32_t(2));
    const auto missing = Ed3kArchiveContents::compareListing(file, {listed.front()});
    QVERIFY(missing && missing->kind == ED3K_CONTENTS_MISSING);
    const auto garbage = Ed3kArchiveContents::compareListing(QByteArrayLiteral("not a manifest"), listed);
    QVERIFY(!garbage || garbage->kind == ED3K_CONTENTS_MANIFEST);
}

void tst_Ed3kArchiveContents::unpackReadFeedsTheRecord()
{
    QTemporaryDir src, dest;
    QVERIFY(src.isValid() && dest.isValid());
    const QByteArray big = payload(400 * 1024, 7);
    const QString zip = src.filePath(QStringLiteral("set.zip"));
    QVERIFY(writeZip(zip, {{QStringLiteral("keep/one.bin"), big}, {QStringLiteral("drop.bin"), "2"}}));
    HookCapture capture;
    usenet::UsenetUnpacker unpacker;
    usenet::UsenetUnpacker::Options options;
    options.onlyEntries.insert(QStringLiteral("set.zip"), {QStringLiteral("keep/one.bin")});
    const auto r = unpacker.unpackAll(src.path(), dest.path(), options);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(capture.listings.empty());   // nothing before the commit
    QCOMPARE(r.contents.size(), 1);
    r.commitContents();
    QCOMPARE(int(capture.listings.size()), 1);
    const ArchiveListing& l = capture.listings.front();
    QCOMPARE(l.firstName, QStringLiteral("set.zip"));
    QCOMPARE(l.first, rootOfFile(zip));
    // the unchosen entry is listed too: hashed in the same read, not written
    QCOMPARE(int(l.entries.size()), 2);
    QVERIFY(!QFile::exists(dest.filePath(QStringLiteral("drop.bin"))));
    const auto one = std::ranges::find(l.entries, QStringLiteral("keep/one.bin"), &ArchiveEntry::path);
    QVERIFY(one != l.entries.end());
    QCOMPARE(one->root, rootOf(big));
}

void tst_Ed3kArchiveContents::splitVolumesHashedInTheSameRead()
{
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("7zz"), {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")});
    if (tool.isEmpty())
        QSKIP("7zz not installed");
    QTemporaryDir in, out;
    QVERIFY(in.isValid() && out.isValid());
    const QByteArray a = payload(700 * 1024, 2);
    QFile f(in.filePath(QStringLiteral("a.bin")));
    QVERIFY(f.open(QIODevice::WriteOnly) && f.write(a) == a.size());
    f.close();
    // stored, so the volumes split the payload; three volumes
    QVERIFY(make7z(tool, out.filePath(QStringLiteral("s.7z")), in.path(), {QStringLiteral("-mx0"), QStringLiteral("-v256k")}));
    const QStringList volumes{out.filePath(QStringLiteral("s.7z.001")), out.filePath(QStringLiteral("s.7z.002")),
                              out.filePath(QStringLiteral("s.7z.003"))};
    for (const QString& v : volumes)
        QVERIFY2(QFile::exists(v), qPrintable(v));
    Ed3kContentsTap tap({}, int(volumes.size()));
    const auto pw = Ed3kArchiveContents::readSet(volumes, {}, {}, tap);
    QVERIFY(pw && pw->isEmpty());
    for (int i = 0; i < volumes.size(); ++i) {
        const auto v = tap.volume(i);
        QVERIFY(v);
        QCOMPARE(v->root, rootOfFile(volumes.at(i)));
    }
    const auto l = tap.listing(QStringLiteral("s.7z.001"), {});
    QVERIFY(l);
    QCOMPARE(int(l->entries.size()), 1);
    QCOMPARE(l->entries.front().root, rootOf(a));
}

void tst_Ed3kArchiveContents::encryptedSetsAreListedWithThePassword()
{
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("7zz"), {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")});
    if (tool.isEmpty())
        QSKIP("7zz not installed");
    QTemporaryDir in, out, dest;
    QVERIFY(in.isValid() && out.isValid() && dest.isValid());
    const QByteArray a = payload(300 * 1024, 5);
    QDir(in.path()).mkpath(QStringLiteral("d"));
    QFile f(in.filePath(QStringLiteral("d/movie.mkv")));
    QVERIFY(f.open(QIODevice::WriteOnly) && f.write(a) == a.size());
    f.close();
    QFile g(in.filePath(QStringLiteral("info.nfo")));
    QVERIFY(g.open(QIODevice::WriteOnly) && g.write("nfo") == 3);
    g.close();
    // header-encrypted: libarchive cannot even list it
    const QString archive = out.filePath(QStringLiteral("enc.7z"));
    QVERIFY(make7z(tool, archive, in.path(), {QStringLiteral("-psecret"), QStringLiteral("-mhe=on")}));

    QVERIFY(!Ed3kArchiveContents::readArchive({archive}, {QStringLiteral("wrong")}, nullptr, tool));
    const auto direct = Ed3kArchiveContents::readArchive({archive}, {QStringLiteral("wrong"), QStringLiteral("secret")}, nullptr, tool);
    QVERIFY(direct);
    QCOMPARE(direct->password, QStringLiteral("secret"));
    QCOMPARE(direct->first, rootOfFile(archive));
    QCOMPARE(int(direct->entries.size()), 2);

    // data-encrypted only: libarchive lists, the tool reads
    const QString dataOnly = out.filePath(QStringLiteral("data.7z"));
    QVERIFY(make7z(tool, dataOnly, in.path(), {QStringLiteral("-psecret")}));
    const auto listed = Ed3kArchiveContents::readArchive({dataOnly}, {QStringLiteral("secret")}, nullptr, tool);
    QVERIFY(listed);
    QCOMPARE(listed->first, rootOfFile(dataOnly));
    QCOMPARE(int(listed->entries.size()), 2);
    QFile::remove(dataOnly);

    // the unpack: the tool streams the entries out, hashed on the way
    HookCapture capture;
    usenet::UsenetUnpacker unpacker;
    usenet::UsenetUnpacker::Options options;
    options.passwords = {QStringLiteral("wrong"), QStringLiteral("secret")};
    options.externalTool = tool;
    options.onlyEntries.insert(QStringLiteral("enc.7z"), {QStringLiteral("d/movie.mkv")});
    const auto r = unpacker.unpackAll(out.path(), dest.path(), options);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(QFile::exists(dest.filePath(QStringLiteral("d/movie.mkv"))));
    QVERIFY(!QFile::exists(dest.filePath(QStringLiteral("info.nfo"))));
    r.commitContents();
    QCOMPARE(int(capture.listings.size()), 1);
    QCOMPARE(capture.listings.front().password, QStringLiteral("secret"));
    QCOMPARE(int(capture.listings.front().entries.size()), 2);
    QCOMPARE(capture.listings.front().first, direct->first);
}

void tst_Ed3kArchiveContents::rarSetsAreTappedInTheUnpackRead_data()
{
    QTest::addColumn<bool>("rar5");
    QTest::addColumn<bool>("solid");
    // the RAR5 fixture is RarReader's (headers), libarchive does not open it as a set
    QTest::newRow("rar4") << false << false;
    QTest::newRow("rar4 solid") << false << true;
}

void tst_Ed3kArchiveContents::rarSetsAreTappedInTheUnpackRead()
{
    QFETCH(bool, rar5);
    QFETCH(bool, solid);
    QTemporaryDir src, dest;
    QVERIFY(src.isValid() && dest.isValid());
    const QByteArray one = payload(200 * 1024, 3);
    const QByteArray two = payload(90 * 1024, 4);
    const QList<QByteArray> volumes = eMule::testing::rar::makeStoredRarSet(
        QList<eMule::testing::rar::RarMember>{{"one.bin", one}, {"sub/two.bin", two}}, 64 * 1024, rar5, solid);
    QVERIFY(volumes.size() > 2);
    QStringList paths;
    for (int i = 0; i < volumes.size(); ++i) {
        const QString path = src.filePath(QStringLiteral("rel.part%1.rar").arg(i + 1));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(volumes.at(i)) == volumes.at(i).size());
        paths << path;
    }
    HookCapture capture;
    usenet::UsenetUnpacker unpacker;
    usenet::UsenetUnpacker::Options options;
    options.onlyEntries.insert(QStringLiteral("rel.part1.rar"), {QStringLiteral("one.bin")});
    const auto r = unpacker.unpackAll(src.path(), dest.path(), options);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(QFile::exists(dest.filePath(QStringLiteral("one.bin"))));
    QVERIFY(!QFile::exists(dest.filePath(QStringLiteral("sub/two.bin"))));
    r.commitContents();
    QCOMPARE(int(capture.listings.size()), 1);
    const ArchiveListing& l = capture.listings.front();
    QCOMPARE(l.first, rootOfFile(paths.first()));
    QCOMPARE(int(l.entries.size()), 2);
    QCOMPARE(l.entries[0].root, rootOf(one));
    QCOMPARE(l.entries[1].path, QStringLiteral("sub/two.bin"));
    QCOMPARE(l.entries[1].root, rootOf(two));

    // a torrent's verify: every volume hashed in the same read
    Ed3kContentsTap tap({}, int(paths.size()));
    QVERIFY(Ed3kArchiveContents::readSet(paths, {}, {}, tap));
    for (int i = 0; i < paths.size(); ++i)
        QCOMPARE(tap.volume(i)->root, rootOfFile(paths.at(i)));
}

QTEST_GUILESS_MAIN(tst_Ed3kArchiveContents)
#include "tst_Ed3kArchiveContents.moc"
