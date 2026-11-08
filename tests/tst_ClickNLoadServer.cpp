/// @file tst_ClickNLoadServer.cpp
/// @brief Click'n'Load 2 on the GUI side: the endpoints pages and add-ons call, as JDownloader
/// answers them; posts reach the handler with their fields.

#include "app/ClickNLoadServer.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <optional>

using namespace eMule;

namespace {

/// One raw HTTP exchange; the whole answer once the server closes.
QByteArray exchange(quint16 port, const QByteArray& request)
{
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, port);
    if (!s.waitForConnected(3000))
        return {};
    s.write(request);
    QByteArray out;
    QElapsedTimer t;
    t.start();
    while (s.state() != QAbstractSocket::UnconnectedState && t.elapsed() < 5000) {
        // the handler may answer later, from the event loop
        QCoreApplication::processEvents();
        if (s.waitForReadyRead(50))
            out += s.readAll();
    }
    out += s.readAll();
    return out;
}

QByteArray post(quint16 port, const QByteArray& path, const QByteArray& form, const QByteArray& extraHeaders = {},
               const QByteArray& type = "application/x-www-form-urlencoded")
{
    return exchange(port, "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port)
                              + "\r\nContent-Type: " + type + "\r\nContent-Length: "
                              + QByteArray::number(form.size()) + "\r\n" + extraHeaders + "\r\n" + form);
}

/// JSON written with ' for " (moc can't read raw strings)
QByteArray json(const char* text)
{
    return QByteArray(text).replace('\'', '"');
}

QByteArray bodyOf(const QByteArray& response)
{
    const qsizetype i = response.indexOf("\r\n\r\n");
    return i < 0 ? QByteArray() : response.mid(i + 4);
}

} // namespace

class tst_ClickNLoadServer : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        m_got.reset();
        m_answer = true;
        m_error.clear();
        m_server = std::make_unique<ClickNLoadServer>([this](const ClickNLoadServer::Post& p,
                                                             const ClickNLoadServer::Done& done) {
            m_got = p;
            // like the core: the answer comes later
            QTimer::singleShot(10, this, [this, done] { done(m_answer, m_error); });
        });
        m_server->setEnabled(true, 0);
        QVERIFY(m_server->isListening());
        m_port = m_server->port();
    }

    void cleanup() { m_server.reset(); }

    /// pages load jdcheck.js and look for the variable before they offer their button
    void jdcheck_saysJDownloader()
    {
        const QByteArray r = exchange(m_port, "GET /jdcheck.js HTTP/1.1\r\nHost: x\r\n\r\n");
        QVERIFY2(r.startsWith("HTTP/1.1 200"), r.constData());
        QVERIFY(bodyOf(r).startsWith("jdownloader=true;"));
        QVERIFY(r.contains("Access-Control-Allow-Origin: *"));
        QCOMPARE(bodyOf(exchange(m_port, "GET /flash/ HTTP/1.1\r\n\r\n")), QByteArray("JDownloader\r\n"));
        QVERIFY(bodyOf(exchange(m_port, "GET /crossdomain.xml HTTP/1.1\r\n\r\n")).contains("allow-access-from domain=\"*\""));
    }

    /// filecrypt-style post: crypted with a bare '+', jk, passwords, package with a site suffix
    void addcrypted2_reachesHandler()
    {
        const QByteArray form = "passwords=pw1%0Apw2&source=https%3A%2F%2Flinks.example%2FFolder%2F1&jk=function+f()"
                                "%7Breturn+'31323334353637383930313233343536'%3B%7D&crypted=ab+cd%2Fef%3D%3D"
                                "&package=My+Set+-+links.cc";
        const QByteArray r = post(m_port, "/flash/addcrypted2", form);
        QCOMPARE(bodyOf(r), QByteArray("success\r\n"));
        QVERIFY(m_got);
        QCOMPARE(int(m_got->kind), int(ClickNLoadServer::Post::Kind::Links));
        QCOMPARE(m_got->crypted, QStringLiteral("ab+cd/ef=="));
        QCOMPARE(m_got->jk, QStringLiteral("function f(){return '31323334353637383930313233343536';}"));
        QCOMPARE(m_got->passwords, (QStringList{QStringLiteral("pw1"), QStringLiteral("pw2")}));
        QCOMPARE(m_got->package, QStringLiteral("My Set"));
        QCOMPARE(m_got->source, QStringLiteral("https://links.example/Folder/1"));
    }

    void addcrypted2_withoutKeyFails()
    {
        QCOMPARE(bodyOf(post(m_port, "/flash/addcrypted2", "crypted=abc")), QByteArray("failed\r\n"));
        QVERIFY(!m_got);
    }

    /// the core's refusal reaches the page
    void handlerFailureAnswersFailed()
    {
        m_answer = false;
        m_error = QStringLiteral("hoster downloads are off");
        QCOMPARE(bodyOf(post(m_port, "/flash/add", "urls=https%3A%2F%2Fh.example%2Ff%2F1")),
                 QByteArray("failed hoster downloads are off\r\n"));
    }

    /// plain links, passwords[] once each, the Referer as the source
    void add_plainLinks()
    {
        const QByteArray r = post(m_port, "/flash/add",
                                  "urls=https%3A%2F%2Fh.example%2Ff%2F1%0D%0Ahttps%3A%2F%2Fh.example%2Ff%2F2"
                                  "&passwords%5B%5D=a&passwords%5B%5D=b",
                                  "Referer: https://site.example/page\r\n");
        QCOMPARE(bodyOf(r), QByteArray("success\r\n"));
        QVERIFY(m_got);
        QCOMPARE(m_got->urls, QStringLiteral("https://h.example/f/1\r\nhttps://h.example/f/2"));
        QCOMPARE(m_got->passwords, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(m_got->source, QStringLiteral("https://site.example/page"));
    }

    void addcrypted_isDlc()
    {
        QCOMPARE(bodyOf(post(m_port, "/flash/addcrypted", "crypted=AAAA+BBBB")), QByteArray("success\r\n"));
        QVERIFY(m_got);
        QCOMPARE(int(m_got->kind), int(ClickNLoadServer::Post::Kind::Dlc));
        QCOMPARE(m_got->crypted, QStringLiteral("AAAA+BBBB"));
    }

    /// a body in two writes still makes one post
    void bodyInPieces()
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, m_port);
        QVERIFY(s.waitForConnected(3000));
        const QByteArray form = "urls=https%3A%2F%2Fh.example%2Ff%2F9";
        s.write("POST /flash/add HTTP/1.1\r\nContent-Length: " + QByteArray::number(form.size()) + "\r\n\r\n"
                + form.left(10));
        s.flush();
        QTest::qWait(100);
        QVERIFY(!m_got);
        s.write(form.mid(10));
        QTRY_VERIFY(m_got);
        QCOMPARE(m_got->urls, QStringLiteral("https://h.example/f/9"));
    }

    /// browsers ask first for a page's post to a private address
    void preflightAllowsPrivateNetwork()
    {
        const QByteArray r = exchange(m_port, "OPTIONS /flash/addcrypted2 HTTP/1.1\r\nOrigin: https://site.example\r\n"
                                              "Access-Control-Request-Private-Network: true\r\n"
                                              "Access-Control-Request-Headers: content-type\r\n\r\n");
        QVERIFY2(r.startsWith("HTTP/1.1 204"), r.constData());
        QVERIFY(r.contains("Access-Control-Allow-Private-Network: true"));
        QVERIFY(r.contains("Access-Control-Allow-Headers: content-type"));
        QVERIFY(!m_got);
    }

    void unknownPathIs404()
    {
        QVERIFY(exchange(m_port, "GET /flash/addlinks HTTP/1.1\r\n\r\n").startsWith("HTTP/1.1 404"));
        QVERIFY(exchange(m_port, "GET /etc/passwd HTTP/1.1\r\n\r\n").startsWith("HTTP/1.1 404"));
    }

    /// JD's JSON endpoint: a CnlQuery object as an application/json body
    void addcnl_jsonBody()
    {
        const QByteArray body = json("{'crypted':'ab+cd/ef==','jk':'function f(){return 31;}','passwords':['pw1','pw2'],'packageName':'Set - links.cc','source':'https://links.example/f/1','autostart':true}");
        const QByteArray r = post(m_port, "/flash/addcnl", body, {}, "application/json");
        QCOMPARE(bodyOf(r), QByteArray("success\r\n"));
        QVERIFY(m_got);
        QCOMPARE(int(m_got->kind), int(ClickNLoadServer::Post::Kind::Links));
        QCOMPARE(m_got->crypted, QStringLiteral("ab+cd/ef=="));
        QCOMPARE(m_got->jk, QStringLiteral("function f(){return 31;}"));
        QCOMPARE(m_got->passwords, (QStringList{QStringLiteral("pw1"), QStringLiteral("pw2")}));
        QCOMPARE(m_got->package, QStringLiteral("Set"));
        QCOMPARE(m_got->source, QStringLiteral("https://links.example/f/1"));
    }

    /// [obj] and {"params":[obj]} carry the object too
    void addcnl_wrappedForms()
    {
        QCOMPARE(bodyOf(post(m_port, "/flash/addcnl", json("[{'urls':'https://h.example/f/1'}]"), {}, "application/json")),
                 QByteArray("success\r\n"));
        QVERIFY(m_got);
        QCOMPARE(m_got->urls, QStringLiteral("https://h.example/f/1"));
        m_got.reset();
        QCOMPARE(bodyOf(post(m_port, "/flash/addcnl", json("{'params':[{'urls':['https://h.example/f/2','https://h.example/f/3']}]}"),
                             {}, "application/json")),
                 QByteArray("success\r\n"));
        QVERIFY(m_got);
        QCOMPARE(m_got->urls, QStringLiteral("https://h.example/f/2\nhttps://h.example/f/3"));
    }

    /// a <script> tag GET with JSONP: JD answers an empty script and never calls back
    void addcnl_getQueryWithCallback()
    {
        const QByteArray q = QUrl::toPercentEncoding(QString::fromUtf8(json("{'urls':'https://h.example/f/7','referrer':'https://site.example/'}")));
        const QByteArray r = exchange(m_port, "GET /flash/addcnl?callback=cb&" + q + "&_=123 HTTP/1.1\r\n\r\n");
        QVERIFY2(r.startsWith("HTTP/1.1 200"), r.constData());
        QVERIFY(r.contains("Content-Type: text/javascript"));
        QVERIFY(bodyOf(r).isEmpty());
        QVERIFY(m_got);
        QCOMPARE(m_got->urls, QStringLiteral("https://h.example/f/7"));
        QCOMPARE(m_got->source, QStringLiteral("https://site.example/"));
    }

    /// a form value holds the object; "key" in place of jk; links and a list together
    void addcnl_formValue()
    {
        const QByteArray q = QUrl::toPercentEncoding(QString::fromUtf8(
            json("{'urls':'https://h.example/f/1','crypted':'AAAA+BBBB','key':'31323334353637383930313233343536','passwords':'a\\nb'}")));
        QCOMPARE(bodyOf(post(m_port, "/flash/addcnl", "cnl=" + q)), QByteArray("success\r\n"));
        QVERIFY(m_got);
        QCOMPARE(m_got->urls, QStringLiteral("https://h.example/f/1"));
        QCOMPARE(m_got->crypted, QStringLiteral("AAAA+BBBB"));
        QCOMPARE(m_got->k, QStringLiteral("31323334353637383930313233343536"));
        QVERIFY(m_got->jk.isEmpty());
        QCOMPARE(m_got->passwords, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    }

    void addcnl_badJsonAndEmpty()
    {
        QVERIFY(post(m_port, "/flash/addcnl", "{not json", {}, "application/json").startsWith("HTTP/1.1 400"));
        QVERIFY(post(m_port, "/flash/addcnl", "[]", {}, "application/json").startsWith("HTTP/1.1 400"));
        QVERIFY(!m_got);
        // nothing to add, or a list without its key: no error, no dialog
        QCOMPARE(bodyOf(post(m_port, "/flash/addcnl", "{}", {}, "application/json")), QByteArray("success\r\n"));
        QCOMPARE(bodyOf(post(m_port, "/flash/addcnl", json("{'crypted':'AAAA'}"), {}, "application/json")),
                 QByteArray("success\r\n"));
        QVERIFY(!m_got);
    }

    void tooLargeRefused()
    {
        QVERIFY(exchange(m_port, "POST /flash/add HTTP/1.1\r\nContent-Length: 999999999\r\n\r\n").startsWith("HTTP/1.1 413"));
    }

    /// loopback only: no other machine posts links
    void listensOnLoopbackOnly()
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, m_port);
        QVERIFY(s.waitForConnected(3000));
        QVERIFY(s.peerAddress().isLoopback());
        const auto servers = m_server->findChildren<QTcpServer*>();
        QVERIFY(!servers.isEmpty());
        for (const QTcpServer* srv : servers)
            if (srv->isListening())
                QVERIFY(srv->serverAddress().isLoopback());
    }

    /// JDownloader has the port: no error, not listening, and the first one keeps working
    void busyPortWaits()
    {
        ClickNLoadServer second([](const ClickNLoadServer::Post&, const ClickNLoadServer::Done& done) { done(true, {}); });
        second.setEnabled(true, m_port);
        QVERIFY(!second.isListening());
        QVERIFY(second.isEnabled());
        QVERIFY(bodyOf(exchange(m_port, "GET /jdcheck.js HTTP/1.1\r\n\r\n")).startsWith("jdownloader=true;"));
        // off frees the port at once
        m_server->setEnabled(false);
        QVERIFY(!m_server->isListening());
        QVERIFY(exchange(m_port, "GET /jdcheck.js HTTP/1.1\r\n\r\n").isEmpty());
    }

    void parseForm_decodes()
    {
        const auto f = ClickNLoadServer::parseForm("a=1+2&b=%2B&c&a=x%26y");
        QCOMPARE(f.size(), 4);
        QCOMPARE(f[0].second, QStringLiteral("1 2"));
        QCOMPARE(f[1].second, QStringLiteral("+"));
        QCOMPARE(f[2].first, QStringLiteral("c"));
        QCOMPARE(f[3].second, QStringLiteral("x&y"));
        QCOMPARE(ClickNLoadServer::cleanPackageName(QStringLiteral("Name - filecrypt.cc")), QStringLiteral("Name"));
        QCOMPARE(ClickNLoadServer::cleanPackageName(QStringLiteral("Plain")), QStringLiteral("Plain"));
    }

private:
    std::unique_ptr<ClickNLoadServer> m_server;
    std::optional<ClickNLoadServer::Post> m_got;
    bool m_answer = true;
    QString m_error;
    quint16 m_port = 0;
};

QTEST_MAIN(tst_ClickNLoadServer)
#include "tst_ClickNLoadServer.moc"
