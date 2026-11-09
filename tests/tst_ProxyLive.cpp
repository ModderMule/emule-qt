/// @file tst_ProxyLive.cpp
/// @brief The Proxy page against a real proxy (report 34). Labelled `live`.
///
/// EMULE_TEST_PROXY in the project-root .env, e.g. socks5://user:pass@host:1080.
/// No default: the account is the user's. Skips without it.
///
/// Each network is checked twice: through the real proxy it works, through a dead
/// proxy (closed loopback port) it fails. The failure proves the traffic really
/// takes the proxy instead of going direct.
///
///   ctest -L live -R tst_ProxyLive

#include "TestHelpers.h"

#include "BtTypes.h"
#include "chat/IrcClient.h"
#include "net/GuardedNetworkAccessManager.h"
#include "prefs/Preferences.h"
#include "session/BtSettingsMap.h"
#include "utils/Opcodes.h"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/sha1_hash.hpp>

#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace eMule;
using eMule::testing::loadProjectEnv;

namespace {

constexpr auto kIpEcho = "https://api.ipify.org";
constexpr auto kIrcServer = "irc.mindforge.org:6667";
constexpr auto kTracker = "http://tracker.opentrackr.org:1337/announce";

struct ProxyTarget {
    int type = PROXYTYPE_NOPROXY;
    QString host;
    uint16 port = 0;
    QString user;
    QString password;
};

ProxyTarget parseProxy(const QString& text)
{
    const QUrl url(text);
    ProxyTarget t;
    const QString scheme = url.scheme().toLower();
    if (scheme == u"socks5" || scheme == u"socks5h")
        t.type = PROXYTYPE_SOCKS5;
    else if (scheme == u"socks4")
        t.type = PROXYTYPE_SOCKS4;
    else if (scheme == u"http")
        t.type = PROXYTYPE_HTTP11;
    t.host = url.host();
    t.port = uint16(url.port(0));
    t.user = url.userName(QUrl::FullyDecoded);
    t.password = url.password(QUrl::FullyDecoded);
    return t;
}

/// A loopback port nothing listens on: a proxy there refuses every connect.
uint16 deadPort()
{
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    return s.serverPort();
}

void setPrefsProxy(const ProxyTarget& t)
{
    thePrefs.setProxyType(t.type);
    thePrefs.setProxyHost(t.host);
    thePrefs.setProxyPort(t.port);
    thePrefs.setProxyEnablePassword(!t.user.isEmpty());
    thePrefs.setProxyUser(t.user);
    thePrefs.setProxyPassword(t.password);
}

/// GET through GuardedNetworkAccessManager; body, or empty + error.
QByteArray fetch(const QUrl& url, QString* error = nullptr)
{
    GuardedNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setTransferTimeout(30000);
    QNetworkReply* reply = nam.get(req);
    QSignalSpy done(reply, &QNetworkReply::finished);
    if (!done.wait(40000)) {
        if (error) *error = QStringLiteral("timeout");
        reply->deleteLater();
        return {};
    }
    const QByteArray body = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
    if (error) *error = reply->errorString();
    reply->deleteLater();
    return body.trimmed();
}

/// One HTTP tracker announce through BtSettingsMap's pack. True on a tracker reply.
bool announceThrough(const bt::BtSettings& s, QString* why, QString* externalIp)
{
    namespace lt = libtorrent;
    QTemporaryDir dir;
    lt::settings_pack pack = bt::BtSettingsMap::toPack(s);
    pack.set_int(lt::settings_pack::alert_mask,
                 lt::alert_category::tracker | lt::alert_category::error | lt::alert_category::status);
    lt::session ses(pack);

    lt::add_torrent_params atp;
    std::array<char, 20> raw{};
    for (auto& c : raw)
        c = char(QRandomGenerator::global()->bounded(256));
    atp.info_hashes.v1 = lt::sha1_hash(raw.data());
    atp.trackers.push_back(kTracker);
    atp.save_path = dir.path().toStdString();
    ses.add_torrent(atp);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 45000) {
        ses.wait_for_alert(lt::milliseconds(500));
        std::vector<lt::alert*> alerts;
        ses.pop_alerts(&alerts);
        for (lt::alert* a : alerts) {
            if (auto* ip = lt::alert_cast<lt::external_ip_alert>(a))
                *externalIp = QString::fromStdString(ip->external_address.to_string());
            if (lt::alert_cast<lt::tracker_reply_alert>(a)) {
                ses.pop_alerts(&alerts);   // external_ip often follows the reply
                for (lt::alert* b : alerts)
                    if (auto* ip = lt::alert_cast<lt::external_ip_alert>(b))
                        *externalIp = QString::fromStdString(ip->external_address.to_string());
                return true;
            }
            if (auto* err = lt::alert_cast<lt::tracker_error_alert>(a)) {
                if (err->error == lt::errors::announce_skipped)   // one per unreachable interface
                    continue;
                *why = QString::fromStdString(err->message());
                return false;
            }
        }
    }
    *why = QStringLiteral("no tracker answer in 45 s");
    return false;
}

bt::BtSettings btSettings(const ProxyTarget& t)
{
    bt::BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.proxyKind = t.type == PROXYTYPE_SOCKS5 ? bt::BtSettings::ProxyKind::Socks5
                : t.type == PROXYTYPE_SOCKS4 ? bt::BtSettings::ProxyKind::Socks4
                                             : bt::BtSettings::ProxyKind::Http;
    s.proxyHost = t.host;
    s.proxyPort = t.port;
    s.proxyUser = t.user;
    s.proxyPassword = t.password;
    return s;
}

/// IRC login result: 1 logged in, 0 refused/failed, -1 server gave no verdict.
int ircLogin(QString* why)
{
    IrcClient irc;
    QSignalSpy loggedIn(&irc, &IrcClient::loggedIn);
    QSignalSpy error(&irc, &IrcClient::socketError);
    QSignalSpy disconnected(&irc, &IrcClient::disconnected);
    const auto nick = QStringLiteral("eMuleQtPx_%1").arg(QRandomGenerator::global()->bounded(10000u, 99999u));
    irc.connectToServer(QString::fromLatin1(kIrcServer), nick);
    (void)QTest::qWaitFor([&] { return !loggedIn.isEmpty() || !error.isEmpty() || !disconnected.isEmpty(); }, 30000);
    if (!loggedIn.isEmpty()) {
        irc.disconnect();
        return 1;
    }
    if (!error.isEmpty())
        *why = error.first().first().toString();
    return 0;
}

} // namespace

class tst_ProxyLive : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void httpLeavesThroughProxy();
    void httpDeadProxyFails();
    void httpLoopbackBypassesProxy();
    void btTrackerThroughProxy();
    void btDeadProxyFails();
    void ircThroughProxy();
    void ircDeadProxyFails();

private:
    ProxyTarget m_proxy;
    ProxyTarget m_dead;
    QString m_directIp;
};

void tst_ProxyLive::initTestCase()
{
    loadProjectEnv();
    const QString text = qEnvironmentVariable("EMULE_TEST_PROXY").trimmed();
    if (text.isEmpty())
        QSKIP("EMULE_TEST_PROXY is not set in .env");
    m_proxy = parseProxy(text);
    QVERIFY2(m_proxy.type != PROXYTYPE_NOPROXY && !m_proxy.host.isEmpty() && m_proxy.port,
             "EMULE_TEST_PROXY: want socks5|socks4|http://[user:pass@]host:port");
    m_dead = m_proxy;
    m_dead.host = QStringLiteral("127.0.0.1");
    m_dead.port = deadPort();

    setPrefsProxy({});
    QString error;
    m_directIp = QString::fromLatin1(fetch(QUrl(QString::fromLatin1(kIpEcho)), &error));
    if (m_directIp.isEmpty())
        QSKIP(qPrintable(QStringLiteral("no internet: %1").arg(error)));
}

void tst_ProxyLive::cleanup()
{
    setPrefsProxy({});
}

void tst_ProxyLive::httpLeavesThroughProxy()
{
    setPrefsProxy(m_proxy);
    QString error;
    const QString ip = QString::fromLatin1(fetch(QUrl(QString::fromLatin1(kIpEcho)), &error));
    QVERIFY2(!ip.isEmpty(), qPrintable(error));
    QVERIFY2(!QHostAddress(ip).isNull(), qPrintable(ip));
    QVERIFY2(ip != m_directIp, "same exit IP as without the proxy");
}

void tst_ProxyLive::httpDeadProxyFails()
{
    setPrefsProxy(m_dead);
    QString error;
    const QByteArray body = fetch(QUrl(QString::fromLatin1(kIpEcho)), &error);
    QVERIFY2(body.isEmpty(), "request went direct past a dead proxy");
}

void tst_ProxyLive::httpLoopbackBypassesProxy()
{
    // the real proxy cannot reach our loopback: an answer means it went direct
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    connect(&server, &QTcpServer::newConnection, &server, [&server] {
        QTcpSocket* s = server.nextPendingConnection();
        connect(s, &QTcpSocket::readyRead, s, [s] {
            if (!s->readAll().contains("\r\n\r\n"))
                return;
            s->write("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nlocal");
            s->disconnectFromHost();
        });
    });
    setPrefsProxy(m_proxy);
    QString error;
    const QByteArray body = fetch(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort())), &error);
    QCOMPARE(body, QByteArray("local"));
}

void tst_ProxyLive::btTrackerThroughProxy()
{
    QString why, ip;
    if (!announceThrough(btSettings({}), &why, &ip))
        QSKIP(qPrintable(QStringLiteral("tracker unreachable even direct: %1").arg(why)));
    const QString directIp = ip;

    ip.clear();
    QVERIFY2(announceThrough(btSettings(m_proxy), &why, &ip), qPrintable(why));
    // opentrackr sends no "external ip"; compared only when a tracker does
    if (!ip.isEmpty() && !directIp.isEmpty())
        QVERIFY2(ip != directIp, "tracker saw the direct IP through the proxy");
}

void tst_ProxyLive::btDeadProxyFails()
{
    QString why, ip;
    QVERIFY2(!announceThrough(btSettings(m_dead), &why, &ip), "announce went direct past a dead proxy");
    qInfo() << "dead proxy:" << why;
}

void tst_ProxyLive::ircThroughProxy()
{
    setPrefsProxy({});
    QString why;
    if (ircLogin(&why) != 1)
        QSKIP(qPrintable(QStringLiteral("IRC unreachable even direct: %1").arg(why)));
    setPrefsProxy(m_proxy);
    if (ircLogin(&why) != 1)
        QSKIP(qPrintable(QStringLiteral("IRC server refused the proxy exit (VPN ban?): %1").arg(why)));
}

void tst_ProxyLive::ircDeadProxyFails()
{
    setPrefsProxy(m_dead);
    QString why;
    QCOMPARE(ircLogin(&why), 0);
    qInfo() << "dead proxy:" << why;
}

QTEST_MAIN(tst_ProxyLive)
#include "tst_ProxyLive.moc"
