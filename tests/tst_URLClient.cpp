/// @file tst_URLClient.cpp
/// @brief Tests for client/URLClient — HTTP download client.

#include "TestHelpers.h"
#include "app/AppConfig.h"
#include "client/URLClient.h"
#include "files/PartFile.h"
#include "net/Address.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
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

/// Minimal range-serving web server. "/moved" answers 302 to "/file".
class RangeServer : public QObject {
public:
    explicit RangeServer(QByteArray content) : m_content(std::move(content))
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* sock = m_server.nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, this, [this, sock] { serve(sock); });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
    }

    [[nodiscard]] uint16 port() const { return m_server.serverPort(); }
    QList<QByteArray> ranges;   ///< every Range value asked for, in order
    int redirects = 0;

private:
    void serve(QTcpSocket* sock)
    {
        QByteArray& in = m_pending[sock];
        in += sock->readAll();
        for (qsizetype end; (end = in.indexOf("\r\n\r\n")) >= 0;) {
            const QByteArray head = in.left(end);
            in.remove(0, end + 4);

            if (head.startsWith("GET /moved ")) {
                ++redirects;
                sock->write("HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:"
                            + QByteArray::number(port()) + "/file\r\nContent-Length: 0\r\n\r\n");
                continue;
            }
            const qsizetype at = head.indexOf("Range: bytes=");
            const QByteArray range = head.mid(at + 13, head.indexOf("\r\n", at) - at - 13);
            ranges.append(range);
            const qsizetype dash = range.indexOf('-');
            const qint64 first = range.left(dash).toLongLong();
            const qint64 last = range.mid(dash + 1).toLongLong();
            sock->write("HTTP/1.1 206 Partial Content\r\nContent-Length: "
                        + QByteArray::number(last - first + 1) + "\r\nContent-Range: bytes "
                        + range + '/' + QByteArray::number(m_content.size()) + "\r\n\r\n");
            sock->write(m_content.mid(first, last - first + 1));
        }
    }

    QTcpServer m_server;
    QByteArray m_content;
    QHash<QTcpSocket*, QByteArray> m_pending;
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
    void fetch_downloadsTheFileOverHttp_data();
    void fetch_downloadsTheFileOverHttp();
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

// A plain URL source (an http source of an ed2k link) used to send no request at all:
// it had no part status, so no block was ever reserved. MFC srchybrid/URLClient.cpp.
void tst_URLClient::fetch_downloadsTheFileOverHttp_data()
{
    QTest::addColumn<QString>("path");
    QTest::newRow("direct") << QStringLiteral("/file");
    QTest::newRow("redirected") << QStringLiteral("/moved");
}

void tst_URLClient::fetch_downloadsTheFileOverHttp()
{
    QFETCH(QString, path);

    const bool savedFilter = thePrefs.filterLANIPs();
    thePrefs.setFilterLANIPs(false);
    const auto restore = qScopeGuard([savedFilter] { thePrefs.setFilterLANIPs(savedFilter); });

    // three blocks, the last one short
    QByteArray content(static_cast<qsizetype>(EMBLOCKSIZE * 2 + 31360), '\0');
    for (qsizetype i = 0; i < content.size(); ++i)
        content[i] = static_cast<char>((i * 7 + (i >> 9)) & 0xFF);
    RangeServer server(content);
    QVERIFY(server.port() != 0);

    eMule::testing::TempDir tmp;
    PartFile pf;
    pf.setFileName(QStringLiteral("from_the_web.bin"));
    pf.setFileSize(static_cast<uint64>(content.size()));
    const QByteArray md4 = QCryptographicHash::hash(content, QCryptographicHash::Md4);
    pf.setFileHash(reinterpret_cast<const uint8*>(md4.constData()));
    QVERIFY(pf.createPartFile(tmp.path()));

    URLClient client;
    const Address loopback = Address::fromString(QStringLiteral("127.0.0.1"));
    QVERIFY(client.setUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server.port()).arg(path),
                          loopback));
    client.setRequestFile(&pf);
    pf.addSource(&client);
    QVERIFY(client.tryToConnect(true));

    QTRY_COMPARE_WITH_TIMEOUT(client.transferredDown(), static_cast<uint64>(content.size()), 10000);

    // one request for the whole contiguous run, both ends inclusive
    QCOMPARE(server.ranges, QList<QByteArray>{"0-" + QByteArray::number(content.size() - 1)});
    QCOMPARE(server.redirects, path == QStringLiteral("/moved") ? 1 : 0);
    QCOMPARE(pf.transferred(), static_cast<uint64>(content.size()));
    QCOMPARE(static_cast<uint64>(pf.completedSize()), static_cast<uint64>(content.size()));

    // what landed in the part file is what the server has
    pf.flushBuffer();
    QFile part(QDir(tmp.path()).filePath(pf.partMetFileName().chopped(4)));
    QVERIFY(part.open(QIODevice::ReadOnly));
    QCOMPARE(part.readAll(), content);

    pf.removeSource(&client);
    client.setReqFile(nullptr);
}

#include "tst_URLClient.moc"
