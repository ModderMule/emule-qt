/// @file tst_Ed3kSession.cpp
/// @brief The ed3k node of the daemon (src/ed3k): key file, two nodes on loopback, state.

#include "Ed3kHttp.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule::ed3k;

class tst_Ed3kSession : public QObject {
    Q_OBJECT

private slots:
    void keyFileCreatedOnceAndReused();
    void twoNodesFindEachOther();
    void stateSavedOnStopAndLoaded();
    void brokenKeyFileIsNotReplaced();
    void httpAnswersFromLiteralAddress_data();
    void httpAnswersFromLiteralAddress();
    void httpResolvesNameAndAsksFirst();

private:
    static Ed3kSettings loopback(const QString& dir);
    /// A and B on 127.0.0.1; A bootstraps from B. true: A has B as a contact.
    static bool connectPair(Ed3kSession& a, Ed3kSession& b);
};

void tst_Ed3kSession::keyFileCreatedOnceAndReused()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    QSignalSpy status(&s, &Ed3kSession::statusChanged);
    QVERIFY(s.start(loopback(dir.path())));
    QVERIFY(s.isRunning());
    QVERIFY(s.udpPort() != 0);
    QVERIFY(status.count() >= 1);
    const QString id = s.nodeIdHex();
    QCOMPARE(id.size(), 32);

    const QString keyPath = QDir(stateDir(dir.path())).filePath(QStringLiteral("node.key"));
    QFile key(keyPath);
    QVERIFY(key.open(QIODevice::ReadOnly));
    const QByteArray first = key.readAll();
    key.close();
    QCOMPARE(first.size(), 32);
#ifdef Q_OS_UNIX
    const auto perms = key.permissions();
    QVERIFY(perms.testFlag(QFileDevice::ReadOwner));
    QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                       | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)));
#endif
    s.stop();
    QVERIFY(!s.isRunning());
    QCOMPARE(s.udpPort(), std::uint16_t(0));

    // second start: the same key, the same node
    QVERIFY(s.start(loopback(dir.path())));
    QCOMPARE(s.nodeIdHex(), id);
    s.stop();
    QVERIFY(key.open(QIODevice::ReadOnly));
    QCOMPARE(key.readAll(), first);
}

void tst_Ed3kSession::twoNodesFindEachOther()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QVERIFY(a.nodeIdHex() != b.nodeIdHex());
    QVERIFY(connectPair(a, b));
    // B learnt A from its lookups too
    QTRY_VERIFY_WITH_TIMEOUT(b.contactCount() > 0, 20000);
}

void tst_Ed3kSession::stateSavedOnStopAndLoaded()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    const QString kad = QDir(stateDir(da.path())).filePath(QStringLiteral("kad.dat"));
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    {
        Ed3kSession a;
        QVERIFY(a.start(loopback(da.path())));
        QVERIFY(connectPair(a, b));
        a.stop();
        QVERIFY(QFile::exists(kad));
        QVERIFY(QFileInfo(kad).size() > 0);
    }
    // no bootstrap call this time: B comes back from kad.dat (loaded before any datagram)
    Ed3kSession again;
    QVERIFY(again.start(loopback(da.path())));
    QTRY_VERIFY_WITH_TIMEOUT(again.contactCount() > 0, 20000);
    again.stop();
    b.stop();
}

void tst_Ed3kSession::brokenKeyFileIsNotReplaced()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(stateDir(dir.path())));
    const QString keyPath = QDir(stateDir(dir.path())).filePath(QStringLiteral("node.key"));
    QFile key(keyPath);
    QVERIFY(key.open(QIODevice::WriteOnly));
    key.write("short");
    key.close();

    Ed3kSession s;
    QVERIFY(!s.start(loopback(dir.path())));
    QVERIFY(!s.isRunning());
    QVERIFY(key.open(QIODevice::ReadOnly));
    QCOMPARE(key.readAll(), QByteArray("short"));
}

void tst_Ed3kSession::httpAnswersFromLiteralAddress_data()
{
    QTest::addColumn<QByteArray>("response");
    QTest::addColumn<quint64>("maxLen");
    QTest::addColumn<bool>("answered");
    QTest::addColumn<int>("status");
    QTest::addColumn<QByteArray>("body");
    QTest::newRow("length") << QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello") << quint64(100)
                            << true << 200 << QByteArray("hello");
    QTest::newRow("chunked") << QByteArray("HTTP/1.1 206 Partial\r\nTransfer-Encoding: chunked\r\n\r\n"
                                           "3\r\nabc\r\n2;x=1\r\nde\r\n0\r\n\r\n")
                             << quint64(100) << true << 206 << QByteArray("abcde");
    QTest::newRow("redirect as it came") << QByteArray("HTTP/1.1 302 Found\r\nLocation: https://x.example/\r\n"
                                                       "Content-Length: 0\r\n\r\n")
                                         << quint64(100) << true << 302 << QByteArray();
    QTest::newRow("too long") << QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello") << quint64(4)
                              << false << 0 << QByteArray();
    QTest::newRow("cut off") << QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nhello") << quint64(100)
                             << false << 0 << QByteArray();
}

void tst_Ed3kSession::httpAnswersFromLiteralAddress()
{
    QFETCH(QByteArray, response);
    QFETCH(quint64, maxLen);
    QFETCH(bool, answered);
    QFETCH(int, status);
    QFETCH(QByteArray, body);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QByteArray request;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        QTcpSocket* c = server.nextPendingConnection();
        connect(c, &QTcpSocket::readyRead, c, [&, c] {
            request += c->readAll();
            if (request.contains("\r\n\r\n")) {
                c->write(response);
                c->disconnectFromHost();
            }
        });
    });

    int answers = 0, failures = 0, gotStatus = 0;
    QByteArray gotBody;
    Ed3kHttp http({[](std::uint64_t, const ::ed3k::Address&) { return false; }, [](std::uint64_t) {},
                   [&](std::uint64_t id, std::uint16_t st, const QByteArray&, const QByteArray& b) {
                       QCOMPARE(id, std::uint64_t(7));
                       ++answers;
                       gotStatus = st;
                       gotBody = b;
                   },
                   [&](std::uint64_t) { ++failures; }},
                  QStringLiteral("eMuleQt/test"));
    ::ed3k::event::HttpRequest req{};
    req.id = 7;
    req.method = ED3K_HTTP_GET;
    req.url = "http://seed.example:" + std::to_string(server.serverPort()) + "/nodes?x=1";
    req.addr = ::ed3k::Address::v4(127, 0, 0, 1, server.serverPort());
    req.port = server.serverPort();
    req.headers = "Accept-Encoding: identity\r\n";
    req.maxLen = maxLen;
    req.timeoutS = 10;
    http.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(answers + failures > 0, 5000);
    QCOMPARE(answers, answered ? 1 : 0);
    QCOMPARE(failures, answered ? 0 : 1);
    if (answered) {
        QCOMPARE(gotStatus, status);
        QCOMPARE(gotBody, body);
    }
    // the core's headers as given, the name of the URL as Host, no cookies
    QVERIFY(request.startsWith("GET /nodes?x=1 HTTP/1.1\r\n"));
    QVERIFY(request.contains("Host: seed.example:"));
    QVERIFY(request.contains("Accept-Encoding: identity\r\n"));
    QVERIFY(request.contains("User-Agent: eMuleQt/test\r\n"));
    QVERIFY(!request.contains("Cookie"));
    QCOMPARE(http.pending(), std::size_t(0));
}

void tst_Ed3kSession::httpResolvesNameAndAsksFirst()
{
    // a name: every address is offered first; Denied ends the request without a connection
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int asked = 0, failures = 0, answers = 0;
    ::ed3k::Address offered;
    Ed3kHttp http({[&](std::uint64_t, const ::ed3k::Address& a) {
                       ++asked;
                       offered = a;
                       return false;
                   },
                   [](std::uint64_t) {}, [&](std::uint64_t, std::uint16_t, const QByteArray&, const QByteArray&) { ++answers; },
                   [&](std::uint64_t) { ++failures; }},
                  QStringLiteral("eMuleQt/test"));
    ::ed3k::event::HttpRequest req{};
    req.id = 9;
    req.method = ED3K_HTTP_GET;
    req.url = "http://localhost:" + std::to_string(server.serverPort()) + "/";
    req.port = server.serverPort();
    req.maxLen = 100;
    req.timeoutS = 10;
    http.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(asked > 0, 5000);
    QCOMPARE(asked, 1);   // not the next address of the name
    QCOMPARE(offered.port, server.serverPort());
    QTest::qWait(100);
    QVERIFY(!server.hasPendingConnections());
    QCOMPARE(answers + failures, 0);   // over: IoCancel follows from the core
    QCOMPARE(http.pending(), std::size_t(0));
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

Ed3kSettings tst_Ed3kSession::loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.udpPort = 0;   // ephemeral
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");   // private addresses only in a named network
    s.forceOpen = true;
    return s;
}

bool tst_Ed3kSession::connectPair(Ed3kSession& a, Ed3kSession& b)
{
    const auto bId = b.nodeId();
    if (!bId || !a.engine())
        return false;
    const ::ed3k::Node bNode{*bId, ::ed3k::Address::v4(127, 0, 0, 1, b.udpPort())};
    bool started = false;
    a.post([&](::ed3k::Engine& e, ::ed3k::Now now) {
        const auto n = e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1});
        started = n.has_value();
    });
    if (!started)
        return false;
    QDeadlineTimer deadline(20000);
    while (a.contactCount() == 0 && !deadline.hasExpired())
        QTest::qWait(50);
    return a.contactCount() > 0;
}

QTEST_GUILESS_MAIN(tst_Ed3kSession)
#include "tst_Ed3kSession.moc"
