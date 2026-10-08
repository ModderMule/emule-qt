/// @file tst_NntpSocket.cpp
/// @brief NNTP transport, authentication and command dispatch.
///
/// Everything here runs against tests/FakeNntpServer.h on loopback, because the
/// cases worth testing are the refusals and none of them can be provoked against
/// a real provider on demand. The live counterpart (tst_UsenetLiveConnect) only
/// proves the TLS branch works against a real endpoint.
///
/// Two of these exist because of specific traps rather than for coverage:
///
///   - respondsToNothing_timesOut. SmtpClient, the class this transport is
///     modelled on, has no timeouts at all: a server that accepts the connection
///     and then goes quiet leaves it stuck in a non-Disconnected state forever,
///     and every later send is refused. A pooled NNTP connection must not be
///     able to do that.
///
///   - readRateLimit_stillDeliversEverything. A readyRead slot that consumes a
///     bounded amount and does not re-arm strands whatever is already buffered:
///     readyRead does not fire again for bytes the socket has already received.
///     That is a stall of exactly one keep-alive interval, and it is a bug this
///     codebase has shipped before.

#include "FakeNntpServer.h"
#include "FakeProxyServer.h"

#include "net/BindAddress.h"
#include "nntp/NntpCommand.h"
#include "prefs/Preferences.h"
#include "nntp/NntpSocket.h"

#include <QElapsedTimer>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>
#include <QTimer>

using namespace eMule::usenet;
using eMule::testing::FakeNntpServer;
using eMule::testing::FakeProxyServer;

namespace {

/// A server record pointed at the fake, cleartext, with working credentials.
NewsServer localServer(quint16 port)
{
    NewsServer s;
    s.name = QStringLiteral("fake");
    s.host = QStringLiteral("127.0.0.1");
    s.port = port;
    s.tlsMode = TlsMode::None;
    s.user = QStringLiteral("testuser");
    s.pass = QStringLiteral("testpass");
    return s;
}

/// A loopback port nothing listens on: taken, then released.
quint16 deadPort()
{
    QTcpServer probe;
    return probe.listen(QHostAddress::LocalHost, 0) ? probe.serverPort() : 0;
}

} // namespace

class tst_NntpSocket : public QObject {
    Q_OBJECT

private slots:
    void connectsAuthenticatesAndBecomesReady();
    void noCredentials_skipsAuthinfo();
    void wrongPassword_failsWithAuthFailed();
    void greeting400_backsOffTheServer();
    void greetingTooManyConnections_isItsOwnError();
    void authTooManyConnections_isNotAnAuthFailure();
    void respondsToNothing_timesOut();
    void capabilities_readsMultilineBlock();
    void capabilities_unsupported_isNotFatalToTheServer();
    void group_missing_reportsGroupNotFound();
    void stat_missingArticle_escalates();
    void aStatRefusalDoesNotBackOffTheAccount();
    void dotStuffedBodyLine_isUnstuffed();
    void dropMidCommand_failsTheCommand();
    void readRateLimit_stillDeliversEverything();
    void readRateLimit_repaysTimeSpentWaiting();
    void readRateLimit_reapplyingKeepsBankedCredit();
    void readRateLimit_reapplyingGrantsNoExtraBudget();
    void readRateLimit_boundsTheSocketBuffer_data();
    void readRateLimit_boundsTheSocketBuffer();
    void invalidServer_failsWithoutConnecting();
    void openConnections_countsAuthenticatedSocketsPerAccount();
    void connectsThroughASocks5ProxyByHostName();
    void connectsThroughAnHttpConnectProxyWithAuthentication();
    void withNoProxyOfItsOwnAnApplicationProxyIsIgnored();
    void aProxyFailureNamesTheProxyNotTheServer();
    void aMissingBoundInterfaceIsNotTheServersFault();
    void pipelinedCommandsFinishInOrder_data();
    void pipelinedCommandsFinishInOrder();
    void aDropFailsEveryPipelinedCommand();
};

void tst_NntpSocket::connectsAuthenticatesAndBecomesReady()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    QSignalSpy failed(&socket, &NntpSocket::failed);

    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));
    QCOMPARE(failed.count(), 0);
    QVERIFY(socket.isReady());

    // MODE READER before AUTHINFO: a server that distinguishes reader from
    // transit rejects everything else until it has been told which we are.
    const QStringList cmds = server.receivedCommands();
    QCOMPARE(cmds.value(0), QStringLiteral("MODE READER"));
    QCOMPARE(cmds.value(1), QStringLiteral("AUTHINFO USER testuser"));
    QCOMPARE(cmds.value(2), QStringLiteral("AUTHINFO PASS testpass"));
}

void tst_NntpSocket::noCredentials_skipsAuthinfo()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);

    NewsServer s = localServer(port);
    s.user.clear();
    s.pass.clear();
    socket.connectToServer(s);

    QVERIFY(ready.wait(5000));
    // Not merely "did not fail": an AUTHINFO with an empty user is a 481 on
    // some providers and a silent lockout on others.
    QVERIFY(!server.receivedCommands().join(u' ').contains(QLatin1String("AUTHINFO")));
}

void tst_NntpSocket::wrongPassword_failsWithAuthFailed()
{
    FakeNntpServer server;
    server.setRejectAuth(true);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy failed(&socket, &NntpSocket::failed);

    socket.connectToServer(localServer(port));
    QVERIFY(failed.wait(5000));

    const auto error = failed.first().at(0).value<NntpError>();
    QCOMPARE(error, NntpError::AuthFailed);
    // Credentials are a server-level fact: retrying this article here is
    // pointless, but so is escalating it to a fill server.
    QVERIFY(!escalatesToNextLevel(error));
    QVERIFY(isFatalToConnection(error));
}

void tst_NntpSocket::greeting400_backsOffTheServer()
{
    FakeNntpServer server;
    // A refusal that does not name the connection limit: the server backs off.
    server.setGreeting(QByteArrayLiteral("400 Service temporarily unavailable"));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy failed(&socket, &NntpSocket::failed);

    socket.connectToServer(localServer(port));
    QVERIFY(failed.wait(5000));

    const auto error = failed.first().at(0).value<NntpError>();
    QCOMPARE(error, NntpError::ServerUnavailable);
    // Must not escalate: the article is very probably here, we just could not
    // get a connection to ask.
    QVERIFY(!escalatesToNextLevel(error));
}

// The connection limit at the greeting. Not ServerUnavailable: that backs the
// account off, and the account's other connections are fine.
void tst_NntpSocket::greetingTooManyConnections_isItsOwnError()
{
    FakeNntpServer server;
    server.setGreeting(QByteArrayLiteral("502 Too many connections"));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy failed(&socket, &NntpSocket::failed);

    socket.connectToServer(localServer(port));
    QVERIFY(failed.wait(5000));

    const auto error = failed.first().at(0).value<NntpError>();
    QCOMPARE(error, NntpError::TooManyConnections);
    QVERIFY(!escalatesToNextLevel(error));
    QVERIFY(isFatalToConnection(error));
}

// Newshosting's shape (log.log, 2026-09-28): the limit is enforced at AUTHINFO
// PASS with a 502. It used to read as a credential failure.
void tst_NntpSocket::authTooManyConnections_isNotAnAuthFailure()
{
    FakeNntpServer server;
    server.setConnectionLimit(1);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket first;
    QSignalSpy firstReady(&first, &NntpSocket::ready);
    first.connectToServer(localServer(port));
    QVERIFY(firstReady.wait(5000));

    NntpSocket second;
    QSignalSpy failed(&second, &NntpSocket::failed);
    second.connectToServer(localServer(port));
    QVERIFY(failed.wait(5000));

    QCOMPARE(failed.first().at(0).value<NntpError>(), NntpError::TooManyConnections);
    QCOMPARE(server.refusedLogins(), 1);
    QVERIFY(first.isReady());
}

void tst_NntpSocket::respondsToNothing_timesOut()
{
    FakeNntpServer server;
    server.setMute(true);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    socket.setResponseTimeout(300);
    QSignalSpy failed(&socket, &NntpSocket::failed);

    socket.connectToServer(localServer(port));
    QVERIFY(failed.wait(5000));

    QCOMPARE(failed.first().at(0).value<NntpError>(), NntpError::Timeout);
    QVERIFY(!socket.isReady());
}

void tst_NntpSocket::capabilities_readsMultilineBlock()
{
    FakeNntpServer server;
    server.setCapabilities({QStringLiteral("VERSION 2"), QStringLiteral("READER"),
                            QStringLiteral("STARTTLS"), QStringLiteral("OVER MSGID")});
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));

    CapabilitiesCommand caps;
    QSignalSpy done(&socket, &NntpSocket::commandFinished);
    socket.sendCommand(&caps);
    QVERIFY(done.wait(5000));

    QVERIFY(!caps.failed());
    QCOMPARE(caps.capabilities().size(), 4);
    QVERIFY(caps.has(QLatin1String("STARTTLS")));
    QVERIFY(caps.has(QLatin1String("READER")));
    // Prefix matching must not make OVER a match for OVERVIEW or vice versa.
    QVERIFY(caps.has(QLatin1String("OVER")));
    QVERIFY(!caps.has(QLatin1String("POST")));
}

void tst_NntpSocket::capabilities_unsupported_isNotFatalToTheServer()
{
    FakeNntpServer server;
    server.setSupportsCapabilities(false);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));

    CapabilitiesCommand caps;
    QSignalSpy done(&socket, &NntpSocket::commandFinished);
    socket.sendCommand(&caps);
    QVERIFY(done.wait(5000));

    QVERIFY(caps.failed());
    // Plenty of providers predate RFC 3977. The command failing must leave the
    // connection usable, or one probe would cost us the whole account.
    QVERIFY(socket.isReady());
    QVERIFY(!isFatalToConnection(NntpError::ArticleNotFound));
}

void tst_NntpSocket::group_missing_reportsGroupNotFound()
{
    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 42, 100, 141);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));

    {
        GroupCommand ok(QStringLiteral("alt.binaries.test"));
        QSignalSpy done(&socket, &NntpSocket::commandFinished);
        socket.sendCommand(&ok);
        QVERIFY(done.wait(5000));
        QVERIFY(!ok.failed());
        QCOMPARE(ok.articleCount(), 42);
        QCOMPARE(ok.lowWaterMark(), 100);
        QCOMPARE(ok.highWaterMark(), 141);
    }
    {
        GroupCommand missing(QStringLiteral("alt.binaries.nope"));
        QSignalSpy done(&socket, &NntpSocket::commandFinished);
        socket.sendCommand(&missing);
        QVERIFY(done.wait(5000));
        QCOMPARE(missing.error(), NntpError::GroupNotFound);
        QVERIFY(escalatesToNextLevel(missing.error()));
        QVERIFY(socket.isReady());
    }
}

void tst_NntpSocket::stat_missingArticle_escalates()
{
    FakeNntpServer server;
    server.addArticle(QStringLiteral("part1@example"), QByteArrayLiteral("data"));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));

    {
        // Bare id: NZBs store message-ids both with and without brackets, and a
        // doubled bracket is a silent 430 rather than an error.
        StatCommand present(QStringLiteral("part1@example"));
        QSignalSpy done(&socket, &NntpSocket::commandFinished);
        socket.sendCommand(&present);
        QVERIFY(done.wait(5000));
        QVERIFY(!present.failed());
        QVERIFY(present.exists());
    }
    {
        StatCommand bracketed(QStringLiteral("<part1@example>"));
        QSignalSpy done(&socket, &NntpSocket::commandFinished);
        socket.sendCommand(&bracketed);
        QVERIFY(done.wait(5000));
        QVERIFY(bracketed.exists());
    }
    {
        StatCommand absent(QStringLiteral("gone@example"));
        QSignalSpy done(&socket, &NntpSocket::commandFinished);
        socket.sendCommand(&absent);
        QVERIFY(done.wait(5000));
        QCOMPARE(absent.error(), NntpError::ArticleNotFound);
        // The one failure that means "ask a different server".
        QVERIFY(escalatesToNextLevel(absent.error()));
        QVERIFY(!isFatalToConnection(absent.error()));
        QVERIFY(socket.isReady());
    }

    QCOMPARE(server.receivedCommands().count(QStringLiteral("STAT <part1@example>")), 2);
}

void tst_NntpSocket::aStatRefusalDoesNotBackOffTheAccount()
{
    // Every "I cannot answer for this article" code has to land on
    // ArticleNotFound. ProtocolError is fatal to the connection, and
    // UsenetWorker::finishJob() turns a fatal non-ArticleNotFound error into
    // NntpServerPool::blockServer() — so a server that answers 412 to a STAT
    // would have its whole account backed off on every probe. ArticleFetcher's
    // stat() path is what constructs one, so this is live.
    for (const int code : {430, 423, 420, 412}) {
        StatCommand stat(QStringLiteral("gone@example"));
        stat.onStatus(code, QStringLiteral("no such thing"));

        QVERIFY(stat.failed());
        QVERIFY(!stat.exists());
        QCOMPARE(stat.error(), NntpError::ArticleNotFound);
        QVERIFY2(!isFatalToConnection(stat.error()),
                 qPrintable(QStringLiteral("code %1 would kill the connection").arg(code)));
        QVERIFY2(escalatesToNextLevel(stat.error()),
                 qPrintable(QStringLiteral("code %1 would not escalate").arg(code)));
    }

    // A code that genuinely means something is wrong still says so, or the
    // mapping would swallow real faults as "article missing" and quietly
    // escalate past a broken account instead of reporting it.
    StatCommand broken(QStringLiteral("whatever@example"));
    broken.onStatus(500, QStringLiteral("Command not recognized"));
    QCOMPARE(broken.error(), NntpError::ProtocolError);
    QVERIFY(isFatalToConnection(broken.error()));
}

void tst_NntpSocket::dotStuffedBodyLine_isUnstuffed()
{
    // A capability whose text begins with a dot is contrived, but it is the only
    // multi-line command available before the article fetcher lands, and the
    // unstuffing it exercises is shared by both.
    FakeNntpServer server;
    server.setCapabilities({QStringLiteral("VERSION 2"), QStringLiteral(".leading-dot")});
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));

    CapabilitiesCommand caps;
    QSignalSpy done(&socket, &NntpSocket::commandFinished);
    socket.sendCommand(&caps);
    QVERIFY(done.wait(5000));

    QCOMPARE(caps.capabilities().size(), 2);
    QCOMPARE(caps.capabilities().at(1), QStringLiteral(".leading-dot"));
}

void tst_NntpSocket::dropMidCommand_failsTheCommand()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));

    server.setDropOnNextCommand(true);

    StatCommand stat(QStringLiteral("whatever@example"));
    QSignalSpy done(&socket, &NntpSocket::commandFinished);
    socket.sendCommand(&stat);
    QVERIFY(done.wait(5000));

    // The command must learn *why* it died. If the socket tore down first and
    // the command only saw a generic disconnect, a scheduler could not tell a
    // dead connection from a missing article.
    QVERIFY(stat.failed());
    QCOMPARE(stat.error(), NntpError::Disconnected);
    QVERIFY(!escalatesToNextLevel(stat.error()));
}

void tst_NntpSocket::readRateLimit_stillDeliversEverything()
{
    // Enough capability lines that a small budget cannot carry them in one
    // drain, so the refill path is the only way they all arrive.
    QStringList many;
    many.reserve(200);
    for (int i = 0; i < 200; ++i)
        many.append(QStringLiteral("CAP-%1 with some padding to make the line longer").arg(i));

    FakeNntpServer server;
    server.setCapabilities(many);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));

    socket.setReadRateLimit(4096);
    QCOMPARE(socket.readRateLimit(), 4096);

    CapabilitiesCommand caps;
    QSignalSpy done(&socket, &NntpSocket::commandFinished);
    socket.sendCommand(&caps);
    QVERIFY(done.wait(10000));

    QVERIFY(!caps.failed());
    QCOMPARE(caps.capabilities().size(), many.size());

    // 0 means unlimited here, as everywhere else in eMuleQt — not "stop".
    socket.setReadRateLimit(0);
    QCOMPARE(socket.readRateLimit(), 0);
    QVERIFY(socket.isReady());
}

void tst_NntpSocket::readRateLimit_repaysTimeSpentWaiting()
{
    // A limited socket spends most of a real link's time waiting for the
    // server's first byte — a command round trip to a provider across an ocean
    // is ~200 ms, two whole refill ticks with nothing to read. Credit that is
    // discarded each tick can never be earned back, so the limiter delivers a
    // fraction of the rate it was given. Here the wait is explicit and the
    // payload is sized so the difference is not a matter of milliseconds.
    QStringList many;
    many.reserve(100);
    for (int i = 0; i < 100; ++i)
        many.append(QStringLiteral("CAP-%1 with some padding to make the line longer").arg(i));

    FakeNntpServer server;
    server.setCapabilities(many);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));

    // ~5 KB of capabilities at 5 KB/s: ten refill ticks, i.e. about a second,
    // if every tick has to be earned as it is spent.
    socket.setReadRateLimit(5000);

    // Idle, with the refill timer running and nothing to read.
    QTest::qWait(600);

    CapabilitiesCommand caps;
    QSignalSpy done(&socket, &NntpSocket::commandFinished);
    QElapsedTimer elapsed;
    elapsed.start();
    socket.sendCommand(&caps);
    QVERIFY(done.wait(10000));
    const qint64 ms = elapsed.elapsed();

    QVERIFY(!caps.failed());
    QCOMPARE(caps.capabilities().size(), many.size());

    // Banked credit covers most of the body at once. Without accumulation this
    // cannot finish before ~1 s; the bound is loose enough that a slow machine
    // does not fail it, and tight enough that a reset budget cannot pass it.
    QVERIFY2(ms < 700, qPrintable(QStringLiteral("took %1 ms — the read budget is "
                                                 "not carrying credit across idle ticks").arg(ms)));
}

void tst_NntpSocket::readRateLimit_reapplyingKeepsBankedCredit()
{
    // UsenetWorker::applyRateLimits() re-applies the same figure on every lease,
    // release and bandwidth-split tick. Doing so must not wipe what the socket
    // banked while it waited — the same payload and bound as the test above,
    // with one re-apply between the wait and the command.
    QStringList many;
    many.reserve(100);
    for (int i = 0; i < 100; ++i)
        many.append(QStringLiteral("CAP-%1 with some padding to make the line longer").arg(i));

    FakeNntpServer server;
    server.setCapabilities(many);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));

    socket.setReadRateLimit(5000);
    QTest::qWait(600);
    socket.setReadRateLimit(5000);

    CapabilitiesCommand caps;
    QSignalSpy done(&socket, &NntpSocket::commandFinished);
    QElapsedTimer elapsed;
    elapsed.start();
    socket.sendCommand(&caps);
    QVERIFY(done.wait(10000));
    const qint64 ms = elapsed.elapsed();

    QVERIFY(!caps.failed());
    QCOMPARE(caps.capabilities().size(), many.size());
    QVERIFY2(ms < 700, qPrintable(QStringLiteral("took %1 ms — re-applying the limit "
                                                 "threw the banked credit away").arg(ms)));
}

void tst_NntpSocket::readRateLimit_reapplyingGrantsNoExtraBudget()
{
    // The other half of the same bug. A reset handed out a fresh tick per call,
    // and restarted the refill timer too: with data streaming in, each readyRead
    // spent the gift and the socket ran over its limit (live: ~12 % over);
    // re-applied faster than the 100 ms refill with the data already buffered,
    // as here, the timer never fired and the transfer stalled outright.
    QStringList many;
    many.reserve(200);
    for (int i = 0; i < 200; ++i)
        many.append(QStringLiteral("CAP-%1 with some padding to make the line longer").arg(i));

    FakeNntpServer server;
    server.setCapabilities(many);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));

    // ~12 KB at 8 KB/s: about 1.4 s when every refill is earned; a fraction of
    // that when a re-apply every 20 ms hands out a tick each time.
    constexpr qint64 kLimit = 8192;
    socket.setReadRateLimit(kLimit);

    QTimer reapply;
    reapply.setInterval(20);
    QObject::connect(&reapply, &QTimer::timeout, &socket,
                     [&socket] { socket.setReadRateLimit(kLimit); });
    reapply.start();

    CapabilitiesCommand caps;
    QSignalSpy done(&socket, &NntpSocket::commandFinished);
    QElapsedTimer elapsed;
    elapsed.start();
    socket.sendCommand(&caps);
    QVERIFY(done.wait(10000));
    const qint64 ms = elapsed.elapsed();
    reapply.stop();

    QVERIFY(!caps.failed());
    QCOMPARE(caps.capabilities().size(), many.size());
    QVERIFY2(ms > 900, qPrintable(QStringLiteral("took %1 ms — re-applying the limit "
                                                 "granted budget the refill never earned")
                                      .arg(ms)));
}

void tst_NntpSocket::invalidServer_failsWithoutConnecting()
{
    NntpSocket socket;
    QSignalSpy failed(&socket, &NntpSocket::failed);

    NewsServer s;
    s.port = 119; // no host
    socket.connectToServer(s);

    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().at(0).value<NntpError>(), NntpError::ConnectFailed);
}

// The Statistics window's "Open Connections": only a socket that got through
// the handshake counts, it counts under its account, and every way a connection
// ends — a drop, an abort, destruction — takes it back out.
void tst_NntpSocket::openConnections_countsAuthenticatedSocketsPerAccount()
{
    const QString account = QStringLiteral("acct-registry");
    const int before = NntpSocket::openConnectionCount();

    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);
    NewsServer config = localServer(port);
    config.accountId = account;

    {
        NntpSocket a;
        NntpSocket b;
        QSignalSpy readyA(&a, &NntpSocket::ready);
        QSignalSpy readyB(&b, &NntpSocket::ready);
        a.connectToServer(config);
        b.connectToServer(config);
        QVERIFY(readyA.wait(5000));
        QVERIFY(readyB.count() > 0 || readyB.wait(5000));
        QCOMPARE(NntpSocket::openConnectionsByAccount().value(account), 2);
        QCOMPARE(NntpSocket::openConnectionCount(), before + 2);

        a.abort();
        QCOMPARE(NntpSocket::openConnectionsByAccount().value(account), 1);
    }   // b destroyed while still open
    QVERIFY(!NntpSocket::openConnectionsByAccount().contains(account));
    QCOMPARE(NntpSocket::openConnectionCount(), before);

    // A server-side drop.
    {
        NntpSocket c;
        QSignalSpy ready(&c, &NntpSocket::ready);
        c.connectToServer(config);
        QVERIFY(ready.wait(5000));
        server.setDropOnNextCommand(true);
        StatCommand stat(QStringLiteral("whatever@example"));
        QSignalSpy done(&c, &NntpSocket::commandFinished);
        c.sendCommand(&stat);
        QVERIFY(done.wait(5000));
        QCOMPARE(NntpSocket::openConnectionsByAccount().value(account), 0);
    }

    // Refused credentials never count at all.
    FakeNntpServer rejecting;
    rejecting.setRejectAuth(true);
    const quint16 rejectPort = rejecting.start();
    QVERIFY(rejectPort != 0);
    NewsServer rejected = localServer(rejectPort);
    rejected.accountId = account;
    NntpSocket d;
    QSignalSpy failed(&d, &NntpSocket::failed);
    d.connectToServer(rejected);
    QVERIFY(failed.wait(5000));
    QCOMPARE(NntpSocket::openConnectionCount(), before);
}

// By *name*: a SOCKS5 request carrying an address would mean the provider's host
// was resolved locally, which is the leak a proxy is usually there to prevent.
void tst_NntpSocket::connectsThroughASocks5ProxyByHostName()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    FakeProxyServer proxy(FakeProxyServer::Kind::Socks5);
    proxy.setCredentials(QStringLiteral("puser"), QStringLiteral("ppass"));
    const quint16 proxyPort = proxy.start();
    QVERIFY(proxyPort != 0);

    NntpSocket socket;
    socket.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"),
                                  proxyPort, QStringLiteral("puser"), QStringLiteral("ppass")));
    QSignalSpy ready(&socket, &NntpSocket::ready);

    NewsServer s = localServer(port);
    s.host = QStringLiteral("localhost");
    socket.connectToServer(s);

    QVERIFY(ready.wait(5000));
    QCOMPARE(proxy.targets(), QStringList{QStringLiteral("localhost:%1").arg(port)});
    QVERIFY2(proxy.sawDomainName(), "the host name was resolved locally, not by the proxy");
    QCOMPARE(server.receivedCommands().value(0), QStringLiteral("MODE READER"));
}

void tst_NntpSocket::connectsThroughAnHttpConnectProxyWithAuthentication()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    FakeProxyServer proxy(FakeProxyServer::Kind::HttpConnect);
    proxy.setCredentials(QStringLiteral("puser"), QStringLiteral("ppass"));
    const quint16 proxyPort = proxy.start();
    QVERIFY(proxyPort != 0);

    NntpSocket socket;
    socket.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"),
                                  proxyPort, QStringLiteral("puser"), QStringLiteral("ppass")));
    QSignalSpy ready(&socket, &NntpSocket::ready);
    QSignalSpy failed(&socket, &NntpSocket::failed);

    socket.connectToServer(localServer(port));

    QVERIFY2(ready.wait(5000), failed.isEmpty() ? "no answer"
                                                : qPrintable(failed.first().at(1).toString()));
    QVERIFY(!proxy.targets().isEmpty());
    QCOMPARE(proxy.targets().constLast(), QStringLiteral("127.0.0.1:%1").arg(port));
}

// NoProxy is explicit, not Qt's DefaultProxy: a proxy that some other part of the
// process set application-wide must not silently carry a news-server connection
// the user switched off.
void tst_NntpSocket::withNoProxyOfItsOwnAnApplicationProxyIsIgnored()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    FakeProxyServer proxy(FakeProxyServer::Kind::Socks5);
    const quint16 proxyPort = proxy.start();
    QVERIFY(proxyPort != 0);

    QNetworkProxy::setApplicationProxy(
        QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), proxyPort));
    const auto restore = qScopeGuard([] {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    });

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));

    QVERIFY(ready.wait(5000));
    QVERIFY2(proxy.targets().isEmpty(), "a socket with no proxy of its own went through one");
}

// The error must say whose fault it was: the queue waits for a proxy, while a
// ConnectFailed backs the provider off and spends the article's retries.
void tst_NntpSocket::aProxyFailureNamesTheProxyNotTheServer()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    const quint16 dead = deadPort();
    QVERIFY(dead != 0);

    NntpSocket socket;
    socket.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), dead));
    QSignalSpy failed(&socket, &NntpSocket::failed);
    socket.connectToServer(localServer(port));

    QVERIFY(failed.wait(5000));
    const auto error = failed.first().at(0).value<NntpError>();
    QCOMPARE(error, NntpError::ProxyFailed);
    QVERIFY2(failed.first().at(1).toString().contains(QStringLiteral("127.0.0.1:%1").arg(dead)),
             qPrintable(failed.first().at(1).toString()));
    QVERIFY(!escalatesToNextLevel(error));
    QVERIFY(isFatalToConnection(error));
    QVERIFY(server.receivedCommands().isEmpty());
}

// Bound to an interface that is not there: no connection, and reported as the local
// route — the queue waits on that, it never backs a provider off or spends a retry.
void tst_NntpSocket::aMissingBoundInterfaceIsNotTheServersFault()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    const QString before = eMule::thePrefs.bindAddress();
    eMule::thePrefs.setBindAddress(QStringLiteral("vpn-not-there0"));
    const auto restore = qScopeGuard([&] {
        eMule::thePrefs.setBindAddress(before);
        eMule::BindAddress::refresh();
    });

    NntpSocket socket;
    QSignalSpy failed(&socket, &NntpSocket::failed);
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QCOMPARE(failed.first().at(0).value<NntpError>(), NntpError::ProxyFailed);
    QCOMPARE(ready.count(), 0);

    // The interface is there again: the same socket connects.
    eMule::thePrefs.setBindAddress(before);
    eMule::BindAddress::refresh();
    NntpSocket again;
    QSignalSpy readyAgain(&again, &NntpSocket::ready);
    again.connectToServer(localServer(port));
    QVERIFY(readyAgain.wait(5000));
}

QTEST_MAIN(tst_NntpSocket)
void tst_NntpSocket::readRateLimit_boundsTheSocketBuffer_data()
{
    // TLS too: QSslSocket buffers in two layers, and a capped buffer that never
    // resumed hung every live connection until the response watchdog fired.
    QTest::addColumn<bool>("tls");
    QTest::newRow("cleartext") << false;
    QTest::newRow("tls") << true;
}

void tst_NntpSocket::readRateLimit_boundsTheSocketBuffer()
{
    QFETCH(bool, tls);

    // The bucket paces the drain; the buffer cap is what paces the wire. Without
    // it Qt read the whole response at link speed and the provider saw bursts
    // then silence. ~420 KB at 128 KB/s: the old code buffered all of it.
    QStringList many;
    many.reserve(6000);
    for (int i = 0; i < 6000; ++i)
        many.append(QStringLiteral("CAP-%1 with some padding to make the line longer").arg(i));

    FakeNntpServer server;
    server.setImplicitTls(tls);
    server.setCapabilities(many);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NewsServer target = localServer(port);
    if (tls) {
        target.tlsMode = TlsMode::Implicit;
        target.certVerification = CertVerification::None;
    }

    constexpr qint64 rate = 128 * 1024;
    NntpSocket socket;
    socket.setReadRateLimit(rate);   // before connect: applied to the new socket
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(target);
    QVERIFY(ready.wait(5000));

    qint64 peak = 0;
    QTimer probe;
    connect(&probe, &QTimer::timeout, &socket,
            [&] { peak = std::max(peak, socket.bufferedBytes()); });
    probe.start(10);

    CapabilitiesCommand caps;
    QSignalSpy done(&socket, &NntpSocket::commandFinished);
    socket.sendCommand(&caps);
    QVERIFY(done.wait(20000));
    probe.stop();

    QVERIFY(!caps.failed());
    QCOMPARE(caps.capabilities().size(), many.size());
    QVERIFY2(peak > 0, "probe never saw buffered data");
    // Qt may overshoot its cap by one read chunk. TLS has two capped layers
    // (encrypted and decrypted), so up to twice that — still far below 420 KB.
    const qint64 bound = (tls ? 2 : 1) * rate + 16 * 1024;
    QVERIFY2(peak <= bound, qPrintable(QStringLiteral("buffered %1 bytes").arg(peak)));
}

void tst_NntpSocket::pipelinedCommandsFinishInOrder_data()
{
    QTest::addColumn<bool>("tls");
    QTest::newRow("cleartext") << false;
    QTest::newRow("tls") << true;
}

void tst_NntpSocket::pipelinedCommandsFinishInOrder()
{
    QFETCH(bool, tls);

    // Sent back to back while the first is unanswered; each must get its own
    // response, a multi-line one in the middle included.
    FakeNntpServer server;
    server.setImplicitTls(tls);
    server.setResponseDelay(50);
    server.addArticle(QStringLiteral("a@x"), QByteArrayLiteral("payload"));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NewsServer target = localServer(port);
    if (tls) {
        target.tlsMode = TlsMode::Implicit;
        target.certVerification = CertVerification::None;
    }

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(target);
    QVERIFY(ready.wait(5000));

    StatCommand first(QStringLiteral("a@x"));
    CapabilitiesCommand caps;
    StatCommand last(QStringLiteral("missing@x"));
    QList<NntpCommand*> order;
    connect(&socket, &NntpSocket::commandFinished, &socket,
            [&order](NntpCommand* c) { order.append(c); });

    socket.sendCommand(&first);
    QVERIFY(socket.acceptsCommands());
    socket.sendCommand(&caps);
    socket.sendCommand(&last);
    QCOMPARE(socket.pipelinedCount(), 2);

    QTRY_COMPARE_WITH_TIMEOUT(order.size(), 3, 5000);
    QCOMPARE(order, (QList<NntpCommand*>{&first, &caps, &last}));
    QVERIFY(!first.failed());
    QVERIFY(first.exists());
    QVERIFY(!caps.failed());
    QVERIFY(caps.has(QLatin1StringView("READER")));
    QCOMPARE(last.error(), NntpError::ArticleNotFound);
    QVERIFY(socket.isReady());
    QCOMPARE(socket.pipelinedCount(), 0);
    QVERIFY2(server.maxOutstanding() >= 3,
             qPrintable(QStringLiteral("outstanding %1").arg(server.maxOutstanding())));
}

void tst_NntpSocket::aDropFailsEveryPipelinedCommand()
{
    // The connection dies under the first: the ones behind it must hear about
    // it too, in order, or their owners wait forever.
    FakeNntpServer server;
    server.setResponseDelay(50);
    server.addArticle(QStringLiteral("b@x"), QByteArrayLiteral("payload"));
    server.setDropOnArticle(QStringLiteral("a@x"));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(localServer(port));
    QVERIFY(ready.wait(5000));

    StatCommand first(QStringLiteral("a@x"));
    StatCommand second(QStringLiteral("b@x"));
    QList<NntpCommand*> order;
    connect(&socket, &NntpSocket::commandFinished, &socket,
            [&order](NntpCommand* c) { order.append(c); });
    QSignalSpy failed(&socket, &NntpSocket::failed);

    socket.sendCommand(&first);
    socket.sendCommand(&second);

    QTRY_COMPARE_WITH_TIMEOUT(order.size(), 2, 5000);
    QCOMPARE(order, (QList<NntpCommand*>{&first, &second}));
    QCOMPARE(first.error(), NntpError::Disconnected);
    QCOMPARE(second.error(), NntpError::Disconnected);
    QCOMPARE(failed.size(), 1);
}

#include "tst_NntpSocket.moc"
