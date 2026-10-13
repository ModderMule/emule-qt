/// @file tst_IpcConnection.cpp
/// @brief Unit tests for IpcConnection framed TCP communication.

#include "IpcConnection.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QtEndian>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QThread>
#include <QTimer>

using namespace eMule::Ipc;

class tst_IpcConnection : public QObject {
    Q_OBJECT

private slots:
    void sendAndReceive_singleMessage();
    void sendAndReceive_multipleMessages();
    void disconnected_signal();
    void protocolError_oversizedFrame();
    void isConnected_state();

    // Busy states: a backlog on the read side, a peer that stopped reading
    void backlog_yieldsToEventLoop_data();
    void backlog_yieldsToEventLoop();
    void backlog_keepsNewestSnapshotPush();
    void nestedEventLoop_holdsDispatchAndReports();
    void slowReader_snapshotPushesAreHeld();
    void stuckReader_isDropped();
};

namespace {

/// One PushStatsUpdate carrying @p value, so a test can tell which one arrived.
IpcMessage statsPush(int value)
{
    IpcMessage msg(IpcMsgType::PushStatsUpdate, 0);
    msg.append(qint64(value));
    return msg;
}

/// A reply too big for a socket buffer to swallow many of.
IpcMessage bulkyReply(int seqId)
{
    return IpcMessage::makeResult(seqId, true, QString(256 * 1024, QLatin1Char('x')));
}

/// A server-side IpcConnection whose peer is a bare socket the test controls:
/// it reads only when told to, like a GUI that is busy elsewhere.
struct HalfPair {
    std::unique_ptr<IpcConnection> server;
    std::unique_ptr<QTcpSocket> peer;
};

HalfPair createHalfPair()
{
    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::LocalHost, 0))
        return {};

    auto peer = std::make_unique<QTcpSocket>();
    // Qt stops draining the kernel buffer once this much is waiting unread
    peer->setReadBufferSize(1024);
    peer->connectToHost(QHostAddress::LocalHost, tcpServer.serverPort());
    if (!tcpServer.waitForNewConnection(3000) || !peer->waitForConnected(3000))
        return {};

    QTcpSocket* serverSocket = tcpServer.nextPendingConnection();
    if (!serverSocket)
        return {};
    return {std::make_unique<IpcConnection>(serverSocket), std::move(peer)};
}

/// Longest stretch the event loop went without a turn while @p done was false.
class LoopGapProbe : public QObject {
public:
    LoopGapProbe()
    {
        m_timer.setInterval(0);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            m_longest = std::max(m_longest, m_sinceTurn.restart());
        });
        m_sinceTurn.start();
        m_timer.start();
    }

    [[nodiscard]] qint64 longestMs()
    {
        m_longest = std::max(m_longest, m_sinceTurn.elapsed());
        return m_longest;
    }

private:
    QTimer m_timer;
    QElapsedTimer m_sinceTurn;
    qint64 m_longest = 0;
};

} // namespace

/// Helper: set up a TCP server/client pair and return IpcConnections for both.
struct ConnectionPair {
    std::unique_ptr<IpcConnection> server;
    std::unique_ptr<IpcConnection> client;
};

static ConnectionPair createPair()
{
    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::LocalHost, 0))
        return {};
    const quint16 port = tcpServer.serverPort();

    auto* clientSocket = new QTcpSocket;
    clientSocket->connectToHost(QHostAddress::LocalHost, port);

    if (!tcpServer.waitForNewConnection(3000))
        return {};
    if (!clientSocket->waitForConnected(3000))
        return {};

    QTcpSocket* serverSocket = tcpServer.nextPendingConnection();
    if (!serverSocket)
        return {};

    return {
        std::make_unique<IpcConnection>(serverSocket),
        std::make_unique<IpcConnection>(clientSocket)
    };
}

void tst_IpcConnection::sendAndReceive_singleMessage()
{
    auto [server, client] = createPair();
    QVERIFY(server);
    QVERIFY(client);

    QSignalSpy spy(server.get(), &IpcConnection::messageReceived);

    IpcMessage msg(IpcMsgType::Handshake, 1);
    msg.append(QStringLiteral("1.0"));
    client->sendMessage(msg);

    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.count(), 1);

    const auto received = spy.at(0).at(0).value<IpcMessage>();
    QCOMPARE(received.type(), IpcMsgType::Handshake);
    QCOMPARE(received.seqId(), 1);
    QCOMPARE(received.fieldString(0), QStringLiteral("1.0"));
}

void tst_IpcConnection::sendAndReceive_multipleMessages()
{
    auto [server, client] = createPair();
    QVERIFY(server);
    QVERIFY(client);

    QSignalSpy spy(server.get(), &IpcConnection::messageReceived);

    // Send 3 messages rapidly
    for (int i = 0; i < 3; ++i) {
        IpcMessage msg(IpcMsgType::GetDownloads, i + 1);
        client->sendMessage(msg);
    }

    // Wait for all 3
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 3, 3000);

    for (int i = 0; i < 3; ++i) {
        const auto received = spy.at(i).at(0).value<IpcMessage>();
        QCOMPARE(received.type(), IpcMsgType::GetDownloads);
        QCOMPARE(received.seqId(), i + 1);
    }
}

void tst_IpcConnection::disconnected_signal()
{
    auto [server, client] = createPair();
    QVERIFY(server);
    QVERIFY(client);

    QSignalSpy spy(server.get(), &IpcConnection::disconnected);

    client->close();

    QVERIFY(spy.wait(3000));
    // May fire more than once (disconnect + error), just verify at least one
    QVERIFY(spy.count() >= 1);
}

void tst_IpcConnection::protocolError_oversizedFrame()
{
    auto [server, client] = createPair();
    QVERIFY(server);
    QVERIFY(client);

    QSignalSpy errorSpy(server.get(), &IpcConnection::protocolError);

    // Send a raw frame with oversized length header
    uint8_t header[4];
    qToBigEndian<uint32_t>(MaxPayloadSize + 1, header);
    QByteArray badFrame(reinterpret_cast<const char*>(header), 4);
    // Add some dummy data so readyRead fires
    badFrame.append(QByteArray(64, 'x'));
    client->socket()->write(badFrame);

    QVERIFY(errorSpy.wait(3000));
    QCOMPARE(errorSpy.count(), 1);
}

void tst_IpcConnection::isConnected_state()
{
    auto [server, client] = createPair();
    QVERIFY(server);
    QVERIFY(client);

    QVERIFY(server->isConnected());
    QVERIFY(client->isConnected());

    client->close();
    QTRY_VERIFY_WITH_TIMEOUT(!client->isConnected(), 3000);
}

void tst_IpcConnection::backlog_yieldsToEventLoop_data()
{
    QTest::addColumn<int>("budgetMs");
    QTest::addColumn<bool>("yields");

    QTest::newRow("budgeted") << 15 << true;
    // The old behaviour, kept as the control: without it the row above proves nothing
    QTest::newRow("unbounded") << 0 << false;
}

void tst_IpcConnection::backlog_yieldsToEventLoop()
{
    QFETCH(int, budgetMs);
    QFETCH(bool, yields);

    auto [server, client] = createPair();
    QVERIFY(server);
    QVERIFY(client);
    server->setDispatchBudgetMs(budgetMs);

    // A handler that costs a millisecond, as applying a list reply does, and a
    // backlog of them — what a GUI finds waiting after the machine was busy.
    constexpr int kFrames = 400;
    int received = 0;
    connect(server.get(), &IpcConnection::messageReceived, server.get(), [&received] {
        ++received;
        QThread::msleep(1);
    });

    QByteArray backlog;
    for (int i = 0; i < kFrames; ++i)
        backlog += IpcMessage(IpcMsgType::GetDownloads, i + 1).toFrame();
    client->socket()->write(backlog);
    // Into the kernel and across, without a turn of the event loop: the server
    // finds the whole backlog waiting on its first read, as after a stall.
    QVERIFY(client->socket()->waitForBytesWritten(3000));
    QThread::msleep(100);
    QCOMPARE(received, 0);

    LoopGapProbe probe;
    QTRY_COMPARE_WITH_TIMEOUT(received, kFrames, 10000);

    // 400 ms of handler time in total. Dispatched in one go that is 400 ms without
    // input or paint; budgeted, the loop turns every few milliseconds.
    if (yields)
        QVERIFY2(probe.longestMs() < 150, qPrintable(QString::number(probe.longestMs())));
    else
        QVERIFY2(probe.longestMs() > 150, qPrintable(QString::number(probe.longestMs())));
}

void tst_IpcConnection::backlog_keepsNewestSnapshotPush()
{
    auto [server, client] = createPair();
    QVERIFY(server);
    QVERIFY(client);
    server->setCoalesceIncoming(true);

    QList<IpcMessage> received;
    connect(server.get(), &IpcConnection::messageReceived, server.get(),
            [&received](const IpcMessage& msg) {
        // The first one holds the loop while the rest piles up behind it
        if (received.isEmpty())
            QThread::msleep(100);
        received.append(msg);
    });

    client->sendMessage(IpcMessage(IpcMsgType::PushServerMessage, 0));   // the slow first
    constexpr int kStats = 500;
    QByteArray backlog;
    for (int i = 1; i <= kStats; ++i) {
        backlog += statsPush(i).toFrame();
        // Events and replies in between: none of these may be dropped or reordered
        if (i % 100 == 0) {
            IpcMessage event(IpcMsgType::PushNotifierEvent, 0);
            event.append(qint64(i));
            backlog += event.toFrame();
            backlog += IpcMessage::makeResult(i, true).toFrame();
        }
    }
    client->socket()->write(backlog);

    QTRY_VERIFY_WITH_TIMEOUT(!received.isEmpty()
        && received.last().type() == IpcMsgType::Result && received.last().seqId() == kStats, 5000);
    QTest::qWait(50);

    QList<qint64> stats, events, replies;
    for (const IpcMessage& msg : std::as_const(received)) {
        if (msg.type() == IpcMsgType::PushStatsUpdate)
            stats.append(msg.fieldInt(0));
        else if (msg.type() == IpcMsgType::PushNotifierEvent)
            events.append(msg.fieldInt(0));
        else if (msg.type() == IpcMsgType::Result)
            replies.append(msg.seqId());
    }

    // Every event and every reply, in order
    QCOMPARE(events, (QList<qint64>{100, 200, 300, 400, 500}));
    QCOMPARE(replies, (QList<qint64>{100, 200, 300, 400, 500}));

    // Of the 500 states only a handful: each one delivered was the newest when it
    // went out, and the last one delivered is the last one sent.
    QVERIFY2(stats.size() <= 20, qPrintable(QString::number(stats.size())));
    QCOMPARE(stats.last(), qint64(kStats));
    QVERIFY(std::is_sorted(stats.cbegin(), stats.cend()));
    QCOMPARE(server->coalescedFrames() + static_cast<quint64>(stats.size()), quint64(kStats));

    // The newest state keeps its own place in the stream: after the reply before it
    QCOMPARE(received.at(received.size() - 3).type(), IpcMsgType::PushStatsUpdate);
}

void tst_IpcConnection::nestedEventLoop_holdsDispatchAndReports()
{
    auto [server, client] = createPair();
    QVERIFY(server);
    QVERIFY(client);

    server->setStallNoticeMs(100);
    QSignalSpy blockedSpy(server.get(), &IpcConnection::dispatchBlocked);

    QList<int> order;
    bool pendingSeenInside = false;
    int deliveredInside = -1;
    connect(server.get(), &IpcConnection::messageReceived, server.get(),
            [&](const IpcMessage& msg) {
        order.append(msg.seqId());
        if (msg.seqId() != 1)
            return;
        // What a modal dialog opened by a reply handler does, with the core
        // carrying on talking underneath it
        QEventLoop dialog;
        QTimer::singleShot(50, client.get(), [&client] {
            client->sendMessage(IpcMessage(IpcMsgType::GetDownloads, 2));
            client->sendMessage(IpcMessage(IpcMsgType::GetDownloads, 3));
        });
        QTimer::singleShot(300, &dialog, &QEventLoop::quit);
        dialog.exec();
        pendingSeenInside = server->hasPendingInput();
        deliveredInside = static_cast<int>(order.size());
    });

    client->sendMessage(IpcMessage(IpcMsgType::GetDownloads, 1));
    QTRY_COMPARE_WITH_TIMEOUT(order, (QList<int>{1, 2, 3}), 3000);

    // Nothing ran inside the first handler, the wait was visible from outside, and
    // it was said once — the GUI raises the dialog on that signal.
    QCOMPARE(deliveredInside, 1);
    QVERIFY(pendingSeenInside);
    QCOMPARE(blockedSpy.count(), 1);

    // An ordinary slow handler runs no event loop and is not a stall
    QTest::qWait(250);
    QCOMPARE(blockedSpy.count(), 1);
}

void tst_IpcConnection::slowReader_snapshotPushesAreHeld()
{
    auto [server, peer] = createHalfPair();
    QVERIFY(server);
    QVERIFY(peer);
    constexpr qint64 kSoft = 64 * 1024;
    server->setWriteLimits(kSoft, 0);

    // The peer reads nothing: replies pile up until the kernel takes no more
    int seq = 0;
    for (int i = 0; i < 400 && server->pendingWriteBytes() <= kSoft; ++i) {
        QCOMPARE(server->sendMessage(bulkyReply(++seq)), IpcConnection::SendResult::Sent);
        QCoreApplication::processEvents();
    }
    QVERIFY2(server->pendingWriteBytes() > kSoft, "the peer's buffers never filled");
    const qint64 backlogBefore = server->pendingWriteBytes();

    // State pushes are not added to the pile, and only the newest is kept
    for (int i = 1; i <= 50; ++i)
        QCOMPARE(server->sendPush(statsPush(i)), IpcConnection::SendResult::Held);
    QCOMPARE(server->heldPushes(), 1);
    QCOMPARE(server->pendingWriteBytes(), backlogBefore);

    // An event is never held: it happened, and the GUI has to hear of it
    IpcMessage event(IpcMsgType::PushNotifierEvent, 0);
    QCOMPARE(server->sendPush(event), IpcConnection::SendResult::Sent);

    // The peer catches up: the one held state follows, carrying the newest value
    peer->setReadBufferSize(0);
    QByteArray stream;
    QList<IpcMessage> got;
    const auto pump = [&] {
        stream += peer->readAll();
        while (auto frame = tryDecodeFrame(stream)) {
            stream.remove(0, frame->bytesConsumed);
            got.append(IpcMessage(std::move(frame->message)));
        }
    };
    QTRY_VERIFY_WITH_TIMEOUT((pump(), !got.isEmpty()
        && got.last().type() == IpcMsgType::PushStatsUpdate), 10000);

    QCOMPARE(server->heldPushes(), 0);
    QCOMPARE(got.last().fieldInt(0), qint64(50));
    QCOMPARE(std::count_if(got.cbegin(), got.cend(), [](const IpcMessage& msg) {
        return msg.type() == IpcMsgType::PushStatsUpdate; }), 1);
    QCOMPARE(std::count_if(got.cbegin(), got.cend(), [](const IpcMessage& msg) {
        return msg.type() == IpcMsgType::Result; }), seq);
}

void tst_IpcConnection::stuckReader_isDropped()
{
    auto [server, peer] = createHalfPair();
    QVERIFY(server);
    QVERIFY(peer);
    constexpr qint64 kHard = 1024 * 1024;
    server->setWriteLimits(64 * 1024, kHard);

    QSignalSpy errorSpy(server.get(), &IpcConnection::protocolError);
    QSignalSpy goneSpy(server.get(), &IpcConnection::disconnected);

    // A GUI that has hung for good. Unbounded, this loop grew the daemon by
    // 256 KiB a turn for as long as it ran.
    auto result = IpcConnection::SendResult::Sent;
    qint64 peak = 0;
    for (int i = 0; i < 400 && result == IpcConnection::SendResult::Sent; ++i) {
        result = server->sendMessage(bulkyReply(i + 1));
        peak = std::max(peak, server->pendingWriteBytes());
        QCoreApplication::processEvents();
    }

    QCOMPARE(result, IpcConnection::SendResult::Overflow);
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(goneSpy.count() >= 1);
    QVERIFY(!server->isConnected());
    QVERIFY2(peak < kHard + 2 * 256 * 1024 + 64 * 1024, qPrintable(QString::number(peak)));

    // Dropped means dropped: nothing more is queued for it
    QCOMPARE(server->sendMessage(bulkyReply(1)), IpcConnection::SendResult::Closed);
    QCOMPARE(server->sendPush(statsPush(1)), IpcConnection::SendResult::Closed);
}

QTEST_GUILESS_MAIN(tst_IpcConnection)
#include "tst_IpcConnection.moc"
