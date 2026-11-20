/// @file tst_Ed3kTorrentJoin.cpp
/// @brief The torrent join (src/ed3k/Ed3kTorrentJoin, torrent-join.md Part C) between real
/// BtSessions and ed3k nodes on loopback: a torrent completed over BitTorrent gives its set
/// (members verified and shared, a manifest); with that set, a torrent with no BitTorrent
/// peer is filled from ed3k alone (blocks into the files, pieces to libtorrent); the join
/// is saved and loaded again; what completed is shared again after a restart, and its
/// records published again; ed3k is not asked for what libtorrent has already; a member
/// file gone before the worker reads it fails its pieces; a member whose root another join
/// of the node serves from disk is read from there (#224).

#include "BtSession.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"
#include "Ed3kTorrentJoin.h"
#include "Ed3kLocalSharer.h"

#include <libtorrent/create_torrent.hpp>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
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
    // loopback only: with port 0 each local address gets its own port, and
    // listen_port() may name another one than 127.0.0.1's (multi-adapter Windows)
    s.bindAddresses = {QStringLiteral("127.0.0.1")};
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

// was: LocalSharer here (#224); now tests/Ed3kLocalSharer.h, shared with the other joins' tests
using test::LocalSharer;

} // namespace

class tst_Ed3kTorrentJoin : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void completeTorrentGivesItsSet();
    void createdTorrentGivesItsSet();
    void ed3kAloneFillsATorrent();
    void aLocalCopyFillsATorrent();
    void linkJoinFetchesItsSet();
    void ed3kSkipsPiecesLibtorrentHas();
    void aGoneMemberFailsItsRead();
    void i2pTorrentJoinsOnlyWhenAllowed();
    void privateTorrentJoinsOnlyWhenShared();
    void aFirstSetSearchThatFindsNothingEnds();
    void aFalseClaimNeverFailsATorrentPiece();
    void aTorrentAloneFindsItsPeerOverEd3k();
    void aRestartKeepsTheSetOfAResumedTorrent();

private:
    QTemporaryDir m_dir;
    QString m_seedDir;
    /// two files: the hybrid torrent pads between them
    QByteArray m_a, m_b;
    QByteArray m_torrent;
    QByteArray m_privateTorrent;
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
    // the same files, marked private: another info hash
    t.set_priv(true);
    const std::vector<char> priv = t.generate_buf();
    m_privateTorrent = QByteArray(priv.data(), qsizetype(priv.size()));
}

// #191: a torrent made here and seeded at once (seed mode, complete from the start) joins
// all the same: BLAKE3 roots, the canonical set (the one a downloader builds), records
void tst_Ed3kTorrentJoin::createdTorrentGivesItsSet()
{
    QVERIFY2(!m_manifest.isEmpty(), "needs completeTorrentGivesItsSet first");
    const QString dir = m_dir.filePath(QStringLiteral("made1"));
    Ed3kSession node;
    QVERIFY(node.start(loopback(dir)));
    BtSession bt;
    bt.start(btSettings(dir));
    Ed3kTorrentJoin join(node, bt, dir);
    join.setSearchEnabled(false);
    join.start();
    QSignalSpy verified(&join, &Ed3kTorrentJoin::memberVerified);
    QSignalSpy completed(&join, &Ed3kTorrentJoin::torrentCompleted);

    const BtAddResult added = bt.addCreatedTorrent(m_torrent, m_seedDir);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    join.adoptCreated(added.id);
    QTRY_VERIFY_WITH_TIMEOUT(join.hasJoin(added.id), kTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 2, kTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, kTimeoutMs);
    QCOMPARE(join.completedManifest(added.id), m_manifest);
    QVERIFY(join.sharesRoot(hashOf(m_a).root));
    QVERIFY(join.sharesRoot(hashOf(m_b).root));
    join.stop();

    // the same torrent added as complete without adoptCreated: no join (unchanged rule)
    const QString dir2 = m_dir.filePath(QStringLiteral("made2"));
    Ed3kSession node2;
    QVERIFY(node2.start(loopback(dir2)));
    BtSession bt2;
    bt2.start(btSettings(dir2));
    const BtAddResult plain = bt2.addCreatedTorrent(m_torrent, m_seedDir);
    QTRY_VERIFY_WITH_TIMEOUT(bt2.torrent(plain.id) && bt2.torrent(plain.id)->haveBytes == bt2.torrent(plain.id)->totalSize, kTimeoutMs);
    Ed3kTorrentJoin join2(node2, bt2, dir2);
    join2.setSearchEnabled(false);
    join2.start();
    QVERIFY(!join2.hasJoin(plain.id));
    join2.stop();
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

    // report 23: the block hashes of that hash are kept with size and mtime, so the next
    // start shares the members at once, without reading them again
    Ed3kTorrentJoin third(node, leecher, dir);
    third.setSearchEnabled(false);
    third.start();
    // was: QVERIFY(third.sharesRoot(...)) right after start(); the kept hashes are read on the
    // read pool now, so the share comes one queued call later
    QTRY_VERIFY_WITH_TIMEOUT(third.sharesRoot(hashOf(m_a).root), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(third.sharesRoot(hashOf(m_b).root), 5000);
    third.stop();

    // a member changed on disk (same size, new mtime): hashed again, and it no longer fits
    const auto layout = leecher.layout(added.id);
    QVERIFY(layout.has_value());
    QString pathA;
    for (const auto& f : layout->files)
        if (!f.pad && pathA.isEmpty())
            pathA = f.path;
    {
        QFile f(pathA);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTimeUtc().addSecs(60), QFileDevice::FileModificationTime));
    }
    Ed3kTorrentJoin fourth(node, leecher, dir);
    fourth.setSearchEnabled(false);
    fourth.start();
    QVERIFY(!fourth.sharesRoot(hashOf(m_a).root));   // its hash runs first
    // was: QVERIFY(fourth.sharesRoot(hashOf(m_b).root)); kept hashes read on the read pool now
    QTRY_VERIFY_WITH_TIMEOUT(fourth.sharesRoot(hashOf(m_b).root), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(fourth.sharesRoot(hashOf(m_a).root), 10000);
    fourth.stop();

    join.stop();
    leecher.stop();
    seeder.stop();
    node.stop();
}

// §4.2 with the set: no BitTorrent peer at all, both members come from another ed3k
// node; each piece their blocks complete goes to libtorrent, which finishes the torrent.
// Then the join is saved, and loaded again with its set.
// #224: with the set, members whose roots are here already (another join serves them from
// disk) are read from there: no BitTorrent peer, no ed3k source, the torrent finishes
void tst_Ed3kTorrentJoin::aLocalCopyFillsATorrent()
{
    if (m_manifest.isEmpty())
        QSKIP("needs the set of completeTorrentGivesItsSet");
    const QString dir = m_dir.filePath(QStringLiteral("local1"));
    Ed3kSession node;
    QVERIFY(node.start(loopback(dir)));
    LocalSharer local(node, dir);
    local.start();
    QVERIFY(local.shareFile(m_seedDir + QStringLiteral("/set/a.bin"), m_a));
    QVERIFY(local.shareFile(m_seedDir + QStringLiteral("/set/b.bin"), m_b));

    BtSession leecher;
    leecher.start(btSettings(dir));
    Ed3kTorrentJoin join(node, leecher, dir);
    join.setSearchEnabled(false);
    join.start();
    QSignalSpy finished(&leecher, &BtSession::torrentFinished);

    const BtAddResult added = leecher.addTorrentFile(m_torrent);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QTRY_VERIFY_WITH_TIMEOUT(join.hasJoin(added.id), kTimeoutMs);
    QVERIFY(join.setManifest(added.id, {reinterpret_cast<const std::byte*>(m_manifest.constData()), std::size_t(m_manifest.size())}));

    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), kTimeoutMs);
    QVERIFY(join.receivedBlocks(added.id, 0) > 0);
    QVERIFY(join.receivedBlocks(added.id, 1) > 0);
    const auto layout = leecher.layout(added.id);
    QVERIFY(layout.has_value());
    QStringList paths;
    for (const auto& f : layout->files)
        if (!f.pad)
            paths << f.path;
    QCOMPARE(paths.size(), 2);
    QCOMPARE(readFile(paths.at(0)), m_a);
    QCOMPARE(readFile(paths.at(1)), m_b);

    join.stop();
    local.stop();
    leecher.stop();
    node.stop();
}

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

// Block 0 of member A spans pieces 0-3: a recheck of them asks the host to read the block.
// The read pool is held busy, the file goes away, then the worker reads: the read fails,
// nothing crashes. §4.2: the torrent's hash judges and nothing waits for our read, so the
// pieces libtorrent read before the file went keep their verdict (report 31).
void tst_Ed3kTorrentJoin::aGoneMemberFailsItsRead()
{
    if (m_manifest.isEmpty())
        QSKIP("needs the set of completeTorrentGivesItsSet");
    constexpr int kHad = 4 * kPieceLength;
    // the block values of A only: every block request is refused
    QTemporaryDir da;
    QVERIFY(da.isValid());
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    shareFrom(a, QByteArray(), hashOf(m_a), this);

    const QString dir = m_dir.filePath(QStringLiteral("leech5"));
    const BtSettings settings = btSettings(dir);
    const QString memberFile = settings.downloadDir + QStringLiteral("/set/a.bin");
    QVERIFY(QDir().mkpath(settings.downloadDir + QStringLiteral("/set")));
    {
        QFile f(memberFile);
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
    QSignalSpy checked(&leecher, &BtSession::torrentChecked);

    const BtAddResult added = leecher.addTorrentFile(m_torrent);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QTRY_COMPARE_WITH_TIMEOUT(leecher.havePieces(added.id).count(true), qsizetype(4), kTimeoutMs);
    QVERIFY(join.setManifest(added.id, {reinterpret_cast<const std::byte*>(m_manifest.constData()), std::size_t(m_manifest.size())}));
    join.addSource(added.id, 0, ::ed3k::Source{*a.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, a.udpPort()), std::nullopt, {}});
    QTRY_VERIFY_WITH_TIMEOUT(join.hasBlockValues(added.id, 0), kTimeoutMs);

    // both read threads busy until the file is gone
    QSemaphore gate;
    QThreadPool& pool = node.readPool();
    for (int i = 0; i < pool.maxThreadCount(); ++i)
        pool.start([&gate] { gate.acquire(); });
    const qsizetype checks = checked.count();
    QVERIFY(leecher.recheck(added.id));
    QTRY_VERIFY_WITH_TIMEOUT(join.readsInFlight() > 0, kTimeoutMs);
    QVERIFY(QFile::remove(memberFile));
    gate.release(pool.maxThreadCount());

    QTRY_COMPARE_WITH_TIMEOUT(join.readsInFlight(), std::size_t(0), kTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(checked.count() > checks, kTimeoutMs);
    QVERIFY(join.hasJoin(added.id));

    join.stop();
    leecher.stop();
    node.stop();
    a.stop();
}

// btI2pEd3kJoin: an I2P torrent joins while allowed; off drops its join, on brings it back
void tst_Ed3kTorrentJoin::i2pTorrentJoinsOnlyWhenAllowed()
{
    const QString dir = m_dir.filePath(QStringLiteral("i2p"));
    Ed3kSession node;
    QVERIFY(node.start(loopback(dir)));
    BtSettings bs = btSettings(dir);
    bs.i2pEnabled = true;
    BtSession bt;
    bt.start(bs);
    Ed3kTorrentJoin join(node, bt, dir);
    join.setSearchEnabled(false);
    join.setI2pJoins(false);
    join.start();

    const BtAddResult i2p = bt.addTorrentFile(m_torrent, dir + QStringLiteral("/Incoming"), true, true);
    QCOMPARE(i2p.outcome, BtAddOutcome::Added);
    QVERIFY(bt.torrent(i2p.id)->i2p);
    QTest::qWait(200);
    QVERIFY(!join.hasJoin(i2p.id));

    join.setI2pJoins(true);
    QTRY_VERIFY_WITH_TIMEOUT(join.hasJoin(i2p.id), kTimeoutMs);
    join.setI2pJoins(false);
    QVERIFY(!join.hasJoin(i2p.id));

    join.stop();
    bt.stop();
    node.stop();
}

// torrent.md §6.3: a private torrent joins only while private torrents are shared
// (btShareMode "all"); off withdraws its join and shares, on brings them back
void tst_Ed3kTorrentJoin::privateTorrentJoinsOnlyWhenShared()
{
    const QString dir = m_dir.filePath(QStringLiteral("private"));
    Ed3kSession node;
    QVERIFY(node.start(loopback(dir)));
    BtSession bt;
    bt.start(btSettings(dir));
    Ed3kTorrentJoin join(node, bt, dir);
    join.setSearchEnabled(false);
    join.start();   // private joins off by default
    QSignalSpy completed(&join, &Ed3kTorrentJoin::torrentCompleted);

    const BtAddResult added = bt.addCreatedTorrent(m_privateTorrent, m_seedDir);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QVERIFY(bt.torrent(added.id)->isPrivate);
    join.adoptCreated(added.id);
    QTest::qWait(200);
    QVERIFY(!join.hasJoin(added.id));

    // shared: its set is made as for a public one, under its own info hash
    join.setPrivateJoins(true);
    QTRY_VERIFY_WITH_TIMEOUT(join.hasJoin(added.id), kTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, kTimeoutMs);
    const QByteArray manifest = join.completedManifest(added.id);
    QVERIFY(!manifest.isEmpty() && manifest != m_manifest);
    QTRY_VERIFY_WITH_TIMEOUT(join.sharesRoot(hashOf(m_a).root) && join.sharesRoot(hashOf(m_b).root), kTimeoutMs);

    // off: join and shares withdrawn; on: what was kept is shared again
    join.setPrivateJoins(false);
    QVERIFY(!join.hasJoin(added.id));
    QVERIFY(!join.sharesRoot(hashOf(m_a).root) && !join.sharesRoot(hashOf(m_b).root));
    join.setSearchEnabled(true);
    join.setPrivateJoins(true);
    QTRY_VERIFY_WITH_TIMEOUT(join.sharesRoot(hashOf(m_a).root) && join.sharesRoot(hashOf(m_b).root), kTimeoutMs);
    // complete, its set kept: no Kad lookup for its own set
    QVERIFY(join.hasJoin(added.id));
    QVERIFY(!join.setSearching(added.id));

    join.stop();
    bt.stop();
    node.stop();
}

// A fresh node numbers its alias searches from 0. A set search with ID 0 that ended
// (nothing found) was taken for "no search": tryNextSet picked it again and recursed
// until the stack overflowed
void tst_Ed3kTorrentJoin::aFirstSetSearchThatFindsNothingEnds()
{
    const QString dir = m_dir.filePath(QStringLiteral("search0"));
    QTemporaryDir peerDir;
    QVERIFY(peerDir.isValid());
    Ed3kSession node, peer;
    QVERIFY(node.start(loopback(dir)) && peer.start(loopback(peerDir.path())));
    // connected: a lookup is held while the node has no contact, and never ends
    const ::ed3k::Node peerNode{*peer.nodeId(), ::ed3k::Address::v4(127, 0, 0, 1, peer.udpPort())};
    node.post([peerNode](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&peerNode, 1}); });
    QTRY_VERIFY_WITH_TIMEOUT(node.contactCount() > 0, 20000);
    BtSession bt;
    bt.start(btSettings(dir));
    Ed3kTorrentJoin join(node, bt, dir);
    join.start();   // search on: the join's set search is the node's first

    const BtAddResult added = bt.addTorrentFile(m_torrent, dir + QStringLiteral("/Incoming"));
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QTRY_VERIFY_WITH_TIMEOUT(join.hasJoin(added.id), kTimeoutMs);
    // the peer has no records: the lookups end and AliasFound follows
    QTest::qWait(3000);
    QVERIFY(join.hasJoin(added.id));
    QVERIFY(!join.info(added.id)->manifest);

    join.stop();
    bt.stop();
    node.stop();
    peer.stop();
}

// report 31, §4.2: a claimed set whose root of b.bin is wrong. The torrent's hash judges:
// a recheck of complete data finishes, the claim is found false. It used to spoil b.bin's
// piece on every check, and the torrent stayed short of 100 %.
void tst_Ed3kTorrentJoin::aFalseClaimNeverFailsATorrentPiece()
{
    // 256 KiB pieces: b.bin is one ed3k block inside one piece, judged by its root alone
    lt::create_torrent t(lt::list_files(QString(m_seedDir + QStringLiteral("/set")).toStdString()), 256 * 1024);
    lt::set_piece_hashes(t, m_seedDir.toStdString());
    const std::vector<char> buf = t.generate_buf();
    const QByteArray torrent(buf.data(), qsizetype(buf.size()));

    // added to an empty folder (a join is made), then the data comes and is checked
    const auto addThenFill = [&](BtSession& bt, Ed3kTorrentJoin& join, const QString& save) -> QString {
        const BtAddResult added = bt.addTorrentFile(torrent, save);
        if (added.outcome != BtAddOutcome::Added)
            return {};
        if (!QTest::qWaitFor([&] { return join.hasJoin(added.id); }, kTimeoutMs))
            return {};
        return added.id;
    };
    const auto copyData = [&](const QString& save) {
        QVERIFY(QDir().mkpath(save + QStringLiteral("/set")));
        for (const char* name : {"a.bin", "b.bin"}) {
            const QString n = QString::fromLatin1(name);
            QFile::remove(save + QStringLiteral("/set/") + n);
            QVERIFY(QFile::copy(m_seedDir + QStringLiteral("/set/") + n, save + QStringLiteral("/set/") + n));
        }
    };
    const auto complete = [](BtSession& bt, const QString& id) {
        const auto row = bt.torrent(id);
        return row && row->totalSize > 0 && row->haveBytes >= row->totalSize;
    };

    // the true set first: a join that completes builds it
    QByteArray manifest;
    {
        const QString dir = m_dir.filePath(QStringLiteral("claim-true"));
        Ed3kSession node;
        QVERIFY(node.start(loopback(dir)));
        BtSession bt;
        bt.start(btSettings(dir));
        Ed3kTorrentJoin join(node, bt, dir);
        join.setSearchEnabled(false);
        join.start();
        const QString save = dir + QStringLiteral("/data");
        const QString id = addThenFill(bt, join, save);
        QVERIFY(!id.isEmpty());
        copyData(save);
        QVERIFY(bt.recheck(id));
        QTRY_VERIFY_WITH_TIMEOUT(complete(bt, id), kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!join.completedManifest(id).isEmpty(), kTimeoutMs);
        manifest = join.completedManifest(id);
        join.stop();
        bt.stop();
        node.stop();
    }

    // the same set with b.bin's root swapped for the root of other bytes of its size
    QByteArray other = m_b;
    other[1000] = char(other[1000] ^ 0x5A);
    const auto rootBytes = [](const Hashed& h) {
        return QByteArray(reinterpret_cast<const char*>(h.root.bytes.data()), qsizetype(h.root.bytes.size()));
    };
    const QByteArray trueRoot = rootBytes(hashOf(m_b));
    const qsizetype at = manifest.indexOf(trueRoot);
    QVERIFY(at >= 0);
    QCOMPARE(manifest.indexOf(trueRoot, at + 1), qsizetype(-1));
    QByteArray falseSet = manifest;
    falseSet.replace(at, trueRoot.size(), rootBytes(hashOf(other)));

    const QString dir = m_dir.filePath(QStringLiteral("claim-false"));
    Ed3kSession node;
    QVERIFY(node.start(loopback(dir)));
    BtSession bt;
    bt.start(btSettings(dir));
    Ed3kTorrentJoin join(node, bt, dir);
    join.setSearchEnabled(false);
    join.start();
    QSignalSpy problems(&join, &Ed3kTorrentJoin::memberProblem);
    const QString save = dir + QStringLiteral("/data");
    const QString id = addThenFill(bt, join, save);
    QVERIFY(!id.isEmpty());
    QVERIFY(join.setManifest(id, {reinterpret_cast<const std::byte*>(falseSet.constData()), std::size_t(falseSet.size())}));
    QCOMPARE(join.info(id)->judge, ::ed3k::Judge::Torrent);
    copyData(save);
    QVERIFY(bt.recheck(id));

    QTRY_VERIFY_WITH_TIMEOUT(complete(bt, id), 20000);
    // CLAIM_FALSE: the set is dropped, the torrent goes on
    QTRY_VERIFY_WITH_TIMEOUT(!problems.isEmpty(), 10000);
    QCOMPARE(problems.first().at(1).toInt(), -1);

    join.stop();
    bt.stop();
    node.stop();
}

// Two eMule Qt nodes, one swarm, no BitTorrent peer between them: the seeder's join
// publishes its set (MEMBER rows, ALIAS btih/btmh, member sources); a node that joins the
// torrent alone finds it over Kad (ALIAS → set → manifest → members) and finishes from it
void tst_Ed3kTorrentJoin::aTorrentAloneFindsItsPeerOverEd3k()
{
    const QString seedDir = m_dir.filePath(QStringLiteral("swarm-seed"));
    const QString leechDir = m_dir.filePath(QStringLiteral("swarm-leech"));
    QTemporaryDir routerDir;
    QVERIFY(routerDir.isValid());
    // a third node: a search never asks the own store, and Kad stores on the closest others
    Ed3kSession router, seedNode, leechNode;
    QVERIFY(router.start(loopback(routerDir.path())) && seedNode.start(loopback(seedDir)) && leechNode.start(loopback(leechDir)));
    // each bootstraps from another: a node whose only contact came in holds its lookups
    const auto bootstrapFrom = [](Ed3kSession& node, const Ed3kSession& from) {
        const ::ed3k::Node contact{*from.nodeId(), ::ed3k::Address::v4(127, 0, 0, 1, from.udpPort())};
        node.post([contact](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&contact, 1}); });
    };
    bootstrapFrom(seedNode, router);
    bootstrapFrom(leechNode, router);
    bootstrapFrom(router, seedNode);
    QTRY_VERIFY_WITH_TIMEOUT(router.contactCount() >= 2 && seedNode.contactCount() > 0 && leechNode.contactCount() > 0, 20000);
    // the seeder's records stored (ALIAS, MEMBER) and its sources published
    int stored = 0, published = 0;
    connect(&seedNode, &Ed3kSession::eventPolled, this, [&](const ::ed3k::Event& ev, bool*) {
        if (const auto* st = std::get_if<::ed3k::event::KadStored>(&ev); st && st->nodes > 0)
            ++stored;
        else if (std::holds_alternative<::ed3k::event::SourcesPublished>(ev))
            ++published;
    });

    // the seeder: made here, complete, its set built and published
    BtSession seedBt;
    seedBt.start(btSettings(seedDir));
    Ed3kTorrentJoin seedJoin(seedNode, seedBt, seedDir);
    seedJoin.start();
    QSignalSpy seeded(&seedJoin, &Ed3kTorrentJoin::torrentCompleted);
    const BtAddResult made = seedBt.addCreatedTorrent(m_torrent, m_seedDir);
    QCOMPARE(made.outcome, BtAddOutcome::Added);
    seedJoin.adoptCreated(made.id);
    QTRY_COMPARE_WITH_TIMEOUT(seeded.count(), 1, kTimeoutMs);
    const QByteArray manifest = seedJoin.completedManifest(made.id);
    QVERIFY(!manifest.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(stored >= 4 && published >= 1, kTimeoutMs);

    // the leecher: the .torrent alone, no peer to reach over BitTorrent
    BtSession leechBt;
    leechBt.start(btSettings(leechDir));
    Ed3kTorrentJoin leechJoin(leechNode, leechBt, leechDir);
    leechJoin.start();
    QSignalSpy finished(&leechBt, &BtSession::torrentFinished);
    const BtAddResult added = leechBt.addTorrentFile(m_torrent);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QTRY_VERIFY_WITH_TIMEOUT(leechJoin.hasJoin(added.id), kTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(leechJoin.info(added.id)->manifest.has_value(), kTimeoutMs);
    QCOMPARE(*leechJoin.info(added.id)->manifest, hashOf(manifest).root);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), kTimeoutMs);
    QCOMPARE(leechBt.torrent(added.id)->peers, 0);

    const auto layout = leechBt.layout(added.id);
    QVERIFY(layout.has_value());
    QStringList paths;
    for (const auto& f : layout->files)
        if (!f.pad)
            paths << f.path;
    QCOMPARE(paths.size(), 2);
    QCOMPARE(readFile(paths.at(0)), m_a);
    QCOMPARE(readFile(paths.at(1)), m_b);

    leechJoin.stop();
    seedJoin.stop();
    leechBt.stop();
    seedBt.stop();
    leechNode.stop();
    seedNode.stop();
    router.stop();
}

// A daemon restart: libtorrent adds its torrents after start (async), and the join may
// start before. What a completed torrent kept (set, ALIAS records, members) was dropped
// as "torrent gone", so a restarted seeder published nothing and searched its own set
void tst_Ed3kTorrentJoin::aRestartKeepsTheSetOfAResumedTorrent()
{
    const QString dir = m_dir.filePath(QStringLiteral("restart"));
    Ed3kSession node;
    QVERIFY(node.start(loopback(dir)));
    QString id;
    QByteArray manifest;
    {
        BtSession bt;
        bt.start(btSettings(dir));
        Ed3kTorrentJoin join(node, bt, dir);
        join.setSearchEnabled(false);
        join.start();
        QSignalSpy completed(&join, &Ed3kTorrentJoin::torrentCompleted);
        const BtAddResult made = bt.addCreatedTorrent(m_torrent, m_seedDir);
        QCOMPARE(made.outcome, BtAddOutcome::Added);
        join.adoptCreated(made.id);
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, kTimeoutMs);
        id = made.id;
        manifest = join.completedManifest(id);
        join.stop();
        bt.stop();
    }
    const ::ed3k::Root manifestRoot = hashOf(manifest).root;
    // Ed3kJoinBase::keptDir
    const QString kept = QDir(eMule::ed3k::stateDir(dir)).filePath(QStringLiteral("torrent/") + QString::fromLatin1(QUrl::toPercentEncoding(id)));
    QVERIFY(QFileInfo::exists(kept));

    // BitTorrent off: nothing to judge by, the kept set stays
    {
        BtSession off;
        Ed3kTorrentJoin join(node, off, dir);
        join.start();
        QVERIFY(QFileInfo::exists(kept));
        QVERIFY(!join.sharesRoot(manifestRoot));
        join.stop();
    }

    // the new session's rows come with its add alerts, after the join started
    BtSession bt;
    bt.start(btSettings(dir));
    QVERIFY(!bt.torrent(id).has_value() && bt.isResuming(id));
    Ed3kTorrentJoin join(node, bt, dir);
    QSignalSpy republished(&join, &Ed3kTorrentJoin::keptRecordsPublished);
    join.start();
    QVERIFY(QFileInfo::exists(kept));
    QTRY_VERIFY_WITH_TIMEOUT(join.hasJoin(id), kTimeoutMs);
    QVERIFY(!bt.isResuming(id));
    QTRY_VERIFY_WITH_TIMEOUT(join.sharesRoot(manifestRoot), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(join.sharesRoot(hashOf(m_a).root) && join.sharesRoot(hashOf(m_b).root), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(republished.count(), 1, 10000);
    QCOMPARE(republished.at(0).at(0).toString(), id);
    // its own set is not searched for
    QVERIFY(!join.setSearching(id));

    join.stop();
    bt.stop();
    node.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kTorrentJoin)
#include "tst_Ed3kTorrentJoin.moc"
