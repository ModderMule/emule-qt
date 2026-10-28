/// @file tst_Ed3kLive.cpp
/// @brief Watching live streams (src/ed3k/Ed3kLive): node A broadcasts a channel in the test
/// (liveBroadcast, livePushSegment), node B watches its ed3k:live link and serves the init
/// data and the segments as one progressive HTTP response on its website server's port
/// (loopback); the end of the broadcast ends the response; leave.

#include "Ed3kLive.h"
#include "Ed3kNames.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kSiteServer.h"

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

} // namespace

class tst_Ed3kLive : public QObject {
    Q_OBJECT

private slots:
    void aBroadcastIsServedOverLoopbackHttp();
    void badLinksAndOtherHosts();
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

QTEST_GUILESS_MAIN(tst_Ed3kLive)
#include "tst_Ed3kLive.moc"
