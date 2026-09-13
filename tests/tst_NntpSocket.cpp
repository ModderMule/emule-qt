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

#include "nntp/NntpCommand.h"
#include "nntp/NntpSocket.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using namespace eMule::usenet;
using eMule::testing::FakeNntpServer;

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

} // namespace

class tst_NntpSocket : public QObject {
    Q_OBJECT

private slots:
    void connectsAuthenticatesAndBecomesReady();
    void noCredentials_skipsAuthinfo();
    void wrongPassword_failsWithAuthFailed();
    void greeting400_backsOffTheServer();
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
    void invalidServer_failsWithoutConnecting();
    void openConnections_countsAuthenticatedSocketsPerAccount();
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
    // What a provider says when the account's connection limit is reached.
    server.setGreeting(QByteArrayLiteral("400 Too many connections"));
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

QTEST_MAIN(tst_NntpSocket)
#include "tst_NntpSocket.moc"
