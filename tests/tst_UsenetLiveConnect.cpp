/// @file tst_UsenetLiveConnect.cpp
/// @brief Live TLS handshake and authentication against a real provider.
///
/// The one thing the fake server cannot prove. tst_NntpSocket drives the whole
/// state machine over cleartext loopback, but implicit TLS and STARTTLS are
/// negotiated by Qt against a real certificate chain, and a certificate policy
/// that is wrong only shows up against a real endpoint.
///
/// Credentials come from the environment, never from the repository:
///
///   EMULE_NNTP_HOST   news.example.com
///   EMULE_NNTP_PORT   563            (optional, default 563)
///   EMULE_NNTP_TLS    implicit|starttls|none   (optional, default implicit)
///   EMULE_NNTP_USER   (optional)
///   EMULE_NNTP_PASS   (optional)
///
/// Without EMULE_NNTP_HOST the cases skip rather than fail: an unconfigured
/// checkout must not look broken. Labelled "live" and built only under
/// EMULE_LIVE_TESTS, so `ctest -LE live` never reaches it.

#include "nntp/NntpCommand.h"
#include "nntp/NntpSocket.h"

#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTest>

using namespace eMule::usenet;

namespace {

QString env(const char* name)
{
    return QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
}

bool haveProvider()
{
    return !env("EMULE_NNTP_HOST").isEmpty();
}

NewsServer providerFromEnv()
{
    NewsServer s;
    s.name = QStringLiteral("live");
    s.host = env("EMULE_NNTP_HOST");
    s.user = env("EMULE_NNTP_USER");
    s.pass = env("EMULE_NNTP_PASS");

    const QString mode = env("EMULE_NNTP_TLS").toLower();
    if (mode == QLatin1String("none"))
        s.tlsMode = TlsMode::None;
    else if (mode == QLatin1String("starttls"))
        s.tlsMode = TlsMode::StartTls;
    else
        s.tlsMode = TlsMode::Implicit;

    const int port = env("EMULE_NNTP_PORT").toInt();
    s.port = port > 0 ? static_cast<quint16>(port)
                      : (s.tlsMode == TlsMode::Implicit ? kDefaultNntpTlsPort
                                                        : kDefaultNntpPort);
    return s;
}

} // namespace

class tst_UsenetLiveConnect : public QObject {
    Q_OBJECT

private slots:
    void connectsAndAuthenticates();
    void capabilitiesAreReadable();
    void unknownArticleIsReported();
    void wrongPasswordIsRejected();
};

void tst_UsenetLiveConnect::connectsAndAuthenticates()
{
    if (!haveProvider())
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
    if (!haveProvider())
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
    if (!haveProvider())
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
    if (!haveProvider())
        QSKIP("Set EMULE_NNTP_HOST to run the live tests");
    if (env("EMULE_NNTP_USER").isEmpty())
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
