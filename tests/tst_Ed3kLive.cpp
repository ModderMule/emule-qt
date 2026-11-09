/// @file tst_Ed3kLive.cpp
/// @brief Watching live streams (src/ed3k/Ed3kLive): node A broadcasts a channel in the test
/// (liveBroadcast, livePushSegment), node B watches its ed3k:live link and serves the init
/// data and the segments as one progressive HTTP response on its website server's port
/// (loopback); the end of the broadcast ends the response; leave. A gap, a new init or a new
/// broadcast end the responses (#15) and the next request begins with the new init; the end
/// is never an empty chunk. A broadcast from an HLS folder (Ed3kLiveBroadcast) is watched.
/// Batch 14: its channel file is found by an ed3k channel search (SearchType::Ed3kLive) and
/// watched; a broadcast goes on after a restart within 10 min (§4); an oversized init is
/// skipped once and the next segment has gap (#11); what can't be played is not joined (§2.3).
/// Batch 15: the channel file is kept shared off air and after a restart (the same bytes), and
/// Delete Channel withdraws it (#5); no codecs named is left to the player (#7).

#include "Ed3kLive.h"
#include "Ed3kLiveBroadcast.h"
#include "Ed3kLiveFiles.h"
#include "Ed3kNames.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kSiteServer.h"
#include "Ed3kSearch.h"
#include "Ed3kStore.h"

#include "search/SearchFile.h"
#include "search/SearchParams.h"
#include "utils/Opcodes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

::ed3k::Node nodeOf(const Ed3kSession& s)
{
    return ::ed3k::Node{*s.nodeId(), ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort())};
}

/// An HLS folder as an encoder writes it: init.mp4, live<n>.m4s, live.m3u8
struct HlsFolder {
    QDir dir;
    QByteArray playlist = "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-MAP:URI=\"init.mp4\"\n";
    int n = 0;
    explicit HlsFolder(const QString& path, const QByteArray& init = "HLSINIT:" + QByteArray(800, 'i')) : dir(path)
    {
        writeFile(dir.filePath(QStringLiteral("init.mp4")), init);
    }
    void add()
    {
        const QString name = QStringLiteral("live%1.m4s").arg(n);
        writeFile(dir.filePath(name), "HLS" + QByteArray::number(n) + ":" + QByteArray(1500, 's'));
        playlist += "#EXTINF:1.000000,\n" + name.toLatin1() + "\n";
        writeFile(dir.filePath(QStringLiteral("live.m3u8")), playlist);
        ++n;
    }
};

/// channels.json of a config dir
QJsonArray channelsOf(const QString& configDir)
{
    QFile f(QDir(stateDir(configDir)).filePath(QStringLiteral("live/channels.json")));
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).array() : QJsonArray();
}

QJsonObject onAirOf(const QString& configDir, const QString& title)
{
    for (const QJsonValue& v : channelsOf(configDir))
        if (v.toObject().value(QStringLiteral("title")).toString() == title)
            return v.toObject().value(QStringLiteral("onAir")).toObject();
    return {};
}

/// the LiveSegment events a session sees
struct SegmentSpy {
    std::vector<::ed3k::event::LiveSegment> got;
    void attach(Ed3kSession& s, QObject* ctx)
    {
        QObject::connect(&s, &Ed3kSession::eventPolled, ctx, [this](const ::ed3k::Event& ev, bool*) {
            if (const auto* seg = std::get_if<::ed3k::event::LiveSegment>(&ev))
                got.push_back(*seg);
        }, Qt::DirectConnection);
    }
};

/// plays everything: the tests not about §2.3 must not hang on what this machine decodes
/// (Qt's FFmpeg on Linux may lack H.264)
QString playsAll(const QString&, const QString&)
{
    return {};
}

/// a refusing check for H.265 (§2.3 tests, whatever this machine plays)
QString noHevc(const QString& media, const QString& codecs)
{
    Q_UNUSED(media)
    return codecs.contains(QLatin1String("hev1")) ? QStringLiteral("no H.265 here") : QString();
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
    void aChannelFileIsReadAndResumeIsTimed();
    void aChannelFileIsFoundByAChannelSearch();
    void aBroadcastGoesOnAfterARestart();
    void anOversizedInitIsSkippedOnceWithAGap();
    void whatCannotBePlayedIsNotJoined();
    void theChannelFileIsKeptOffAirAndAfterARestart();
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
    live.setPlayCheck(playsAll);
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
    live.setPlayCheck(playsAll);
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
    live.setPlayCheck(playsAll);
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
    live.setPlayCheck(playsAll);
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

void tst_Ed3kLive::aChannelFileIsReadAndResumeIsTimed()
{
    // the signed channel file (§1.2) as the core makes it, read back for rows and the check
    ::ed3k::LiveBroadcast ch;
    ch.seed.fill(0x51);
    ch.id = {std::byte{'c'}, std::byte{'f'}};
    ch.media = "video/mp4";
    ch.codecs = "avc1.64001f,mp4a.40.2";
    const ::ed3k::LiveDesc desc{"Harbour Cam", "the harbour", "en", 2500, 2000, 1700000000};
    auto file = ::ed3k::liveChannelFile(ch, desc);
    QVERIFY(file);
    const QByteArray bytes(reinterpret_cast<const char*>(file->bytes().data()), qsizetype(file->size()));
    QVERIFY(Ed3kLiveFiles::hasMagic(bytes));
    const auto info = Ed3kLiveFiles::parse(bytes);
    QVERIFY(info);
    QCOMPARE(info->title, QStringLiteral("Harbour Cam"));
    QCOMPARE(info->comment, QStringLiteral("the harbour"));
    QCOMPARE(info->lang, QStringLiteral("en"));
    QCOMPARE(info->media, QStringLiteral("video/mp4"));
    QCOMPARE(info->codecs, QStringLiteral("avc1.64001f,mp4a.40.2"));
    QCOMPARE(info->kbps, 2500u);
    QCOMPARE(info->segmentMs, 2000u);
    QCOMPARE(info->created, std::uint64_t(1700000000));
    QCOMPARE(info->id, QByteArray("cf"));
    QCOMPARE(info->key.size(), 32);
    QVERIFY(!Ed3kLiveFiles::parse("ed3klive\x02junk"));
    QVERIFY(!Ed3kLiveFiles::parse(QByteArray(QByteArray("ed3klive\x01", 9) + QByteArray(70 * 1024, 'x'))));
    QCOMPARE(Ed3kLiveFiles::fileNameOf(QStringLiteral("a/b: c")), QStringLiteral("b_ c.ed3klive"));
    // a channel row: title, kbps, media and codecs, the language
    SearchFile row;
    row.setFileName(QStringLiteral("Harbour Cam.ed3klive"), false, true, true);
    Ed3kSearch::applyChannel(row, nullptr);
    QCOMPARE(row.getStrTagValue(FT_MEDIA_TITLE), QStringLiteral("Harbour Cam"));
    Ed3kSearch::applyChannel(row, &*info);
    QCOMPARE(row.getIntTagValue(FT_MEDIA_BITRATE), 2500u);
    QCOMPARE(row.getStrTagValue(FT_MEDIA_CODEC), QStringLiteral("video/mp4 avc1.64001f,mp4a.40.2 [en]"));

    // §4: back within 10 min of the cut goes on (start, next seq); later, or nothing sent: new
    const Ed3kLiveBroadcast::OnAir a{1000, 42, 2000, {}};
    const auto r = Ed3kLiveBroadcast::resumeOf(a, 2000 + 600);
    QVERIFY(r);
    QCOMPARE(r->start, 1000u);
    QCOMPARE(r->seq, 42u);
    QVERIFY(!Ed3kLiveBroadcast::resumeOf(a, 2000 + 601));
    QVERIFY(!Ed3kLiveBroadcast::resumeOf({1000, 0, 2000, {}}, 2001));
    QVERIFY(!Ed3kLiveBroadcast::resumeOf({3000, 5, 2000, {}}, 2001));   // a start ahead of now
}

void tst_Ed3kLive::aChannelFileIsFoundByAChannelSearch()
{
    QTemporaryDir da, db, dc, hls;
    QVERIFY(da.isValid() && db.isValid() && dc.isValid() && hls.isValid());
    Ed3kSession a, b, c;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QVERIFY(c.start(loopback(dc.path())));
    // A and C know B: the keyword records go there, the search asks there
    const ::ed3k::Node bNode = nodeOf(b);
    a.post([&](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1}); });
    c.post([&](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1}); });
    QTRY_VERIFY_WITH_TIMEOUT(a.contactCount() > 0 && c.contactCount() > 0, 20000);
    std::uint64_t stored = 0;
    connect(&a, &Ed3kSession::eventPolled, this, [&](const ::ed3k::Event& ev, bool*) {
        if (const auto* k = std::get_if<::ed3k::event::KeywordsPublished>(&ev))
            stored += k->stored;
    }, Qt::DirectConnection);

    // A broadcasts: its channel file is shared and published under its keywords (§1.2)
    HlsFolder folder(hls.path());
    folder.add();
    Ed3kLiveBroadcast caster(a, da.path());
    Ed3kLiveBroadcast::Options o;
    o.folder = hls.path();
    o.title = QStringLiteral("Lighthouse Channel");
    o.codecs = QStringLiteral("avc1.64001f,mp4a.40.2");
    o.lang = QStringLiteral("en");
    o.kbps = 2500;
    const auto started = caster.start(o);
    QVERIFY2(started, started ? "" : started.error().toUtf8().constData());
    QCOMPARE(started->channelFile.size(), 64);
    QTRY_VERIFY_WITH_TIMEOUT(stored > 0, 30000);
    const QJsonArray saved = channelsOf(da.path());
    QCOMPARE(saved.size(), 1);
    const auto created = saved.at(0).toObject().value(QStringLiteral("desc")).toObject().value(QStringLiteral("created")).toInteger();
    QVERIFY(created > 0);

    // C: an ed3k channel search finds it; the file fetched fills the row
    Ed3kNames names(c, dc.path());
    names.start();
    Ed3kSiteServer sites(c, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    Ed3kLive live(c, sites);
    live.setPlayCheck(playsAll);
    Ed3kSearch search(c);
    search.setLiveFetch([&](const ::ed3k::Root& root, std::uint64_t size, Ed3kLiveFiles::Fetched done) {
        live.files().fetch(root, size, std::move(done), Ed3kSearch::kLiveFetchMs);
    });
    std::vector<std::unique_ptr<SearchFile>> rows;
    std::optional<QString> ended;
    search.setSinks([&](uint32, std::unique_ptr<SearchFile> f) { rows.push_back(std::move(f)); },
                    [&](uint32, const QString& error) { ended = error; });
    SearchParams p;
    p.type = SearchType::Ed3kLive;
    p.expression = QStringLiteral("lighthouse");
    search.startMetaSearch(51, SearchType::Ed3kLive, p, {}, {});
    QTRY_VERIFY_WITH_TIMEOUT(ended.has_value(), 120000);
    QVERIFY(ended->isEmpty());
    const QByteArray root = QByteArray::fromHex(started->channelFile.toLatin1());
    const auto it = std::ranges::find_if(rows, [&](const auto& r) { return r->ed3kRoot() == root; });
    QVERIFY(it != rows.end());
    QCOMPARE((*it)->fileName(), QStringLiteral("Lighthouse Channel.ed3klive"));
    QCOMPARE((*it)->getStrTagValue(FT_MEDIA_TITLE), QStringLiteral("Lighthouse Channel"));
    QCOMPARE((*it)->getIntTagValue(FT_MEDIA_BITRATE), 2500u);
    QCOMPARE((*it)->getStrTagValue(FT_MEDIA_CODEC), QStringLiteral("video/mp4 avc1.64001f,mp4a.40.2 [en]"));
    // only channel files in a channel search
    for (const auto& r : rows)
        QVERIFY(r->fileName().endsWith(QStringLiteral(".ed3klive")));

    // "Watch": the file fetched by the search is watched at once (the same stream)
    ::ed3k::Root r;
    std::memcpy(r.bytes.data(), root.constData(), 32);
    std::optional<std::expected<QString, QString>> watched;
    live.watchFound(r, std::uint64_t((*it)->fileSize()), [&](std::expected<QString, QString> id) { watched = std::move(id); });
    QVERIFY(watched.has_value());
    QVERIFY2(*watched, watched->has_value() ? "" : watched->error().toUtf8().constData());
    QCOMPARE(**watched, started->id);
    QCOMPARE(live.info(started->id)->title, QStringLiteral("Lighthouse Channel"));

    // on air again with the same description: the same file (the same created)
    QVERIFY(caster.stop(started->id));
    const auto again = caster.start(o);
    QVERIFY(again);
    QCOMPARE(again->channelFile, started->channelFile);
    // a new description: a new file (a later created), the one before withdrawn
    QVERIFY(caster.describe(again->id, QStringLiteral("now with sound"), QStringLiteral("en"), 3000));
    const auto info = caster.info(again->id);
    QCOMPARE(info->channelFile.size(), 64);
    QVERIFY(info->channelFile != started->channelFile);
    const auto created2 = channelsOf(da.path()).at(0).toObject().value(QStringLiteral("desc")).toObject().value(QStringLiteral("created")).toInteger();
    QVERIFY(created2 > created);
    QVERIFY(caster.stop(again->id));
    a.disconnect(this);
    names.stop();
}

void tst_Ed3kLive::aBroadcastGoesOnAfterARestart()
{
    QTemporaryDir da, hls;
    QVERIFY(da.isValid() && hls.isValid());
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    HlsFolder folder(hls.path());
    folder.add();
    const QString title = QStringLiteral("Restart TV");
    std::uint64_t start = 0, seq = 0;
    {
        Ed3kLiveBroadcast caster(a, da.path());
        Ed3kLiveBroadcast::Options o;
        o.folder = hls.path();
        o.title = title;
        const auto started = caster.start(o);
        QVERIFY(started);
        QVERIFY(!started->resumed);
        folder.add();
        caster.scan();
        QCOMPARE(caster.info(started->id)->segments, 2u);
        // what is on air is kept: the broadcast, the next seq, the cut
        const QJsonObject onAir = onAirOf(da.path(), title);
        QCOMPARE(onAir.value(QStringLiteral("seq")).toInteger(), 2);
        QCOMPARE(onAir.value(QStringLiteral("folder")).toString(), hls.path());
        start = std::uint64_t(onAir.value(QStringLiteral("start")).toInteger());
        seq = 2;
        QVERIFY(start > 0);
        // ed3k stops and starts again (a cut): it goes on with the same broadcast at its next seq
        a.stop();
        QVERIFY(caster.list().empty());
        QVERIFY(a.start(loopback(da.path())));
        QTRY_VERIFY_WITH_TIMEOUT(!caster.list().empty(), 10000);
        const auto resumed = caster.list().front();
        QVERIFY(resumed.resumed);
        QCOMPARE(resumed.id, started->id);
        folder.add();
        caster.scan();
        const QJsonObject after = onAirOf(da.path(), title);
        QCOMPARE(std::uint64_t(after.value(QStringLiteral("start")).toInteger()), start);
        QCOMPARE(std::uint64_t(after.value(QStringLiteral("seq")).toInteger()), seq + 1);
        // the daemon quits: no end record, kept on air
    }
    QVERIFY(!onAirOf(da.path(), title).isEmpty());
    // later than 10 min after the cut: a new broadcast (a later start, seq from 0)
    a.stop();
    {
        QJsonArray all = channelsOf(da.path());
        QJsonObject c = all.at(0).toObject();
        QJsonObject onAir = c.value(QStringLiteral("onAir")).toObject();
        onAir.insert(QStringLiteral("cut"), qint64(QDateTime::currentSecsSinceEpoch() - 700));
        onAir.insert(QStringLiteral("start"), qint64(start - 700));
        c.insert(QStringLiteral("onAir"), onAir);
        all.replace(0, c);
        writeFile(QDir(stateDir(da.path())).filePath(QStringLiteral("live/channels.json")), QJsonDocument(all).toJson());
    }
    QVERIFY(a.start(loopback(da.path())));
    Ed3kLiveBroadcast caster(a, da.path());   // resumed at once: ed3k runs
    QCOMPARE(caster.list().size(), std::size_t(1));
    QVERIFY(!caster.list().front().resumed);
    const QJsonObject fresh = onAirOf(da.path(), title);
    QVERIFY(std::uint64_t(fresh.value(QStringLiteral("start")).toInteger()) >= start);
    QCOMPARE(fresh.value(QStringLiteral("seq")).toInteger(), 1);   // the newest segment, seq 0
    // stop() ends it: nothing to resume
    QVERIFY(caster.stop(caster.list().front().id));
    QVERIFY(onAirOf(da.path(), title).isEmpty());
}

void tst_Ed3kLive::anOversizedInitIsSkippedOnceWithAGap()
{
    QTemporaryDir da, db, hls;
    QVERIFY(da.isValid() && db.isValid() && hls.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    // init data over 256 KiB (§2.1): not sent
    HlsFolder folder(hls.path(), QByteArray(300 * 1024, 'I'));
    folder.add();
    Ed3kLiveBroadcast caster(a, da.path());
    Ed3kLiveBroadcast::Options o;
    o.folder = hls.path();
    o.title = QStringLiteral("Big Init TV");
    const auto started = caster.start(o);
    QVERIFY(started);
    QCOMPARE(caster.info(started->id)->segments, 0u);
    QCOMPARE(caster.info(started->id)->skipped, 1u);
    QVERIFY(!caster.info(started->id)->error.isEmpty());
    // not tried again at every scan (was: the same segment forever)
    for (int i = 0; i < 5; ++i)
        caster.scan();
    QCOMPARE(caster.info(started->id)->skipped, 1u);

    // a viewer; the encoder writes a small init and the next segment: it goes out with gap
    SegmentSpy spy;
    spy.attach(b, this);
    Ed3kNames names(b, db.path());
    names.start();
    Ed3kSiteServer sites(b, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    Ed3kLive live(b, sites);
    live.setPlayCheck(playsAll);
    const auto id = live.watch(started->link);
    QVERIFY(id);
    QVERIFY(live.addPeer(*id, sourceOf(a)));
    QTRY_VERIFY_WITH_TIMEOUT(caster.info(started->id)->neighbours > 0, 30000);
    writeFile(folder.dir.filePath(QStringLiteral("init.mp4")), "SMALLINIT:" + QByteArray(500, 'i'));
    folder.add();
    caster.scan();
    QCOMPARE(caster.info(started->id)->segments, 1u);   // only the new one (was: both)
    QVERIFY(caster.info(started->id)->error.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!spy.got.empty(), 30000);
    QCOMPARE(spy.got.front().seq, 0u);
    QVERIFY(spy.got.front().gap);
    QVERIFY(caster.stop(started->id));
    b.disconnect(this);
    names.stop();
}

void tst_Ed3kLive::whatCannotBePlayedIsNotJoined()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    ::ed3k::LiveBroadcast ch;
    ch.seed.fill(0x52);
    ch.id = {std::byte{'h'}};
    ch.media = "video/mp4";
    ch.codecs = "hev1.1.6.L93.B0,mp4a.40.2";
    Ed3kNames names(b, db.path());
    names.start();
    Ed3kSiteServer sites(b, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    Ed3kLive live(b, sites);
    live.setPlayCheck(noHevc);

    // a channel file naming what can't be played: refused, not joined (§2.3)
    auto file = ::ed3k::liveChannelFile(ch, ::ed3k::LiveDesc{"HEVC TV", "", "", 0, 0, 1});
    QVERIFY(file);
    const QByteArray bytes(reinterpret_cast<const char*>(file->bytes().data()), qsizetype(file->size()));
    const auto refused = live.watchFile(bytes);
    QVERIFY(!refused);
    QCOMPARE(refused.error(), QStringLiteral("no H.265 here"));
    QVERIFY(live.list().empty());
    QVERIFY(!live.watchFile("not a channel file"));

    // a link: the first peer names the codecs, then the stream is left (Unsupported)
    auto stream = a.engine()->liveBroadcast(a.now(), ch);
    QVERIFY(stream);
    a.pump();
    const auto key = ::ed3k::roomMemberKey(ch.seed);
    QVERIFY(key);
    const QString link = QStringLiteral("ed3k:live?key=%1&id=%2").arg(base32(*key), base32(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("h"), 1)));
    const auto id = live.watch(link);
    QVERIFY(id);
    QVERIFY(live.addPeer(*id, sourceOf(a)));
    QTimer push;
    connect(&push, &QTimer::timeout, &a, [&] {
        (void)a.engine()->livePushSegment(a.now(), *stream, spanOf("SEG:" + QByteArray(1000, 's')), 1000, false, {});
        a.pump();
    });
    push.start(500);
    QTRY_VERIFY_WITH_TIMEOUT(live.info(*id)->state == Ed3kLive::State::Unsupported, 60000);
    QCOMPARE(live.info(*id)->error, QStringLiteral("no H.265 here"));
    push.stop();
    // a player asking for it: gone
    auto http = get(sites.port(), *id, "/stream");
    QVERIFY(http);
    QByteArray got;
    QTRY_VERIFY_WITH_TIMEOUT((got += http->readAll()).contains("\r\n\r\n"), 10000);
    QVERIFY(got.startsWith("HTTP/1.1 410"));
    QVERIFY(live.leave(*id));
    // the check itself: an unknown container or codec is left to the player
    QVERIFY(Ed3kLive::canPlay(QStringLiteral("video/mp2t"), QStringLiteral("x-unknown")).isEmpty());
    // #7: a known container without codecs named: can't tell, the player decides
    for (const char* media : {"video/mp4", "audio/mp4", "video/webm", "video/mp2t", "audio/mpeg"})
        QVERIFY2(Ed3kLive::canPlay(QString::fromLatin1(media), QString()).isEmpty(), media);
    QVERIFY(Ed3kLive::canPlay(QStringLiteral("video/webm"), QStringLiteral("  ")).isEmpty());
    (void)a.engine()->liveEnd(a.now(), *stream);
    a.pump();
    names.stop();
}

// live.md §1.2 (#5): the channel file describes the channel: shared while it is kept, on air
// or not, at each start with the same created (the same bytes, the same root); Delete Channel
// withdraws it and drops the channel with its keys
void tst_Ed3kLive::theChannelFileIsKeptOffAirAndAfterARestart()
{
    QTemporaryDir da, hls;
    QVERIFY(da.isValid() && hls.isValid());
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    HlsFolder folder(hls.path());
    folder.add();
    Ed3kLiveBroadcast::Options o;
    o.folder = hls.path();
    o.title = QStringLiteral("Off Air TV");
    o.codecs = QStringLiteral("avc1.64001f");
    o.comment = QStringLiteral("evenings");
    QString root;
    QString channel;
    qint64 created = 0;
    {
        Ed3kLiveBroadcast caster(a, da.path());
        QVERIFY(caster.channels().empty());
        const auto started = caster.start(o);
        QVERIFY2(started, started ? "" : started.error().toUtf8().constData());
        root = started->channelFile;
        channel = started->channel;
        QCOMPARE(root.size(), 64);
        QCOMPARE(channel.size(), 32);
        created = channelsOf(da.path()).at(0).toObject().value(QStringLiteral("desc")).toObject().value(QStringLiteral("created")).toInteger();
        // the user stops it: off air, the file stays shared (was: withdrawn)
        QVERIFY(caster.stop(started->id));
        const auto off = caster.channels();
        QCOMPARE(off.size(), std::size_t(1));
        QVERIFY(off.front().stream.isEmpty());
        QCOMPARE(off.front().channel, channel);
        QCOMPARE(off.front().channelFile, root);
        QCOMPARE(off.front().title, o.title);
    }
    // the daemon again (a new broadcaster, the same Config): published at start, off air,
    // the same created, so the same bytes and root
    {
        Ed3kLiveBroadcast caster(a, da.path());
        QVERIFY(caster.list().empty());
        QCOMPARE(caster.channels().size(), std::size_t(1));
        QCOMPARE(caster.channels().front().channelFile, root);
        QCOMPARE(channelsOf(da.path()).at(0).toObject().value(QStringLiteral("desc")).toObject().value(QStringLiteral("created")).toInteger(),
                 created);
        // ed3k stops and starts: still the same
        a.stop();
        QVERIFY(a.start(loopback(da.path())));
        QTRY_VERIFY_WITH_TIMEOUT(caster.channels().front().channelFile == root, 5000);
    }
    // a process that starts without ed3k running: published once ed3k is up
    a.stop();
    Ed3kLiveBroadcast caster(a, da.path());
    QVERIFY(caster.channels().front().channelFile.isEmpty());
    QVERIFY(a.start(loopback(da.path())));
    QTRY_COMPARE_WITH_TIMEOUT(caster.channels().front().channelFile, root, 5000);
    // on air again with the same description: the same file
    const auto again = caster.start(o);
    QVERIFY(again);
    QCOMPARE(again->channelFile, root);
    QCOMPARE(caster.channels().front().stream, again->id);
    // Delete Channel (on air: ended first): withdrawn, dropped, the keys gone
    const QString keyFile = QDir(stateDir(da.path())).filePath(QStringLiteral("live/%1.key").arg(channel));
    QVERIFY(QFileInfo::exists(keyFile));
    QVERIFY(caster.deleteChannel(channel));
    QVERIFY(caster.list().empty());
    QVERIFY(caster.channels().empty());
    QVERIFY(channelsOf(da.path()).isEmpty());
    QVERIFY(!QFileInfo::exists(keyFile));
    QVERIFY(!caster.deleteChannel(channel));
    QVERIFY(!caster.deleteChannel(QStringLiteral("zz")));
    // not published at the next start
    Ed3kLiveBroadcast later(a, da.path());
    QVERIFY(later.channels().empty());
}

QTEST_GUILESS_MAIN(tst_Ed3kLive)
#include "tst_Ed3kLive.moc"
