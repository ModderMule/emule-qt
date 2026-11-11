/// @file tst_BtSmoke.cpp
/// @brief eMule::Bt links and moc runs; the torrent id and the inputs an add takes.

#include "BtIds.h"
#include "BtSession.h"
#include "add/BtAddSource.h"
#include "session/BtSettingsMap.h"
#include "torrent/BtTorrentView.h"

#include <libtorrent/i2p_stream.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/version.hpp>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace eMule::bt;

class tst_BtSmoke : public QObject {
    Q_OBJECT

private slots:
    void constructs();
    void startStopIsIdempotent();
    void bindSelectionMapsToInterfaces();
    void blockedStartsPausedThenResumes();
    void rateLimitsAndDemand();
    void ids();
    void magnets();
    void torrentFiles();
    void fileAvailability();
    void inspectTorrentFile();
    void i2pSettingsMap();
    void proxySettingsMap();
    void i2pAddPolicy();
    void stopAfterMetadataPolicy();
    void i2pPeerAddress();
    void i2pTrackerErrorText();
    void i2pTorrentIsFlagged();
    void peerFlagsChokeDirection();
    void addDoesNotWait();
    void stopRightAfterAddKeepsIt();
    void removeWhileAdding();
};

void tst_BtSmoke::constructs()
{
    BtSession session;
    QCOMPARE(session.metaObject()->className(), "eMule::bt::BtSession");
    QVERIFY(!session.isRunning());
    QVERIFY(session.torrents().isEmpty());
    QVERIFY(!session.status().running);
    QVERIFY(LIBTORRENT_VERSION_NUM >= 20100);
    // off: adds are refused, not crashed
    QCOMPARE(session.addMagnet(QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567")).outcome, BtAddOutcome::NotRunning);
}

void tst_BtSmoke::startStopIsIdempotent()
{
    QTemporaryDir dir;
    BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.stateDir = dir.filePath(QStringLiteral("BT"));
    s.downloadDir = dir.filePath(QStringLiteral("Incoming"));
    BtSession session;
    session.start(s);
    QVERIFY(session.isRunning());
    session.start(s);
    QVERIFY(session.isRunning());
    QVERIFY(session.diskIo() != nullptr);
    session.stop();
    QVERIFY(!session.isRunning());
    session.stop();
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("BT/session.dat"))));
}

// bindAddress pref: libtorrent listens and dials from the bound addresses only
void tst_BtSmoke::bindSelectionMapsToInterfaces()
{
    BtSettings s;
    s.listenPort = 6881;
    QCOMPARE(BtSettingsMap::listenInterfaces(s), std::string("0.0.0.0:6881,[::]:6881"));
    QCOMPARE(BtSettingsMap::outgoingInterfaces(s), std::string());

    s.bindAddresses = {QStringLiteral("10.8.0.2"), QStringLiteral("fd00::2")};
    QCOMPARE(BtSettingsMap::listenInterfaces(s), std::string("10.8.0.2:6881,[fd00::2]:6881"));
    QCOMPARE(BtSettingsMap::outgoingInterfaces(s), std::string("10.8.0.2,fd00::2"));
    const libtorrent::settings_pack p = BtSettingsMap::toPack(s);
    QCOMPARE(p.get_str(libtorrent::settings_pack::listen_interfaces), std::string("10.8.0.2:6881,[fd00::2]:6881"));
    // download ACKs must not eat the upload limit (#87)
    QVERIFY(!p.get_bool(libtorrent::settings_pack::rate_limit_ip_overhead));

    // blocked: no wildcard fallback
    s.networkBlocked = true;
    QCOMPARE(BtSettingsMap::listenInterfaces(s), std::string());
    QCOMPARE(BtSettingsMap::outgoingInterfaces(s), std::string());
}

// blocked at start: the session comes up paused, unblocking resumes it
void tst_BtSmoke::blockedStartsPausedThenResumes()
{
    QTemporaryDir dir;
    BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.networkBlocked = true;
    s.stateDir = dir.filePath(QStringLiteral("BT"));
    s.downloadDir = dir.filePath(QStringLiteral("Incoming"));
    BtSession session;
    session.start(s);
    QVERIFY(session.isRunning());
    s.networkBlocked = false;
    s.bindAddresses = {QStringLiteral("127.0.0.1")};
    session.applySettings(s);
    QTRY_VERIFY_WITH_TIMEOUT(session.status().listening, 5000);
    session.stop();
}

void tst_BtSmoke::rateLimitsAndDemand()
{
    QTemporaryDir dir;
    BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.stateDir = dir.filePath(QStringLiteral("BT"));
    s.downloadDir = dir.filePath(QStringLiteral("Incoming"));
    BtSession session;
    // before start: kept and applied by start()
    session.setRateLimits(100, 50);
    QCOMPARE(session.demand().downActive, false);
    session.start(s);
    QVERIFY(session.isRunning());
    session.setRateLimits(200, 0);
    session.setRateLimits(200, 0);
    // no torrent: nothing asked of either line
    const BtDemand d = session.demand();
    QVERIFY(!d.downActive);
    QVERIFY(!d.upActive);
    QCOMPARE(d.downRate, 0);
    session.stop();
}

void tst_BtSmoke::ids()
{
    BtInfoHash h;
    h.v1 = QByteArray::fromHex("0123456789abcdef0123456789abcdef01234567");
    const QString id = BtIds::idOf(h);
    QCOMPARE(id, QStringLiteral("bt:v1:0123456789ABCDEF0123456789ABCDEF01234567"));
    QVERIFY(BtIds::isTorrentId(id));
    QCOMPARE(BtIds::parse(id)->v1, h.v1);

    // hybrid: the v1 hash is the identity
    h.v2 = QByteArray(32, '\x11');
    QCOMPARE(BtIds::idOf(h), id);
    h.v1.clear();
    QCOMPARE(BtIds::idOf(h), QStringLiteral("bt:v2:") + QString(64, QLatin1Char('1')));
    QCOMPARE(BtIds::parse(BtIds::idOf(h))->v2, h.v2);

    // an ED2K MD4, an NZB id, bad hex, a wrong length
    QVERIFY(!BtIds::isTorrentId(QStringLiteral("0123456789ABCDEF0123456789ABCDEF")));
    QVERIFY(!BtIds::isTorrentId(QStringLiteral("nzb:1234")));
    QVERIFY(!BtIds::isTorrentId(QStringLiteral("bt:v1:0123456789ABCDEF0123456789ABCDEF0123456G")));
    QVERIFY(!BtIds::isTorrentId(QStringLiteral("bt:v1:0123")));
    QVERIFY(BtIds::idOf(BtInfoHash{}).isEmpty());
}

void tst_BtSmoke::magnets()
{
    QVERIFY(BtAddSource::isBtMagnet(QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=x")));
    QVERIFY(BtAddSource::isBtMagnet(QStringLiteral("  MAGNET:?dn=x&xt=urn:btmh:1220") + QString(64, QLatin1Char('a'))));
    QVERIFY(!BtAddSource::isBtMagnet(QStringLiteral("magnet:?xt=urn:ed2k:31D6CFE0D16AE931B73C59D7E0C089C0&xl=0")));
    QVERIFY(!BtAddSource::isBtMagnet(QStringLiteral("ed2k://|file|a|1|31D6CFE0D16AE931B73C59D7E0C089C0|/")));
    auto ok = BtAddSource::fromMagnet(QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=hello"));
    QVERIFY(ok.has_value());
    QCOMPARE(QString::fromStdString(ok->name), QStringLiteral("hello"));
    QVERIFY(!BtAddSource::fromMagnet(QStringLiteral("magnet:?xt=urn:btih:zz")).has_value());

    // an ed3k:file link of a torrent set (torrent.md §7): added as a magnet of its bt=
    const QString hash = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
    const QString link = QStringLiteral("ed3k:file?b3=abc&sz=5&dn=set&bt=") + hash;
    QVERIFY(BtAddSource::isBtMagnet(link));
    QVERIFY(!BtAddSource::isBtMagnet(QStringLiteral("ed3k:file?b3=abc&sz=5")));
    auto fromLink = BtAddSource::fromMagnet(link);
    QVERIFY(fromLink.has_value());
    QCOMPARE(QString::fromStdString(fromLink->name), QStringLiteral("set"));
    QVERIFY(fromLink->info_hashes.has_v1());
    QCOMPARE(BtAddSource::ed3kLinkOf(link), link);
    // a magnet's xs carries it
    const QString withXs = QStringLiteral("magnet:?xt=urn:btih:") + hash + QStringLiteral("&xs=")
                           + QString::fromLatin1(QUrl::toPercentEncoding(link));
    QCOMPARE(BtAddSource::ed3kLinkOf(withXs), link);
    QVERIFY(BtAddSource::ed3kLinkOf(QStringLiteral("magnet:?xt=urn:btih:") + hash).isEmpty());
}

void tst_BtSmoke::torrentFiles()
{
    QVERIFY(!BtAddSource::fromTorrentFile({}).has_value());
    QVERIFY(!BtAddSource::fromTorrentFile(QByteArray("not bencode")).has_value());
    QVERIFY(!BtAddSource::fromTorrentFile(QByteArray("d4:infod4:name1:aee")).has_value());
}

void tst_BtSmoke::fileAvailability()
{
    // a.bin = piece 0, b.bin = pieces 1-2 (16 KiB pieces)
    const QByteArray info = QByteArray("d5:filesl"
                                       "d6:lengthi16384e4:pathl5:a.binee"
                                       "d6:lengthi16385e4:pathl5:b.binee"
                                       "e4:name4:demo12:piece lengthi16384e6:pieces60:")
                            + QByteArray(60, 'x') + QByteArray("e");
    const QByteArray torrent = QByteArray("d4:info") + info + QByteArray("e");
    const lt::add_torrent_params atp =
        lt::load_torrent_buffer(lt::span<char const>(torrent.constData(), torrent.size()));
    QVERIFY(atp.ti && atp.ti->num_pieces() == 3);

    auto rows = BtTorrentView::files(*atp.ti, {}, {}, {2, 0, 1});
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).availability, 1.0);
    QCOMPARE(rows.at(1).availability, 0.5);   // piece 1 missing, piece 2 has a peer

    // seeding: libtorrent sends no counts → N/A
    rows = BtTorrentView::files(*atp.ti, {}, {}, {});
    QCOMPARE(rows.at(0).availability, -1.0);
    QCOMPARE(rows.at(1).availability, -1.0);
}

void tst_BtSmoke::inspectTorrentFile()
{
    // v1, two files in a folder, one tracker, a web seed (keys sorted, as bencode wants)
    const QByteArray info = QByteArray("d5:filesl"
                                       "d6:lengthi10e4:pathl3:dir5:a.txtee"
                                       "d6:lengthi20e4:pathl5:b.bineee"
                                       "4:name4:demo12:piece lengthi16384e6:pieces20:")
                            + QByteArray(20, 'x') + QByteArray("e");
    const QByteArray torrent = QByteArray("d8:announce27:http://tracker.example/annc"
                                          "7:comment5:hello10:created by4:test13:creation datei1700000000e"
                                          "4:info")
                             + info + QByteArray("8:url-list19:http://seed.examplee");

    const auto preview = BtSession::inspectTorrentFile(torrent);
    QVERIFY2(preview.has_value(), qPrintable(preview.has_value() ? QString() : preview.error()));
    QCOMPARE(preview->name, QStringLiteral("demo"));
    QCOMPARE(preview->totalSize, 30);
    QCOMPARE(preview->pieceLength, 16384);
    QCOMPARE(preview->pieces, 1);
    QCOMPARE(preview->hashV1.size(), 40);
    QVERIFY(preview->hashV2.isEmpty());
    QVERIFY(preview->id.startsWith(QStringLiteral("bt:v1:")));
    QCOMPARE(preview->comment, QStringLiteral("hello"));
    QCOMPARE(preview->creator, QStringLiteral("test"));
    QCOMPARE(preview->createdAt, 1700000000);
    QCOMPARE(preview->files.size(), 2);
    QCOMPARE(preview->files.at(0).path, QStringLiteral("demo/dir/a.txt"));
    QCOMPARE(preview->files.at(0).size, 10);
    QCOMPARE(preview->files.at(1).path, QStringLiteral("demo/b.bin"));
    QCOMPARE(preview->trackers.size(), 1);
    QCOMPARE(preview->trackers.at(0).url, QStringLiteral("http://tracker.example/annc"));
    // libtorrent ends a multi-file web seed with a slash
    QCOMPARE(preview->webSeeds, QList<QString>{QStringLiteral("http://seed.example/")});

    QVERIFY(!BtSession::inspectTorrentFile(QByteArray("garbage")).has_value());
}

// I2P: SAM only with a bridge, mixed only while on, tunnels clamped. Without a bridge the
// placeholder 127.0.0.1:0, never an empty hostname (.i2p trackers would go to DNS)
void tst_BtSmoke::i2pSettingsMap()
{
    namespace lt = libtorrent;
    const auto placeholder = [](const lt::settings_pack& p) {
        return p.get_str(lt::settings_pack::i2p_hostname) == "127.0.0.1" && p.get_int(lt::settings_pack::i2p_port) == 0;
    };
    BtSettings s;
    lt::settings_pack p = BtSettingsMap::toPack(s);
    QVERIFY(placeholder(p));
    QVERIFY(!p.get_bool(lt::settings_pack::allow_i2p_mixed));
    // default: first working tracker of every tier (#147); on: every tracker (#144)
    QVERIFY(!p.get_bool(lt::settings_pack::announce_to_all_trackers));
    QVERIFY(p.get_bool(lt::settings_pack::announce_to_all_tiers));
    s.announceToAll = true;
    p = BtSettingsMap::toPack(s);
    QVERIFY(p.get_bool(lt::settings_pack::announce_to_all_trackers));
    QVERIFY(p.get_bool(lt::settings_pack::announce_to_all_tiers));
    s.announceToAll = false;

    // on, bridge not ready yet (bundled router starting): still no SAM session
    s.i2pEnabled = true;
    s.i2pMixed = true;
    s.i2pSamHost = QStringLiteral("127.0.0.1");
    p = BtSettingsMap::toPack(s);
    QVERIFY(placeholder(p));
    QVERIFY(p.get_bool(lt::settings_pack::allow_i2p_mixed));

    s.i2pSamPort = 7656;
    s.i2pTunnelLength = 9;
    s.i2pTunnelQuantity = 0;
    p = BtSettingsMap::toPack(s);
    QCOMPARE(p.get_str(lt::settings_pack::i2p_hostname), std::string("127.0.0.1"));
    QCOMPARE(p.get_int(lt::settings_pack::i2p_port), 7656);
    QCOMPARE(p.get_int(lt::settings_pack::i2p_inbound_length), 7);
    QCOMPARE(p.get_int(lt::settings_pack::i2p_outbound_quantity), 1);

    // off: an I2P torrent waits instead of going clearnet
    s.i2pEnabled = false;
    p = BtSettingsMap::toPack(s);
    QVERIFY(placeholder(p));
    QVERIFY(!p.get_bool(lt::settings_pack::allow_i2p_mixed));
}

void tst_BtSmoke::proxySettingsMap()
{
    namespace lt = libtorrent;
    BtSettings s;
    lt::settings_pack p = BtSettingsMap::toPack(s);
    QCOMPARE(p.get_int(lt::settings_pack::proxy_type), int(lt::settings_pack::none));

    // the Proxy page reaches BitTorrent (report 34)
    s.proxyKind = BtSettings::ProxyKind::Socks5;
    s.proxyHost = QStringLiteral("proxy.example");
    s.proxyPort = 1080;
    p = BtSettingsMap::toPack(s);
    QCOMPARE(p.get_int(lt::settings_pack::proxy_type), int(lt::settings_pack::socks5));
    QCOMPARE(p.get_str(lt::settings_pack::proxy_hostname), std::string("proxy.example"));
    QCOMPARE(p.get_int(lt::settings_pack::proxy_port), 1080);
    QVERIFY(p.get_bool(lt::settings_pack::proxy_peer_connections));
    QVERIFY(p.get_bool(lt::settings_pack::proxy_tracker_connections));
    QVERIFY(p.get_bool(lt::settings_pack::proxy_hostnames));

    s.proxyUser = QStringLiteral("u");
    s.proxyPassword = QStringLiteral("pw");
    p = BtSettingsMap::toPack(s);
    QCOMPARE(p.get_int(lt::settings_pack::proxy_type), int(lt::settings_pack::socks5_pw));
    QCOMPARE(p.get_str(lt::settings_pack::proxy_username), std::string("u"));

    s.proxyKind = BtSettings::ProxyKind::Http;
    p = BtSettingsMap::toPack(s);
    QCOMPARE(p.get_int(lt::settings_pack::proxy_type), int(lt::settings_pack::http_pw));

    s.proxyKind = BtSettings::ProxyKind::Socks4;
    p = BtSettingsMap::toPack(s);
    QCOMPARE(p.get_int(lt::settings_pack::proxy_type), int(lt::settings_pack::socks4));

    // no host: off again
    s.proxyHost.clear();
    p = BtSettingsMap::toPack(s);
    QCOMPARE(p.get_int(lt::settings_pack::proxy_type), int(lt::settings_pack::none));
}

void tst_BtSmoke::i2pAddPolicy()
{
    namespace lt = libtorrent;
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&tr=http%3A%2F%2Ftracker.example.org%2Fannounce");
    const QString i2pTracker = QStringLiteral("http://tracker.example.i2p/a");
    BtSettings s;
    s.i2pTrackers = {i2pTracker, QStringLiteral("http://second.example.i2p/announce")};

    // off: nothing changes
    auto atp = *BtAddSource::fromMagnet(magnet);
    QVERIFY(!BtAddSource::applyI2p(atp, s, std::nullopt));
    QVERIFY(!(atp.flags & lt::torrent_flags::i2p_torrent));
    QCOMPARE(atp.trackers.size(), std::size_t(1));

    // on, not mixed, not asked: a clearnet torrent stays one
    s.i2pEnabled = true;
    atp = *BtAddSource::fromMagnet(magnet);
    QVERIFY(!BtAddSource::applyI2p(atp, s, std::nullopt));
    QCOMPARE(atp.trackers.size(), std::size_t(1));

    // asked: flagged, the I2P trackers in a tier after the torrent's
    atp = *BtAddSource::fromMagnet(magnet);
    QVERIFY(BtAddSource::applyI2p(atp, s, true));
    QVERIFY(atp.flags & lt::torrent_flags::i2p_torrent);
    QCOMPARE(atp.trackers.size(), std::size_t(3));
    QCOMPARE(atp.tracker_tiers.size(), std::size_t(3));
    QCOMPARE(atp.trackers[1], i2pTracker.toStdString());
    QVERIFY(atp.tracker_tiers[1] > atp.tracker_tiers[0]);

    // the new-torrent default, and an explicit "no" beating it
    s.i2pOnlyNew = true;
    atp = *BtAddSource::fromMagnet(magnet);
    QVERIFY(BtAddSource::applyI2p(atp, s, std::nullopt));
    atp = *BtAddSource::fromMagnet(magnet);
    QVERIFY(!BtAddSource::applyI2p(atp, s, false));
    s.i2pOnlyNew = false;

    // mixed: a clearnet torrent gets the I2P trackers, unflagged
    s.i2pMixed = true;
    atp = *BtAddSource::fromMagnet(magnet);
    QVERIFY(!BtAddSource::applyI2p(atp, s, std::nullopt));
    QVERIFY(!(atp.flags & lt::torrent_flags::i2p_torrent));
    QCOMPARE(atp.trackers.size(), std::size_t(3));

    // an .i2p tracker of its own makes it an I2P torrent, and no tracker is added twice
    atp = *BtAddSource::fromMagnet(QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&tr=")
                                   + QString::fromLatin1(QUrl::toPercentEncoding(i2pTracker)));
    QVERIFY(BtAddSource::applyI2p(atp, s, std::nullopt));
    QCOMPARE(atp.trackers.size(), std::size_t(2));

    QVERIFY(BtAddSource::isI2pTracker("http://opentracker.dg2.i2p:80/a"));
    QVERIFY(BtAddSource::isI2pTracker("http://btrackrqkjp6kgelov5a3uxisis77ofxqt5nvy5hvvtoybjpmq4q.b32.i2p/announce"));
    QVERIFY(!BtAddSource::isI2pTracker("http://tracker.example.org/i2p"));
}

void tst_BtSmoke::stopAfterMetadataPolicy()
{
    namespace lt = libtorrent;
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567");
    const QByteArray torrent = QByteArray("d4:infod6:lengthi10e4:name1:a12:piece lengthi16384e6:pieces20:")
                             + QByteArray(20, 'x') + QByteArray("ee");
    BtSettings s;

    // off, not asked: untouched
    auto atp = *BtAddSource::fromMagnet(magnet);
    const auto before = atp.flags;
    QVERIFY(!BtAddSource::applyStopAfterMetadata(atp, s, std::nullopt));
    QCOMPARE(atp.flags, before);

    // asked: a magnet skips every file until its metadata is in, the session stops it then
    atp = *BtAddSource::fromMagnet(magnet);
    QVERIFY(BtAddSource::applyStopAfterMetadata(atp, s, true));
    QVERIFY(atp.flags & lt::torrent_flags::default_dont_download);
    QVERIFY(atp.file_priorities.empty());

    // the default, and an explicit "no" beating it
    s.stopAfterMetadata = true;
    atp = *BtAddSource::fromMagnet(magnet);
    QVERIFY(BtAddSource::applyStopAfterMetadata(atp, s, std::nullopt));
    atp = *BtAddSource::fromMagnet(magnet);
    QVERIFY(!BtAddSource::applyStopAfterMetadata(atp, s, false));
    QVERIFY(!(atp.flags & lt::torrent_flags::default_dont_download));

    // a .torrent has its file list: added paused, nothing left to wait for
    auto file = BtAddSource::fromTorrentFile(torrent);
    QVERIFY2(file.has_value(), qPrintable(file.has_value() ? QString() : file.error()));
    QVERIFY(!BtAddSource::applyStopAfterMetadata(*file, s, std::nullopt));
    QVERIFY(file->flags & lt::torrent_flags::paused);
    QVERIFY(!(file->flags & lt::torrent_flags::auto_managed));
    QVERIFY(!(file->flags & lt::torrent_flags::default_dont_download));
}

void tst_BtSmoke::i2pTrackerErrorText()
{
    namespace lt = libtorrent;
    // report 38: SAM's bare "invalid key" says what it means
    const QString unknown = BtTorrentView::errorText(lt::error_code(lt::i2p_error::invalid_key, lt::i2p_category()));
    QVERIFY(unknown.contains(QStringLiteral("address book")));
    QVERIFY(unknown.contains(QStringLiteral("invalid key")));
    const lt::error_code other = lt::errors::timed_out;
    QCOMPARE(BtTorrentView::errorText(other), QString::fromStdString(other.message()));
}

void tst_BtSmoke::i2pPeerAddress()
{
    libtorrent::sha256_hash h;
    h.clear();
    // 32 zero bytes: 52 'a', the RFC 4648 alphabet in lower case, no padding
    QCOMPARE(BtTorrentView::i2pAddressOf(h), QString(52, QLatin1Char('a')) + QStringLiteral(".b32.i2p"));
    h[0] = std::uint8_t(0xff);
    QVERIFY(BtTorrentView::i2pAddressOf(h).startsWith(QStringLiteral("74")));
}

// asked at add: the row says so, and libtorrent keeps the flag
void tst_BtSmoke::i2pTorrentIsFlagged()
{
    QTemporaryDir dir;
    BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.stateDir = dir.filePath(QStringLiteral("BT"));
    s.downloadDir = dir.filePath(QStringLiteral("Incoming"));
    s.i2pEnabled = true;
    s.i2pTrackers = {QStringLiteral("http://tracker.example.i2p/a")};
    BtSession session;
    session.start(s);
    const auto added = session.addMagnet(QStringLiteral("magnet:?xt=urn:btih:89abcdef0123456789abcdef0123456789abcdef"), {},
                                         true, true);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QVERIFY(session.torrent(added.id)->i2p);
    QCOMPARE(session.trackers(added.id).size(), 1);
    const auto plain = session.addMagnet(QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"), {},
                                         true);
    QVERIFY(!session.torrent(plain.id)->i2p);
    QVERIFY(session.status().i2pEnabled);
    QVERIFY(!session.status().i2pSam);
    session.stop();
}

void tst_BtSmoke::peerFlagsChokeDirection()
{
    namespace lt = libtorrent;
    // choked = we choke them, remote_choked = they choke us (#147)
    lt::peer_info p;
    p.source = {};
    p.flags = lt::peer_info::outgoing_connection;

    // seeding: we upload to them, they choke us, we want nothing
    p.flags |= lt::peer_info::remote_interested | lt::peer_info::remote_choked;
    QCOMPARE(BtTorrentView::peerFlags(p), QStringLiteral("U"));

    // they wait for a slot of ours
    p.flags |= lt::peer_info::choked;
    QCOMPARE(BtTorrentView::peerFlags(p), QStringLiteral("u"));

    // downloading from them, they choke our upload interest away: D
    p.flags = lt::peer_info::outgoing_connection | lt::peer_info::interesting | lt::peer_info::choked;
    QCOMPARE(BtTorrentView::peerFlags(p), QStringLiteral("D"));
    p.flags |= lt::peer_info::remote_choked;
    QCOMPARE(BtTorrentView::peerFlags(p), QStringLiteral("d"));

    // they unchoked us but we want nothing: K; we unchoked them, they want nothing: ?
    p.flags = lt::peer_info::outgoing_connection | lt::peer_info::choked;
    QCOMPARE(BtTorrentView::peerFlags(p), QStringLiteral("K"));
    p.flags = lt::peer_info::outgoing_connection | lt::peer_info::remote_choked;
    QCOMPARE(BtTorrentView::peerFlags(p), QStringLiteral("?"));
}

QTEST_GUILESS_MAIN(tst_BtSmoke)

namespace {
BtSettings quietSettings(const QTemporaryDir& dir)
{
    BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.stateDir = dir.filePath(QStringLiteral("BT"));
    s.downloadDir = dir.filePath(QStringLiteral("Incoming"));
    return s;
}
const QString kMagnet = QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&tr=http%3A%2F%2Ftracker.example%2Fa");
} // namespace

// the row is there at once, torrentAdded follows with libtorrent's answer
void tst_BtSmoke::addDoesNotWait()
{
    QTemporaryDir dir;
    BtSession session;
    session.start(quietSettings(dir));
    QSignalSpy added(&session, &BtSession::torrentAdded);
    const auto r = session.addMagnet(kMagnet, {}, true);
    QCOMPARE(r.outcome, BtAddOutcome::Added);
    QVERIFY(session.torrent(r.id).has_value());
    QCOMPARE(session.trackers(r.id).size(), 1);
    QCOMPARE(session.addMagnet(kMagnet, {}, true).outcome, BtAddOutcome::Duplicate);
    QTRY_COMPARE(added.count(), 1);
    QCOMPARE(added.first().first().toString(), r.id);
    QCOMPARE(session.addMagnet(kMagnet, {}, true).outcome, BtAddOutcome::Duplicate);
    session.stop();
}

// stopped before the add_torrent_alert came: resume data all the same
void tst_BtSmoke::stopRightAfterAddKeepsIt()
{
    QTemporaryDir dir;
    QString id;
    {
        BtSession session;
        session.start(quietSettings(dir));
        id = session.addMagnet(kMagnet, {}, true).id;
        session.stop();
    }
    BtSession again;
    again.start(quietSettings(dir));
    QTRY_VERIFY(again.torrent(id).has_value());
    again.stop();
}

// removed before libtorrent had it: gone, also after its answer and a restart
void tst_BtSmoke::removeWhileAdding()
{
    QTemporaryDir dir;
    BtSession session;
    session.start(quietSettings(dir));
    QSignalSpy added(&session, &BtSession::torrentAdded);
    QSignalSpy removed(&session, &BtSession::torrentRemoved);
    const QString id = session.addMagnet(kMagnet, {}, true).id;
    QVERIFY(session.remove(id, false));
    QCOMPARE(removed.count(), 1);
    QVERIFY(!session.torrent(id));
    QTest::qWait(500);   // libtorrent's answer comes and goes
    QCOMPARE(added.count(), 0);
    QVERIFY(!session.torrent(id));
    // listed again: a new add, not a duplicate
    QCOMPARE(session.addMagnet(kMagnet, {}, true).outcome, BtAddOutcome::Added);
    QTRY_COMPARE(added.count(), 1);
    session.stop();
}

#include "tst_BtSmoke.moc"
