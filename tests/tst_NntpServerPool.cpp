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

#include <QPointer>
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
NewsServer make(const QString& name, quint16 port, int level, int maxConnections = 2,
                int group = 0)
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
    s.group = group;
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
    void blockingKeepsBusyLeasesAliveUntilRelease();
    void retryIntervalZeroDisablesBlocking();
    void changingCredentialsDropsConnections();
    void generationBumpsOnEveryChange();
    void groupedServersShareOneConnectionBudget();
    void groupBudgetIsTheSmallestMemberLimit();
    void ungroupedServersKeepSeparateBudgets();
    void zeroConnectionServerIsNeverLeased();
    void ladderIsBuiltFromTheConfiguredLevelsOnly();
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

// A backoff must not reach into connections that are mid-article. Aborting one
// raises no failed() — NntpSocket::abort() marks itself Disconnected before it
// touches the QTcpSocket — so the article on it would never finish: its worker
// slot held forever, its item stalled, and the job left pointing at a socket
// deleteLater() has since freed. One damaged article on a busy account used to
// take every other article on that worker with it.
void tst_NntpServerPool::blockingKeepsBusyLeasesAliveUntilRelease()
{
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NntpServerPool pool;
    pool.setServers({make(QStringLiteral("solo"), port, 0, /*maxConnections*/ 2)});

    NntpSocket* first = pool.acquire(0);
    NntpSocket* second = pool.acquire(0);
    QVERIFY(first != nullptr && second != nullptr);
    QSignalSpy ready(second, &NntpSocket::ready);
    QVERIFY(ready.wait(5000));

    // The first lease fails and backs the account off while the second is still
    // out — the article on it has nothing to do with the fault.
    QPointer<NntpSocket> busy(second);
    pool.release(first, /*reusable*/ false);
    pool.blockServer(make(QStringLiteral("solo"), port, 0).key());
    QTest::qWait(50);   // let any deleteLater() run

    QVERIFY(!busy.isNull());
    QVERIFY(busy->isReady());
    QCOMPARE(pool.busyCount(), 1);

    // Given back, it goes — a blocked server's connections are suspect, they
    // just may not be taken away mid-article.
    pool.release(second);
    QTest::qWait(50);
    QCOMPARE(pool.totalCount(), 0);
    QVERIFY(busy.isNull());
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
void tst_NntpServerPool::groupedServersShareOneConnectionBudget()
{
    // The same provider reached through two host names. Two rows, one account,
    // and the plan's limit applies to the account -- not to each row, which is
    // how you get an over-subscribed login and a 502.
    FakeNntpServer one;
    FakeNntpServer two;
    const quint16 p1 = one.start();
    const quint16 p2 = two.start();
    QVERIFY(p1 != 0 && p2 != 0);

    NntpServerPool pool;
    pool.setServers({make(QStringLiteral("a"), p1, 0, 2, /*group*/ 7),
                     make(QStringLiteral("b"), p2, 0, 2, /*group*/ 7)});

    QCOMPARE(pool.capacity(), 2);
    QVERIFY(pool.acquire(0) != nullptr);
    QVERIFY(pool.acquire(0) != nullptr);
    QCOMPARE(pool.acquire(0), nullptr);
    QCOMPARE(pool.totalCount(), 2);
}

void tst_NntpServerPool::groupBudgetIsTheSmallestMemberLimit()
{
    // Members disagreeing is ambiguous by construction, so the bucket takes the
    // smallest: too few connections costs throughput, too many gets the account
    // suspended.
    FakeNntpServer one;
    FakeNntpServer two;
    const quint16 p1 = one.start();
    const quint16 p2 = two.start();
    QVERIFY(p1 != 0 && p2 != 0);

    NntpServerPool pool;
    pool.setServers({make(QStringLiteral("a"), p1, 0, 3, /*group*/ 4),
                     make(QStringLiteral("b"), p2, 0, 1, /*group*/ 4)});

    QCOMPARE(pool.capacity(), 1);
    QVERIFY(pool.acquire(0) != nullptr);
    QCOMPARE(pool.acquire(0), nullptr);
}

void tst_NntpServerPool::ungroupedServersKeepSeparateBudgets()
{
    // 0 means "no group", not "one shared group". Collapsing every ungrouped
    // account into a single bucket would silently halve everyone's throughput.
    FakeNntpServer one;
    FakeNntpServer two;
    const quint16 p1 = one.start();
    const quint16 p2 = two.start();
    QVERIFY(p1 != 0 && p2 != 0);

    NntpServerPool pool;
    pool.setServers({make(QStringLiteral("a"), p1, 0, 2),
                     make(QStringLiteral("b"), p2, 0, 2)});

    QCOMPARE(pool.capacity(), 4);
    for (int i = 0; i < 4; ++i)
        QVERIFY2(pool.acquire(0) != nullptr, qPrintable(QStringLiteral("lease %1").arg(i)));
    QCOMPARE(pool.acquire(0), nullptr);
}

void tst_NntpServerPool::zeroConnectionServerIsNeverLeased()
{
    // UsenetQueue divides maxConnections across workers, so a slice can legally
    // arrive with nothing to spend. Both halves matter: the row keeps its rung,
    // so every worker numbers the ladder identically, and it leases nothing, so
    // the divided budget is not quietly replicated back up to one-per-worker.
    FakeNntpServer server;
    const quint16 port = server.start();
    QVERIFY(port != 0);

    const NewsServer broke = make(QStringLiteral("none"), port, 0, /*maxConnections*/ 0);
    NntpServerPool pool;
    pool.setServers({broke});

    QCOMPARE(pool.levelOf(broke.key()), 0);
    QCOMPARE(pool.capacity(), 0);
    QCOMPARE(pool.acquire(0), nullptr);
    QCOMPARE(pool.totalCount(), 0);
}

void tst_NntpServerPool::ladderIsBuiltFromTheConfiguredLevelsOnly()
{
    // The one definition of the ladder, shared with UsenetQueue. If the two ever
    // build it differently, a rung means one thing to the scheduler and another
    // to the pool it asks -- which is a routing bug that shows up as articles
    // never reaching the fill server.
    NewsServer disabled = make(QStringLiteral("off"), 119, 5);
    disabled.enabled = false;

    NewsServer starved = make(QStringLiteral("starved"), 443, 10, /*maxConnections*/ 0);

    const QList<int> ladder = nntpLevelLadder(
        {make(QStringLiteral("a"), 119, 0), disabled, starved});

    // Disabled rows are gone; a row with no connections is NOT -- filtering on
    // maxConnections here is what would make a worker's slice renumber the rungs.
    QCOMPARE(ladder, QList<int>({0, 10}));
}

#include "tst_NntpServerPool.moc"
