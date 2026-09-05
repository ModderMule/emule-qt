/// @file tst_UsenetLiveConnect.cpp
/// @brief Live TLS handshake and authentication against a real provider.
///
/// The one thing the fake server cannot prove. tst_NntpSocket drives the whole
/// state machine over cleartext loopback, but implicit TLS and STARTTLS are
/// negotiated by Qt against a real certificate chain, and a certificate policy
/// that is wrong only shows up against a real endpoint.
///
/// Credentials come from the environment, never from the repository — see
/// UsenetLiveEnv.h for the variables and for why an unset one skips rather than
/// fails. Labelled "live" and built only under EMULE_LIVE_TESTS, so
/// `ctest -LE live` never reaches it.

#include "nntp/NntpCommand.h"
#include "nntp/NntpSocket.h"

#include "UsenetLiveEnv.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule::usenet;
using eMule::testing::loadProjectEnv;
using namespace eMule::testing::usenet;

class tst_UsenetLiveConnect : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void connectsAndAuthenticates();
    void capabilitiesAreReadable();
    void unknownArticleIsReported();
    void wrongPasswordIsRejected();
};

void tst_UsenetLiveConnect::initTestCase()
{
    // .env fills in whatever the process environment did not already set.
    loadProjectEnv();
}

void tst_UsenetLiveConnect::connectsAndAuthenticates()
{
    if (!haveLiveProvider())
        QSKIP("Set EMULE_NNTP_HOST (and optionally USER/PASS) to run the live tests");

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    QSignalSpy failed(&socket, &NntpSocket::failed);

    socket.connectToServer(providerFromEnv());
    QVERIFY2(ready.wait(30000),
             failed.isEmpty() ? "timed out"
                              : qPrintable(failed.first().at(1).toString()));
    QCOMPARE(failed.count(), 0);
    QVERIFY(socket.isReady());
}

void tst_UsenetLiveConnect::capabilitiesAreReadable()
{
    if (!haveLiveProvider())
        QSKIP("Set EMULE_NNTP_HOST to run the live tests");

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(providerFromEnv());
    QVERIFY(ready.wait(30000));

    CapabilitiesCommand caps;
    QSignalSpy done(&socket, &NntpSocket::commandFinished);
    socket.sendCommand(&caps);
    QVERIFY(done.wait(30000));

    // A provider that predates RFC 3977 answers 500, which is not an error and
    // must leave the connection usable — losing an account to a capability
    // probe would be a poor trade.
    if (caps.failed()) {
        QVERIFY(socket.isReady());
        QSKIP("Provider does not implement CAPABILITIES");
    }
    QVERIFY(!caps.capabilities().isEmpty());
    qInfo() << "capabilities:" << caps.capabilities();
}

void tst_UsenetLiveConnect::unknownArticleIsReported()
{
    if (!haveLiveProvider())
        QSKIP("Set EMULE_NNTP_HOST to run the live tests");

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(providerFromEnv());
    QVERIFY(ready.wait(30000));

    // A message-id no server can hold. 430 is the routing signal that sends an
    // article up to the next priority level, so it has to arrive as
    // ArticleNotFound and leave the connection alive for the next request.
    StatCommand stat(QStringLiteral("emuleqt-does-not-exist@invalid"));
    QSignalSpy done(&socket, &NntpSocket::commandFinished);
    socket.sendCommand(&stat);
    QVERIFY(done.wait(30000));

    QVERIFY(stat.failed());
    QCOMPARE(stat.error(), NntpError::ArticleNotFound);
    QVERIFY(escalatesToNextLevel(stat.error()));
    QVERIFY(!isFatalToConnection(stat.error()));
    QVERIFY(socket.isReady());
}

void tst_UsenetLiveConnect::wrongPasswordIsRejected()
{
    if (!haveLiveProvider())
        QSKIP("Set EMULE_NNTP_HOST to run the live tests");
    if (liveEnv("EMULE_NNTP_USER").isEmpty())
        QSKIP("Provider needs no authentication; nothing to reject");

    NewsServer bad = providerFromEnv();
    bad.pass = QStringLiteral("definitely-not-the-password");

    NntpSocket socket;
    QSignalSpy failed(&socket, &NntpSocket::failed);
    socket.connectToServer(bad);
    QVERIFY(failed.wait(30000));

    // Credentials are a server-level fact: this must not send the article to a
    // fill server, and it must not be retried here either.
    const auto error = failed.first().at(0).value<NntpError>();
    QCOMPARE(error, NntpError::AuthFailed);
    QVERIFY(!escalatesToNextLevel(error));
}

QTEST_MAIN(tst_UsenetLiveConnect)
#include "tst_UsenetLiveConnect.moc"
