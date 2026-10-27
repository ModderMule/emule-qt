/// @file tst_Ed3kTorrentJoin.cpp
/// @brief The torrent join (src/ed3k/Ed3kTorrentJoin, torrent-join.md Part C) between real
/// BtSessions and ed3k nodes on loopback: a torrent completed over BitTorrent gives its set
/// (members verified and shared, a manifest); with that set, a torrent with no BitTorrent
/// peer is filled from ed3k alone (blocks into the files, pieces to libtorrent); the join
/// is saved and loaded again; what completed is shared again after a restart, and its
/// records published again; ed3k is not asked for what libtorrent has already.

#include "BtSession.h"
#include "Ed3kSession.h"
#include "Ed3kTorrentJoin.h"

#include <libtorrent/create_torrent.hpp>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <cstring>

namespace lt = libtorrent;
using namespace eMule;
using namespace eMule::ed3k;
using namespace eMule::bt;

namespace {

constexpr int kTimeoutMs = 60'000;
constexpr int kPieceLength = 64 * 1024;

QByteArray payload(int size, int seed)
{
    QByteArray data(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 13 + (i >> 7) * 3 + seed * 29) & 0xFF);
    return data;
}

struct Hashed {
    ::ed3k::Root root;
    std::uint64_t size = 0;
    ::ed3k::Bytes hashes;
};

Hashed hashOf(const QByteArray& data)
{
    ::ed3k::Hasher h;
    (void)h.update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())});
    auto done = h.finish();
    Hashed out;
    if (done) {
        out.root = done->root;
        out.size = done->size;
        const auto b = done->hashes.bytes();
        out.hashes.assign(b.begin(), b.end());
    }
    return out;
}

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

BtSettings btSettings(const QString& dir)
{
    BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.seedRatioPercent = 0;
    s.stateDir = dir + QStringLiteral("/BT");
    s.downloadDir = dir + QStringLiteral("/Incoming");
    s.userAgent = QStringLiteral("eMuleQt-test/0.0.0");
    return s;
}

/// A shares @p data under its root and answers every request with a slot and the block.
void shareFrom(Ed3kSession& a, const QByteArray& data, const Hashed& h, QObject* context)
{
    a.post([h](::ed3k::Engine& e, ::ed3k::Now now) {
        const ::ed3k::SharedFile f{h.root, h.size, "f.bin", std::nullopt, std::nullopt, ::ed3k::Have::complete(), std::span{h.hashes}};
        QVERIFY(e.fileOpen(now, f).has_value());
    });
    QObject::connect(&a, &Ed3kSession::eventPolled, context, [&a, data, root = h.root](const ::ed3k::Event& ev, bool* handled) {
        namespace event = ::ed3k::event;
        if (const auto* r = std::get_if<event::FileRequested>(&ev); r && r->root == root) {
            if (r->first)
                (void)a.engine()->queueSet(a.now(), r->node, 0, 0);
            *handled = true;
        } else if (const auto* w = std::get_if<event::WantBlock>(&ev); w && w->root == root) {
            auto buf = ::ed3k::Buffer::alloc(w->len);
            if (buf && w->offset + w->len <= std::uint64_t(data.size())) {
                std::memcpy(buf->bytes().data(), data.constData() + w->offset, w->len);
                (void)a.engine()->blockProvide(a.now(), w->id, std::move(*buf));
            } else {
                (void)a.engine()->blockFail(a.now(), w->id);
            }
            *handled = true;
        }
    });
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class tst_Ed3kTorrentJoin : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void completeTorrentGivesItsSet();
    void ed3kAloneFillsATorrent();
    void linkJoinFetchesItsSet();
    void ed3kSkipsPiecesLibtorrentHas();

private:
    QTemporaryDir m_dir;
    QString m_seedDir;
    /// two files: the hybrid torrent pads between them
    QByteArray m_a, m_b;
    QByteArray m_torrent;
    /// what completeTorrentGivesItsSet published
    QByteArray m_manifest;
};

void tst_Ed3kTorrentJoin::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_seedDir = m_dir.filePath(QStringLiteral("seed"));
    QVERIFY(QDir().mkpath(m_seedDir + QStringLiteral("/set")));
    m_a = payload(300'000, 1);
    m_b = payload(200'000 + 77, 2);
    for (const auto& [name, data] : {std::pair{QStringLiteral("a.bin"), m_a}, std::pair{QStringLiteral("b.bin"), m_b}}) {
        QFile f(m_seedDir + QStringLiteral("/set/") + name);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(data), data.size());
    }
    lt::create_torrent t(lt::list_files(QString(m_seedDir + QStringLiteral("/set")).toStdString()), kPieceLength);
    lt::set_piece_hashes(t, m_seedDir.toStdString());
    const std::vector<char> buf = t.generate_buf();
    m_torrent = QByteArray(buf.data(), qsizetype(buf.size()));
}

// §4.2 with no set found: the torrent's hashes judge; once complete, its members are
// hashed, verified and shared, and the set's manifest is built
void tst_Ed3kTorrentJoin::completeTorrentGivesItsSet()
{
    BtSession seeder;
    seeder.start(btSettings(m_dir.filePath(QStringLiteral("seeder1"))));
    const BtAddResult seeded = seeder.addTorrentFile(m_torrent, m_seedDir);
    QCOMPARE(seeded.outcome, BtAddOutcome::Added);
    QTRY_VERIFY_WITH_TIMEOUT(seeder.torrent(seeded.id) && seeder.torrent(seeded.id)->state == BtState::Seeding, kTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(seeder.status().listenPort != 0, 5000);

    const QString dir = m_dir.filePath(QStringLiteral("leech1"));
    Ed3kSession node;
    QVERIFY(node.start(loopback(dir)));
    BtSession leecher;
    leecher.start(btSettings(dir));
    Ed3kTorrentJoin join(node, leecher, dir);
    join.setSearchEnabled(false);
    join.start();
    QSignalSpy verified(&join, &Ed3kTorrentJoin::memberVerified);
    QSignalSpy completed(&join, &Ed3kTorrentJoin::torrentCompleted);
    QSignalSpy finished(&leecher, &BtSession::torrentFinished);

    // a magnet with the seeder as its peer: the join starts once the metadata is there
    const auto row = seeder.torrent(seeded.id);
    QVERIFY(row.has_value() && !row->hashV1.isEmpty());
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:%1&x.pe=127.0.0.1:%2").arg(row->hashV1).arg(seeder.status().listenPort);
    const BtAddResult added = leecher.addMagnet(magnet);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QTRY_VERIFY_WITH_TIMEOUT(join.hasJoin(added.id), kTimeoutMs);
    QCOMPARE(join.info(added.id)->members, std::uint64_t(2));
    QVERIFY(!join.info(added.id)->manifest);
    QVERIFY(leecher.layout(added.id).has_value());

    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), kTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 2, kTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, kTimeoutMs);
    m_manifest = join.completedManifest(added.id);
    QVERIFY(!m_manifest.isEmpty());
    const auto m0 = join.member(added.id, 0);
    QVERIFY(m0 && m0->root && *m0->root == hashOf(m_a).root);
    QCOMPARE(m0->status, ::ed3k::MemberStatus::Verified);
    const ::ed3k::Root manifestRoot = hashOf(m_manifest).root;
    QVERIFY(join.sharesRoot(manifestRoot));

    // a restart: the manifest, the info dictionary and the members are shared again (kept
    // under Config/ed3k/torrent/<id>/; members hashed again first)
    join.stop();
    QVERIFY(!join.sharesRoot(manifestRoot));
    Ed3kTorrentJoin again(node, leecher, dir);
    again.setSearchEnabled(false);
    QSignalSpy republished(&again, &Ed3kTorrentJoin::keptRecordsPublished);
    again.start();
    QVERIFY(again.sharesRoot(manifestRoot));
    QVERIFY(again.sharesRoot(hashOf(leecher.infoSection(added.id)).root));
    QTRY_VERIFY_WITH_TIMEOUT(again.sharesRoot(hashOf(m_a).root) && again.sharesRoot(hashOf(m_b).root), 10000);
    // the set (MEMBER rows) and the ALIAS records (btih and btmh: hybrid) go out again
    QTRY_COMPARE_WITH_TIMEOUT(republished.count(), 1, 10000);
    QCOMPARE(republished.at(0).at(0).toString(), added.id);
    QCOMPARE(republished.at(0).at(1).toInt(), 2);
    again.stop();

    join.stop();
    leecher.stop();
    seeder.stop();
    node.stop();
}

// §4.2 with the set: no BitTorrent peer at all, both members come from another ed3k
// node; each piece their blocks complete goes to libtorrent, which finishes the torrent.
// Then the join is saved, and loaded again with its set.
void tst_Ed3kTorrentJoin::ed3kAloneFillsATorrent()
{
    if (m_manifest.isEmpty())
        QSKIP("needs the set of completeTorrentGivesItsSet");
    // one node per member: a node grants a slot on a peer's first request only
    QTemporaryDir da, dc;
    QVERIFY(da.isValid() && dc.isValid());
    Ed3kSession a, c;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(c.start(loopback(dc.path())));
    shareFrom(a, m_a, hashOf(m_a), this);
    shareFrom(c, m_b, hashOf(m_b), this);

    const QString dir = m_dir.filePath(QStringLiteral("leech2"));
    Ed3kSession node;
    QVERIFY(node.start(loopback(dir)));
    BtSession leecher;
    leecher.start(btSettings(dir));
    Ed3kTorrentJoin join(node, leecher, dir);
    join.setSearchEnabled(false);
    join.start();
    QSignalSpy finished(&leecher, &BtSession::torrentFinished);

    const BtAddResult added = leecher.addTorrentFile(m_torrent);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QVERIFY(join.setManifest(added.id, {reinterpret_cast<const std::byte*>(m_manifest.constData()), std::size_t(m_manifest.size())}));
    QVERIFY(join.info(added.id)->manifest.has_value());
    join.addSource(added.id, 0, ::ed3k::Source{*a.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, a.udpPort()), std::nullopt, {}});
    join.addSource(added.id, 1, ::ed3k::Source{*c.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, c.udpPort()), std::nullopt, {}});

    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), kTimeoutMs);
    const auto layout = leecher.layout(added.id);
    QVERIFY(layout.has_value());
    QStringList paths;
    for (const auto& f : layout->files)
        if (!f.pad)
            paths << f.path;
    QCOMPARE(paths.size(), 2);
    QCOMPARE(readFile(paths.at(0)), m_a);
    QCOMPARE(readFile(paths.at(1)), m_b);

    // saved at stop, loaded with its set
    join.stop();
    QVERIFY(QFile::exists(join.joinPath(added.id)));
    Ed3kTorrentJoin again(node, leecher, dir);
    again.setSearchEnabled(false);
    again.start();
    QVERIFY(again.hasJoin(added.id));
    QVERIFY(again.info(added.id)->manifest.has_value());
    again.stop();

    leecher.stop();
    node.stop();
    a.stop();
    c.stop();
}

// §4.1: a magnet whose `xs` is the ed3k:file link of the set. The manifest comes over
// ed3k first and BLAKE3 judges; the pieces come from a BitTorrent seeder.
void tst_Ed3kTorrentJoin::linkJoinFetchesItsSet()
{
    if (m_manifest.isEmpty())
        QSKIP("needs the set of completeTorrentGivesItsSet");
    BtSession seeder;
    seeder.start(btSettings(m_dir.filePath(QStringLiteral("seeder3"))));
    const BtAddResult seeded = seeder.addTorrentFile(m_torrent, m_seedDir);
    QCOMPARE(seeded.outcome, BtAddOutcome::Added);
    QTRY_VERIFY_WITH_TIMEOUT(seeder.torrent(seeded.id) && seeder.torrent(seeded.id)->state == BtState::Seeding, kTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(seeder.status().listenPort != 0, 5000);
    const QString hashV1 = seeder.torrent(seeded.id)->hashV1;

    // the manifest and both members on other ed3k nodes (one per root: a slot per peer)
    QTemporaryDir dm, da, dc;
    QVERIFY(dm.isValid() && da.isValid() && dc.isValid());
    Ed3kSession m, a, c;
    QVERIFY(m.start(loopback(dm.path())) && a.start(loopback(da.path())) && c.start(loopback(dc.path())));
    const Hashed manifest = hashOf(m_manifest);
    shareFrom(m, m_manifest, manifest, this);
    shareFrom(a, m_a, hashOf(m_a), this);
    shareFrom(c, m_b, hashOf(m_b), this);

    ::ed3k::FileLink fl;
    fl.root = manifest.root;
    fl.size = manifest.size;
    fl.name = "set";
    fl.other = {{"bt", hashV1.toStdString()}};
    const auto link = ::ed3k::writeFileLink(fl);
    QVERIFY(link.has_value());
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:%1&x.pe=127.0.0.1:%2&xs=%3")
                               .arg(hashV1)
                               .arg(seeder.status().listenPort)
                               .arg(QString::fromLatin1(QUrl::toPercentEncoding(QString::fromStdString(*link))));

    const QString dir = m_dir.filePath(QStringLiteral("leech3"));
    Ed3kSession node;
    QVERIFY(node.start(loopback(dir)));
    BtSession leecher;
    leecher.start(btSettings(dir));
    Ed3kTorrentJoin join(node, leecher, dir);
    join.setSearchEnabled(false);
    join.start();
    QSignalSpy verified(&join, &Ed3kTorrentJoin::memberVerified);
    QSignalSpy problems(&join, &Ed3kTorrentJoin::memberProblem);

    const BtAddResult added = leecher.addMagnet(magnet);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QCOMPARE(leecher.ed3kLink(added.id), QString::fromStdString(*link));
    QTRY_VERIFY_WITH_TIMEOUT(join.hasJoin(added.id), kTimeoutMs);
    QCOMPARE(join.info(added.id)->judge, ::ed3k::Judge::Blake3);
    join.addManifestSource(added.id, ::ed3k::Source{*m.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, m.udpPort()), std::nullopt, {}});
    QTRY_VERIFY_WITH_TIMEOUT(join.info(added.id)->manifest.has_value(), kTimeoutMs);
    QCOMPARE(*join.info(added.id)->manifest, manifest.root);
    join.addSource(added.id, 0, ::ed3k::Source{*a.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, a.udpPort()), std::nullopt, {}});
    join.addSource(added.id, 1, ::ed3k::Source{*c.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, c.udpPort()), std::nullopt, {}});

    QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 2, kTimeoutMs);
    QCOMPARE(problems.count(), 0);

    join.stop();
    leecher.stop();
    seeder.stop();
    for (Ed3kSession* s : {&node, &m, &a, &c})
        s->stop();
}

// The first four pieces of member A are on disk before the add: libtorrent's check finds
// them, and ed3k is asked only for the block it lacks (block 0 is pieces 0-3).
void tst_Ed3kTorrentJoin::ed3kSkipsPiecesLibtorrentHas()
{
    if (m_manifest.isEmpty())
        QSKIP("needs the set of completeTorrentGivesItsSet");
    constexpr int kHad = 4 * kPieceLength;
    static_assert(kHad == 256 * 1024, "block 0 of a member is pieces 0-3");
    QTemporaryDir da, dc;
    QVERIFY(da.isValid() && dc.isValid());
    Ed3kSession a, c;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(c.start(loopback(dc.path())));
    shareFrom(a, m_a, hashOf(m_a), this);
    shareFrom(c, m_b, hashOf(m_b), this);

    const QString dir = m_dir.filePath(QStringLiteral("leech4"));
    const BtSettings settings = btSettings(dir);
    QVERIFY(QDir().mkpath(settings.downloadDir + QStringLiteral("/set")));
    {
        QFile f(settings.downloadDir + QStringLiteral("/set/a.bin"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(m_a.left(kHad)), qint64(kHad));
    }
    Ed3kSession node;
    QVERIFY(node.start(loopback(dir)));
    BtSession leecher;
    leecher.start(settings);
    Ed3kTorrentJoin join(node, leecher, dir);
    join.setSearchEnabled(false);
    join.start();
    QSignalSpy finished(&leecher, &BtSession::torrentFinished);

    const BtAddResult added = leecher.addTorrentFile(m_torrent);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QTRY_COMPARE_WITH_TIMEOUT(leecher.havePieces(added.id).count(true), qsizetype(4), kTimeoutMs);
    QVERIFY(join.setManifest(added.id, {reinterpret_cast<const std::byte*>(m_manifest.constData()), std::size_t(m_manifest.size())}));
    join.addSource(added.id, 0, ::ed3k::Source{*a.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, a.udpPort()), std::nullopt, {}});
    join.addSource(added.id, 1, ::ed3k::Source{*c.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, c.udpPort()), std::nullopt, {}});

    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), kTimeoutMs);
    QCOMPARE(readFile(settings.downloadDir + QStringLiteral("/set/a.bin")), m_a);
    // member A: only its second block came over ed3k; member B: its one block
    QCOMPARE(join.receivedBlocks(added.id, 0), 1);
    QCOMPARE(join.receivedBlocks(added.id, 1), 1);

    join.stop();
    leecher.stop();
    node.stop();
    a.stop();
    c.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kTorrentJoin)
#include "tst_Ed3kTorrentJoin.moc"
