/// @file tst_IpcBusyStates.cpp
/// @brief The GUI's IPC client against a core that is slow, silent, loading or gone.
///
/// Regression tests for a GUI that hung on a busy machine and for a start-up that
/// gave no word on why the window stayed empty. Each case builds one busy state
/// with a mock core on loopback and checks the client stays bounded and says what
/// is going on:
///   - a poll is never stacked on an unanswered one,
///   - a core that talks is not dropped for a late pong,
///   - a flood of state pushes costs a handful of handler calls,
///   - the link state names every stage of a start-up, the bad ones included,
///   - requests held during the core's load collapse to the newest,
///   - a core that will not come up is reported with the reason.

#include "TestHelpers.h"

#include "DeferredRequests.h"
#include "IpcConnection.h"
#include "app/CoreStartWatch.h"
#include "app/IpcClient.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

using namespace eMule;
using namespace eMule::Ipc;

namespace {

using Link = IpcClient::CoreLink;
using Problem = CoreStartWatch::Problem;

/// A core as the GUI sees it: a port, a handshake, replies and pushes — each of
/// which a test can switch off to build the busy state it is about.
class MockCore : public QObject {
public:
    bool answerHandshake = true;
    QString refuseWith;          ///< non-empty: turn the handshake down with this text
    bool loading = false;
    QString loadStage;
    qint64 pid = 0;              ///< 0: an older core, which sends no pid
    bool answerPing = true;
    bool answerRequests = true;  ///< everything but Handshake/Subscribe/Ping
    std::function<void(const IpcMessage&)> onRequest;

    MockCore()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                ++connections;
                m_conn = std::make_unique<IpcConnection>(socket);
                connect(m_conn.get(), &IpcConnection::messageReceived, this,
                        &MockCore::onMessage);
            }
        });
    }

    [[nodiscard]] bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    void push(const IpcMessage& msg)
    {
        if (m_conn)
            m_conn->sendMessage(msg);
    }

    void reply(const IpcMessage& request, const QCborValue& data = {})
    {
        push(IpcMessage::makeResult(request.seqId(), true, data));
    }

    void dropClient()
    {
        if (m_conn)
            m_conn->abort();
    }

    [[nodiscard]] int count(IpcMsgType type) const
    {
        return static_cast<int>(std::count_if(received.cbegin(), received.cend(),
            [type](const IpcMessage& msg) { return msg.type() == type; }));
    }

    [[nodiscard]] IpcMessage last(IpcMsgType type) const
    {
        for (auto it = received.crbegin(); it != received.crend(); ++it) {
            if (it->type() == type)
                return *it;
        }
        return {};
    }

    QList<IpcMessage> received;
    int connections = 0;

private:
    void onMessage(const IpcMessage& msg)
    {
        received.append(msg);
        switch (msg.type()) {
        case IpcMsgType::Handshake: {
            if (!refuseWith.isEmpty()) {
                push(IpcMessage::makeError(msg.seqId(), 401, refuseWith));
                return;
            }
            if (!answerHandshake)
                return;
            IpcMessage ok(IpcMsgType::HandshakeOk, msg.seqId());
            ok.append(QStringLiteral("1.0"));
            ok.append(QStringLiteral("mock core"));
            ok.append(QStringLiteral("session-1"));
            ok.append(loading);
            ok.append(loadStage);
            if (pid != 0)
                ok.append(pid);
            push(ok);
            return;
        }
        case IpcMsgType::Subscribe:
            return;
        case IpcMsgType::Ping:
            if (answerPing)
                reply(msg);
            return;
        case IpcMsgType::SyncLogs:
        case IpcMsgType::GetServerMessages:
            reply(msg, QCborArray{});
            return;
        default:
            break;
        }
        if (onRequest)
            onRequest(msg);
        else if (answerRequests)
            reply(msg);
    }

    QTcpServer m_server;
    std::unique_ptr<IpcConnection> m_conn;
};

/// A loopback port nothing listens on.
quint16 closedPort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    return probe.serverPort();   // freed as probe goes out of scope
}

IpcMessage statsPush(int value)
{
    IpcMessage msg(IpcMsgType::PushStatsUpdate, 0);
    QCborMap stats;
    stats.insert(QStringLiteral("value"), value);
    msg.append(stats);
    return msg;
}

IpcMessage logBatch(std::initializer_list<int> ids)
{
    QCborArray rows;
    for (const int id : ids) {
        rows.append(QCborArray{qint64(id), QStringLiteral("emule.general"),
                               qint64(QtInfoMsg), QStringLiteral("line %1").arg(id),
                               qint64(1'700'000'000)});
    }
    IpcMessage msg(IpcMsgType::PushLogBatch, 0);
    msg.append(rows);
    return msg;
}

} // namespace

class tst_IpcBusyStates : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // Polling a core that is slow to answer
    void poll_isNotStackedOnAnUnansweredOne();
    void poll_asksOnceMoreWithTheNewestCallback();
    void poll_ownersDoNotShareARequest();
    void poll_isFreeAgainAfterAReconnect();
    void plainRequests_stillStack();

    // Keepalive against a core that is alive but not answering the ping
    void keepalive_coreThatTalksIsNotDropped();
    void keepalive_silentCoreIsDropped();
    void keepalive_pongBehindABlockedLoopIsNotATimeout();

    // A flood from the core
    void flood_ofStatePushesReachesTheHandlerAFewTimes();
    void logBatch_isBracketedAndNeverShownTwice();

    // What the status bar is told
    void link_namesEveryStageOfASlowStart();
    void link_reportsACoreThatNeverAnswers();
    void link_reportsARefusedHandshake();
    void link_reportsALaunchedCoreAsStarting();
    void link_takesTheCorePidFromTheHandshake();
    void link_launchedCoreBusyLoadingIsWaitedForOnOneConnection();
    void keepalive_loadingCoreIsWaitedFor();

    // Requests held while the core loads
    void deferred_identicalReadsCollapseToTheNewest();
    void deferred_aWriteKeepsItsPlaceBeforeTheReadAfterIt();
    void deferred_differentQuestionsAreAllKept();
    void deferred_fullRefusesTheNewcomer();
    void deferred_repeatedReadDoesNotGrowWithoutBound();

    // The start-up watch
    void watch_coreThatDiedIsReportedWithoutWaitingOutTheGrace();
    void watch_coreThatAcceptsButNeverAnswers();
    void watch_nothingListening();
    void watch_refusalIsReportedAtOnce();
    void watch_clearsWhenTheCoreAnswers();
};

void tst_IpcBusyStates::initTestCase()
{
    qRegisterMetaType<IpcMessage>();
    qRegisterMetaType<CoreStartWatch::Problem>();
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

void tst_IpcBusyStates::poll_isNotStackedOnAnUnansweredOne()
{
    MockCore core;
    QVERIFY(core.listen());
    core.answerRequests = false;   // busy: it reads the request and says nothing

    IpcClient client;
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    // A panel's 500 ms timer over a core that takes a minute: 120 ticks
    int replies = 0;
    for (int tick = 0; tick < 120; ++tick) {
        client.sendPoll(this, IpcMessage(IpcMsgType::GetDownloads),
                        [&replies](const IpcMessage&) { ++replies; });
        QCoreApplication::processEvents();
    }
    QTest::qWait(100);

    // One question on the wire, not 120 — and so one list to apply, not 120
    QCOMPARE(core.count(IpcMsgType::GetDownloads), 1);
    QCOMPARE(client.pollsSkipped(), quint64(119));
    QCOMPARE(replies, 0);
}

void tst_IpcBusyStates::poll_asksOnceMoreWithTheNewestCallback()
{
    MockCore core;
    QVERIFY(core.listen());
    core.answerRequests = false;

    IpcClient client;
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    QList<int> ran;
    for (int tick = 1; tick <= 5; ++tick) {
        client.sendPoll(this, IpcMessage(IpcMsgType::GetDownloads),
                        [&ran, tick](const IpcMessage&) { ran.append(tick); });
    }
    QTRY_COMPARE_WITH_TIMEOUT(core.count(IpcMsgType::GetDownloads), 1, 3000);

    // The core gets round to it. The reply belongs to the first tick; what was
    // asked meanwhile is worth exactly one fresh request, not four.
    core.reply(core.last(IpcMsgType::GetDownloads));
    QTRY_COMPARE_WITH_TIMEOUT(core.count(IpcMsgType::GetDownloads), 2, 3000);
    QCOMPARE(ran, (QList<int>{1}));

    core.reply(core.last(IpcMsgType::GetDownloads));
    QTRY_COMPARE_WITH_TIMEOUT(ran, (QList<int>{1, 5}), 3000);

    // Nobody asked since: the line goes quiet
    QTest::qWait(100);
    QCOMPARE(core.count(IpcMsgType::GetDownloads), 2);
}

void tst_IpcBusyStates::poll_ownersDoNotShareARequest()
{
    MockCore core;
    QVERIFY(core.listen());
    core.answerRequests = false;

    IpcClient client;
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    // The status bar and the Statistics page both poll GetStats, for different
    // widgets. Sharing one request would starve whichever asks second.
    QObject statusBar, statsPage;
    int forStatusBar = 0, forStatsPage = 0;
    for (int tick = 0; tick < 10; ++tick) {
        client.sendPoll(&statusBar, IpcMessage(IpcMsgType::GetStats),
                        [&forStatusBar](const IpcMessage&) { ++forStatusBar; });
        client.sendPoll(&statsPage, IpcMessage(IpcMsgType::GetStats),
                        [&forStatsPage](const IpcMessage&) { ++forStatsPage; });
    }
    QTRY_COMPARE_WITH_TIMEOUT(core.count(IpcMsgType::GetStats), 2, 3000);

    // A different question from the same owner is its own request too
    IpcMessage history(IpcMsgType::GetSpeedHistory);
    history.append(qint64(7));
    client.sendPoll(&statusBar, history, {});
    history = IpcMessage(IpcMsgType::GetSpeedHistory);
    history.append(qint64(8));
    client.sendPoll(&statusBar, history, {});
    QTRY_COMPARE_WITH_TIMEOUT(core.count(IpcMsgType::GetSpeedHistory), 2, 3000);

    for (const IpcMessage& msg : std::as_const(core.received)) {
        if (msg.type() == IpcMsgType::GetStats)
            core.reply(msg);
    }
    QTRY_VERIFY_WITH_TIMEOUT(forStatusBar >= 1 && forStatsPage >= 1, 3000);
}

void tst_IpcBusyStates::poll_isFreeAgainAfterAReconnect()
{
    MockCore core;
    QVERIFY(core.listen());
    core.answerRequests = false;

    IpcClient client;
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    QList<bool> outcomes;
    client.sendPoll(this, IpcMessage(IpcMsgType::GetUploads),
                    [&outcomes](const IpcMessage& reply) { outcomes.append(reply.isValid()); });
    QTRY_COMPARE_WITH_TIMEOUT(core.count(IpcMsgType::GetUploads), 1, 3000);

    // The core falls over with the poll unanswered. Its callback fails — and the
    // slot must not stay taken, or that list would never refresh again.
    core.dropClient();
    QTRY_VERIFY_WITH_TIMEOUT(!client.isConnected(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(outcomes, (QList<bool>{false}), 3000);

    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);
    client.sendPoll(this, IpcMessage(IpcMsgType::GetUploads), {});
    QTRY_COMPARE_WITH_TIMEOUT(core.count(IpcMsgType::GetUploads), 2, 3000);
}

void tst_IpcBusyStates::plainRequests_stillStack()
{
    MockCore core;
    QVERIFY(core.listen());
    core.answerRequests = false;

    IpcClient client;
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    // The control for the tests above, and the contract for everything that is
    // not a poll: a user action is sent every time it is asked for.
    for (int i = 0; i < 5; ++i)
        client.sendRequest(IpcMessage(IpcMsgType::GetDownloads));
    QTRY_COMPARE_WITH_TIMEOUT(core.count(IpcMsgType::GetDownloads), 5, 3000);
}

// ---------------------------------------------------------------------------
// Keepalive
// ---------------------------------------------------------------------------

void tst_IpcBusyStates::keepalive_coreThatTalksIsNotDropped()
{
    MockCore core;
    QVERIFY(core.listen());
    core.answerPing = false;   // too busy for the ping…

    IpcClient client;
    client.setTimeouts(2000, 200, 100);   // ping every 200 ms, 100 ms for the pong
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);
    QSignalSpy lostSpy(&client, &IpcClient::disconnected);

    // …but pushing state the whole time. A reconnect here refetched every list
    // from a core that was already behind.
    QTimer pusher;
    int value = 0;
    connect(&pusher, &QTimer::timeout, &core, [&] { core.push(statsPush(++value)); });
    pusher.start(40);

    QTest::qWait(1000);   // five keepalive windows
    QVERIFY(core.count(IpcMsgType::Ping) >= 3);
    QCOMPARE(lostSpy.count(), 0);
    QVERIFY(client.isConnected());
    QCOMPARE(client.coreLink(), Link::Ready);
}

void tst_IpcBusyStates::keepalive_silentCoreIsDropped()
{
    MockCore core;
    QVERIFY(core.listen());
    core.answerPing = false;

    IpcClient client;
    client.setTimeouts(2000, 200, 100);   // ping every 200 ms, 100 ms for the pong
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);
    QSignalSpy lostSpy(&client, &IpcClient::disconnected);

    // The control: not a byte from the core, so the keepalive still does its job
    QTRY_COMPARE_WITH_TIMEOUT(lostSpy.count(), 1, 2000);
    QCOMPARE(client.coreLink(), Link::NotResponding);
}

void tst_IpcBusyStates::keepalive_pongBehindABlockedLoopIsNotATimeout()
{
    MockCore core;
    QVERIFY(core.listen());

    IpcClient client;
    client.setTimeouts(2000, 200, 100);   // ping every 200 ms, 100 ms for the pong
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);
    QSignalSpy lostSpy(&client, &IpcClient::disconnected);

    // The core answers the ping at once, then this thread — the GUI's — is held
    // for longer than the pong window, as by a slow paint or a disk stall. When
    // it comes back the timeout and the pong are both due; the pong must win.
    int stalls = 0;
    core.answerPing = false;
    core.onRequest = {};
    QTimer watch;
    connect(&watch, &QTimer::timeout, &core, [&] {
        if (stalls == 0 && core.count(IpcMsgType::Ping) >= 1) {
            ++stalls;
            core.reply(core.last(IpcMsgType::Ping));
            QCoreApplication::sendPostedEvents();
            QThread::msleep(400);
            core.answerPing = true;
        }
    });
    watch.start(5);

    QTRY_COMPARE_WITH_TIMEOUT(stalls, 1, 3000);
    QTest::qWait(400);
    QCOMPARE(lostSpy.count(), 0);
    QVERIFY(client.isConnected());
}

// ---------------------------------------------------------------------------
// Floods
// ---------------------------------------------------------------------------

void tst_IpcBusyStates::flood_ofStatePushesReachesTheHandlerAFewTimes()
{
    MockCore core;
    QVERIFY(core.listen());

    IpcClient client;
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    // A handler that costs what refreshing a panel costs
    QList<qint64> statsSeen;
    int downloadNudges = 0, notices = 0;
    connect(&client, &IpcClient::statsUpdated, this, [&](const IpcMessage& msg) {
        statsSeen.append(msg.fieldMap(0).value(QStringLiteral("value")).toInteger());
        QThread::msleep(2);
    });
    connect(&client, &IpcClient::downloadUpdated, this, [&] { ++downloadNudges; });
    connect(&client, &IpcClient::notifierEvent, this, [&] { ++notices; });

    // Minutes of backlog arriving in one go: 3000 state pushes of two kinds, and
    // a few events that each have to get through.
    constexpr int kPushes = 1500;
    for (int i = 1; i <= kPushes; ++i) {
        core.push(statsPush(i));
        core.push(IpcMessage(IpcMsgType::PushDownloadUpdate, 0));
        if (i % 500 == 0)
            core.push(IpcMessage(IpcMsgType::PushNotifierEvent, 0));
    }

    QElapsedTimer clock;
    clock.start();
    QTRY_VERIFY_WITH_TIMEOUT(!statsSeen.isEmpty() && statsSeen.last() == kPushes, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(notices, 3, 3000);

    // Handled one by one this is 3 s of handler time. Only the newest of each
    // kind is worth handling, and the last state shown is the last one sent.
    QVERIFY2(statsSeen.size() < kPushes / 10, qPrintable(QString::number(statsSeen.size())));
    QVERIFY2(downloadNudges < kPushes / 10, qPrintable(QString::number(downloadNudges)));
    QVERIFY2(clock.elapsed() < 2000, qPrintable(QString::number(clock.elapsed())));
}

void tst_IpcBusyStates::logBatch_isBracketedAndNeverShownTwice()
{
    MockCore core;
    QVERIFY(core.listen());

    IpcClient client;
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);
    QTest::qWait(600);   // the connect-time SyncLogs has come and gone

    QSignalSpy startSpy(&client, &IpcClient::logBatchStarted);
    QSignalSpy lineSpy(&client, &IpcClient::logMessageReceived);
    QSignalSpy endSpy(&client, &IpcClient::logBatchFinished);

    core.push(logBatch({1, 2, 3}));
    QTRY_COMPARE_WITH_TIMEOUT(endSpy.count(), 1, 3000);
    QCOMPARE(startSpy.count(), 1);
    QCOMPARE(lineSpy.count(), 3);
    // The shape main.cpp's log handler reads
    const auto line = lineSpy.at(2).at(0).value<IpcMessage>();
    QCOMPARE(line.type(), IpcMsgType::PushLogMessage);
    QCOMPARE(line.fieldInt(0), qint64(3));
    QCOMPARE(line.fieldString(3), QStringLiteral("line 3"));

    // A batch overlapping what is on screen adds only its new lines
    core.push(logBatch({2, 3, 4}));
    QTRY_COMPARE_WITH_TIMEOUT(endSpy.count(), 2, 3000);
    QCOMPARE(lineSpy.count(), 4);
}

// ---------------------------------------------------------------------------
// Link state
// ---------------------------------------------------------------------------

void tst_IpcBusyStates::link_namesEveryStageOfASlowStart()
{
    MockCore core;
    QVERIFY(core.listen());
    core.loading = true;
    core.loadStage = QStringLiteral("known-files");

    IpcClient client;
    QList<Link> seen;
    connect(&client, &IpcClient::coreLinkChanged, this, [&] {
        if (seen.isEmpty() || seen.last() != client.coreLink())
            seen.append(client.coreLink());
    });
    QCOMPARE(client.coreLink(), Link::Offline);

    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    // Connected, and the lists are held: the GUI must be able to say so
    QCOMPARE(seen, (QList<Link>{Link::Connecting, Link::Handshaking, Link::Loading}));
    QVERIFY(client.coreLoading());
    QCOMPARE(client.loadStage(), QStringLiteral("known-files"));

    // The load moves on: same state, new stage, and the GUI is told
    QSignalSpy changeSpy(&client, &IpcClient::coreLinkChanged);
    IpcMessage stage(IpcMsgType::PushCoreLoading, 0);
    stage.append(true);
    stage.append(QStringLiteral("part-files"));
    core.push(stage);
    QTRY_COMPARE_WITH_TIMEOUT(client.loadStage(), QStringLiteral("part-files"), 3000);
    QCOMPARE(changeSpy.count(), 1);
    QCOMPARE(client.coreLink(), Link::Loading);

    IpcMessage done(IpcMsgType::PushCoreLoading, 0);
    done.append(false);
    done.append(QString());
    core.push(done);
    QTRY_COMPARE_WITH_TIMEOUT(client.coreLink(), Link::Ready, 3000);
    QVERIFY(!client.coreLoading());
    QVERIFY(client.loadStage().isEmpty());

    client.disconnectFromDaemon();
    QCOMPARE(client.coreLink(), Link::Offline);
}

void tst_IpcBusyStates::link_reportsACoreThatNeverAnswers()
{
    MockCore core;
    QVERIFY(core.listen());
    core.answerHandshake = false;   // accepts the connection, then nothing

    IpcClient client;
    client.setTimeouts(200, 30'000, 10'000);
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());

    QTRY_COMPARE_WITH_TIMEOUT(client.coreLink(), Link::Handshaking, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(client.coreLink(), Link::NotResponding, 3000);
    QVERIFY(!client.isConnected());

    // It keeps trying, and the retry in between does not paper over the state:
    // "Connecting…" every second would read as progress.
    QList<Link> during;
    connect(&client, &IpcClient::coreLinkChanged, this, [&] { during.append(client.coreLink()); });
    QTRY_VERIFY_WITH_TIMEOUT(core.connections >= 3, 8000);
    QVERIFY(client.connectAttempts() >= 3);
    for (const Link link : std::as_const(during))
        QCOMPARE(link, Link::NotResponding);

    // The core finally answers: straight to Ready, count reset
    core.answerHandshake = true;
    QTRY_COMPARE_WITH_TIMEOUT(client.coreLink(), Link::Ready, 8000);
    QCOMPARE(client.connectAttempts(), 0);
}

void tst_IpcBusyStates::link_reportsARefusedHandshake()
{
    MockCore core;
    QVERIFY(core.listen());
    core.refuseWith = QStringLiteral("Invalid token");

    IpcClient client;
    client.setTimeouts(300, 30'000, 10'000);
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());

    // Used to surface only as "handshake timed out", ten seconds later
    QTRY_COMPARE_WITH_TIMEOUT(client.coreLink(), Link::Refused, 3000);
    QCOMPARE(client.refusalText(), QStringLiteral("Invalid token"));
    QVERIFY(!client.isConnected());

    // The timeout that follows does not replace the reason with a vaguer one
    QTest::qWait(500);
    QCOMPARE(client.coreLink(), Link::Refused);
}

void tst_IpcBusyStates::link_reportsALaunchedCoreAsStarting()
{
    const quint16 port = closedPort();
    QVERIFY(port != 0);

    IpcClient client;
    client.expectDaemonStart();
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), port);
    QCOMPARE(client.coreLink(), Link::Starting);

    // Nothing listens yet; it retries quickly and keeps saying "starting"
    // (a refused connect to loopback takes Windows seconds, not milliseconds)
    QTRY_VERIFY_WITH_TIMEOUT(client.connectAttempts() >= 3, 15'000);
    QCOMPARE(client.coreLink(), Link::Starting);
    QVERIFY(!client.isConnected());
}

void tst_IpcBusyStates::link_launchedCoreBusyLoadingIsWaitedForOnOneConnection()
{
    // Issues #24/#40: a core we launched accepts, then a load stage holds its loop
    // past the handshake timeout. Reconnecting showed "not responding (attempt N)"
    // and queued the new connection behind the same stage.
    MockCore core;
    QVERIFY(core.listen());
    core.answerHandshake = false;

    IpcClient client;
    client.setTimeouts(150, 30'000, 10'000);
    client.expectDaemonStart();
    QList<Link> seen;
    connect(&client, &IpcClient::coreLinkChanged, this, [&] {
        if (seen.isEmpty() || seen.last() != client.coreLink())
            seen.append(client.coreLink());
    });
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());

    QTRY_COMPARE_WITH_TIMEOUT(client.coreLink(), Link::Handshaking, 3000);
    QTest::qWait(700);   // several handshake timeouts
    QCOMPARE(core.connections, 1);
    QCOMPARE(client.coreLink(), Link::Handshaking);

    // The stage ends: the first handshake is answered on the first connection
    core.answerHandshake = true;
    const IpcMessage hello = core.last(IpcMsgType::Handshake);
    IpcMessage ok(IpcMsgType::HandshakeOk, hello.seqId());
    ok.append(QStringLiteral("1.0"));
    ok.append(QStringLiteral("mock core"));
    ok.append(QStringLiteral("session-1"));
    ok.append(false);
    ok.append(QString());
    core.push(ok);
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);
    QCOMPARE(core.connections, 1);
    QVERIFY(!seen.contains(Link::NotResponding));
}

void tst_IpcBusyStates::keepalive_loadingCoreIsWaitedFor()
{
    MockCore core;
    QVERIFY(core.listen());
    core.loading = true;
    core.loadStage = QStringLiteral("shared-files");

    IpcClient client;
    client.setTimeouts(2000, 200, 100);   // ping every 200 ms, 100 ms for the pong
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);
    QSignalSpy lostSpy(&client, &IpcClient::disconnected);

    // A load stage holds the core's loop: no pong, no push
    core.answerPing = false;
    QTest::qWait(1000);
    QCOMPARE(lostSpy.count(), 0);
    QCOMPARE(client.coreLink(), Link::Loading);

    // Loaded: from now on a silent core is dropped as before
    IpcMessage done(IpcMsgType::PushCoreLoading, 0);
    done.append(false);
    done.append(QString());
    core.push(done);
    QTRY_COMPARE_WITH_TIMEOUT(client.coreLink(), Link::Ready, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(lostSpy.count(), 1, 2000);
}

// ---------------------------------------------------------------------------
// Deferred requests
// ---------------------------------------------------------------------------

void tst_IpcBusyStates::link_takesTheCorePidFromTheHandshake()
{
    // A core restarted on Windows is a new process: the GUI must learn its pid
    MockCore core;
    QVERIFY(core.listen());
    core.pid = 4242;
    IpcClient client;
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);
    QCOMPARE(client.corePid(), qint64(4242));
    client.disconnectFromDaemon();

    // An older core sends none: unknown, never a stale or bogus number
    MockCore older;
    QVERIFY(older.listen());
    IpcClient client2;
    client2.connectToDaemon(QHostAddress(QHostAddress::LocalHost), older.port());
    QTRY_VERIFY_WITH_TIMEOUT(client2.isConnected(), 3000);
    QCOMPARE(client2.corePid(), qint64(0));
    client2.disconnectFromDaemon();
}

void tst_IpcBusyStates::deferred_identicalReadsCollapseToTheNewest()
{
    DeferredRequests held;

    // A GUI polling GetDownloads twice a second through a one-minute load
    QList<int> refused;
    for (int seq = 1; seq <= 120; ++seq) {
        if (const auto old = held.hold(IpcMessage(IpcMsgType::GetDownloads, seq)))
            refused.append(old->seqId());
    }

    // Each newcomer released the one before it: every seqId gets an answer, and
    // the load ends with one list to build instead of 120.
    QCOMPARE(refused.size(), 119);
    QCOMPARE(refused.first(), 1);
    QCOMPARE(refused.last(), 119);
    QCOMPARE(held.size(), 1);

    const auto replay = held.take();
    QCOMPARE(static_cast<int>(replay.size()), 1);
    QCOMPARE(replay.front().seqId(), 120);
    QCOMPARE(held.size(), 0);
    QVERIFY(held.take().empty());
}

void tst_IpcBusyStates::deferred_aWriteKeepsItsPlaceBeforeTheReadAfterIt()
{
    DeferredRequests held;

    IpcMessage pause(IpcMsgType::PauseDownload, 2);
    pause.append(QStringLiteral("0123456789ABCDEF0123456789ABCDEF"));

    QVERIFY(!held.hold(IpcMessage(IpcMsgType::GetDownloads, 1)));
    QVERIFY(!held.hold(pause));
    const auto refused = held.hold(IpcMessage(IpcMsgType::GetDownloads, 3));
    QVERIFY(refused);
    QCOMPARE(refused->seqId(), 1);

    // The surviving read comes after the pause, so its answer shows the pause
    const auto replay = held.take();
    QCOMPARE(static_cast<int>(replay.size()), 2);
    QCOMPARE(replay[0].type(), IpcMsgType::PauseDownload);
    QCOMPARE(replay[1].seqId(), 3);

    // A write is never dropped for looking like another
    QVERIFY(!held.hold(pause));
    QVERIFY(!held.hold(pause));
    QCOMPARE(held.size(), 2);
}

void tst_IpcBusyStates::deferred_differentQuestionsAreAllKept()
{
    DeferredRequests held;

    IpcMessage a(IpcMsgType::GetDownloadSources, 1);
    a.append(QStringLiteral("AAAA"));
    IpcMessage b(IpcMsgType::GetDownloadSources, 2);
    b.append(QStringLiteral("BBBB"));

    QVERIFY(!held.hold(a));
    QVERIFY(!held.hold(b));
    QVERIFY(!held.hold(IpcMessage(IpcMsgType::GetUploads, 3)));
    QCOMPARE(held.size(), 3);
}

void tst_IpcBusyStates::deferred_fullRefusesTheNewcomer()
{
    DeferredRequests held;

    for (int seq = 1; seq <= DeferredRequests::kMaxHeld; ++seq) {
        IpcMessage msg(IpcMsgType::PauseDownload, seq);
        QVERIFY(!held.hold(msg));
    }
    QCOMPARE(held.size(), DeferredRequests::kMaxHeld);

    // Past the cap nothing more is kept: the daemon's memory is not the queue
    const auto refused = held.hold(IpcMessage(IpcMsgType::PauseDownload, 99'999));
    QVERIFY(refused);
    QCOMPARE(refused->seqId(), 99'999);
    const auto refusedRead = held.hold(IpcMessage(IpcMsgType::GetDownloads, 100'000));
    QVERIFY(refusedRead);
    QCOMPARE(refusedRead->seqId(), 100'000);
    QCOMPARE(held.size(), DeferredRequests::kMaxHeld);
}

void tst_IpcBusyStates::deferred_repeatedReadDoesNotGrowWithoutBound()
{
    DeferredRequests held;
    QVERIFY(!held.hold(IpcMessage(IpcMsgType::PauseDownload, 1)));

    // A client asking the same thing over and over through a long load
    const int rounds = DeferredRequests::kMaxHeld * 5;
    for (int seq = 2; seq < 2 + rounds; ++seq) {
        const auto refused = held.hold(IpcMessage(IpcMsgType::GetDownloads, seq));
        QCOMPARE(refused.has_value(), seq > 2);
        if (refused)
            QCOMPARE(refused->seqId(), seq - 1);
        QVERIFY(held.slotCount() <= DeferredRequests::kMaxHeld * 2);
    }
    QCOMPARE(held.size(), 2);

    // Still the write first, then the newest read
    const auto replay = held.take();
    QCOMPARE(replay.size(), std::size_t(2));
    QCOMPARE(replay[0].type(), IpcMsgType::PauseDownload);
    QCOMPARE(replay[1].seqId(), 1 + rounds);
}

// ---------------------------------------------------------------------------
// Start-up watch
// ---------------------------------------------------------------------------

void tst_IpcBusyStates::watch_coreThatDiedIsReportedWithoutWaitingOutTheGrace()
{
    const quint16 port = closedPort();
    QVERIFY(port != 0);

    IpcClient client;
    CoreStartWatch watch(&client);
    watch.setCheckIntervalMs(20);
    watch.setGraceMs(60'000);   // would be a minute of silence
    bool alive = true;
    watch.setProbes({[&alive] { return alive; }, [] { return true; }});
    QSignalSpy spy(&watch, &CoreStartWatch::problemChanged);

    client.expectDaemonStart();
    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), port);
    watch.restart();

    // Still starting: patience
    QTest::qWait(200);
    QCOMPARE(watch.problem(), Problem::None);
    QCOMPARE(spy.count(), 0);

    // The launched process is gone. No port will ever open.
    alive = false;
    QTRY_COMPARE_WITH_TIMEOUT(watch.problem(), Problem::CoreExited, 2000);
    QCOMPARE(spy.count(), 1);

    // Retry: the question is open again
    alive = true;
    watch.restart();
    QCOMPARE(watch.problem(), Problem::None);
}

void tst_IpcBusyStates::watch_coreThatAcceptsButNeverAnswers()
{
    MockCore core;
    QVERIFY(core.listen());
    core.answerHandshake = false;

    IpcClient client;
    client.setTimeouts(150, 30'000, 10'000);
    CoreStartWatch watch(&client);
    watch.setCheckIntervalMs(20);
    watch.setGraceMs(400);
    int portProbes = 0;
    watch.setProbes({{}, [&portProbes] { ++portProbes; return false; }});

    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    watch.restart();

    QTest::qWait(200);
    QCOMPARE(watch.problem(), Problem::None);   // inside the grace

    QTRY_COMPARE_WITH_TIMEOUT(watch.problem(), Problem::NoAnswer, 3000);

    // Judged once. The port probe binds the port; repeated every second it could
    // take it from a core that is at that moment trying to open it.
    QTest::qWait(200);
    QVERIFY2(portProbes <= 1, qPrintable(QString::number(portProbes)));
}

void tst_IpcBusyStates::watch_nothingListening()
{
    const quint16 port = closedPort();
    QVERIFY(port != 0);

    IpcClient client;
    CoreStartWatch watch(&client);
    watch.setCheckIntervalMs(20);
    watch.setGraceMs(300);
    watch.setProbes({{}, [] { return true; }});

    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), port);
    watch.restart();
    QTRY_COMPARE_WITH_TIMEOUT(watch.problem(), Problem::Unreachable, 3000);

    // Not trying is not a problem: nothing is overdue once the user disconnects
    client.disconnectFromDaemon();
    watch.restart();
    QTest::qWait(500);
    QCOMPARE(watch.problem(), Problem::None);
}

void tst_IpcBusyStates::watch_refusalIsReportedAtOnce()
{
    MockCore core;
    QVERIFY(core.listen());
    core.refuseWith = QStringLiteral("Invalid token");

    IpcClient client;
    CoreStartWatch watch(&client);
    watch.setGraceMs(60'000);
    watch.setCheckIntervalMs(60'000);   // not from a tick: from the refusal itself

    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    watch.restart();
    QTRY_COMPARE_WITH_TIMEOUT(watch.problem(), Problem::Refused, 3000);
}

void tst_IpcBusyStates::watch_clearsWhenTheCoreAnswers()
{
    MockCore core;
    QVERIFY(core.listen());
    core.answerHandshake = false;

    IpcClient client;
    client.setTimeouts(150, 30'000, 10'000);
    CoreStartWatch watch(&client);
    watch.setCheckIntervalMs(20);
    watch.setGraceMs(200);
    QList<Problem> reported;
    connect(&watch, &CoreStartWatch::problemChanged, this,
            [&reported](Problem problem) { reported.append(problem); });

    client.connectToDaemon(QHostAddress(QHostAddress::LocalHost), core.port());
    watch.restart();
    QTRY_COMPARE_WITH_TIMEOUT(watch.problem(), Problem::NoAnswer, 3000);

    // The core was only busy. The box comes down by itself.
    core.answerHandshake = true;
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 8000);
    QCOMPARE(watch.problem(), Problem::None);
    QCOMPARE(reported, (QList<Problem>{Problem::NoAnswer, Problem::None}));
}

QTEST_GUILESS_MAIN(tst_IpcBusyStates)
#include "tst_IpcBusyStates.moc"
