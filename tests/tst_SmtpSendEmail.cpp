/// @file tst_SmtpSendEmail.cpp
/// @brief Live SMTP send test — sends a real email via SmtpClient.
///
/// Reads SMTP credentials from the project-root .env file and sends
/// a test email. Verifies that the SmtpClient successfully completes
/// the SMTP transaction (EHLO → STARTTLS → AUTH → DATA → QUIT).
///
/// Only built when EMULE_LIVE_TESTS=ON (off by default).

#include "TestHelpers.h"

#include "net/SmtpClient.h"

#include <QSignalSpy>
#include <QTest>

using eMule::SmtpClient;
using eMule::testing::loadEnvFile;

class tst_SmtpSendEmail : public QObject
{
    Q_OBJECT

private slots:
    void sendTestEmail();
};

void tst_SmtpSendEmail::sendTestEmail()
{
    // Load credentials from project-root .env
    const QString envPath = QStringLiteral(EMULE_PROJECT_DATA_DIR "/../.env");
    const auto env = loadEnvFile(envPath);

    if (env.isEmpty())
        QSKIP(qPrintable(QStringLiteral(".env file not found or empty at: %1").arg(envPath)));

    const QString server   = env.value(QStringLiteral("SMTP_SERVER"));
    const int     port     = env.value(QStringLiteral("SMTP_PORT"), QStringLiteral("587")).toInt();
    const bool    tls      = env.value(QStringLiteral("SMTP_TLS"), QStringLiteral("true")) == QStringLiteral("true");
    const int     auth     = env.value(QStringLiteral("SMTP_AUTH"), QStringLiteral("1")).toInt();
    const QString user     = env.value(QStringLiteral("SMTP_USER"));
    const QString password = env.value(QStringLiteral("SMTP_PASSWORD"));
    const QString from     = env.value(QStringLiteral("SMTP_FROM"));
    const QString to       = env.value(QStringLiteral("SMTP_TO"));
    const bool selfSigned  = env.value(QStringLiteral("SMTP_ALLOW_SELF_SIGNED")) == QStringLiteral("true");

    if (server.isEmpty() || from.isEmpty() || to.isEmpty())
        QSKIP("SMTP_SERVER, SMTP_FROM, or SMTP_TO not set in .env — skipping");

    SmtpClient smtp;
    QSignalSpy finishedSpy(&smtp, &SmtpClient::finished);

    const QString subject = QStringLiteral("eMuleQt SMTP Test — %1")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    const QString body = QStringLiteral(
        "This is an automated test email sent by tst_SmtpSendEmail.\n"
        "\n"
        "If you received this, SmtpClient is working correctly.\n"
        "Timestamp: %1\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    qDebug() << "Sending test email from" << from << "to" << to << "via" << server << ":" << port
             << "tls:" << tls << "selfSigned:" << selfSigned;

    smtp.sendMail(server, port, tls, auth, user, password, from, to, subject, body, selfSigned);

    // Wait up to 30 seconds for the SMTP transaction to complete
    QVERIFY2(finishedSpy.wait(30000), "SMTP transaction timed out after 30 seconds");

    // Verify success
    QCOMPARE(finishedSpy.count(), 1);
    const auto args = finishedSpy.takeFirst();
    const bool success = args.at(0).toBool();
    const QString message = args.at(1).toString();

    if (!success)
        qWarning() << "SMTP failure:" << message;

    QVERIFY2(success, qPrintable(QStringLiteral("SMTP send failed: %1").arg(message)));
    qDebug() << "Email sent successfully:" << message;
}

QTEST_MAIN(tst_SmtpSendEmail)
#include "tst_SmtpSendEmail.moc"
