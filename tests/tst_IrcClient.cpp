/// @file tst_IrcClient.cpp
/// @brief Tests for chat/IrcClient — signal dispatch, CTCP handling, perform, connect/disconnect.

#include "TestHelpers.h"
#include "chat/IrcClient.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace eMule;

// Helper: create a local loopback server and connect an IrcClient to it.
// Returns the server-side socket for sending test data.
struct LoopbackFixture {
    QTcpServer server;
    IrcClient client;
    QTcpSocket* serverSocket = nullptr;

    bool setup()
    {
        if (!server.listen(QHostAddress::LocalHost, 0))
            return false;

        QSignalSpy connectedSpy(&client, &IrcClient::connected);
        const int port = server.serverPort();
        client.connectToServer(
            QStringLiteral("127.0.0.1:%1").arg(port),
            QStringLiteral("testNick"),
            QStringLiteral("testUser"));

        // Wait for server to accept connection
        if (!server.waitForNewConnection(3000))
            return false;
        serverSocket = server.nextPendingConnection();
        if (!serverSocket)
            return false;

        // Wait for client to emit connected()
        if (connectedSpy.isEmpty())
            connectedSpy.wait(3000);

        // Drain the login lines (USER + NICK) from the server side
        serverSocket->waitForReadyRead(1000);
        serverSocket->readAll();

        return !connectedSpy.isEmpty();
    }

    void sendToClient(const QByteArray& data)
    {
        if (serverSocket) {
            serverSocket->write(data);
            serverSocket->flush();
        }
    }

    void sendLine(const QString& line)
    {
        sendToClient((line + QStringLiteral("\r\n")).toUtf8());
    }
};

class tst_IrcClient : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void connectAndDisconnect();
    void loginSequence_sendsUserAndNick();
    void ping_autoResponds();
    void channelMessage_signal();
    void privateMessage_signal();
    void action_signal();
    void userJoined_signal();
    void userParted_signal();
    void userQuit_signal();
    void nickChanged_signal();
    void userKicked_signal();
    void topicChanged_signal();
    void modeChanged_signal();
    void notice_signal();
    void numeric001_logsIn();
    void numeric433_nickInUse();
    void names_signal();
    void ctcpVersion_autoResponse();
    void channelList_signals();
    void executePerform();
    void sendMessage_format();
    void joinChannel_format();
    void partChannel_format();
};

void tst_IrcClient::construct_default()
{
    IrcClient client;
    QVERIFY(!client.isConnected());
    QVERIFY(!client.isLoggedIn());
    QVERIFY(client.currentNick().isEmpty());
}

void tst_IrcClient::connectAndDisconnect()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());
    QVERIFY(fix.client.isConnected());

    QSignalSpy disconnSpy(&fix.client, &IrcClient::disconnected);
    fix.client.disconnect();
    QVERIFY(!fix.client.isConnected());
    QVERIFY(!disconnSpy.isEmpty());
}

void tst_IrcClient::loginSequence_sendsUserAndNick()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    IrcClient client;
    QSignalSpy connectedSpy(&client, &IrcClient::connected);
    client.connectToServer(
        QStringLiteral("127.0.0.1:%1").arg(server.serverPort()),
        QStringLiteral("myNick"),
        QStringLiteral("myIdent"));

    QVERIFY(server.waitForNewConnection(3000));
    auto* sock = server.nextPendingConnection();
    QVERIFY(sock);

    if (connectedSpy.isEmpty())
        connectedSpy.wait(3000);

    // Read what the client sent — QTRY_VERIFY ensures the event loop flushes
    QTRY_VERIFY(sock->bytesAvailable() > 0);
    const QString sent = QString::fromUtf8(sock->readAll());
    QVERIFY(sent.contains(QStringLiteral("USER myIdent")));
    QVERIFY(sent.contains(QStringLiteral("NICK myNick")));

    client.disconnect();
}

void tst_IrcClient::ping_autoResponds()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    fix.sendLine(QStringLiteral("PING :irc.server.net"));
    QTest::qWait(200);

    const QString response = QString::fromUtf8(fix.serverSocket->readAll());
    QVERIFY(response.contains(QStringLiteral("PONG")));
    QVERIFY(response.contains(QStringLiteral("irc.server.net")));

    fix.client.disconnect();
}

void tst_IrcClient::channelMessage_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::channelMessageReceived);
    fix.sendLine(QStringLiteral(":Alice!a@h PRIVMSG #emule :Hello world"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("#emule"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("Alice"));
    QCOMPARE(spy[0][2].toString(), QStringLiteral("Hello world"));

    fix.client.disconnect();
}

void tst_IrcClient::privateMessage_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::privateMessageReceived);
    fix.sendLine(QStringLiteral(":Bob!b@h PRIVMSG testNick :secret"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("Bob"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("secret"));

    fix.client.disconnect();
}

void tst_IrcClient::action_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::actionReceived);
    fix.sendLine(QStringLiteral(":Alice!a@h PRIVMSG #chan :\001ACTION waves\001"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("#chan"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("Alice"));
    QCOMPARE(spy[0][2].toString(), QStringLiteral("waves"));

    fix.client.disconnect();
}

void tst_IrcClient::userJoined_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::userJoined);
    fix.sendLine(QStringLiteral(":Alice!a@h JOIN :#channel"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("#channel"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("Alice"));

    fix.client.disconnect();
}

void tst_IrcClient::userParted_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::userParted);
    fix.sendLine(QStringLiteral(":Alice!a@h PART #channel :Bye all"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("#channel"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("Alice"));
    QCOMPARE(spy[0][2].toString(), QStringLiteral("Bye all"));

    fix.client.disconnect();
}

void tst_IrcClient::userQuit_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::userQuit);
    fix.sendLine(QStringLiteral(":Alice!a@h QUIT :Leaving"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("Alice"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("Leaving"));

    fix.client.disconnect();
}

void tst_IrcClient::nickChanged_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::nickChanged);
    fix.sendLine(QStringLiteral(":OldNick!u@h NICK :NewNick"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("OldNick"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("NewNick"));

    fix.client.disconnect();
}

void tst_IrcClient::userKicked_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::userKicked);
    fix.sendLine(QStringLiteral(":op!u@h KICK #chan baduser :Spam"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("#chan"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("baduser"));
    QCOMPARE(spy[0][2].toString(), QStringLiteral("op"));
    QCOMPARE(spy[0][3].toString(), QStringLiteral("Spam"));

    fix.client.disconnect();
}

void tst_IrcClient::topicChanged_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::topicChanged);
    fix.sendLine(QStringLiteral(":nick!u@h TOPIC #chan :New topic"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("#chan"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("nick"));
    QCOMPARE(spy[0][2].toString(), QStringLiteral("New topic"));

    fix.client.disconnect();
}

void tst_IrcClient::modeChanged_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::modeChanged);
    fix.sendLine(QStringLiteral(":nick!u@h MODE #chan +o someuser"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("#chan"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("nick"));
    QCOMPARE(spy[0][2].toString(), QStringLiteral("+o"));
    QCOMPARE(spy[0][3].toString(), QStringLiteral("someuser"));

    fix.client.disconnect();
}

void tst_IrcClient::notice_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::noticeReceived);
    fix.sendLine(QStringLiteral(":server.net NOTICE * :Welcome"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("server.net"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("*"));
    QCOMPARE(spy[0][2].toString(), QStringLiteral("Welcome"));

    fix.client.disconnect();
}

void tst_IrcClient::numeric001_logsIn()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());
    QVERIFY(!fix.client.isLoggedIn());

    QSignalSpy spy(&fix.client, &IrcClient::loggedIn);
    fix.sendLine(QStringLiteral(":server 001 testNick :Welcome to IRC"));
    QTRY_COMPARE(spy.count(), 1);
    QVERIFY(fix.client.isLoggedIn());

    fix.client.disconnect();
}

void tst_IrcClient::numeric433_nickInUse()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::nickInUse);
    fix.sendLine(QStringLiteral(":server 433 * testNick :Nickname is already in use"));
    QTRY_COMPARE(spy.count(), 1);

    fix.client.disconnect();
}

void tst_IrcClient::names_signal()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy namesSpy(&fix.client, &IrcClient::namesReceived);
    QSignalSpy endSpy(&fix.client, &IrcClient::namesFinished);

    fix.sendLine(QStringLiteral(":server 353 testNick = #chan :@op +voice regular"));
    QTRY_COMPARE(namesSpy.count(), 1);

    const QStringList nicks = namesSpy[0][1].toStringList();
    QCOMPARE(nicks.size(), 3);
    QVERIFY(nicks.contains(QStringLiteral("@op")));
    QVERIFY(nicks.contains(QStringLiteral("+voice")));
    QVERIFY(nicks.contains(QStringLiteral("regular")));

    fix.sendLine(QStringLiteral(":server 366 testNick #chan :End of NAMES list"));
    QTRY_COMPARE(endSpy.count(), 1);
    QCOMPARE(endSpy[0][0].toString(), QStringLiteral("#chan"));

    // Should also have sent a MODE request
    fix.serverSocket->waitForReadyRead(1000);
    const QString sent = QString::fromUtf8(fix.serverSocket->readAll());
    QVERIFY(sent.contains(QStringLiteral("MODE #chan")));

    fix.client.disconnect();
}

void tst_IrcClient::ctcpVersion_autoResponse()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy spy(&fix.client, &IrcClient::ctcpRequestReceived);
    fix.sendLine(QStringLiteral(":Alice!a@h PRIVMSG testNick :\001VERSION\001"));
    QTRY_COMPARE(spy.count(), 1);

    QCOMPARE(spy[0][0].toString(), QStringLiteral("Alice"));
    QCOMPARE(spy[0][1].toString(), QStringLiteral("VERSION"));

    // Server should have received a NOTICE with VERSION reply
    fix.serverSocket->waitForReadyRead(500);
    const QString response = QString::fromUtf8(fix.serverSocket->readAll());
    QVERIFY(response.contains(QStringLiteral("NOTICE Alice")));
    QVERIFY(response.contains(QStringLiteral("VERSION")));

    fix.client.disconnect();
}

void tst_IrcClient::channelList_signals()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy startSpy(&fix.client, &IrcClient::channelListStarted);
    QSignalSpy listSpy(&fix.client, &IrcClient::channelListed);
    QSignalSpy endSpy(&fix.client, &IrcClient::channelListFinished);

    fix.sendLine(QStringLiteral(":server 321 testNick Channel :Users Name"));
    QTRY_COMPARE(startSpy.count(), 1);

    fix.sendLine(QStringLiteral(":server 322 testNick #emule 42 :eMule help channel"));
    QTRY_COMPARE(listSpy.count(), 1);
    QCOMPARE(listSpy[0][0].toString(), QStringLiteral("#emule"));
    QCOMPARE(listSpy[0][1].toInt(), 42);
    QCOMPARE(listSpy[0][2].toString(), QStringLiteral("eMule help channel"));

    fix.sendLine(QStringLiteral(":server 323 testNick :End of LIST"));
    QTRY_COMPARE(endSpy.count(), 1);

    fix.client.disconnect();
}

void tst_IrcClient::executePerform()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy sentSpy(&fix.client, &IrcClient::rawLineSent);

    fix.client.executePerform(QStringLiteral("/join #channel1|/join #channel2"));
    QTRY_VERIFY(sentSpy.count() >= 2);

    bool foundChan1 = false, foundChan2 = false;
    for (const auto& call : sentSpy) {
        const QString line = call[0].toString();
        if (line.contains(QStringLiteral("#channel1")))
            foundChan1 = true;
        if (line.contains(QStringLiteral("#channel2")))
            foundChan2 = true;
    }
    QVERIFY(foundChan1);
    QVERIFY(foundChan2);

    fix.client.disconnect();
}

void tst_IrcClient::sendMessage_format()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy sentSpy(&fix.client, &IrcClient::rawLineSent);
    fix.client.sendMessage(QStringLiteral("#test"), QStringLiteral("hello"));

    QTRY_COMPARE(sentSpy.count(), 1);
    QCOMPARE(sentSpy[0][0].toString(),
             QStringLiteral("PRIVMSG #test :hello"));

    fix.client.disconnect();
}

void tst_IrcClient::joinChannel_format()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy sentSpy(&fix.client, &IrcClient::rawLineSent);
    fix.client.joinChannel(QStringLiteral("#emule"));

    QTRY_COMPARE(sentSpy.count(), 1);
    QCOMPARE(sentSpy[0][0].toString(), QStringLiteral("JOIN #emule"));

    fix.client.disconnect();
}

void tst_IrcClient::partChannel_format()
{
    LoopbackFixture fix;
    QVERIFY(fix.setup());

    QSignalSpy sentSpy(&fix.client, &IrcClient::rawLineSent);
    fix.client.partChannel(QStringLiteral("#emule"), QStringLiteral("Goodbye"));

    QTRY_COMPARE(sentSpy.count(), 1);
    QCOMPARE(sentSpy[0][0].toString(), QStringLiteral("PART #emule :Goodbye"));

    fix.client.disconnect();
}

QTEST_GUILESS_MAIN(tst_IrcClient)
#include "tst_IrcClient.moc"
