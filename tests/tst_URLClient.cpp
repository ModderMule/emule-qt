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
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
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
    /// @p tls: serve https with the self-signed certificate in tests/data/tls.
    explicit RangeServer(QByteArray content, bool tls = false) : m_content(std::move(content))
    {
        if (tls) {
            auto* sslServer = new QSslServer(this);
            m_server = sslServer;
            QSslConfiguration config = QSslConfiguration::defaultConfiguration();
            config.setLocalCertificate(testCertificate());
            QFile keyFile(eMule::testing::testDataDir() + QStringLiteral("/tls/localhost-key.pem"));
            if (keyFile.open(QIODevice::ReadOnly))
                config.setPrivateKey(QSslKey(&keyFile, QSsl::Rsa));
            config.setPeerVerifyMode(QSslSocket::VerifyNone);
            sslServer->setSslConfiguration(config);
        } else {
            m_server = new QTcpServer(this);
        }
        m_server->listen(QHostAddress::LocalHost, 0);
        // pendingConnectionAvailable: after the handshake, where there is one
        connect(m_server, &QTcpServer::pendingConnectionAvailable, this, [this] {
            while (QTcpSocket* sock = m_server->nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, this, [this, sock] { serve(sock); });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
    }

    [[nodiscard]] uint16 port() const { return m_server->serverPort(); }

    [[nodiscard]] static QSslCertificate testCertificate()
    {
        QFile file(eMule::testing::testDataDir() + QStringLiteral("/tls/localhost-cert.pem"));
        return file.open(QIODevice::ReadOnly) ? QSslCertificate(&file) : QSslCertificate();
    }
    QList<QByteArray> ranges;   ///< every Range value asked for, in order
    int redirects = 0;
    QByteArray redirectTarget;  ///< where "/elsewhere" sends the client

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
            if (head.startsWith("GET /elsewhere ")) {
                ++redirects;
                sock->write("HTTP/1.1 302 Found\r\nLocation: " + redirectTarget
                            + "\r\nContent-Length: 0\r\n\r\n");
                continue;
            }
            if (head.startsWith("GET /relmoved ")) {
                ++redirects;
                sock->write("HTTP/1.1 302 Found\r\nLocation: /file\r\nContent-Length: 0\r\n\r\n");
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

    QTcpServer* m_server = nullptr;
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
    void setUrl_schemesAndUserInfo();
    void fetch_overHttps_data();
    void fetch_overHttps();
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
    QTest::newRow("redirected, relative Location") << QStringLiteral("/relmoved");
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
    // Without one the finished file is delivered to the root of the drive
    const QString incoming = tmp.filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(incoming));
    const QString savedIncoming = thePrefs.incomingDir();
    thePrefs.setIncomingDir(incoming);
    const auto restoreIncoming = qScopeGuard([savedIncoming] { thePrefs.setIncomingDir(savedIncoming); });
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
    QCOMPARE(server.redirects, path == QStringLiteral("/file") ? 0 : 1);
    QCOMPARE(pf.transferred(), static_cast<uint64>(content.size()));
    QCOMPARE(static_cast<uint64>(pf.completedSize()), static_cast<uint64>(content.size()));

    // what was delivered is what the server has (the last block completes the file)
    pf.flushBuffer();
    const QString delivered = QDir(incoming).filePath(pf.fileName());
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(delivered), 10000);
    QFile done(delivered);
    QVERIFY(done.open(QIODevice::ReadOnly));
    QCOMPARE(done.readAll(), content);

    pf.removeSource(&client);
    client.setReqFile(nullptr);
}

// MFC takes http only (srchybrid/URLClient.cpp:96-104); the port took anything and
// sent a plain GET to port 80, https included.
void tst_URLClient::setUrl_schemesAndUserInfo()
{
    URLClient client;
    QVERIFY(client.setUrl(QStringLiteral("https://example.com/a.bin")));
    QVERIFY(client.urlIsTls());
    QCOMPARE(client.urlPort(), uint16{443});
    QVERIFY(client.setUrl(QStringLiteral("HTTP://example.com/a.bin")));
    QVERIFY(!client.urlIsTls());
    QCOMPARE(client.urlPort(), uint16{80});

    QVERIFY(!client.setUrl(QStringLiteral("ftp://example.com/a.bin")));
    QVERIFY(!client.setUrl(QStringLiteral("http://user:secret@example.com/a.bin")));
    QVERIFY(!client.setUrl(QStringLiteral("//example.com/a.bin")));

    ExposedURLClient exposed;
    QVERIFY(exposed.setUrl(QStringLiteral("https://example.com/a.bin")));
    QVERIFY(exposed.buildGetHeader().contains("Host: example.com\r\n"));
    QVERIFY(exposed.setUrl(QStringLiteral("https://example.com:8443/a.bin")));
    QVERIFY(exposed.buildGetHeader().contains("Host: example.com:8443\r\n"));
}

void tst_URLClient::fetch_overHttps_data()
{
    QTest::addColumn<bool>("trusted");
    QTest::addColumn<QString>("host");
    QTest::newRow("trusted certificate, via an http redirect") << true << QStringLiteral("localhost");
    QTest::newRow("unknown certificate") << false << QStringLiteral("localhost");
    // trusted, but issued for another name than the one in the URL
    QTest::newRow("wrong host name") << true << QStringLiteral("other.invalid");
}

void tst_URLClient::fetch_overHttps()
{
    QFETCH(bool, trusted);
    QFETCH(QString, host);
    if (!QSslSocket::supportsSsl())
        QSKIP("no TLS backend");

    const bool savedFilter = thePrefs.filterLANIPs();
    thePrefs.setFilterLANIPs(false);
    const QSslConfiguration savedTls = QSslConfiguration::defaultConfiguration();
    const auto restore = qScopeGuard([savedFilter, savedTls] {
        thePrefs.setFilterLANIPs(savedFilter);
        QSslConfiguration::setDefaultConfiguration(savedTls);
    });
    if (trusted) {
        // this process only: the test certificate is its own authority
        QSslConfiguration config = savedTls;
        config.addCaCertificate(RangeServer::testCertificate());
        QSslConfiguration::setDefaultConfiguration(config);
    }

    QByteArray content(static_cast<qsizetype>(EMBLOCKSIZE + 4321), '\0');
    for (qsizetype i = 0; i < content.size(); ++i)
        content[i] = static_cast<char>((i * 11 + (i >> 7)) & 0xFF);
    RangeServer server(content, /*tls*/ true);
    QVERIFY(server.port() != 0);

    eMule::testing::TempDir tmp;
    // Without one the finished file is delivered to the root of the drive
    const QString incoming = tmp.filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(incoming));
    const QString savedIncoming = thePrefs.incomingDir();
    thePrefs.setIncomingDir(incoming);
    const auto restoreIncoming = qScopeGuard([savedIncoming] { thePrefs.setIncomingDir(savedIncoming); });
    PartFile pf;
    pf.setFileName(QStringLiteral("from_the_web_tls.bin"));
    pf.setFileSize(static_cast<uint64>(content.size()));
    const QByteArray md4 = QCryptographicHash::hash(content, QCryptographicHash::Md4);
    pf.setFileHash(reinterpret_cast<const uint8*>(md4.constData()));
    QVERIFY(pf.createPartFile(tmp.path()));

    // The trusted row starts on plain http and is sent on to https.
    const bool expectData = trusted && host == QStringLiteral("localhost");
    RangeServer plain(content);
    plain.redirectTarget = "https://127.0.0.1:" + QByteArray::number(server.port()) + "/file";

    URLClient client;
    const QString url = expectData
        ? QStringLiteral("http://127.0.0.1:%1/elsewhere").arg(plain.port())
        : QStringLiteral("https://%1:%2/file").arg(host).arg(server.port());
    QVERIFY(client.setUrl(url, Address::fromString(QStringLiteral("127.0.0.1"))));
    client.setRequestFile(&pf);
    pf.addSource(&client);
    QVERIFY(client.tryToConnect(true));

    if (expectData) {
        QTRY_COMPARE_WITH_TIMEOUT(client.transferredDown(), static_cast<uint64>(content.size()), 10000);
        QCOMPARE(plain.redirects, 1);
        QVERIFY(client.urlIsTls());
        QCOMPARE(server.ranges, QList<QByteArray>{"0-" + QByteArray::number(content.size() - 1)});
        pf.flushBuffer();
        const QString delivered = QDir(incoming).filePath(pf.fileName());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(delivered), 10000);
        QFile done(delivered);
        QVERIFY(done.open(QIODevice::ReadOnly));
        QCOMPARE(done.readAll(), content);
    } else {
        // The handshake fails; no request ever reaches the server, nothing is written.
        QTRY_VERIFY_WITH_TIMEOUT(client.socket() == nullptr, 10000);
        QVERIFY(server.ranges.isEmpty());
        QCOMPARE(client.transferredDown(), uint64{0});
    }

    pf.removeSource(&client);
    client.setReqFile(nullptr);
}

#include "tst_URLClient.moc"
