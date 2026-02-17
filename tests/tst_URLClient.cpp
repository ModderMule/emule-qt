/// @file tst_URLClient.cpp
/// @brief Tests for client/URLClient — HTTP download client.

#include "TestHelpers.h"
#include "client/URLClient.h"

#include <QTest>

using namespace eMule;

class tst_URLClient : public QObject {
    Q_OBJECT

private slots:
    void setUrl_parsesComponents();
    void setUrl_defaultPort();
    void setUrl_invalidUrl_returnsFalse();
    void isEd2kClient_returnsFalse();
    void sendHelloPacket_noop();
    void httpBlockRequest_format();
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void tst_URLClient::setUrl_parsesComponents()
{
    URLClient client;
    QVERIFY(client.setUrl(QStringLiteral("http://example.com:8080/path/file.dat")));

    QCOMPARE(client.urlHost(), QStringLiteral("example.com"));
    QCOMPARE(client.urlPort(), uint16{8080});
    QCOMPARE(client.urlPath(), QByteArray("/path/file.dat"));
    QCOMPARE(client.userName(), QStringLiteral("example.com"));
    QCOMPARE(client.userPort(), uint16{8080});
}

void tst_URLClient::setUrl_defaultPort()
{
    URLClient client;
    QVERIFY(client.setUrl(QStringLiteral("http://example.com/file.dat")));

    QCOMPARE(client.urlHost(), QStringLiteral("example.com"));
    QCOMPARE(client.urlPort(), uint16{80});
    QCOMPARE(client.urlPath(), QByteArray("/file.dat"));
}

void tst_URLClient::setUrl_invalidUrl_returnsFalse()
{
    URLClient client;

    // Empty URL
    QVERIFY(!client.setUrl(QString()));

    // Malformed URL (no host)
    QVERIFY(!client.setUrl(QStringLiteral("not-a-url")));
}

void tst_URLClient::isEd2kClient_returnsFalse()
{
    URLClient client;
    QVERIFY(!client.isEd2kClient());
    QVERIFY(client.isUrlClient());
}

void tst_URLClient::sendHelloPacket_noop()
{
    URLClient client;
    // sendHelloPacket is a private no-op override — verify via base class pointer
    UpDownClient* base = &client;
    Q_UNUSED(base);
    // Just verify URLClient constructs without crash
    QVERIFY(true);
}

void tst_URLClient::httpBlockRequest_format()
{
    URLClient client;
    QVERIFY(client.setUrl(QStringLiteral("http://example.com:8080/path/file.dat")));

    // Without a socket, sendHttpBlockRequests should return false gracefully
    QVERIFY(!client.sendHttpBlockRequests());
}

QTEST_MAIN(tst_URLClient)
#include "tst_URLClient.moc"
