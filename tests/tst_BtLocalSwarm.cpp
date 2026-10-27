/// @file tst_BtLocalSwarm.cpp
/// @brief Two BtSessions on loopback, one seeding, one leeching: a real transfer with no
/// network (research doc §5.6). Also the ed3k join door (BtJoinDiskIo): a hook that spoils
/// a piece makes libtorrent fetch it again, with a verdict at once or later from another
/// thread; and the resume data of a restart.

#include "BtSession.h"
#include "session/BtJoinDiskIo.h"

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/torrent_info.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>

namespace lt = libtorrent;
using namespace eMule::bt;

namespace {

constexpr int kTimeoutMs = 60'000;
constexpr qsizetype kPayload = 3 * 1024 * 1024 + 123;
constexpr int kPieceLength = 256 * 1024;

/// Spoils the first piece it sees, or none; the verdict at once, or later from the test's
/// thread (the hook of an ed3k join answers from the engine's thread).
class SpoilHook final : public BtJoinHook {
public:
    SpoilHook(bool spoilFirst, bool deferred) : m_spoilFirst(spoilFirst), m_deferred(deferred) {}

    void pieceHashed(lt::piece_index_t piece, std::vector<char> data, std::function<void(bool)> done) override
    {
        {
            std::scoped_lock lock(mutex);
            pieces[static_cast<int>(piece)] = std::move(data);
        }
        const bool spoil = calls.fetch_add(1) == 0 && m_spoilFirst;
        if (!m_deferred) {
            done(spoil);
            return;
        }
        QMetaObject::invokeMethod(qApp, [done = std::move(done), spoil] { done(spoil); }, Qt::QueuedConnection);
    }

    std::atomic<int> calls{0};
    /// the bytes the hook was given, by piece
    std::mutex mutex;
    std::map<int, std::vector<char>> pieces;

private:
    bool m_spoilFirst;
    bool m_deferred;
};

BtSettings settingsIn(const QTemporaryDir& dir, const QString& name)
{
    BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.seedRatioPercent = 0;
    s.stateDir = dir.filePath(name + QStringLiteral("/BT"));
    s.downloadDir = dir.filePath(name + QStringLiteral("/Incoming"));
    s.userAgent = QStringLiteral("eMuleQt-test/0.0.0");
    return s;
}

struct Torrent {
    QByteArray bytes;
    lt::info_hash_t hashes;
};

Torrent makeTorrent(const QString& file, const QString& root, lt::create_flags_t flags)
{
    lt::create_torrent t(lt::list_files(file.toStdString()), kPieceLength, flags);
    lt::set_piece_hashes(t, root.toStdString());
    const std::vector<char> buf = t.generate_buf();
    Torrent out;
    out.bytes = QByteArray(buf.data(), static_cast<qsizetype>(buf.size()));
    out.hashes = lt::load_torrent_buffer(lt::span<char const>(buf.data(), static_cast<std::ptrdiff_t>(buf.size()))).ti->info_hashes();
    return out;
}

} // namespace

class tst_BtLocalSwarm : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void seedAndLeech();
    void joinHookPassesEveryPiece_data();
    void joinHookPassesEveryPiece();
    void joinHookSpoilsAPiece_data();
    void joinHookSpoilsAPiece();
    void resumeAfterRestart();
    void peersTrackersAndOptions();
    void ipFilterBlocksPeer();

private:
    void runJoin(bool spoil, bool deferred);
    /// Starts a seeder of a torrent of the payload; its port to `port`.
    void startSeeder(BtSession& seeder, const Torrent& torrent, quint16& port);
    static QString magnet(const Torrent& torrent, quint16 port);
    QByteArray readDownloaded(const BtSession& s, const QString& id) const;

    QTemporaryDir m_dir;
    QString m_seedDir;
    QByteArray m_payload;
    /// v1 and v2
    Torrent m_hybrid;
    /// v1 only: the hook sees each piece once (no v2 block hashes asked for)
    Torrent m_v1;
    int m_seeders = 0;
};

void tst_BtLocalSwarm::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_seedDir = m_dir.filePath(QStringLiteral("seed"));
    QVERIFY(QDir().mkpath(m_seedDir));
    m_payload.resize(kPayload);
    QRandomGenerator rng(42);
    for (auto& b : m_payload)
        b = static_cast<char>(rng.generate() & 0xff);
    const QString file = m_seedDir + QStringLiteral("/payload.bin");
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(m_payload), kPayload);
    f.close();

    m_hybrid = makeTorrent(file, m_seedDir, {});
    QVERIFY(m_hybrid.hashes.has_v1() && m_hybrid.hashes.has_v2());
    m_v1 = makeTorrent(file, m_seedDir, lt::create_torrent::v1_only);
    QVERIFY(m_v1.hashes.has_v1() && !m_v1.hashes.has_v2());
}

void tst_BtLocalSwarm::seedAndLeech()
{
    BtSession seeder;
    quint16 port = 0;
    startSeeder(seeder, m_hybrid, port);
    QVERIFY(port != 0);

    BtSession leecher;
    leecher.start(settingsIn(m_dir, QStringLiteral("leech")));
    QSignalSpy finished(&leecher, &BtSession::torrentFinished);
    const BtAddResult added = leecher.addMagnet(magnet(m_hybrid, port));
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QVERIFY(added.id.startsWith(QStringLiteral("bt:v1:")));
    QCOMPARE(leecher.addMagnet(magnet(m_hybrid, port)).outcome, BtAddOutcome::Duplicate);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0, kTimeoutMs);
    QCOMPARE(finished.first().first().toString(), added.id);
    QCOMPARE(readDownloaded(leecher, added.id), m_payload);
    QTRY_VERIFY_WITH_TIMEOUT(leecher.torrent(added.id)->state == BtState::Seeding, 5000);
    QCOMPARE(leecher.files(added.id).size(), 1);
    leecher.stop();
    seeder.stop();
}

void tst_BtLocalSwarm::joinHookPassesEveryPiece_data()
{
    QTest::addColumn<bool>("deferred");
    QTest::newRow("verdict at once") << false;
    QTest::newRow("verdict later, another thread") << true;
}

void tst_BtLocalSwarm::joinHookPassesEveryPiece()
{
    // a hook that passes every piece changes nothing: every piece goes through it once
    QFETCH(bool, deferred);
    runJoin(false, deferred);
}

void tst_BtLocalSwarm::joinHookSpoilsAPiece_data()
{
    joinHookPassesEveryPiece_data();
}

void tst_BtLocalSwarm::joinHookSpoilsAPiece()
{
    // a spoiled piece fails in libtorrent. Its one local seeder is banned for it, as
    // libtorrent blames a peer that alone sent a failed piece, so the download stops
    // there: the test checks the failure, not a second fetch
    QFETCH(bool, deferred);
    runJoin(true, deferred);
}

void tst_BtLocalSwarm::runJoin(bool spoil, bool deferred)
{
    BtSession seeder;
    quint16 port = 0;
    startSeeder(seeder, m_v1, port);
    QVERIFY(port != 0);

    BtSession leecher;
    leecher.start(settingsIn(m_dir, QStringLiteral("join%1%2").arg(spoil ? 1 : 0).arg(deferred ? 1 : 0)));
    QVERIFY(leecher.diskIo() != nullptr);
    auto hook = std::make_shared<SpoilHook>(spoil, deferred);
    leecher.diskIo()->attach(m_v1.hashes, hook);
    QVERIFY(leecher.diskIo()->attached(m_v1.hashes));
    QSignalSpy finished(&leecher, &BtSession::torrentFinished);
    const BtAddResult added = leecher.addMagnet(magnet(m_v1, port));
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    const auto state = [&] {
        const auto row = leecher.torrent(added.id);
        return QStringLiteral("hook calls %1, state %2, done %3/%4, failed %5")
            .arg(hook->calls.load())
            .arg(row ? int(row->state) : -1)
            .arg(row ? row->done : -1)
            .arg(row ? row->size : -1)
            .arg(row ? row->failedBytes : -1);
    };
    if (spoil) {
        QTRY_VERIFY2_WITH_TIMEOUT(leecher.torrent(added.id).has_value() && leecher.torrent(added.id)->failedBytes >= kPieceLength,
                                  qPrintable(state()), kTimeoutMs);
        QVERIFY(finished.isEmpty());
    } else {
        QTRY_VERIFY2_WITH_TIMEOUT(finished.count() > 0, qPrintable(state()), kTimeoutMs);
        const int pieces = static_cast<int>((kPayload + kPieceLength - 1) / kPieceLength);
        QCOMPARE(hook->calls.load(), pieces);
        QCOMPARE(readDownloaded(leecher, added.id), m_payload);
        // the hook saw each piece's bytes as written
        {
            std::scoped_lock lock(hook->mutex);
            QCOMPARE(int(hook->pieces.size()), pieces);
            for (const auto& [piece, bytes] : hook->pieces)
                QCOMPARE(QByteArray(bytes.data(), qsizetype(bytes.size())), m_payload.mid(piece * kPieceLength, kPieceLength));
        }
        QCOMPARE(leecher.torrent(added.id)->failedBytes, 0);
    }
    leecher.diskIo()->detach(m_v1.hashes);
    QVERIFY(!leecher.diskIo()->attached(m_v1.hashes));
    // a deferred verdict still queued lands after the session is gone: dropped, no crash
    leecher.stop();
    seeder.stop();
    QTest::qWait(50);
}

void tst_BtLocalSwarm::resumeAfterRestart()
{
    // the leecher of seedAndLeech has resume data now
    BtSession again;
    QSignalSpy added(&again, &BtSession::torrentAdded);
    again.start(settingsIn(m_dir, QStringLiteral("leech")));
    QTRY_VERIFY_WITH_TIMEOUT(added.count() > 0, 10'000);
    const QString id = added.first().first().toString();
    QTRY_VERIFY_WITH_TIMEOUT(again.torrent(id).has_value() && again.torrent(id)->state == BtState::Seeding, 10'000);
    QVERIFY(again.remove(id, false));
    QVERIFY(again.torrents().isEmpty());
    again.stop();
}

void tst_BtLocalSwarm::peersTrackersAndOptions()
{
    BtSession seeder;
    quint16 port = 0;
    startSeeder(seeder, m_v1, port);
    const QString seedId = seeder.torrents().first().id;

    BtTorrentOptions opts;
    opts.uploadLimitKb = 256;
    opts.maxPeers = 20;
    opts.ratioMode = BtLimitMode::Custom;
    opts.ratioPercent = 250;
    opts.seedTimeMode = BtLimitMode::Unlimited;
    QVERIFY(seeder.setOptions(seedId, opts));
    const auto got = seeder.options(seedId);
    QVERIFY(got.has_value());
    QCOMPARE(got->uploadLimitKb, 256);
    QCOMPARE(got->downloadLimitKb, 0);
    QCOMPARE(got->maxPeers, 20);
    QCOMPARE(got->ratioMode, BtLimitMode::Custom);
    QCOMPARE(got->ratioPercent, 250);
    QCOMPARE(got->seedTimeMode, BtLimitMode::Unlimited);
    QVERIFY(!seeder.options(QStringLiteral("bt:v1:0000000000000000000000000000000000000000")).has_value());

    BtSession leecher;
    leecher.start(settingsIn(m_dir, QStringLiteral("leechPeers")));
    const BtAddResult added = leecher.addMagnet(magnet(m_v1, port));
    QCOMPARE(added.outcome, BtAddOutcome::Added);

    // Loopback ignores rate limits: the transfer is over in moments, so collect what
    // both ends saw meanwhile rather than one snapshot.
    bool sawOutgoingSeed = false;   // the leecher dialled a peer that has everything
    bool sawIncoming = false;       // ... and the seeder saw it come in
    bool sawAvailable = false;      // every piece with at least one peer
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (const auto& p : leecher.peers(added.id))
            sawOutgoingSeed = sawOutgoingSeed
                || (p.address.startsWith(QStringLiteral("127.0.0.1:")) && !p.flags.contains(QLatin1Char('I')) && p.progress >= 1.0);
        for (const auto& p : seeder.peers(seedId))
            sawIncoming = sawIncoming || p.flags.contains(QLatin1Char('I'));
        const QByteArray avail = leecher.availabilityMap(added.id);
        sawAvailable = sawAvailable
            || (!avail.isEmpty() && std::all_of(avail.cbegin(), avail.cend(), [](char c) { return c != 0; }));
        return sawOutgoingSeed && sawIncoming && sawAvailable;
    }()), kTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(leecher.torrent(added.id).has_value() && leecher.torrent(added.id)->totalSize == kPayload, 10'000);

    // trackers: add, no duplicate, remove
    const QString url = QStringLiteral("udp://127.0.0.1:9/announce");
    QVERIFY(seeder.addTrackers(seedId, {url, url, QStringLiteral(" ")}));
    QTRY_COMPARE_WITH_TIMEOUT(seeder.trackers(seedId).size(), 1, 5000);
    QCOMPARE(seeder.trackers(seedId).first().url, url);
    QVERIFY(seeder.removeTrackers(seedId, {url}));
    QTRY_VERIFY_WITH_TIMEOUT(seeder.trackers(seedId).isEmpty(), 5000);
    QVERIFY(!seeder.addTrackers(QStringLiteral("bt:v1:0000000000000000000000000000000000000000"), {url}));

    leecher.stop();
    seeder.stop();

    // options survive a restart: speeds in the resume data, seed limits in the JSON
    BtSession again;
    QSignalSpy addedAgain(&again, &BtSession::torrentAdded);
    again.start(settingsIn(m_dir, QStringLiteral("seeder%1").arg(m_seeders)));
    QTRY_VERIFY_WITH_TIMEOUT(addedAgain.count() > 0, 10'000);
    const auto restored = again.options(seedId);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->uploadLimitKb, 256);
    QCOMPARE(restored->maxPeers, 20);
    QCOMPARE(restored->ratioMode, BtLimitMode::Custom);
    QCOMPARE(restored->ratioPercent, 250);
    QCOMPARE(restored->seedTimeMode, BtLimitMode::Unlimited);

    // back to the defaults: the JSON entry goes
    QVERIFY(again.setOptions(seedId, BtTorrentOptions{}));
    const auto cleared = again.options(seedId);
    QCOMPARE(cleared->uploadLimitKb, 0);
    QCOMPARE(cleared->maxPeers, 0);
    QCOMPARE(cleared->ratioMode, BtLimitMode::Global);
    QFile json(m_dir.filePath(QStringLiteral("seeder%1/BT/torrent-options.json").arg(m_seeders)));
    QVERIFY(json.open(QIODevice::ReadOnly));
    QVERIFY(!json.readAll().contains(seedId.toUtf8()));
    again.stop();
}

void tst_BtLocalSwarm::ipFilterBlocksPeer()
{
    BtSession seeder;
    quint16 port = 0;
    startSeeder(seeder, m_v1, port);
    QVERIFY(port != 0);

    BtSession leecher;
    // set before start: the session keeps it across start/stop
    leecher.setIpFilter({{0x7F000000u, 0x7FFFFFFFu}}, {});
    leecher.start(settingsIn(m_dir, QStringLiteral("filtered")));
    QSignalSpy finished(&leecher, &BtSession::torrentFinished);
    const BtAddResult added = leecher.addMagnet(magnet(m_v1, port));
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QTRY_VERIFY_WITH_TIMEOUT(leecher.blockedPeers() > 0, 10'000);
    QTest::qWait(1000);
    QVERIFY(finished.isEmpty());
    QCOMPARE(leecher.torrent(added.id)->done, 0);
    leecher.stop();
    seeder.stop();
}

void tst_BtLocalSwarm::startSeeder(BtSession& seeder, const Torrent& torrent, quint16& port)
{
    port = 0;
    // a state dir of its own: one of an earlier test would resume the torrent
    seeder.start(settingsIn(m_dir, QStringLiteral("seeder%1").arg(++m_seeders)));
    const BtAddResult r = seeder.addTorrentFile(torrent.bytes, m_seedDir);
    QCOMPARE(r.outcome, BtAddOutcome::Added);
    QTRY_VERIFY_WITH_TIMEOUT(seeder.torrent(r.id).has_value() && seeder.torrent(r.id)->state == BtState::Seeding, kTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(seeder.status().listenPort != 0, 5000);
    port = seeder.status().listenPort;
}

QString tst_BtLocalSwarm::magnet(const Torrent& torrent, quint16 port)
{
    const auto& v1 = torrent.hashes.v1;
    const QByteArray hex = QByteArray(reinterpret_cast<const char*>(v1.data()), static_cast<qsizetype>(v1.size())).toHex();
    return QStringLiteral("magnet:?xt=urn:btih:%1&dn=payload.bin&x.pe=127.0.0.1:%2").arg(QString::fromLatin1(hex)).arg(port);
}

QByteArray tst_BtLocalSwarm::readDownloaded(const BtSession& s, const QString& id) const
{
    const auto row = s.torrent(id);
    if (!row)
        return {};
    QFile f(row->savePath + QStringLiteral("/payload.bin"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

QTEST_GUILESS_MAIN(tst_BtLocalSwarm)
#include "tst_BtLocalSwarm.moc"
