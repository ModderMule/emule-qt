/// @file tst_HttpClientReqSocket.cpp
/// @brief Tests for HttpClientReqSocket — HTTP over EMSocket.

#include "TestHelpers.h"
#include "net/HttpClientReqSocket.h"
#include "utils/Opcodes.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>

#include <cstring>

using namespace eMule;

class tst_HttpClientReqSocket : public QObject {
    Q_OBJECT

private slots:
    void constructionDefaults();
    void rawDataModeEnabled();
    void httpStateTransitions();
    void parseSimpleHttpResponse();
    void httpDownSocketConstruction();
};

// ---------------------------------------------------------------------------
// Test: construction defaults
// ---------------------------------------------------------------------------

void tst_HttpClientReqSocket::constructionDefaults()
{
    HttpClientReqSocket sock;
    QVERIFY(sock.isRawDataMode());
    QCOMPARE(sock.httpState(), HttpSocketState::RecvExpected);
    QVERIFY(sock.httpHeaders().empty());
}

// ---------------------------------------------------------------------------
// Test: raw data mode is always on
// ---------------------------------------------------------------------------

void tst_HttpClientReqSocket::rawDataModeEnabled()
{
    HttpClientReqSocket sock;
    QVERIFY(sock.isRawDataMode());
}

// ---------------------------------------------------------------------------
// Test: HTTP state transitions
// ---------------------------------------------------------------------------

void tst_HttpClientReqSocket::httpStateTransitions()
{
    HttpClientReqSocket sock;
    QCOMPARE(sock.httpState(), HttpSocketState::RecvExpected);

    sock.setHttpState(HttpSocketState::RecvHeaders);
    QCOMPARE(sock.httpState(), HttpSocketState::RecvHeaders);

    sock.setHttpState(HttpSocketState::RecvBody);
    QCOMPARE(sock.httpState(), HttpSocketState::RecvBody);

    sock.clearHttpHeaders();
    QVERIFY(sock.httpHeaders().empty());
}

// ---------------------------------------------------------------------------
// Test: parse simple HTTP response over loopback
// ---------------------------------------------------------------------------

void tst_HttpClientReqSocket::parseSimpleHttpResponse()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    HttpClientReqSocket clientSocket;
    QSignalSpy responseSpy(&clientSocket, &HttpClientReqSocket::httpResponseReceived);
    QSignalSpy bodySpy(&clientSocket, &HttpClientReqSocket::httpBodyDataReceived);
    QVERIFY(responseSpy.isValid());
    QVERIFY(bodySpy.isValid());

    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // Send HTTP response
    QByteArray response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "Hello";

    serverSide->write(response);
    serverSide->flush();

    QTRY_COMPARE_WITH_TIMEOUT(responseSpy.count(), 1, 3000);
    QCOMPARE(responseSpy.first().at(0).toInt(), 200);

    // Body data should also arrive
    QTRY_VERIFY_WITH_TIMEOUT(bodySpy.count() >= 1, 3000);

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Test: HttpClientDownSocket construction
// ---------------------------------------------------------------------------

void tst_HttpClientReqSocket::httpDownSocketConstruction()
{
    HttpClientDownSocket downSock;
    QVERIFY(downSock.isRawDataMode());
    QCOMPARE(downSock.httpState(), HttpSocketState::RecvExpected);
}

QTEST_MAIN(tst_HttpClientReqSocket)
#include "tst_HttpClientReqSocket.moc"
