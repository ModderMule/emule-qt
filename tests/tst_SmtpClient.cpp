/// @file tst_SmtpClient.cpp
/// @brief SmtpClient against a scripted server on loopback: the authentication
///        methods and security modes of MFC's SMTP dialog (IDD_SMTPSERVER).

#include "net/SmtpClient.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace eMule;

namespace {

/// Answers like a mail server and keeps every line the client sent.
class ScriptedSmtpServer : public QObject {
public:
    explicit ScriptedSmtpServer(bool acceptLogin = true)
        : m_acceptLogin(acceptLogin)
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            m_peer = m_server.nextPendingConnection();
            connect(m_peer, &QTcpSocket::readyRead, this, [this] { onData(); });
            reply("220 mock ESMTP");
        });
    }

    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }
    QStringList lines;

private:
    void reply(const char* text) { m_peer->write(QByteArray(text) + "\r\n"); }

    void onData()
    {
        while (m_peer->canReadLine()) {
            const QString line = QString::fromUtf8(m_peer->readLine()).trimmed();
            lines << line;
            if (m_inData) {
                if (line == QStringLiteral(".")) {
                    m_inData = false;
                    reply("250 queued");
                }
            } else if (line.startsWith(QStringLiteral("EHLO"))) {
                m_peer->write("250-mock\r\n250 AUTH PLAIN LOGIN\r\n");
            } else if (line == QStringLiteral("AUTH LOGIN")) {
                m_loginStep = 1;
                reply(m_acceptLogin ? "334 VXNlcm5hbWU6" : "504 unrecognised authentication type");
            } else if (m_loginStep == 1) {
                m_loginStep = 2;
                reply("334 UGFzc3dvcmQ6");
            } else if (m_loginStep == 2) {
                m_loginStep = 0;
                reply("235 ok");
            } else if (line.startsWith(QStringLiteral("AUTH PLAIN"))) {
                reply("235 ok");
            } else if (line.startsWith(QStringLiteral("MAIL FROM")) || line.startsWith(QStringLiteral("RCPT TO"))) {
                reply("250 ok");
            } else if (line == QStringLiteral("DATA")) {
                m_inData = true;
                reply("354 go ahead");
            } else if (line == QStringLiteral("QUIT")) {
                reply("221 bye");
            }
        }
    }

    QTcpServer m_server;
    QTcpSocket* m_peer = nullptr;
    bool m_acceptLogin;
    bool m_inData = false;
    int m_loginStep = 0;
};

bool send(SmtpClient& smtp, quint16 port, SmtpAuth auth, bool* ok)
{
    QSignalSpy done(&smtp, &SmtpClient::finished);
    smtp.sendMail(QStringLiteral("127.0.0.1"), port, SmtpSecurity::None, auth,
                  QStringLiteral("alice"), QStringLiteral("s3cret"),
                  QStringLiteral("from@example.org"), QStringLiteral("to@example.org"),
                  QStringLiteral("subject"), QStringLiteral("body"));
    if (!done.wait(5000))
        return false;
    *ok = done.first().at(0).toBool();
    return true;
}

} // namespace

class tst_SmtpClient : public QObject {
    Q_OBJECT

private slots:
    void authLogin_sendsUserThenPassword();
    void authLogin_refusalEndsTheSend();
    void authPlain_isOneLine();
    void noAuth_goesStraightToMailFrom();
    void securityModes_portsAndLegacySwitch();
};

// MFC offers LOGIN beside PLAIN; some servers accept nothing else.
void tst_SmtpClient::authLogin_sendsUserThenPassword()
{
    ScriptedSmtpServer server;
    SmtpClient smtp;
    bool ok = false;
    QVERIFY(send(smtp, server.port(), SmtpAuth::Login, &ok));
    QVERIFY(ok);

    const auto at = server.lines.indexOf(QStringLiteral("AUTH LOGIN"));
    QVERIFY2(at > 0, qPrintable(server.lines.join(u'\n')));
    QCOMPARE(server.lines.at(at + 1), QString::fromLatin1(QByteArray("alice").toBase64()));
    QCOMPARE(server.lines.at(at + 2), QString::fromLatin1(QByteArray("s3cret").toBase64()));
    QCOMPARE(server.lines.at(at + 3), QStringLiteral("MAIL FROM:<from@example.org>"));
    QVERIFY(server.lines.contains(QStringLiteral("QUIT")));
}

void tst_SmtpClient::authLogin_refusalEndsTheSend()
{
    ScriptedSmtpServer server(/*acceptLogin*/ false);
    SmtpClient smtp;
    bool ok = true;
    QVERIFY(send(smtp, server.port(), SmtpAuth::Login, &ok));
    QVERIFY(!ok);
    QVERIFY(!server.lines.join(u'\n').contains(QStringLiteral("MAIL FROM")));
}

void tst_SmtpClient::authPlain_isOneLine()
{
    ScriptedSmtpServer server;
    SmtpClient smtp;
    bool ok = false;
    QVERIFY(send(smtp, server.port(), SmtpAuth::Plain, &ok));
    QVERIFY(ok);
    const QByteArray token = QByteArray("\0alice\0s3cret", 13).toBase64();
    QVERIFY2(server.lines.contains(QStringLiteral("AUTH PLAIN ") + QString::fromLatin1(token)),
             qPrintable(server.lines.join(u'\n')));
}

void tst_SmtpClient::noAuth_goesStraightToMailFrom()
{
    ScriptedSmtpServer server;
    SmtpClient smtp;
    bool ok = false;
    QVERIFY(send(smtp, server.port(), SmtpAuth::None, &ok));
    QVERIFY(ok);
    QCOMPARE(server.lines.at(1), QStringLiteral("MAIL FROM:<from@example.org>"));
}

// MFC SMTPdialog.cpp:98-116, and what the one "use TLS" switch stood for before.
void tst_SmtpClient::securityModes_portsAndLegacySwitch()
{
    QCOMPARE(SmtpClient::defaultPort(SmtpSecurity::None), 25);
    QCOMPARE(SmtpClient::defaultPort(SmtpSecurity::SslTls), 465);
    QCOMPARE(SmtpClient::defaultPort(SmtpSecurity::StartTls), 587);

    QCOMPARE(SmtpClient::securityFromLegacyTls(false, 465), SmtpSecurity::None);
    QCOMPARE(SmtpClient::securityFromLegacyTls(true, 465), SmtpSecurity::SslTls);
    QCOMPARE(SmtpClient::securityFromLegacyTls(true, 587), SmtpSecurity::StartTls);
    QCOMPARE(SmtpClient::securityFromLegacyTls(true, 25), SmtpSecurity::StartTls);
}

QTEST_GUILESS_MAIN(tst_SmtpClient)
#include "tst_SmtpClient.moc"
