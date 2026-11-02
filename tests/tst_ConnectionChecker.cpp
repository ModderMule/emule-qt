/// @file tst_ConnectionChecker.cpp
/// @brief Connection checker (#115): Kad mode, URL confirm, URL-only mode, real probe.

#include "net/ConnectionChecker.h"

#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <vector>

using namespace eMule;

namespace {

/// Probe the test answers by hand.
struct FakeProbe {
    std::vector<ConnectionChecker::ProbeDone> pending;
    int asked = 0;

    void install(ConnectionChecker& c)
    {
        c.setProbe([this](const QUrl&, ConnectionChecker::ProbeDone done) {
            ++asked;
            pending.push_back(std::move(done));
        });
    }
    /// Answer the oldest open probe.
    bool answer(bool reached)
    {
        if (pending.empty())
            return false;
        auto done = std::move(pending.front());
        pending.erase(pending.begin());
        done(reached);
        return true;
    }
};

const QString kUrl = QStringLiteral("check.example");

} // namespace

class tst_ConnectionChecker : public QObject {
    Q_OBJECT

private slots:
    void probeUrlNormalised();
    void disabledNeverOffline();
    void kadAloneDecidesWithoutUrl();
    void kadWithoutEvidenceIsNoVerdict();
    void kadHappyMeansNoProbes();
    void urlConfirmsKadOffline();
    void urlReachedOverridesKad();
    void urlBringsBackWhileKadSilent();
    void urlOnlyNeedsTwoFailures();
    void urlOnlyWithoutUrlIsIdle();
    void kadStartEndsUrlOnlyOffline();
    void kadStopFallsBackToUrl();
    void disableGoesOnline();
    void realProbeReachesLoopback();
    void realProbeFailsOnClosedPort();
};

void tst_ConnectionChecker::probeUrlNormalised()
{
    QCOMPARE(ConnectionChecker::probeUrl(QStringLiteral(" example.com ")),
             QUrl(QStringLiteral("http://example.com")));
    QCOMPARE(ConnectionChecker::probeUrl(QStringLiteral("https://a.b/x")),
             QUrl(QStringLiteral("https://a.b/x")));
    QVERIFY(ConnectionChecker::probeUrl(QString()).isEmpty());
    QVERIFY(ConnectionChecker::probeUrl(QStringLiteral("ftp://a.b")).isEmpty());
}

void tst_ConnectionChecker::disabledNeverOffline()
{
    ConnectionChecker c;
    FakeProbe p;
    p.install(c);
    c.configure(false, kUrl, 30);
    c.setKadState(true, true, true);
    QVERIFY(!c.isOffline());
    QCOMPARE(p.asked, 0);
}

void tst_ConnectionChecker::kadAloneDecidesWithoutUrl()
{
    ConnectionChecker c;
    QSignalSpy spy(&c, &ConnectionChecker::offlineChanged);
    c.configure(true, {}, 30);
    c.setKadState(true, false, true);
    QVERIFY(!c.isOffline());
    c.setKadState(true, true, true);
    QVERIFY(c.isOffline());
    QVERIFY(c.offlineSince() > 0);
    QVERIFY(!c.reason().isEmpty());
    c.setKadState(true, false, true);
    QVERIFY(!c.isOffline());
    QCOMPARE(spy.count(), 2);
}

void tst_ConnectionChecker::kadWithoutEvidenceIsNoVerdict()
{
    // UDP blocked from the start: Kad never heard anybody, its silence means nothing
    ConnectionChecker c;
    FakeProbe p;
    p.install(c);
    c.configure(true, {}, 30);
    c.setKadState(true, true, false);
    QVERIFY(!c.isOffline());
    QVERIFY(!c.isKadMode());
    QCOMPARE(p.asked, 0);
}

void tst_ConnectionChecker::kadHappyMeansNoProbes()
{
    ConnectionChecker c;
    FakeProbe p;
    p.install(c);
    c.setKadState(true, false, true);
    c.configure(true, kUrl, 30);
    QVERIFY(!c.isProbing());
    QCOMPARE(p.asked, 0);
}

void tst_ConnectionChecker::urlConfirmsKadOffline()
{
    ConnectionChecker c;
    FakeProbe p;
    p.install(c);
    c.setKadState(true, false, true);
    c.configure(true, kUrl, 30);
    c.setKadState(true, true, true);
    QVERIFY(!c.isOffline());   // waits for the URL
    QCOMPARE(p.asked, 1);
    QVERIFY(p.answer(false));
    QVERIFY(c.isOffline());
}

void tst_ConnectionChecker::urlReachedOverridesKad()
{
    ConnectionChecker c;
    FakeProbe p;
    p.install(c);
    c.setKadState(true, true, true);
    c.configure(true, kUrl, 30);
    QVERIFY(p.answer(true));
    QVERIFY(!c.isOffline());
}

void tst_ConnectionChecker::urlBringsBackWhileKadSilent()
{
    ConnectionChecker c;
    FakeProbe p;
    p.install(c);
    c.setKadState(true, true, true);
    c.configure(true, kUrl, 1);
    QVERIFY(p.answer(false));
    QVERIFY(c.isOffline());
    QVERIFY(c.isProbing());   // keeps asking for a quick way back
    QTRY_COMPARE_WITH_TIMEOUT(p.asked, 2, 3000);
    QVERIFY(p.answer(true));
    QVERIFY(!c.isOffline());
}

void tst_ConnectionChecker::urlOnlyNeedsTwoFailures()
{
    ConnectionChecker c;
    FakeProbe p;
    p.install(c);
    c.configure(true, kUrl, 1);   // Kad off
    QCOMPARE(p.asked, 1);         // first probe at once
    QVERIFY(p.answer(false));
    QVERIFY(!c.isOffline());      // one lost request is no outage
    QTRY_COMPARE_WITH_TIMEOUT(p.asked, 2, 3000);
    QVERIFY(p.answer(false));
    QVERIFY(c.isOffline());
    QTRY_COMPARE_WITH_TIMEOUT(p.asked, 3, 3000);
    QVERIFY(p.answer(true));
    QVERIFY(!c.isOffline());
}

void tst_ConnectionChecker::urlOnlyWithoutUrlIsIdle()
{
    ConnectionChecker c;
    FakeProbe p;
    p.install(c);
    c.configure(true, {}, 30);
    QVERIFY(!c.isOffline());
    QVERIFY(!c.isProbing());
    QCOMPARE(p.asked, 0);
}

void tst_ConnectionChecker::kadStartEndsUrlOnlyOffline()
{
    ConnectionChecker c;
    FakeProbe p;
    p.install(c);
    c.configure(true, kUrl, 1);
    QVERIFY(p.answer(false));
    QTRY_COMPARE_WITH_TIMEOUT(p.asked, 2, 3000);
    QVERIFY(p.answer(false));
    QVERIFY(c.isOffline());
    // Kad heard a peer: the line works
    c.setKadState(true, false, true);
    QVERIFY(!c.isOffline());
    QVERIFY(!c.isProbing());
}

void tst_ConnectionChecker::kadStopFallsBackToUrl()
{
    ConnectionChecker c;
    FakeProbe p;
    p.install(c);
    c.setKadState(true, false, true);
    c.configure(true, kUrl, 30);
    QCOMPARE(p.asked, 0);
    c.setKadState(false, false, false);
    QVERIFY(!c.isKadMode());
    QVERIFY(c.isProbing());
    QCOMPARE(p.asked, 1);
    // a late answer from Kad mode must not count after the switch
    c.setKadState(true, false, true);
    QVERIFY(p.answer(false));
    QVERIFY(!c.isOffline());
}

void tst_ConnectionChecker::disableGoesOnline()
{
    ConnectionChecker c;
    c.configure(true, {}, 30);
    c.setKadState(true, true, true);
    QVERIFY(c.isOffline());
    c.configure(false, {}, 30);
    QVERIFY(!c.isOffline());
}

void tst_ConnectionChecker::realProbeReachesLoopback()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int served = 0;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (QTcpSocket* s = server.nextPendingConnection()) {
            connect(s, &QTcpSocket::readyRead, s, [s, &served] {
                s->readAll();
                // an error status still proves the line
                s->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                s->disconnectFromHost();
                ++served;
            });
        }
    });

    ConnectionChecker c;
    QSignalSpy spy(&c, &ConnectionChecker::offlineChanged);
    c.configure(true, QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort()), 1);
    QTRY_VERIFY_WITH_TIMEOUT(served >= 2, 5000);
    QVERIFY(!c.isOffline());
    QCOMPARE(spy.count(), 0);
}

void tst_ConnectionChecker::realProbeFailsOnClosedPort()
{
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        port = probe.serverPort();
    }   // closed again: nothing listens there now

    ConnectionChecker c;
    c.configure(true, QStringLiteral("http://127.0.0.1:%1/").arg(port), 1);
    // macOS: a refused connect reports connected() and then times out (5 s a probe)
    QTRY_VERIFY_WITH_TIMEOUT(c.isOffline(), 20000);
}

QTEST_GUILESS_MAIN(tst_ConnectionChecker)
#include "tst_ConnectionChecker.moc"
