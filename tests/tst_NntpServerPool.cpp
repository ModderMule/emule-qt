/// @file tst_NntpServerPool.cpp
/// @brief Priority-level leasing, rotation, exclusion and server backoff.
///
/// The pool is the half of Usenet fault tolerance that lives outside the socket.
/// Its contract is small but every clause of it is load-bearing:
///
///   - Levels are a ladder, not a preference. Escalating past a level that is
///     merely busy would hammer a fill server for articles the main provider
///     has; never escalating would strand every article the main provider lost.
///   - The exclusion list is keyed by NewsServer::key(), not by list index,
///     because the user can reorder the list mid-download.
///   - Sparse user levels (0/5/10) normalize to 0/1/2, or the ladder has empty
///     rungs and the escalation stops early and silently.
///
/// These run against the fake server so a lease actually connects; asserting on
/// pool bookkeeping alone would not catch a lease that never becomes usable.

#include "FakeNntpServer.h"

#include "nntp/NntpServerPool.h"
#include "nntp/NntpSocket.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule::usenet;
using eMule::testing::FakeNntpServer;

namespace {

/// Credentials match FakeNntpServer's defaults, so a lease actually reaches
/// ready(). Server identity comes from the *port*, not the user name: key() is
/// host:port/user by design, and two accounts really are two endpoints. Giving
/// them distinct user names instead would make every lease fail authentication
/// while the pool bookkeeping still looked right.
NewsServer make(const QString& name, quint16 port, int level, int maxConnections = 2)
{
    NewsServer s;
    s.name = name;
    s.host = QStringLiteral("127.0.0.1");
    s.port = port;
    s.tlsMode = TlsMode::None;
    s.user = QStringLiteral("testuser");
    s.pass = QStringLiteral("testpass");
    s.level = level;
    s.maxConnections = maxConnections;
    return s;
}

} // namespace

class tst_NntpServerPool : public QObject {
    Q_OBJECT

private slots:
    void sparseLevelsAreNormalized();
    void disabledAndInvalidServersAreNotLevelled();
    void acquireHonoursTheLevel();
    void acquireExcludesTriedServers();
    void acquireRotatesWithinALevel();
    void acquireRespectsMaxConnections();
    void releaseReusesAnIdleConnection();
    void releaseUnusableDropsIt();
    void blockedServerIsSkippedAndRestored();
    void retryIntervalZeroDisablesBlocking();
    void changingCredentialsDropsConnections();
    void generationBumpsOnEveryChange();
};

void tst_NntpServerPool::sparseLevelsAreNormalized()
{
    NntpServerPool pool;
    pool.setServers({make(QStringLiteral("a"), 119, 0),
                     make(QStringLiteral("b"), 563, 5),
                     make(QStringLiteral("c"), 443, 10)});

    QCOMPARE(pool.maxLevel(), 2);
    QCOMPARE(pool.levelOf(make(QStringLiteral("a"), 119, 0).key()), 0);
    QCOMPARE(pool.levelOf(make(QStringLiteral("b"), 563, 5).key()), 1);
    QCOMPARE(pool.levelOf(make(QStringLiteral("c"), 443, 10).key()), 2);
}

void tst_NntpServerPool::disabledAndInvalidServersAreNotLevelled()
{
    NewsServer disabled = make(QStringLiteral("off"), 119, 0);
    disabled.enabled = false;
    NewsServer hostless = make(QStringLiteral("bad"), 563, 1);
    hostless.host.clear();

    NntpServerPool pool;
    pool.setServers({disabled, hostless, make(QStringLiteral("ok"), 443, 2)});

    // One usable server means one rung, whatever numbers the others carried.
    QCOMPARE(pool.maxLevel(), 0);
    QCOMPARE(pool.levelOf(disabled.key()), -1);
    QCOMPARE(pool.levelOf(hostless.key()), -1);
    QCOMPARE(pool.levelOf(make(QStringLiteral("ok"), 443, 2).key()), 0);
}

void tst_NntpServerPool::acquireHonoursTheLevel()
{
    FakeNntpServer main;
    FakeNntpServer fill;
    const quint16 mainPort = main.start();
    const quint16 fillPort = fill.start();
    QVERIFY(mainPort != 0 && fillPort != 0);

    NntpServerPool pool;
    pool.setServers({make(QStringLiteral("main"), mainPort, 0),
                     make(QStringLiteral("fill"), fillPort, 1)});

    NntpSocket* l0 = pool.acquire(0);
    QVERIFY(l0 != nullptr);
    QCOMPARE(l0->server().name, QStringLiteral("main"));

    NntpSocket* l1 = pool.acquire(1);
    QVERIFY(l1 != nullptr);
    QCOMPARE(l1->server().name, QStringLiteral("fill"));

    // A rung that does not exist yields nothing rather than falling back --
    // silently wrapping around would re-ask a server that already said 430.
    QCOMPARE(pool.acquire(2), nullptr);
}

void tst_NntpServerPool::acquireExcludesTriedServers()
{
    FakeNntpServer sa;
    FakeNntpServer sb;
    const quint16 portA = sa.start();
    const quint16 portB = sb.start();
    QVERIFY(portA != 0 && portB != 0);

    const NewsServer a = make(QStringLiteral("a"), portA, 0);
    const NewsServer b = make(QStringLiteral("b"), portB, 0);

    NntpServerPool pool;
    pool.setServers({a, b});

    NntpSocket* first = pool.acquire(0, {a.key()});
    QVERIFY(first != nullptr);
    QCOMPARE(first->server().name, QStringLiteral("b"));

    // Both excluded: nothing left on this level.
    QCOMPARE(pool.acquire(0, {a.key(), b.key()}), nullptr);
}

void tst_NntpServerPool::acquireRotatesWithinALevel()
{
    FakeNntpServer sa;
    FakeNntpServer sb;
    const quint16 portA = sa.start();
    const quint16 portB = sb.start();
    QVERIFY(portA != 0 && portB != 0);

    NntpServerPool pool;
    pool.setServers({make(QStringLiteral("a"), portA, 0), make(QStringLiteral("b"), portB, 0)});

    NntpSocket* first = pool.acquire(0);
    NntpSocket* second = pool.acquire(0);
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);

    // Equals on a level rotate. Without this the first account absorbs the
    // whole queue while its neighbour sits idle.
    QVERIFY(first->server().name != second->server().name);
}

void tst_NntpServerPool::acquireRespectsMaxConnections()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpServerPool pool;
    pool.setServers({make(QStringLiteral("solo"), port, 0, /*maxConnections*/ 2)});

    QVERIFY(pool.acquire(0) != nullptr);
    QVERIFY(pool.acquire(0) != nullptr);
    QCOMPARE(pool.busyCount(), 2);

    // At the limit acquire() returns nullptr. That is "wait", not "escalate":
    // exceeding a provider's connection count gets the account throttled.
    QCOMPARE(pool.acquire(0), nullptr);
    QCOMPARE(pool.totalCount(), 2);
}

void tst_NntpServerPool::releaseReusesAnIdleConnection()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpServerPool pool;
    pool.setServers({make(QStringLiteral("solo"), port, 0, 4)});

    NntpSocket* first = pool.acquire(0);
    QVERIFY(first != nullptr);
    QSignalSpy ready(first, &NntpSocket::ready);
    QVERIFY(ready.wait(5000));

    pool.release(first);
    QCOMPARE(pool.busyCount(), 0);

    // The whole point of pooling: TLS plus AUTHINFO costs more round trips than
    // the article fetch, so a second acquire must not open a second socket.
    NntpSocket* second = pool.acquire(0);
    QCOMPARE(second, first);
    QCOMPARE(pool.totalCount(), 1);
    QCOMPARE(server.connectionCount(), 1);
}

void tst_NntpServerPool::releaseUnusableDropsIt()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpServerPool pool;
    pool.setServers({make(QStringLiteral("solo"), port, 0, 4)});

    NntpSocket* first = pool.acquire(0);
    QVERIFY(first != nullptr);
    QSignalSpy ready(first, &NntpSocket::ready);
    QVERIFY(ready.wait(5000));

    pool.release(first, /*reusable*/ false);
    QCOMPARE(pool.totalCount(), 0);

    // A fresh acquire opens a genuinely new connection.
    NntpSocket* second = pool.acquire(0);
    QVERIFY(second != nullptr);
    QSignalSpy ready2(second, &NntpSocket::ready);
    QVERIFY(ready2.wait(5000));
    QCOMPARE(server.connectionCount(), 2);
}

void tst_NntpServerPool::blockedServerIsSkippedAndRestored()
{
    FakeNntpServer sa;
    FakeNntpServer sb;
    const quint16 portA = sa.start();
    const quint16 portB = sb.start();
    QVERIFY(portA != 0 && portB != 0);

    const NewsServer a = make(QStringLiteral("a"), portA, 0);
    const NewsServer b = make(QStringLiteral("b"), portB, 0);

    NntpServerPool pool;
    pool.setServers({a, b});

    pool.blockServer(a.key());
    QVERIFY(pool.isServerBlocked(a.key()));
    QVERIFY(!pool.isServerBlocked(b.key()));

    // Level 0 still works -- the block takes one server out of rotation, it
    // does not take the level out of the ladder.
    NntpSocket* lease = pool.acquire(0);
    QVERIFY(lease != nullptr);
    QCOMPARE(lease->server().name, QStringLiteral("b"));

    // With both blocked the level yields nothing, and still must not escalate:
    // "could not connect" is not evidence the article is missing.
    pool.blockServer(b.key());
    QCOMPARE(pool.acquire(0), nullptr);
}

void tst_NntpServerPool::retryIntervalZeroDisablesBlocking()
{
    const NewsServer a = make(QStringLiteral("a"), 119, 0);

    NntpServerPool pool;
    pool.setRetryInterval(0);
    pool.setServers({a});

    pool.blockServer(a.key());
    QVERIFY(!pool.isServerBlocked(a.key()));
}

void tst_NntpServerPool::changingCredentialsDropsConnections()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NewsServer s = make(QStringLiteral("solo"), port, 0, 4);

    NntpServerPool pool;
    pool.setServers({s});

    NntpSocket* lease = pool.acquire(0);
    QVERIFY(lease != nullptr);
    QSignalSpy ready(lease, &NntpSocket::ready);
    QVERIFY(ready.wait(5000));
    pool.release(lease);
    QCOMPARE(pool.totalCount(), 1);

    // key() is host:port/user, so a password change leaves it identical while
    // invalidating every authenticated socket behind it. Comparing keys alone
    // would keep handing out connections authenticated with the old secret.
    s.pass = QStringLiteral("newpass");
    pool.setServers({s});
    QCOMPARE(pool.totalCount(), 0);
}

void tst_NntpServerPool::generationBumpsOnEveryChange()
{
    NntpServerPool pool;
    const int before = pool.generation();

    pool.setServers({make(QStringLiteral("a"), 119, 0)});
    QCOMPARE(pool.generation(), before + 1);

    pool.setServers({});
    QCOMPARE(pool.generation(), before + 2);
    QCOMPARE(pool.maxLevel(), 0);
    QCOMPARE(pool.acquire(0), nullptr);
}

QTEST_MAIN(tst_NntpServerPool)
#include "tst_NntpServerPool.moc"
