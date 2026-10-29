/// @file tst_BtSmoke.cpp
/// @brief eMule::Bt links and moc runs; the torrent id and the inputs an add takes.

#include "BtIds.h"
#include "BtSession.h"
#include "add/BtAddSource.h"
#include "session/BtSettingsMap.h"

#include <libtorrent/version.hpp>

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
    void inspectTorrentFile();
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

QTEST_GUILESS_MAIN(tst_BtSmoke)
#include "tst_BtSmoke.moc"
