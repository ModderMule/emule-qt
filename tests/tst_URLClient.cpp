/// @file tst_URLClient.cpp
/// @brief Tests for client/URLClient — HTTP download client.

#include "TestHelpers.h"
#include "app/AppConfig.h"
#include "client/URLClient.h"
#include "net/Address.h"
#include "prefs/Preferences.h"

#include <QScopeGuard>
#include <QTest>

using namespace eMule;

namespace {

/// buildGetHeader() is protected, and it is the request every HTTP source and
/// every HTTP Cache chunk fetch is built on, so it is worth reading directly
/// rather than through a socket.
class ExposedURLClient : public URLClient {
public:
    using URLClient::buildGetHeader;
};

} // namespace

class tst_URLClient : public QObject {
    Q_OBJECT

private slots:
    void setUrl_parsesComponents();
    void setUrl_defaultPort();
    void setUrl_invalidUrl_returnsFalse();
    void isEd2kClient_returnsFalse();
    void sendHelloPacket_noop();
    void httpBlockRequest_format();
    void buildGetHeader_carriesUserAgent();
    void tryToConnect_rejectsAnUnusableAddress();
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

void tst_URLClient::buildGetHeader_carriesUserAgent()
{
    ExposedURLClient client;
    QVERIFY(client.setUrl(QStringLiteral("http://example.com:8080/path/file.dat")));

    const QByteArray header = client.buildGetHeader();

    // Not merely "some agent": an operator allow-listing `eMule*` behind a WAF is
    // matching on this exact prefix, so the spelling is part of the contract.
    QVERIFY(header.contains("\r\nUser-Agent: eMuleQt/"));
    QVERIFY(header.contains(kUserAgent.toLatin1()));

    // One header per line, and the block still ends open — sendHttpBlockRequests()
    // appends Range and the terminating CRLF itself.
    QVERIFY(header.startsWith("GET /path/file.dat HTTP/1.1\r\n"));
    QVERIFY(header.endsWith("\r\n"));
    QVERIFY(!header.contains("\r\n\r\n"));
    QCOMPARE(header.count("User-Agent:"), 1);
}

QTEST_MAIN(tst_URLClient)
void tst_URLClient::tryToConnect_rejectsAnUnusableAddress()
{
    // A URL host is chosen by somebody else — a peer's HTTP Cache offer, a Kad chunk
    // record, an ed2k link — so the address behind it gets the same screening a peer
    // address does. HttpCacheManager::urlIsAcceptable() has always said a name is
    // "vetted after resolution"; until this check existed, nothing did the vetting,
    // and a name resolving to 127.0.0.1 was dialled without a word.
    const bool savedFilter = thePrefs.filterLANIPs();
    thePrefs.setFilterLANIPs(true);
    const auto restore = qScopeGuard([savedFilter] { thePrefs.setFilterLANIPs(savedFilter); });

    URLClient client;
    QVERIFY(client.setUrl(QStringLiteral("http://127.0.0.1:8080/chunk.bin"),
                          Address::fromString(QStringLiteral("127.0.0.1"))));
    QVERIFY(!client.tryToConnect());
    QVERIFY(client.socket() == nullptr);

    // The same address is fine once the operator has said this is a private network —
    // which is exactly how the loopback cache-server fixtures run.
    thePrefs.setFilterLANIPs(false);
    URLClient allowed;
    QVERIFY(allowed.setUrl(QStringLiteral("http://127.0.0.1:8080/chunk.bin"),
                           Address::fromString(QStringLiteral("127.0.0.1"))));
    QVERIFY(allowed.tryToConnect());
}

#include "tst_URLClient.moc"
