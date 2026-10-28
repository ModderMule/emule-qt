/// @file tst_Ed3kLive.cpp
/// @brief Watching live streams (src/ed3k/Ed3kLive): node A broadcasts a channel in the test
/// (liveBroadcast, livePushSegment), node B watches its ed3k:live link and serves the init
/// data and the segments as one progressive HTTP response on its website server's port
/// (loopback); the end of the broadcast ends the response; leave. A gap, a new init or a new
/// broadcast end the responses (#15) and the next request begins with the new init; the end
/// is never an empty chunk. A broadcast from an HLS folder (Ed3kLiveBroadcast) is watched.

#include "Ed3kLive.h"
#include "Ed3kLiveBroadcast.h"
#include "Ed3kNames.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kSiteServer.h"
#include "Ed3kStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace eMule;
using namespace eMule::ed3k;

namespace {

Ed3kSettings loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

::ed3k::Source sourceOf(const Ed3kSession& s)
{
    return ::ed3k::Source{*s.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort()), std::nullopt, {}};
}

std::span<const std::byte> spanOf(const QByteArray& b)
{
    return {reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())};
}

/// a GET of the stream on the website server's port
std::unique_ptr<QTcpSocket> get(quint16 port, const QString& id, const QByteArray& path = "/", const QByteArray& accept = "*/*")
{
    auto s = std::make_unique<QTcpSocket>();
    s->connectToHost(QHostAddress::LocalHost, port);
    if (!s->waitForConnected(5000))
        return nullptr;
    s->write("GET " + path + " HTTP/1.1\r\nHost: live-" + id.toLatin1() + ".localhost:" + QByteArray::number(port) + "\r\nAccept: " + accept
             + "\r\n\r\n");
    return s;
}

/// Qt's warnings counted (an empty last chunk: "Chunk must have length > 0")
int s_chunkWarnings = 0;
QtMessageHandler s_oldHandler = nullptr;
void countWarnings(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    if (msg.contains(QLatin1String("Chunk must have length")))
        ++s_chunkWarnings;
    if (s_oldHandler)
        s_oldHandler(type, ctx, msg);
}

void writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

} // namespace

class tst_Ed3kLive : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { s_oldHandler = qInstallMessageHandler(countWarnings); }
    void aBroadcastIsServedOverLoopbackHttp();
    void badLinksAndOtherHosts();
    void hlsPlaylistsAreRead();
    void aGapOrNewInitRestartsTheViewers();
    void aBroadcastFromAnHlsFolderIsWatched();
    void cleanup() { QTest::qWait(10); }
};

void tst_Ed3kLive::aBroadcastIsServedOverLoopbackHttp()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));

    // A: a channel (live.md §1.1, §4)
    ::ed3k::LiveBroadcast ch;
    ch.seed.fill(0x44);
    ch.id = {std::byte{'t'}, std::byte{'v'}};
    ch.media = "video/mp4";
    ch.codecs = "avc1.64001f";
    auto stream = a.engine()->liveBroadcast(a.now(), ch);
    QVERIFY(stream);
    a.pump();
    const auto key = ::ed3k::roomMemberKey(ch.seed);   // the same Ed25519 derivation
    QVERIFY(key);
    const QString link = QStringLiteral("ed3k:live?key=%1&id=%2&dn=Test%20TV").arg(base32(*key), base32(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("tv"), 2)));

    // B: its website server and the watcher
    Ed3kNames names(b, db.path());
    names.start();
    Ed3kSiteServer sites(b, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    Ed3kLive live(b, sites);
    const auto id = live.watch(link);
    QVERIFY2(id, id ? "" : id.error().toUtf8().constData());
    QCOMPARE(*id, QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(stream->bytes.data()), 16).toHex()));
    QCOMPARE(live.info(*id)->title, QStringLiteral("Test TV"));
    QCOMPARE(live.urlOf(*id), QStringLiteral("http://live-%1.localhost:%2/").arg(*id).arg(sites.port()));
    QVERIFY(live.addPeer(*id, sourceOf(a)));

    // the response waits for the first segment; A pushes one a second with its init data
    QTcpSocket http;
    http.connectToHost(QHostAddress::LocalHost, sites.port());
    QVERIFY(http.waitForConnected(5000));
    http.write("GET / HTTP/1.1\r\nHost: live-" + id->toLatin1() + ".localhost:" + QByteArray::number(sites.port()) + "\r\n\r\n");
    const QByteArray init = "INIT:ftyp+moov:" + QByteArray(1000, 'i');
    int n = 0;
    QTimer push;
    connect(&push, &QTimer::timeout, &a, [&] {
        const QByteArray seg = "SEG" + QByteArray::number(n++) + ":" + QByteArray(3000, 's');
        (void)a.engine()->livePushSegment(a.now(), *stream, spanOf(seg), 1000, false, spanOf(init));
        a.pump();
    });
    push.start(500);
    QByteArray got;
    QTRY_VERIFY_WITH_TIMEOUT((got += http.readAll()).contains("SEG2:"), 60000);
    QVERIFY(got.startsWith("HTTP/1.1 200"));
    const QByteArray headers = got.left(got.indexOf("\r\n\r\n")).toLower();
    QVERIFY2(headers.contains("content-type: video/mp4; codecs=\"avc1.64001f\""), headers.constData());
    QVERIFY(headers.contains("transfer-encoding: chunked"));
    // the init data before the first segment
    QVERIFY(got.indexOf("INIT:ftyp+moov:") > 0 && got.indexOf("INIT:ftyp+moov:") < got.indexOf("SEG"));
    QTRY_VERIFY_WITH_TIMEOUT(live.info(*id)->state == Ed3kLive::State::Playing && live.info(*id)->neighbours > 0, 10000);
    QVERIFY(live.info(*id)->segments >= 2);
    QCOMPARE(live.info(*id)->media, QStringLiteral("video/mp4"));

    // the broadcast ends: so does the response (the last chunk)
    push.stop();
    QVERIFY(a.engine()->liveEnd(a.now(), *stream));
    a.pump();
    QTRY_VERIFY_WITH_TIMEOUT(live.info(*id)->state == Ed3kLive::State::Ended, 60000);
    QTRY_VERIFY_WITH_TIMEOUT((got += http.readAll()).endsWith("0\r\n\r\n"), 10000);
    // the last chunk is a box the player skips, never empty (Qt: "Chunk must have length > 0")
    QVERIFY(got.endsWith(QByteArray("8\r\n\x00\x00\x00\x08" "free\r\n0\r\n\r\n", 18)));
    QCOMPARE(s_chunkWarnings, 0);
    // a later request: gone
    QTcpSocket late;
    late.connectToHost(QHostAddress::LocalHost, sites.port());
    QVERIFY(late.waitForConnected(5000));
    late.write("GET / HTTP/1.1\r\nHost: live-" + id->toLatin1() + ".localhost:" + QByteArray::number(sites.port()) + "\r\n\r\n");
    QByteArray lateGot;
    QTRY_VERIFY_WITH_TIMEOUT((lateGot += late.readAll()).contains("\r\n\r\n"), 10000);
    QVERIFY(lateGot.startsWith("HTTP/1.1 410"));

    QVERIFY(live.leave(*id));
    QVERIFY(!live.info(*id));
    QVERIFY(!live.leave(*id));
    names.stop();
}

void tst_Ed3kLive::badLinksAndOtherHosts()
{
    QTemporaryDir d;
    Ed3kSession s;
    QVERIFY(s.start(loopback(d.path())));
    Ed3kNames names(s, d.path());
    names.start();
    Ed3kSiteServer sites(s, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    Ed3kLive live(s, sites);
    QVERIFY(!live.watch(QStringLiteral("ed3k:live?key=abc")));
    QVERIFY(!live.leave(QStringLiteral("00")));
    // a stream not watched: 404; another host stays the website server's (421)
    QTcpSocket h;
    h.connectToHost(QHostAddress::LocalHost, sites.port());
    QVERIFY(h.waitForConnected(5000));
    h.write("GET / HTTP/1.1\r\nHost: live-" + QByteArray(32, 'a') + ".localhost:" + QByteArray::number(sites.port()) + "\r\n\r\n");
    QByteArray got;
    QTRY_VERIFY_WITH_TIMEOUT((got += h.readAll()).contains("\r\n\r\n"), 10000);
    QVERIFY(got.startsWith("HTTP/1.1 404"));
    QTcpSocket o;
    o.connectToHost(QHostAddress::LocalHost, sites.port());
    QVERIFY(o.waitForConnected(5000));
    o.write("GET / HTTP/1.1\r\nHost: live-" + QByteArray(32, 'a') + ".example:" + QByteArray::number(sites.port()) + "\r\n\r\n");
    got.clear();
    QTRY_VERIFY_WITH_TIMEOUT((got += o.readAll()).contains("\r\n\r\n"), 10000);
    QVERIFY(got.startsWith("HTTP/1.1 421"));
    names.stop();
}

void tst_Ed3kLive::hlsPlaylistsAreRead()
{
    // as ffmpeg -f hls -hls_segment_type fmp4 writes it (RFC 8216)
    const auto pl = Ed3kHlsPlaylist::parse("#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:41\n"
                                           "#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:2.002000,\nlive41.m4s\n#EXTINF:1.5,\nlive42.m4s\n"
                                           "#EXT-X-DISCONTINUITY\n#EXT-X-MAP:URI=\"init2.mp4\"\n#EXTINF:2,\nlive43.m4s\n");
    QVERIFY(pl);
    QCOMPARE(pl->mediaSequence, 41u);
    QCOMPARE(pl->entries.size(), std::size_t(3));
    QCOMPARE(pl->entries.at(0).uri, QStringLiteral("live41.m4s"));
    QCOMPARE(pl->entries.at(0).ms, 2002u);
    QCOMPARE(pl->entries.at(0).map, QStringLiteral("init.mp4"));
    QVERIFY(!pl->entries.at(1).discontinuity);
    QCOMPARE(pl->entries.at(1).ms, 1500u);
    QVERIFY(pl->entries.at(2).discontinuity);
    QCOMPARE(pl->entries.at(2).map, QStringLiteral("init2.mp4"));
    QVERIFY(!pl->ended);
    QVERIFY(!Ed3kHlsPlaylist::parse("not a playlist"));
}

void tst_Ed3kLive::aGapOrNewInitRestartsTheViewers()
{
    QTemporaryDir da, db;
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    ::ed3k::LiveBroadcast ch;
    ch.seed.fill(0x45);
    ch.id = {std::byte{'g'}};
    ch.media = "video/mp4";
    auto stream = a.engine()->liveBroadcast(a.now(), ch);
    QVERIFY(stream);
    a.pump();
    const auto key = ::ed3k::roomMemberKey(ch.seed);
    QVERIFY(key);
    const QString link = QStringLiteral("ed3k:live?key=%1&id=%2").arg(base32(*key), base32(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("g"), 1)));
    Ed3kNames names(b, db.path());
    names.start();
    Ed3kSiteServer sites(b, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    Ed3kLive live(b, sites);
    const auto id = live.watch(link);
    QVERIFY(id);
    QVERIFY(live.addPeer(*id, sourceOf(a)));
    const auto push = [&](const QByteArray& seg, bool gap, const QByteArray& init) {
        QVERIFY(a.engine()->livePushSegment(a.now(), *stream, spanOf(seg), 1000, gap, spanOf(init)));
        a.pump();
    };
    const QByteArray init1 = "INIT1:" + QByteArray(500, '1');
    const QByteArray init2 = "INIT2:" + QByteArray(500, '2');
    // a browser at / gets the player page that reconnects
    QTRY_VERIFY_WITH_TIMEOUT(live.info(*id)->neighbours > 0, 30000);
    push("SEGA:" + QByteArray(2000, 'a'), false, init1);
    QTRY_VERIFY_WITH_TIMEOUT(live.info(*id)->segments >= 1, 30000);
    auto page = get(sites.port(), *id, "/", "text/html,*/*");
    QVERIFY(page);
    QByteArray pageGot;
    QTRY_VERIFY_WITH_TIMEOUT((pageGot += page->readAll()).contains("</html>"), 10000);
    QVERIFY(pageGot.toLower().contains("content-type: text/html"));
    QVERIFY(pageGot.contains("/stream"));

    auto v1 = get(sites.port(), *id, "/stream");
    QVERIFY(v1);
    QByteArray got1;
    QTRY_VERIFY_WITH_TIMEOUT((got1 += v1->readAll()).contains("SEGA:"), 10000);
    QVERIFY(got1.indexOf("INIT1:") > 0 && got1.indexOf("INIT1:") < got1.indexOf("SEGA:"));
    push("SEGB:" + QByteArray(2000, 'b'), false, init1);
    QTRY_VERIFY_WITH_TIMEOUT((got1 += v1->readAll()).contains("SEGB:"), 30000);
    QVERIFY(!got1.endsWith("0\r\n\r\n"));
    // a gap: the response ends (one fMP4 stream can't reset), the next begins anew
    push("SEGC:" + QByteArray(2000, 'c'), true, init1);
    QTRY_VERIFY_WITH_TIMEOUT((got1 += v1->readAll()).endsWith("0\r\n\r\n"), 30000);
    QVERIFY(!got1.contains("SEGC:"));
    QCOMPARE(live.info(*id)->restarts, 1u);
    auto v2 = get(sites.port(), *id, "/stream?r=1");
    QByteArray got2;
    QTRY_VERIFY_WITH_TIMEOUT((got2 += v2->readAll()).contains("SEGC:"), 10000);
    QVERIFY(got2.indexOf("INIT1:") > 0 && got2.indexOf("INIT1:") < got2.indexOf("SEGC:"));
    QVERIFY(!got2.contains("SEGB:"));   // the window began anew
    // another init: the same; the next response begins with the new init data
    push("SEGD:" + QByteArray(2000, 'd'), false, init2);
    QTRY_VERIFY_WITH_TIMEOUT((got2 += v2->readAll()).endsWith("0\r\n\r\n"), 30000);
    QVERIFY(!got2.contains("INIT2:"));
    auto v3 = get(sites.port(), *id, "/stream?r=2");
    QByteArray got3;
    QTRY_VERIFY_WITH_TIMEOUT((got3 += v3->readAll()).contains("SEGD:"), 10000);
    QVERIFY(got3.indexOf("INIT2:") > 0 && got3.indexOf("INIT2:") < got3.indexOf("SEGD:"));
    QVERIFY(!got3.contains("INIT1:"));
    QCOMPARE(live.info(*id)->restarts, 2u);
    QCOMPARE(s_chunkWarnings, 0);
    (void)a.engine()->liveEnd(a.now(), *stream);
    a.pump();
    names.stop();
}

void tst_Ed3kLive::aBroadcastFromAnHlsFolderIsWatched()
{
    QTemporaryDir da, db, hls;
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    // what the encoder wrote so far: init.mp4, two segments, the playlist
    const QDir dir(hls.path());
    writeFile(dir.filePath(QStringLiteral("init.mp4")), "HLSINIT:" + QByteArray(800, 'i'));
    QByteArray playlist = "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-MAP:URI=\"init.mp4\"\n";
    int n = 0;
    const auto addSegment = [&] {
        const QString name = QStringLiteral("live%1.m4s").arg(n);
        writeFile(dir.filePath(name), "HLS" + QByteArray::number(n) + ":" + QByteArray(1500, 's'));
        playlist += "#EXTINF:1.000000,\n" + name.toLatin1() + "\n";
        writeFile(dir.filePath(QStringLiteral("live.m3u8")), playlist);
        ++n;
    };
    addSegment();
    addSegment();

    Ed3kLiveBroadcast caster(a, da.path());
    Ed3kLiveBroadcast::Options bad;
    bad.folder = QStringLiteral("/no/such/folder");
    bad.title = QStringLiteral("x");
    QVERIFY(!caster.start(bad));
    Ed3kLiveBroadcast::Options o;
    o.folder = hls.path();
    o.title = QStringLiteral("Folder TV");
    o.codecs = QStringLiteral("avc1.64001f");
    const auto started = caster.start(o);
    QVERIFY2(started, started ? "" : started.error().toUtf8().constData());
    QVERIFY(started->link.startsWith(QStringLiteral("ed3k:live?key=")));
    QVERIFY(started->link.endsWith(QStringLiteral("&dn=Folder%20TV")));
    QVERIFY(!started->link.contains(QStringLiteral("&s=")));
    // a live stream begins at the newest segment
    QCOMPARE(caster.info(started->id)->segments, 1u);
    // the channel key is kept: the same title keeps its link
    QVERIFY(QFileInfo::exists(QDir(stateDir(da.path())).filePath(QStringLiteral("live/channels.json"))));

    Ed3kNames names(b, db.path());
    names.start();
    Ed3kSiteServer sites(b, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    Ed3kLive live(b, sites);
    const auto id = live.watch(started->link);
    QVERIFY(id);
    QCOMPARE(*id, started->id);
    QCOMPARE(live.info(*id)->title, QStringLiteral("Folder TV"));
    QVERIFY(live.addPeer(*id, sourceOf(a)));
    auto http = get(sites.port(), *id, "/stream");
    QVERIFY(http);
    // the encoder goes on: each new segment of the playlist is pushed
    QTimer more;
    connect(&more, &QTimer::timeout, &a, addSegment);
    more.start(700);
    QByteArray got;
    QTRY_VERIFY_WITH_TIMEOUT((got += http->readAll()).contains("HLS4:"), 60000);
    QVERIFY(got.indexOf("HLSINIT:") > 0 && got.indexOf("HLSINIT:") < got.indexOf("HLS"
                                                                                   "4:"));
    QVERIFY(!got.contains("HLS0:"));
    QTRY_VERIFY_WITH_TIMEOUT(caster.info(started->id)->neighbours > 0, 10000);
    QCOMPARE(live.info(*id)->codecs, QStringLiteral("avc1.64001f"));
    // Stop: the end record; the viewer's response ends
    more.stop();
    QVERIFY(caster.stop(started->id));
    QVERIFY(!caster.isBroadcast(started->id));
    QTRY_VERIFY_WITH_TIMEOUT(live.info(*id)->state == Ed3kLive::State::Ended, 60000);
    QTRY_VERIFY_WITH_TIMEOUT((got += http->readAll()).endsWith("0\r\n\r\n"), 10000);
    // the same title again: the same channel (link)
    const auto again = caster.start(o);
    QVERIFY(again);
    QCOMPARE(again->link, started->link);
    QVERIFY(caster.stop(again->id));
    names.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kLive)
#include "tst_Ed3kLive.moc"
