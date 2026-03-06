/// @file tst_IpcConnection.cpp
/// @brief Unit tests for IpcConnection framed TCP communication.

#include "IpcConnection.h"

#include <QtEndian>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace eMule::Ipc;

class tst_IpcConnection : public QObject {
    Q_OBJECT

private slots:
    void sendAndReceive_singleMessage();
    void sendAndReceive_multipleMessages();
    void disconnected_signal();
    void protocolError_oversizedFrame();
    void isConnected_state();
};

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

QTEST_GUILESS_MAIN(tst_IpcConnection)
#include "tst_IpcConnection.moc"
